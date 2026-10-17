/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "miller-test-backend.h"
#include <gio/gunixsocketaddress.h>
#include <sys/stat.h>
#include <unistd.h>

GKeyFile *
miller_test_backend_metadata (void)
{
    const char *runtime = g_getenv ("XDG_RUNTIME_DIR");
    g_autofree char *expected = g_build_filename (runtime, "miller-backend.ini", NULL);
    g_assert_cmpstr (g_getenv ("MILLER_TEST_BACKEND"), ==, expected);
    struct stat info;
    g_assert_cmpint (lstat (expected, &info), ==, 0);
    g_assert_true (S_ISREG (info.st_mode));
    g_assert_cmpuint (info.st_uid, ==, getuid ());
    g_assert_cmpuint (info.st_mode & 0777, ==, 0600);
    GKeyFile *config = g_key_file_new ();
    g_assert_true (g_key_file_load_from_file (config, expected, 0, NULL));
    g_autofree char *control = g_key_file_get_string (config, "backend", "control", NULL);
    g_autofree char *expected_control = g_build_filename (runtime, "miller-backend.sock", NULL);
    g_assert_cmpstr (control, ==, expected_control);
    return config;
}

void
miller_test_backend_control (const char *request)
{
    g_autoptr (GKeyFile) config = miller_test_backend_metadata ();
    g_autofree char *control = g_key_file_get_string (config, "backend", "control", NULL);
    g_autofree char *expected = g_build_filename (g_getenv ("XDG_RUNTIME_DIR"), "miller-backend.sock", NULL);
    g_assert_cmpstr (control, ==, expected);
    struct stat info;
    g_assert_cmpint (lstat (control, &info), ==, 0);
    g_assert_true (S_ISSOCK (info.st_mode));
    g_assert_cmpuint (info.st_uid, ==, getuid ());
    g_assert_cmpuint (info.st_mode & 0777, ==, 0600);
    g_autoptr (GSocketClient) client = g_socket_client_new ();
    g_socket_client_set_timeout (client, 5);
    g_autoptr (GSocketAddress) address = g_unix_socket_address_new (control);
    g_autoptr (GError) error = NULL;
    g_autoptr (GSocketConnection) connection = g_socket_client_connect (client, G_SOCKET_CONNECTABLE (address), NULL, &error);
    g_assert_no_error (error);
    g_autofree char *line = g_strconcat (request, "\n", NULL);
    g_assert_true (g_output_stream_write_all (g_io_stream_get_output_stream (G_IO_STREAM (connection)),
                                             line, strlen (line), NULL, NULL, &error));
    g_assert_no_error (error);
    g_autoptr (GDataInputStream) reply = g_data_input_stream_new (g_io_stream_get_input_stream (G_IO_STREAM (connection)));
    g_autofree char *result = g_data_input_stream_read_line (reply, NULL, NULL, &error);
    g_assert_no_error (error);
    g_assert_cmpstr (result, ==, "ok");
}

