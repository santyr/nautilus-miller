/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "miller-test-input.h"
#include <glib/gstdio.h>
#include <math.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define REMOTE "org.gnome.Mutter.RemoteDesktop"
#define REMOTE_PATH "/org/gnome/Mutter/RemoteDesktop"
#define SESSION REMOTE ".Session"
#define BUS "org.freedesktop.DBus"
#define BUS_PATH "/org/freedesktop/DBus"

struct _MillerTestInput
{
    GDBusConnection *connection;
    char *owner;
    char *session;
    guint pid;
};

static gboolean
deny (GError **error, const char *message)
{
    g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, message);
    return FALSE;
}

static gboolean
owned_path (const char *path, mode_t type, gboolean private)
{
    struct stat st;
    return path != NULL && g_lstat (path, &st) == 0 && (st.st_mode & S_IFMT) == type &&
           st.st_uid == geteuid () && (!private || (st.st_mode & 0077) == 0);
}

gboolean
miller_test_input_validate (const char *marker, const char *runtime, const char *display,
                           const char *address, guint *pid, GError **error)
{
    if (pid != NULL) { *pid = 0; }
    if (marker == NULL || runtime == NULL || display == NULL || address == NULL ||
        !g_path_is_absolute (runtime) || !g_str_has_prefix (address, "unix:"))
    {
        return deny (error, "Input requires the owned private test session");
    }
    g_autofree char *resolved = realpath (runtime, NULL);
    g_autofree char *root = g_path_get_dirname (runtime);
    g_autofree char *root_name = g_path_get_basename (root);
    g_autofree char *expected_runtime = g_build_filename (root, "runtime", NULL);
    g_autofree char *expected_marker = g_build_filename (runtime, "miller-session.ini", NULL);
    g_autofree char *expected_display = g_build_filename (runtime, "miller-test", NULL);
    if (g_strcmp0 (resolved, runtime) != 0 || !g_str_has_prefix (root_name, "nautilus-miller-tests-") ||
        g_strcmp0 (runtime, expected_runtime) != 0 || g_strcmp0 (marker, expected_marker) != 0 ||
        g_strcmp0 (display, expected_display) != 0 || !owned_path (root, S_IFDIR, TRUE) ||
        !owned_path (runtime, S_IFDIR, TRUE) || !owned_path (marker, S_IFREG, TRUE) ||
        !owned_path (display, S_IFSOCK, FALSE))
    {
        return deny (error, "Input refuses an unowned, unsafe or non-private runtime/display/marker");
    }
    g_autoptr (GKeyFile) data = g_key_file_new ();
    if (!g_key_file_load_from_file (data, marker, G_KEY_FILE_NONE, NULL))
    {
        return deny (error, "Invalid private input marker");
    }
    g_autofree char *recorded_runtime = g_key_file_get_string (data, "session", "runtime", NULL);
    g_autofree char *recorded_display = g_key_file_get_string (data, "session", "display", NULL);
    g_autofree char *recorded_address = g_key_file_get_string (data, "session", "bus-address", NULL);
    gint recorded_pid = g_key_file_get_integer (data, "session", "compositor-pid", NULL);
    if (g_strcmp0 (recorded_runtime, runtime) != 0 || g_strcmp0 (recorded_display, display) != 0 ||
        g_strcmp0 (recorded_address, address) != 0 || recorded_pid <= 0)
    {
        return deny (error, "Input marker does not match the current private session");
    }
    if (pid != NULL) { *pid = recorded_pid; }
    return TRUE;
}

static GVariant *
bus_call (MillerTestInput *input, const char *method, GVariant *parameters,
          const GVariantType *reply, GError **error)
{
    return g_dbus_connection_call_sync (input->connection, BUS, BUS_PATH, BUS, method,
        parameters, reply, G_DBUS_CALL_FLAGS_NO_AUTO_START, 3000, NULL, error);
}

static gboolean
check_owner (MillerTestInput *input, GError **error)
{
    g_autoptr (GVariant) owner = bus_call (input, "GetNameOwner", g_variant_new ("(s)", REMOTE),
                                         G_VARIANT_TYPE ("(s)"), error);
    if (owner == NULL) { return FALSE; }
    const char *name;
    g_variant_get (owner, "(&s)", &name);
    if (g_strcmp0 (name, input->owner) != 0)
    {
        return deny (error, "Private compositor owner changed; input session is invalid");
    }
    g_autoptr (GVariant) process = bus_call (input, "GetConnectionUnixProcessID", g_variant_new ("(s)", name),
                                           G_VARIANT_TYPE ("(u)"), error);
    if (process == NULL) { return FALSE; }
    guint pid;
    g_variant_get (process, "(u)", &pid);
    return pid == input->pid || deny (error, "Private compositor owner does not match session marker");
}

