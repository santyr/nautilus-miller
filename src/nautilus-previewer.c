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

#include "nautilus-file.h"
#include "nautilus-files-view.h"
#include "nautilus-window.h"
#include "nautilus-window-slot.h"

#include <gio/gio.h>
#include <glib/gi18n.h>

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
static GSignalGroup *slot_signals;
static gboolean owner_closed;

static void
owner_active_changed (NautilusWindowSlot *slot, GParamSpec *pspec, gpointer data)
{
    if (slot == current_slot && !nautilus_window_slot_get_active (slot))
    {
        nautilus_previewer_call_close ();
    }
}

static void
owner_unmapped (GtkWidget *widget, gpointer data)
{
    if (widget == GTK_WIDGET (current_slot)) { nautilus_previewer_call_close (); }
}

typedef struct
{
    char *uri, *activation_token;
    gboolean close_if_already_visible;
    guint64 generation;
    gint64 expires;
} PendingShow;

static PendingShow *pending_show;
static guint64 show_generation, inflight_generation, window_generation;
static guint pending_timeout;
static gboolean export_pending;
static void try_show_file (void);
static gboolean ensure_previewer_proxy (void);

static void
pending_show_free (PendingShow *show)
{
    g_free (show->uri);
    g_free (show->activation_token);
    g_free (show);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC (PendingShow, pending_show_free)

static gboolean
owner_is_live (void)
{
    return initialized && !owner_closed && current_slot != NULL && current_window != NULL &&
           nautilus_window_slot_get_active (current_slot) &&
           gtk_widget_get_mapped (GTK_WIDGET (current_slot)) &&
           gtk_widget_get_root (GTK_WIDGET (current_slot)) == current_window;
}

static gboolean
previewer_has_our_parent (void)
{
    if (!previewer_ready || exported_window_handle == NULL) { return FALSE; }
    g_autoptr (GVariant) parent = g_dbus_proxy_get_cached_property (previewer_proxy, "ParentHandle");
    /* Sushi >=45 publishes the actual owner. Older providers retain their
     * established contract, which cannot distinguish another client's window. */
    return parent == NULL || g_str_equal (g_variant_get_string (parent, NULL), exported_window_handle);
}

static void
cancel_pending_show (void)
{
    show_generation++;
    g_clear_handle_id (&pending_timeout, g_source_remove);
    g_clear_pointer (&pending_show, pending_show_free);
}

static void
preview_unavailable (void)
{
    gboolean notify = owner_is_live ();
    cancel_pending_show ();
    owner_closed = TRUE;
    if (notify && NAUTILUS_IS_WINDOW (current_window))
    {
        AdwToast *toast = adw_toast_new (_("Preview unavailable. Check that Sushi is installed and running."));
        adw_toast_set_use_markup (toast, FALSE);
        nautilus_window_show_toast (NAUTILUS_WINDOW (current_window), toast);
    }
}

static gboolean
pending_show_expired (gpointer data)
{
    pending_timeout = 0;
    preview_unavailable ();
    return G_SOURCE_REMOVE;
}

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
    guint64 window_generation;
    gboolean completed;
} PreviewExportData;

