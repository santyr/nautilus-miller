/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <gio/gio.h>
#include "nautilus-previewer.h"

static GDBusConnection *connection;
static GDBusNodeInfo *node;
static guint canonical_registration;
static guint private_registration;

static GVariant *
get_property (GDBusConnection *bus, const char *sender,
              const char *path, const char *interface, const char *property,
              GError **error, gpointer data)
{
    return g_variant_new_boolean (GPOINTER_TO_INT (data));
}

static const GDBusInterfaceVTable vtable = { .get_property = get_property };

static void
request_name (const char *name, gboolean acquire)
{
    g_autoptr (GError) error = NULL;
    g_autoptr (GVariant) reply = g_dbus_connection_call_sync (connection,
        "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
        acquire ? "RequestName" : "ReleaseName",
        acquire ? g_variant_new ("(su)", name, 0) : g_variant_new ("(s)", name),
        NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    g_assert_no_error (error);
    g_assert_nonnull (reply);
}

static guint
register_provider (const char *suffix, gboolean visible)
{
    g_autofree char *path = g_strconcat ("/org/gnome/NautilusPreviewer", suffix, NULL);
    g_autofree char *name = g_strconcat ("org.gnome.NautilusPreviewer", suffix, NULL);
    guint registration = g_dbus_connection_register_object (connection, path,
        node->interfaces[0], &vtable, GINT_TO_POINTER (visible), NULL, NULL);
    g_assert_cmpuint (registration, >, 0);
    request_name (name, TRUE);
    return registration;
}

static void
wait_visible (gboolean expected)
{
    gint64 deadline = g_get_monotonic_time () + 5 * G_TIME_SPAN_SECOND;
    while (nautilus_previewer_is_visible () != expected && g_get_monotonic_time () < deadline)
    {
        g_main_context_iteration (NULL, FALSE);
        g_usleep (1000);
    }
    g_assert_cmpint (nautilus_previewer_is_visible (), ==, expected);
}

static void
spin (guint milliseconds)
{
    gint64 deadline = g_get_monotonic_time () + milliseconds * G_TIME_SPAN_MILLISECOND;
    while (g_get_monotonic_time () < deadline)
    {
        g_main_context_iteration (NULL, FALSE);
        g_usleep (1000);
    }
}

static void
cleanup (void)
{
    nautilus_previewer_teardown (connection);
    if (canonical_registration != 0)
    {
        request_name ("org.gnome.NautilusPreviewer", FALSE);
        g_dbus_connection_unregister_object (connection, canonical_registration);
        canonical_registration = 0;
    }
    if (private_registration != 0)
    {
        request_name ("org.gnome.NautilusPreviewerMiller", FALSE);
        g_dbus_connection_unregister_object (connection, private_registration);
        private_registration = 0;
    }
    spin (20);
}

static void
test_normal_setup (void)
{
    canonical_registration = register_provider ("", TRUE);
    nautilus_previewer_setup ();
    wait_visible (TRUE);
    cleanup ();
}

static void
test_canonical_service (gconstpointer profile)
{
    canonical_registration = register_provider ("", TRUE);
    nautilus_previewer_setup_for_connection (connection, profile);
    wait_visible (TRUE);
    cleanup ();
}

static void
test_profile_preference (void)
{
    canonical_registration = register_provider ("", FALSE);
    private_registration = register_provider ("Miller", TRUE);
    nautilus_previewer_setup_for_connection (connection, "Miller");
    wait_visible (TRUE);
    cleanup ();
}

static void
test_missing_then_appears (void)
{
    nautilus_previewer_setup_for_connection (connection, "Miller");
    spin (100);
    g_assert_false (nautilus_previewer_is_visible ());
    canonical_registration = register_provider ("", TRUE);
    wait_visible (TRUE);
    cleanup ();
}

static void
test_provider_restart (void)
{
    canonical_registration = register_provider ("", TRUE);
    nautilus_previewer_setup_for_connection (connection, "Miller");
    wait_visible (TRUE);
    request_name ("org.gnome.NautilusPreviewer", FALSE);
    g_dbus_connection_unregister_object (connection, canonical_registration);
    canonical_registration = 0;
    wait_visible (FALSE);
    /* A real restart has a new unique bus owner, not just a reacquired name. */
    GDBusConnection *first_owner = connection;
    connection = g_dbus_connection_new_for_address_sync (
        g_getenv ("DBUS_SESSION_BUS_ADDRESS"),
        G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
        NULL, NULL, NULL);
    g_assert_nonnull (connection);
    canonical_registration = register_provider ("", TRUE);
    wait_visible (TRUE);
    cleanup ();
    g_clear_object (&connection);
    connection = first_owner;
}

static void
test_teardown_during_discovery (void)
{
    canonical_registration = register_provider ("", TRUE);
    nautilus_previewer_setup_for_connection (connection, "Miller");
    nautilus_previewer_teardown (connection);
    spin (100);
    g_assert_false (nautilus_previewer_is_visible ());
    nautilus_previewer_setup_for_connection (connection, "Devel");
    wait_visible (TRUE);
    cleanup ();
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_autoptr (GTestDBus) bus = g_test_dbus_new (G_TEST_DBUS_NONE);
    g_test_dbus_up (bus);
    connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
    node = g_dbus_node_info_new_for_xml (
        "<node><interface name='org.gnome.NautilusPreviewer2'>"
        "<property name='Visible' type='b' access='read'/></interface></node>", NULL);
    g_test_add_func ("/previewer/profile-setup", test_normal_setup);
    g_test_add_data_func ("/previewer/production-canonical", "", test_canonical_service);
    g_test_add_data_func ("/previewer/miller-canonical", "Miller", test_canonical_service);
    g_test_add_data_func ("/previewer/devel-canonical", "Devel", test_canonical_service);
    g_test_add_func ("/previewer/profile-preference", test_profile_preference);
    g_test_add_func ("/previewer/missing-then-appears", test_missing_then_appears);
    g_test_add_func ("/previewer/provider-restart", test_provider_restart);
    g_test_add_func ("/previewer/teardown-during-discovery", test_teardown_during_discovery);
    int result = g_test_run ();
    g_clear_pointer (&node, g_dbus_node_info_unref);
    g_clear_object (&connection);
    g_test_dbus_down (bus);
    return result;
}
