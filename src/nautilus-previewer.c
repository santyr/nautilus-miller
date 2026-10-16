/*
 * nautilus-previewer: nautilus previewer DBus wrapper
 *
 * Copyright (C) 2011, Red Hat, Inc.
 *
 * Nautilus is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * Nautilus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Cosimo Cecchi <cosimoc@redhat.com>
 *
 */
#define G_LOG_DOMAIN "nautilus-previewer"

#include "config.h"

#include "nautilus-previewer.h"

#include "nautilus-files-view.h"
#include "nautilus-window.h"
#include "nautilus-window-slot.h"

#include <gio/gio.h>

#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/wayland/gdkwayland.h>
#endif
#ifdef GDK_WINDOWING_X11
#include <gdk/x11/gdkx.h>
#endif

#define PREVIEWER2_DBUS_IFACE "org.gnome.NautilusPreviewer2"
#define PREVIEWER_DBUS_NAME "org.gnome.NautilusPreviewer"
#define PREVIEWER_DBUS_PATH "/org/gnome/NautilusPreviewer"

/* Discovery belongs to this application instance. Delayed callbacks carry a
 * generation so teardown/restart cannot publish an obsolete proxy. */
static GStrv previewer_names = NULL;
static guint previewer_index;
static guint64 discovery_generation;
static gint64 retry_after;
static gboolean initialized;
static gboolean previewer_ready;
static gboolean fetching_bus;
static GDBusConnection *previewer_connection;
static GDBusProxy *previewer_proxy;
static guint subscription_id;
static guint owner_subscription_id;
static GCancellable *cancellable;

static NautilusWindowSlot *current_slot = NULL; /* weak ref */
static GtkRoot *current_window = NULL; /* weak ref */
static gchar *exported_window_handle = NULL;

static void real_call_show_file (const gchar *uri,
                                 const gchar *window_handle,
                                 gboolean     close_if_already_visible,
                                 const char  *activation_token);
static void create_new_proxy (void);
static void previewer_selection_event (GDBusConnection *connection,
                                       const gchar     *sender_name,
                                       const gchar     *object_path,
                                       const gchar     *interface_name,
                                       const gchar     *signal_name,
                                       GVariant        *parameters,
                                       gpointer         user_data);

#ifdef GDK_WINDOWING_WAYLAND
typedef struct
{
    gchar *uri;
    gboolean close_if_already_visible;
    char *activation_token;
} PreviewExportData;

static void
preview_export_data_free (gpointer _data)
{
    PreviewExportData *data = _data;
    g_free (data->uri);
    g_free (data->activation_token);
    g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PreviewExportData, preview_export_data_free)

static void
wayland_window_handle_exported (GdkToplevel *toplevel,
                                const char  *wayland_handle_str,
                                gpointer     user_data)
{
    PreviewExportData *data = user_data;
    g_autofree char *wayland_handle = g_strdup_printf ("wayland:%s", wayland_handle_str);

    real_call_show_file (data->uri, wayland_handle, data->close_if_already_visible, data->activation_token);
}
#endif

static void
clear_exported_window_handle (void)
{
    if (exported_window_handle == NULL)
    {
        return;
    }

#ifdef GDK_WINDOWING_WAYLAND
    if (current_window != NULL &&
        GDK_IS_WAYLAND_DISPLAY (gtk_widget_get_display (GTK_WIDGET (current_window))))
    {
        GdkSurface *gdk_surface = gtk_native_get_surface (GTK_NATIVE (current_window));
        if (GDK_IS_WAYLAND_TOPLEVEL (gdk_surface) &&
            g_str_has_prefix (exported_window_handle, "wayland:"))
        {
            gdk_wayland_toplevel_drop_exported_handle (GDK_WAYLAND_TOPLEVEL (gdk_surface),
                                                       exported_window_handle + strlen ("wayland:"));
        }
    }
#endif

    g_free (exported_window_handle);
    exported_window_handle = NULL;
}

typedef struct
{
    guint64 generation;
    guint index;
} DiscoveryCall;

static DiscoveryCall *
discovery_call_new (void)
{
    DiscoveryCall *call = g_new0 (DiscoveryCall, 1);
    call->generation = discovery_generation;
    call->index = previewer_index;
    return call;
}