static void
preview_export_data_free (gpointer pointer)
{
    PreviewExportData *data = pointer;
    if (!data->completed)
    {
        export_pending = FALSE;
        g_free (data);
        try_show_file ();
        return;
    }
    g_free (data);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC (PreviewExportData, preview_export_data_free)

static void
wayland_window_handle_exported (GdkToplevel *toplevel,
                                const char *handle,
                                gpointer user_data)
{
    PreviewExportData *data = user_data;
    gboolean valid = initialized && data->window_generation == window_generation && current_window != NULL;
    data->completed = TRUE;
    export_pending = FALSE;
    if (valid)
    {
        g_free (exported_window_handle);
        exported_window_handle = g_strdup_printf ("wayland:%s", handle);
    }
    else
    {
        gdk_wayland_toplevel_drop_exported_handle (GDK_WAYLAND_TOPLEVEL (toplevel), handle);
    }
    try_show_file ();
}
#endif

static void
clear_exported_window_handle (void)
{
    window_generation++;
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
    try_show_file ();
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
previewer2_method_ready_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
    g_autoptr (PendingShow) show = user_data;
    g_autoptr (GError) error = NULL;
    g_autoptr (GVariant) reply = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
    if (show == NULL) { return; }
    if (inflight_generation == show->generation) { inflight_generation = 0; }
    if (error != NULL && initialized && show->generation == show_generation && owner_is_live ())
    {
        preview_unavailable ();
    }
    try_show_file ();
}

static void
try_show_file (void)
{
    if (pending_show == NULL) { return; }
    if (!owner_is_live ()) { cancel_pending_show (); return; }
    NautilusFilesView *view = nautilus_window_slot_get_current_view (current_slot);
    g_autolist (NautilusFile) selection = view != NULL ? nautilus_files_view_get_selection (view) : NULL;
    if (selection == NULL || nautilus_file_is_gone (selection->data))
    {
        cancel_pending_show ();
        owner_closed = TRUE;
        return;
    }
    /* Discovery/export can finish before FilesView's coalesced notification.
     * Revalidate through its current selection, keeping the original deadline. */
    g_autofree char *uri = nautilus_file_get_uri (selection->data);
    if (!g_str_equal (pending_show->uri, uri))
    {
        g_free (pending_show->uri);
        pending_show->uri = g_steal_pointer (&uri);
        pending_show->close_if_already_visible = FALSE;
    }
    GtkWidget *focus = gtk_root_get_focus (current_window);
    if (focus != NULL && view != NULL && focus != GTK_WIDGET (view) &&
        !gtk_widget_is_ancestor (focus, GTK_WIDGET (view)))
    {
        cancel_pending_show ();
        owner_closed = TRUE;
        return;
    }
    if (g_get_monotonic_time () >= pending_show->expires) { preview_unavailable (); return; }
    ensure_previewer_proxy ();
    if (exported_window_handle == NULL)
    {
        if (export_pending) { return; }
        GdkSurface *surface = gtk_native_get_surface (GTK_NATIVE (current_window));
#ifdef GDK_WINDOWING_X11
        if (GDK_IS_X11_SURFACE (surface))
        {
            exported_window_handle = g_strdup_printf ("x11:%x", (guint) gdk_x11_surface_get_xid (surface));
        }
        else
#endif
#ifdef GDK_WINDOWING_WAYLAND
        if (GDK_IS_WAYLAND_TOPLEVEL (surface))
        {
            g_autoptr (PreviewExportData) data = g_new0 (PreviewExportData, 1);
            data->window_generation = window_generation;
            export_pending = TRUE;
            if (gdk_wayland_toplevel_export_handle (GDK_WAYLAND_TOPLEVEL (surface),
                wayland_window_handle_exported, data, preview_export_data_free))
            {
                (void) g_steal_pointer (&data);
                return;
            }
            preview_unavailable ();
            return;
        }
        else
#endif
        {
            preview_unavailable ();
            return;
        }
    }
    if (!previewer_ready || inflight_generation != 0) { return; }
    g_autoptr (PendingShow) show = g_steal_pointer (&pending_show);
    g_clear_handle_id (&pending_timeout, g_source_remove);
    inflight_generation = show->generation;
    gint remaining = MAX (1, (show->expires - g_get_monotonic_time ()) / 1000);
    GVariant *parameters = g_variant_new ("(ssbs)", show->uri, exported_window_handle,
        show->close_if_already_visible, show->activation_token != NULL ? show->activation_token : "");
    g_dbus_proxy_call (previewer_proxy, "ShowFile", parameters, G_DBUS_CALL_FLAGS_NONE,
        remaining, cancellable, previewer2_method_ready_cb, g_steal_pointer (&show));
}

void
nautilus_previewer_call_show_file (const char *uri, NautilusWindowSlot *slot,
                                  gboolean close_if_already_visible)
{
    if (!nautilus_window_slot_get_active (slot) || gtk_widget_get_root (GTK_WIDGET (slot)) == NULL) { return; }
    if (slot_signals == NULL)
    {
        slot_signals = g_signal_group_new (NAUTILUS_TYPE_WINDOW_SLOT);
        g_signal_group_connect (slot_signals, "notify::active", G_CALLBACK (owner_active_changed), NULL);
        g_signal_group_connect (slot_signals, "unmap", G_CALLBACK (owner_unmapped), NULL);
    }
    g_signal_group_set_target (slot_signals, slot);
    g_set_weak_pointer (&current_slot, slot);
    GtkRoot *window = gtk_widget_get_root (GTK_WIDGET (slot));
    if (current_window != window)
    {
        clear_exported_window_handle ();
        g_set_weak_pointer (&current_window, window);
    }
    cancel_pending_show ();
    owner_closed = FALSE;
    pending_show = g_new0 (PendingShow, 1);
    pending_show->uri = g_strdup (uri);
    pending_show->close_if_already_visible = close_if_already_visible;
    pending_show->generation = show_generation;
    pending_show->expires = g_get_monotonic_time () + 5 * G_TIME_SPAN_SECOND;
    g_autoptr (GdkAppLaunchContext) context = gdk_display_get_app_launch_context (gtk_root_get_display (window));
    pending_show->activation_token = g_app_launch_context_get_startup_notify_id (G_APP_LAUNCH_CONTEXT (context), NULL, NULL);
    pending_timeout = g_timeout_add (5000, pending_show_expired, NULL);
    try_show_file ();
}

void
nautilus_previewer_call_close (void)
{
    gboolean owns_preview = !owner_closed &&
        (inflight_generation != 0 || previewer_has_our_parent ());
    owner_closed = TRUE;
    cancel_pending_show ();
    if (!previewer_ready || !owns_preview)
    {
        return;
    }

    /* don't autostart the previewer if it's not running */
    g_dbus_proxy_call (previewer_proxy,
                       "Close",
                       NULL,
                       G_DBUS_CALL_FLAGS_NO_AUTO_START,
                       5000,
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
    if (!owner_is_live () || !previewer_has_our_parent ())
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
    cancel_pending_show ();
    owner_closed = TRUE;
    inflight_generation = 0;
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
    g_clear_object (&slot_signals);
    g_clear_weak_pointer (&current_window);
    g_clear_weak_pointer (&current_slot);
}

gboolean
nautilus_previewer_is_active_for_slot (NautilusWindowSlot *slot)
{
    return slot != NULL && current_slot == slot && !owner_closed &&
           owner_is_live () && (pending_show != NULL || inflight_generation != 0 ||
                                (previewer_has_our_parent () && nautilus_previewer_is_visible ()));
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
