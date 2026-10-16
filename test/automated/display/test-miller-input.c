/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "miller-test-input.h"
#include <gtk/gtk.h>
#include <unistd.h>

static MillerTestInput *input;

static void
key (guint keysym, gboolean pressed)
{
    g_autoptr (GError) error = NULL;
    g_assert_true (miller_test_input_key (input, keysym, pressed, &error));
    g_assert_no_error (error);
}

static void
drain_for (gint64 duration)
{
    gint64 end = g_get_monotonic_time () + duration;
    while (g_get_monotonic_time () < end)
    {
        while (g_main_context_iteration (NULL, FALSE)) {}
        g_usleep (1000);
    }
}

static void
test_genuine_entry_input (void)
{
    if (input == NULL) { g_test_skip ("Requires with-test-session.py private compositor"); return; }
    GtkWidget *window = gtk_window_new ();
    GtkWidget *entry = gtk_entry_new ();
    gtk_window_set_child (GTK_WINDOW (window), entry);
    gtk_window_fullscreen (GTK_WINDOW (window));
    gtk_window_present (GTK_WINDOW (window));
    gtk_widget_grab_focus (entry);
    gint64 end = g_get_monotonic_time () + 5 * G_TIME_SPAN_SECOND;
    while (!gtk_window_is_active (GTK_WINDOW (window)) && g_get_monotonic_time () < end)
    {
        g_main_context_iteration (NULL, FALSE);
        g_usleep (1000);
    }
    g_assert_true (gtk_window_is_active (GTK_WINDOW (window)));
    key (GDK_KEY_x, TRUE); key (GDK_KEY_x, FALSE);
    drain_for (200 * G_TIME_SPAN_MILLISECOND);
    g_assert_cmpstr (gtk_editable_get_text (GTK_EDITABLE (entry)), ==, "x");
    key (GDK_KEY_Control_L, TRUE); key (GDK_KEY_a, TRUE); key (GDK_KEY_a, FALSE); key (GDK_KEY_Control_L, FALSE);
    key (GDK_KEY_Shift_L, TRUE); key (GDK_KEY_x, TRUE); key (GDK_KEY_x, FALSE); key (GDK_KEY_Shift_L, FALSE);
    drain_for (200 * G_TIME_SPAN_MILLISECOND);
    g_assert_cmpstr (gtk_editable_get_text (GTK_EDITABLE (entry)), ==, "X");
    gtk_root_set_focus (GTK_ROOT (window), NULL);
    g_autoptr (GError) error = NULL;
    g_assert_true (miller_test_input_motion (input, -10000, -10000, &error));
    g_assert_true (miller_test_input_motion (input, 640, 450, &error));
    g_assert_true (miller_test_input_button (input, 272, TRUE, &error));
    g_assert_true (miller_test_input_button (input, 272, FALSE, &error));
    g_assert_no_error (error);
    drain_for (200 * G_TIME_SPAN_MILLISECOND);
    GtkWidget *focused = gtk_root_get_focus (GTK_ROOT (window));
    g_assert_nonnull (focused);
    g_assert_true (focused == entry || gtk_widget_is_ancestor (focused, entry));
    gtk_root_set_focus (GTK_ROOT (window), NULL);
    gtk_window_destroy (GTK_WINDOW (window));
    drain_for (100 * G_TIME_SPAN_MILLISECOND);
}

static void
test_reject_changed_owner_pid (void)
{
    if (input == NULL) { g_test_skip ("Requires private compositor"); return; }
    const char *marker = g_getenv ("MILLER_TEST_SESSION");
    g_autofree char *original = NULL;
    g_assert_true (g_file_get_contents (marker, &original, NULL, NULL));
    g_autoptr (GKeyFile) data = g_key_file_new ();
    g_assert_true (g_key_file_load_from_data (data, original, -1, G_KEY_FILE_NONE, NULL));
    g_key_file_set_integer (data, "session", "compositor-pid", getpid ());
    g_assert_true (g_key_file_save_to_file (data, marker, NULL));
    g_autoptr (GError) error = NULL;
    MillerTestInput *rejected = miller_test_input_new (&error);
    g_assert_true (g_file_set_contents (marker, original, -1, NULL));
    g_assert_null (rejected);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_nonnull (strstr (error->message, "owner"));
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    if (g_getenv ("MILLER_TEST_SESSION") != NULL)
    {
        g_autoptr (GError) error = NULL;
        input = miller_test_input_new (&error);
        g_assert_no_error (error);
        g_assert_nonnull (input);
        gtk_init ();
    }
    g_test_add_func ("/miller/input/owner-pid-guard", test_reject_changed_owner_pid);
    g_test_add_func ("/miller/input/genuine-entry", test_genuine_entry_input);
    int result = g_test_run ();
    miller_test_input_free (input);
    return result;
}