static gboolean
call_is_current (DiscoveryCall *call)
{
    return initialized && call->generation == discovery_generation &&
           call->index == previewer_index;
}

static void
clear_proxy (void)
{
    previewer_ready = FALSE;
    if (subscription_id != 0)
    {
        g_dbus_connection_signal_unsubscribe (previewer_connection, subscription_id);
        subscription_id = 0;
    }
    g_clear_object (&previewer_proxy);
}

static void
next_provider (void)
{
    previewer_index++;
    if (previewer_names[previewer_index] != NULL)
    {
        create_new_proxy ();
    }
    else
    {
        fetching_bus = FALSE;
        /* Missing providers must be retryable, without a render-loop retry
         * storm from is_visible(). Name-owner changes bypass this cooldown. */
        retry_after = g_get_monotonic_time () + G_TIME_SPAN_SECOND;
    }
}

static void
on_ping_finished (GObject *object, GAsyncResult *res, gpointer user_data)
{
    g_autofree DiscoveryCall *call = user_data;
    g_autoptr (GError) error = NULL;
    g_autoptr (GVariant) reply = g_dbus_proxy_call_finish (G_DBUS_PROXY (object), res, &error);
    if (!call_is_current (call)) { return; }
    if (error != NULL)
    {
        next_provider ();
        return;
    }
    g_set_object (&previewer_proxy, G_DBUS_PROXY (object));
    previewer_ready = TRUE;
    fetching_bus = FALSE;
    subscription_id = g_dbus_connection_signal_subscribe (
        previewer_connection, g_dbus_proxy_get_name (previewer_proxy),
        PREVIEWER2_DBUS_IFACE, "SelectionEvent",
        g_dbus_proxy_get_object_path (previewer_proxy), NULL,
        G_DBUS_SIGNAL_FLAGS_NONE, previewer_selection_event, NULL, NULL);
}

static void
on_proxy_ready (GObject *object, GAsyncResult *res, gpointer user_data)
{
    g_autofree DiscoveryCall *call = user_data;
    g_autoptr (GError) error = NULL;
    g_autoptr (GDBusProxy) proxy = g_dbus_proxy_new_finish (res, &error);
    if (!call_is_current (call)) { return; }
    if (proxy == NULL)
    {
        next_provider ();
        return;
    }
    g_dbus_proxy_call (proxy, "org.freedesktop.DBus.Peer.Ping", NULL,
                       G_DBUS_CALL_FLAGS_NONE, 1500, cancellable,
                       on_ping_finished, discovery_call_new ());
}

static void
create_new_proxy (void)
{
    const char *name = previewer_names[previewer_index];
    g_autofree char *path = g_strconcat (PREVIEWER_DBUS_PATH, name + strlen (PREVIEWER_DBUS_NAME), NULL);
    g_dbus_proxy_new (previewer_connection,
                      G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START_AT_CONSTRUCTION,
                      NULL, name, path, PREVIEWER2_DBUS_IFACE, cancellable,
                      on_proxy_ready, discovery_call_new ());
}

static void
restart_discovery (void)
{
    discovery_generation++;
    g_cancellable_cancel (cancellable);
    g_clear_object (&cancellable);
    cancellable = g_cancellable_new ();
    clear_proxy ();
    previewer_index = 0;
    retry_after = 0;
    fetching_bus = TRUE;
    create_new_proxy ();
}

static void
name_owner_changed (GDBusConnection *connection, const char *sender,
                    const char *path, const char *interface, const char *signal,
                    GVariant *parameters, gpointer data)
{
    const char *name, *old_owner, *new_owner;
    g_variant_get (parameters, "(&s&s&s)", &name, &old_owner, &new_owner);
    if (!initialized || !g_strv_contains ((const char * const *) previewer_names, name)) { return; }
    if ((previewer_ready && g_str_equal (name, g_dbus_proxy_get_name (previewer_proxy))) ||
        (!previewer_ready && new_owner[0] != '\0'))
    {
        restart_discovery ();
    }
}