static GVariant *
remote_call (MillerTestInput *input, const char *path, const char *interface,
             const char *method, GVariant *parameters, const GVariantType *reply, GError **error)
{
    g_autoptr (GVariant) owned_parameters = parameters != NULL ? g_variant_ref_sink (parameters) : NULL;
    if (!check_owner (input, error)) { return NULL; }
    /* The unique owner prevents a well-known-name restart race from retargeting. */
    return g_dbus_connection_call_sync (input->connection, input->owner, path, interface,
        method, owned_parameters, reply, G_DBUS_CALL_FLAGS_NO_AUTO_START, 3000, NULL, error);
}

static gboolean
notify (MillerTestInput *input, const char *method, GVariant *parameters, GError **error)
{
    g_autoptr (GVariant) result = remote_call (input, input->session, SESSION, method, parameters,
                                              G_VARIANT_TYPE_UNIT, error);
    return result != NULL;
}

MillerTestInput *
miller_test_input_new (GError **error)
{
    const char *address = g_getenv ("DBUS_SESSION_BUS_ADDRESS");
    guint pid;
    if (!miller_test_input_validate (g_getenv ("MILLER_TEST_SESSION"), g_getenv ("XDG_RUNTIME_DIR"),
                                    g_getenv ("WAYLAND_DISPLAY"), address, &pid, error)) { return NULL; }
    g_autoptr (MillerTestInput) input = g_new0 (MillerTestInput, 1);
    input->pid = pid;
    input->connection = g_dbus_connection_new_for_address_sync (address,
        G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
        NULL, NULL, error);
    if (input->connection == NULL) { return NULL; }
    g_autoptr (GVariant) owner = bus_call (input, "GetNameOwner", g_variant_new ("(s)", REMOTE),
                                         G_VARIANT_TYPE ("(s)"), error);
    if (owner == NULL) { return NULL; }
    g_variant_get (owner, "(s)", &input->owner);
    if (!check_owner (input, error)) { return NULL; }
    g_autofree char *proc = g_strdup_printf ("/proc/%u", pid);
    g_autofree char *exe_path = g_build_filename (proc, "exe", NULL);
    g_autofree char *exe = g_file_read_link (exe_path, NULL);
    g_autofree char *basename = exe != NULL ? g_path_get_basename (exe) : NULL;
    g_autofree char *cmd_path = g_build_filename (proc, "cmdline", NULL);
    g_autofree char *cmd = NULL;
    gsize length = 0;
    gboolean headless = FALSE, display = FALSE;
    if (g_file_get_contents (cmd_path, &cmd, &length, NULL))
    {
        for (gsize i = 0; i < length; i += strlen (cmd + i) + 1)
        {
            headless |= g_str_equal (cmd + i, "--headless");
            display |= g_str_equal (cmd + i, "--wayland-display=miller-test");
        }
    }
    if (!owned_path (proc, S_IFDIR, FALSE) || g_strcmp0 (basename, "mutter") != 0 || !headless || !display)
    {
        deny (error, "Input refuses a process other than its owned headless compositor");
        return NULL;
    }
    g_autoptr (GVariant) session = remote_call (input, REMOTE_PATH, REMOTE, "CreateSession", NULL,
                                               G_VARIANT_TYPE ("(o)"), error);
    if (session == NULL) { return NULL; }
    g_variant_get (session, "(o)", &input->session);
    if (!notify (input, "Start", NULL, error) ||
        !miller_test_input_keycode (input, 42, TRUE, error) ||
        !miller_test_input_keycode (input, 42, FALSE, error) ||
        !miller_test_input_motion (input, 0, 0, error)) { return NULL; }
    return g_steal_pointer (&input);
}

void
miller_test_input_free (MillerTestInput *input)
{
    if (input == NULL) { return; }
    if (input->session != NULL)
    {
        g_autoptr (GVariant) stopped = g_dbus_connection_call_sync (input->connection, input->owner,
            input->session, SESSION, "Stop", NULL, NULL, G_DBUS_CALL_FLAGS_NO_AUTO_START, 1000, NULL, NULL);
    }
    g_clear_object (&input->connection);
    g_free (input->owner);
    g_free (input->session);
    g_free (input);
}

gboolean
miller_test_input_key (MillerTestInput *input, guint keysym, gboolean pressed, GError **error)
{
    return notify (input, "NotifyKeyboardKeysym", g_variant_new ("(ub)", keysym, pressed), error);
}

gboolean
miller_test_input_keycode (MillerTestInput *input, guint keycode, gboolean pressed, GError **error)
{
    return notify (input, "NotifyKeyboardKeycode", g_variant_new ("(ub)", keycode, pressed), error);
}

gboolean
miller_test_input_button (MillerTestInput *input, guint button, gboolean pressed, GError **error)
{
    if (button > 0x2ff) { g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid input button"); return FALSE; }
    return notify (input, "NotifyPointerButton", g_variant_new ("(ib)", (gint) button, pressed), error);
}

gboolean
miller_test_input_motion (MillerTestInput *input, double x, double y, GError **error)
{
    if (!isfinite (x) || !isfinite (y)) { g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid input coordinates"); return FALSE; }
    return notify (input, "NotifyPointerMotionRelative", g_variant_new ("(dd)", x, y), error);
}
