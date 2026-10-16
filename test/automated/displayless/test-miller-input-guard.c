/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "miller-test-input.h"
#include <glib/gstdio.h>
#include <unistd.h>

typedef struct
{
    char *root, *runtime, *display, *marker;
    GKeyFile *data;
    GSocket *socket;
} Fixture;
static const char *bus_address = "unix:path=/tmp/disposable-input-fixture-bus";

static void
write_marker (Fixture *fixture)
{
    g_assert_true (g_key_file_save_to_file (fixture->data, fixture->marker, NULL));
    g_assert_cmpint (g_chmod (fixture->marker, 0600), ==, 0);
}

static void
setup (Fixture *fixture, gconstpointer unused)
{
    fixture->root = g_dir_make_tmp ("nautilus-miller-tests-XXXXXX", NULL);
    fixture->runtime = g_build_filename (fixture->root, "runtime", NULL);
    fixture->display = g_build_filename (fixture->runtime, "miller-test", NULL);
    fixture->marker = g_build_filename (fixture->runtime, "miller-session.ini", NULL);
    g_assert_cmpint (g_mkdir (fixture->runtime, 0700), ==, 0);
    fixture->socket = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, NULL);
    g_autoptr (GSocketAddress) address = g_unix_socket_address_new (fixture->display);
    g_assert_true (g_socket_bind (fixture->socket, address, FALSE, NULL));
    fixture->data = g_key_file_new ();
    g_key_file_set_string (fixture->data, "session", "runtime", fixture->runtime);
    g_key_file_set_string (fixture->data, "session", "display", fixture->display);
    g_key_file_set_string (fixture->data, "session", "bus-address", bus_address);
    g_key_file_set_integer (fixture->data, "session", "compositor-pid", getpid ());
    write_marker (fixture);
}

static void
teardown (Fixture *fixture, gconstpointer unused)
{
    g_clear_object (&fixture->socket);
    g_remove (fixture->marker);
    g_remove (fixture->display);
    g_rmdir (fixture->runtime);
    g_rmdir (fixture->root);
    g_key_file_unref (fixture->data);
    g_free (fixture->root); g_free (fixture->runtime);
    g_free (fixture->display); g_free (fixture->marker);
}

static void
assert_rejected (Fixture *fixture, const char *runtime, const char *display, const char *address)
{
    g_autoptr (GError) error = NULL;
    guint pid = 0;
    g_assert_false (miller_test_input_validate (fixture->marker, runtime, display, address, &pid, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_cmpuint (pid, ==, 0);
}

static void
test_valid (Fixture *fixture, gconstpointer unused)
{
    guint pid = 0;
    g_assert_true (miller_test_input_validate (fixture->marker, fixture->runtime, fixture->display, bus_address, &pid, NULL));
    g_assert_cmpuint (pid, ==, getpid ());
}
static void test_missing (Fixture *f, gconstpointer d) { g_remove (f->marker); assert_rejected (f, f->runtime, f->display, bus_address); }
static void test_mode (Fixture *f, gconstpointer d) { g_chmod (f->marker, 0644); assert_rejected (f, f->runtime, f->display, bus_address); }
static void test_runtime_mode (Fixture *f, gconstpointer d) { g_chmod (f->runtime, 0755); assert_rejected (f, f->runtime, f->display, bus_address); }
static void test_display (Fixture *f, gconstpointer d) { assert_rejected (f, f->runtime, "/run/user/1000/wayland-0", bus_address); }
static void test_bus (Fixture *f, gconstpointer d) { assert_rejected (f, f->runtime, f->display, "unix:path=/run/user/1000/bus"); }
static void test_runtime (Fixture *f, gconstpointer d) { assert_rejected (f, "/run/user/1000", f->display, bus_address); }
static void test_pid (Fixture *f, gconstpointer d) { g_key_file_set_integer (f->data, "session", "compositor-pid", 0); write_marker (f); assert_rejected (f, f->runtime, f->display, bus_address); }
static void test_symlink (Fixture *f, gconstpointer d) { g_remove (f->marker); g_assert_cmpint (symlink ("miller-test", f->marker), ==, 0); assert_rejected (f, f->runtime, f->display, bus_address); }

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
#define CASE(name, function) g_test_add ("/miller/input-guard/" name, Fixture, NULL, setup, function, teardown)
    CASE ("valid-metadata-only", test_valid);
    CASE ("missing", test_missing);
    CASE ("marker-mode", test_mode);
    CASE ("runtime-mode", test_runtime_mode);
    CASE ("display", test_display);
    CASE ("bus", test_bus);
    CASE ("host-runtime", test_runtime);
    CASE ("invalid-pid", test_pid);
    CASE ("symlink", test_symlink);
    return g_test_run ();
}