static void
set_profile (const char *profile)
{
    g_autoptr (GPtrArray) names = g_ptr_array_new_with_free_func (g_free);
    if (profile[0] != '\0')
    {
        g_ptr_array_add (names, g_strconcat (PREVIEWER_DBUS_NAME, profile, NULL));
    }
    g_ptr_array_add (names, g_strdup (PREVIEWER_DBUS_NAME));
    if (!g_str_equal (profile, "Devel"))
    {
        g_ptr_array_add (names, g_strdup (PREVIEWER_DBUS_NAME "Devel"));
    }
    g_ptr_array_add (names, NULL);
    previewer_names = (GStrv) g_ptr_array_free (g_steal_pointer (&names), FALSE);
}

static void
watch_providers (void)
{
    owner_subscription_id = g_dbus_connection_signal_subscribe (
        previewer_connection, "org.freedesktop.DBus", "org.freedesktop.DBus",
        "NameOwnerChanged", "/org/freedesktop/DBus", NULL,
        G_DBUS_SIGNAL_FLAGS_NONE, name_owner_changed, NULL, NULL);
}

static void
on_connection_ready (GObject *object, GAsyncResult *res, gpointer user_data)
{
    g_autofree DiscoveryCall *call = user_data;
    g_autoptr (GError) error = NULL;
    g_autoptr (GDBusConnection) connection = g_bus_get_finish (res, &error);
    if (!call_is_current (call)) { return; }
    fetching_bus = FALSE;
    if (connection == NULL)
    {
        retry_after = g_get_monotonic_time () + G_TIME_SPAN_SECOND;
        return;
    }
    g_set_object (&previewer_connection, connection);
    watch_providers ();
    restart_discovery ();
}

static gboolean
ensure_previewer_proxy (void)
{
    if (!initialized) { return FALSE; }
    if (previewer_ready) { return TRUE; }
    if (fetching_bus || g_get_monotonic_time () < retry_after) { return FALSE; }
    if (previewer_connection != NULL)
    {
        restart_discovery ();
    }
    else
    {
        fetching_bus = TRUE;
        g_bus_get (G_BUS_TYPE_SESSION, cancellable, on_connection_ready, discovery_call_new ());
    }
    return FALSE;
}

static void
previewer2_method_ready_cb (GObject      *source,
                            GAsyncResult *res,
                            gpointer      user_data)
{
    GDBusProxy *proxy = G_DBUS_PROXY (source);
    g_autoptr (GError) error = NULL;

    g_dbus_proxy_call_finish (proxy, res, &error);

    if (error != NULL)
    {
        g_debug ("Unable to call method on NautilusPreviewer: %s", error->message);
    }
}

void
nautilus_previewer_call_show_file (const gchar        *uri,
                                   NautilusWindowSlot *slot,
                                   gboolean            close_if_already_visible)
{
    g_set_weak_pointer (&current_slot, slot);

    GtkRoot *window = gtk_widget_get_root (GTK_WIDGET (slot));

    g_autoptr (GdkAppLaunchContext) launch_context = gdk_display_get_app_launch_context (
        gtk_root_get_display (window));
    g_autofree char *activation_token = g_app_launch_context_get_startup_notify_id (
        G_APP_LAUNCH_CONTEXT (launch_context),
        NULL,
        NULL);

    /* Reuse existing handle if called again for the same window. */
    if (current_window == window &&
        exported_window_handle != NULL)
    {
        real_call_show_file (uri, exported_window_handle, close_if_already_visible, activation_token);
        return;
    }

    /* Otherwise, obtain a new window handle. */
    clear_exported_window_handle ();
    g_set_weak_pointer (&current_window, window);

    GdkSurface *gdk_surface = gtk_native_get_surface (GTK_NATIVE (window));
#ifdef GDK_WINDOWING_X11
    if (GDK_IS_X11_DISPLAY (gtk_widget_get_display (GTK_WIDGET (window))))
    {
        guint xid = (guint) gdk_x11_surface_get_xid (gdk_surface);
        g_autofree char *window_handle = g_strdup_printf ("x11:%x", xid);

        real_call_show_file (uri, window_handle, close_if_already_visible, activation_token);
        return;
    }
#endif
#ifdef GDK_WINDOWING_WAYLAND
    if (GDK_IS_WAYLAND_DISPLAY (gtk_widget_get_display (GTK_WIDGET (window))))
    {
        g_autoptr (PreviewExportData) data = g_new0 (PreviewExportData, 1);

        data->uri = g_strdup (uri);
        data->close_if_already_visible = close_if_already_visible;
        data->activation_token = g_strdup (activation_token);

        if (gdk_wayland_toplevel_export_handle (GDK_WAYLAND_TOPLEVEL (gdk_surface),
                                                wayland_window_handle_exported,
                                                data,
                                                preview_export_data_free))
        {
            /* Don't let autoptr free data successfully taken by the call. */
            (void) g_steal_pointer (&data);
            return;
        }
    }
#endif

    g_warning ("Couldn't export handle, unsupported windowing system");

    /* Let's use a fallback, so at least a preview will be displayed */
    real_call_show_file (uri, "x11:0", close_if_already_visible, activation_token);
}

static void
real_call_show_file (const gchar *uri,
                     const gchar *window_handle,
                     gboolean     close_if_already_visible,
                     const char  *activation_token)
{
    g_set_str (&exported_window_handle, window_handle);

    if (!ensure_previewer_proxy ())
    {
        return;
    }

    if (activation_token == NULL)
    {
        activation_token = "";
    }

    GVariant *parameters = g_variant_new (
        "(ssbs)",
        uri, window_handle, close_if_already_visible, activation_token);
    g_dbus_proxy_call (previewer_proxy,
                       "ShowFile",
                       parameters,
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       cancellable,
                       previewer2_method_ready_cb,
                       NULL);
}

void
nautilus_previewer_call_close (void)
{
    if (!ensure_previewer_proxy ())
    {
        return;
    }

    /* don't autostart the previewer if it's not running */
    g_dbus_proxy_call (previewer_proxy,
                       "Close",
                       NULL,
                       G_DBUS_CALL_FLAGS_NO_AUTO_START,
                       -1,
                       cancellable,
                       previewer2_method_ready_cb,
                       NULL);
}

static void
previewer_selection_event (GDBusConnection *connection,
                           const gchar     *sender_name,
                           const gchar     *object_path,
                           const gchar     *interface_name,
                           const gchar     *signal_name,
                           GVariant        *parameters,
                           gpointer         user_data)
{
    if (current_slot == NULL)
    {
        return;
    }

    NautilusFilesView *view = nautilus_window_slot_get_current_view (current_slot);
    GtkDirectionType direction;

    if (view == NULL)
    {
        return;
    }

    g_variant_get (parameters, "(u)", &direction);
    nautilus_files_view_preview_selection_event (view, direction);
}

void
nautilus_previewer_setup_for_connection (GDBusConnection *connection, const char *profile)
{
    g_return_if_fail (G_IS_DBUS_CONNECTION (connection));
    g_return_if_fail (profile != NULL);
    nautilus_previewer_teardown (NULL);
    initialized = TRUE;
    set_profile (profile);
    previewer_connection = g_object_ref (connection);
    watch_providers ();
    restart_discovery ();
}

void
nautilus_previewer_setup (void)
{
    if (initialized) { return; }
    initialized = TRUE;
    set_profile (PROFILE);
    cancellable = g_cancellable_new ();
    ensure_previewer_proxy ();
}

void
nautilus_previewer_teardown (GDBusConnection *connection)
{
    initialized = FALSE;
    discovery_generation++;
    g_cancellable_cancel (cancellable);
    clear_proxy ();
    if (owner_subscription_id != 0)
    {
        g_dbus_connection_signal_unsubscribe (previewer_connection, owner_subscription_id);
        owner_subscription_id = 0;
    }
    g_clear_object (&cancellable);
    g_clear_object (&previewer_connection);
    g_clear_pointer (&previewer_names, g_strfreev);
    fetching_bus = FALSE;
    previewer_index = 0;
    retry_after = 0;
    clear_exported_window_handle ();
    g_clear_weak_pointer (&current_window);
    g_clear_weak_pointer (&current_slot);
}

gboolean
nautilus_previewer_is_visible (void)
{
    if (!ensure_previewer_proxy ())
    {
        return FALSE;
    }

    g_autoptr (GVariant) variant = g_dbus_proxy_get_cached_property (previewer_proxy, "Visible");

    if (variant)
    {
        return g_variant_get_boolean (variant);
    }

    return FALSE;
}
