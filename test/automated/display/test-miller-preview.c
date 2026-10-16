/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <test-utilities.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#include "nautilus-application.h"
#include "nautilus-file.h"
#include "nautilus-file-utilities.h"
#include "nautilus-global-preferences.h"
#include "nautilus-miller-preview.h"
#include "nautilus-resources.h"
#include "nautilus-tag-manager.h"

typedef struct
{
    GtkWidget *window, *entry;
    NautilusMillerPreview *preview;
    GFile *root, *first, *second;
} Fixture;

static void
pump (guint milliseconds)
{
    gint64 until = g_get_monotonic_time () + milliseconds * G_TIME_SPAN_MILLISECOND;
    while (g_get_monotonic_time () < until)
    {
        while (g_main_context_iteration (NULL, FALSE)) {}
        g_usleep (1000);
    }
}

static gboolean
loading (Fixture *f)
{
    gboolean value;
    g_object_get (f->preview, "loading", &value, NULL);
    return value;
}

static void
settle (Fixture *f)
{
    gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    while (loading (f) || g_main_context_pending (NULL))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline);
        g_main_context_iteration (NULL, FALSE);
        g_usleep (1000);
    }
    pump (30);
}

static GtkWidget *
find_type (GtkWidget *widget, GType type)
{
    if (G_TYPE_CHECK_INSTANCE_TYPE (widget, type)) { return widget; }
    for (GtkWidget *child = gtk_widget_get_first_child (widget); child != NULL; child = gtk_widget_get_next_sibling (child))
    {
        GtkWidget *found = find_type (child, type);
        if (found != NULL) { return found; }
    }
    return NULL;
}

static GtkWidget *
find_label (GtkWidget *widget, const char *fragment)
{
    if (GTK_IS_LABEL (widget) && strstr (gtk_label_get_text (GTK_LABEL (widget)), fragment) != NULL) { return widget; }
    for (GtkWidget *child = gtk_widget_get_first_child (widget); child != NULL; child = gtk_widget_get_next_sibling (child))
    {
        GtkWidget *found = find_label (child, fragment);
        if (found != NULL) { return found; }
    }
    return NULL;
}

static char *
preview_text (Fixture *f)
{
    GtkTextView *view = GTK_TEXT_VIEW (find_type (GTK_WIDGET (f->preview), GTK_TYPE_TEXT_VIEW));
    g_assert_nonnull (view);
    g_assert_false (gtk_text_view_get_editable (view));
    GtkTextBuffer *buffer = gtk_text_view_get_buffer (view);
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds (buffer, &start, &end);
    return gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
}

static void
select_one (Fixture *f, GFile *location)
{
    g_autoptr (NautilusFile) file = nautilus_file_get (location);
    GList files = { .data = file };
    nautilus_miller_preview_set_selection (f->preview, &files);
}

static void
setup (Fixture *f, gconstpointer unused)
{
    f->root = g_file_new_for_path (test_get_tmp_dir ());
    f->first = g_file_get_child (f->root, "first.txt");
    f->second = g_file_get_child (f->root, "second.txt");
    g_assert_true (g_file_replace_contents (f->first, "alpha", 5, NULL, FALSE, 0, NULL, NULL, NULL));
    g_assert_true (g_file_replace_contents (f->second, "beta", 4, NULL, FALSE, 0, NULL, NULL, NULL));
    f->window = gtk_window_new ();
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    f->entry = gtk_entry_new ();
    f->preview = NAUTILUS_MILLER_PREVIEW (nautilus_miller_preview_new ());
    gtk_box_append (GTK_BOX (box), f->entry);
    gtk_box_append (GTK_BOX (box), GTK_WIDGET (f->preview));
    gtk_window_set_child (GTK_WINDOW (f->window), box);
    gtk_window_set_default_size (GTK_WINDOW (f->window), 500, 450);
    gtk_window_present (GTK_WINDOW (f->window));
    pump (40);
    nautilus_miller_preview_set_active (f->preview, TRUE);
    settle (f);
}

static void
teardown (Fixture *f, gconstpointer unused)
{
    if (f->window != NULL) { gtk_window_destroy (GTK_WINDOW (f->window)); }
    pump (30);
    g_clear_object (&f->root); g_clear_object (&f->first); g_clear_object (&f->second);
    test_clear_tmp_dir ();
}

static void
test_empty (Fixture *f, gconstpointer unused)
{
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "Select a file to preview"));
    g_assert_false (loading (f));
}

static void
test_folder (Fixture *f, gconstpointer unused)
{
    int watch = inotify_init1 (IN_NONBLOCK | IN_CLOEXEC);
    g_assert_cmpint (watch, >=, 0);
    g_autofree char *path = g_file_get_path (f->root);
    g_assert_cmpint (inotify_add_watch (watch, path, IN_OPEN), >=, 0);
    select_one (f, f->root); settle (f);
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "Folder"));
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "Size not calculated"));
    char events[4096];
    g_assert_cmpint (read (watch, events, sizeof events), ==, -1);
    g_assert_cmpint (errno, ==, EAGAIN);
    close (watch);
}

static void
test_multiple (Fixture *f, gconstpointer unused)
{
    g_autoptr (NautilusFile) a = nautilus_file_get (f->first);
    g_autoptr (NautilusFile) b = nautilus_file_get (f->second);
    g_autoptr (NautilusFile) folder = nautilus_file_get (f->root);
    file_load_attributes (a, NAUTILUS_FILE_ATTRIBUTE_INFO);
    file_load_attributes (b, NAUTILUS_FILE_ATTRIBUTE_INFO);
    file_load_attributes (folder, NAUTILUS_FILE_ATTRIBUTE_INFO);
    GList third = { .data = folder }, second = { .data = b, .next = &third }, first = { .data = a, .next = &second };
    nautilus_miller_preview_set_selection (f->preview, &first); settle (f);
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "3 items"));
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "9 bytes"));
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "partial"));
}

static void
test_text_bounds (Fixture *f, gconstpointer unused)
{
    g_autofree char *contents = g_strnfill (200 * 1024, 'x');
    contents[128 * 1024 + 1] = '\0'; /* Outside the input bound: not inspected. */
    g_assert_true (g_file_replace_contents (f->first, contents, 200 * 1024, NULL, FALSE, 0, NULL, NULL, NULL));
    select_one (f, f->first); settle (f);
    g_autofree char *text = preview_text (f);
    g_assert_cmpuint (strlen (text), ==, 32 * 1024);
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "Truncated"));
    contents[120 * 1024] = '\0'; /* Inside the bound: binary fallback. */
    g_assert_true (g_file_replace_contents (f->first, contents, 200 * 1024, NULL, FALSE, 0, NULL, NULL, NULL));
    select_one (f, f->first); settle (f);
    g_clear_pointer (&text, g_free); text = preview_text (f);
    g_assert_cmpuint (strlen (text), ==, 0);
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "binary or invalid UTF-8"));
}

static void
test_empty_file (Fixture *f, gconstpointer unused)
{
    g_assert_true (g_file_replace_contents (f->first, "", 0, NULL, FALSE, 0, NULL, NULL, NULL));
    select_one (f, f->first); settle (f);
    g_autofree char *text = preview_text (f);
    g_assert_cmpuint (strlen (text), ==, 0);
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "Text preview"));
}

static void
test_utf8_bounds (Fixture *f, gconstpointer unused)
{
    g_autofree char *contents = g_strnfill (129 * 1024, 'x');
    memcpy (contents + 32 * 1024 - 1, "é", 2);
    memcpy (contents + 128 * 1024 - 1, "é", 2);
    g_assert_true (g_file_replace_contents (f->first, contents, 129 * 1024, NULL, FALSE, 0, NULL, NULL, NULL));
    select_one (f, f->first); settle (f);
    g_autofree char *text = preview_text (f);
    g_assert_true (g_utf8_validate (text, -1, NULL));
    g_assert_cmpuint (strlen (text), ==, 32 * 1024 - 1);
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "Truncated"));
}

static void
test_invalid_binary (Fixture *f, gconstpointer unused)
{
    const char bytes[] = { 'a', (char) 0xff, 'b' };
    g_assert_true (g_file_replace_contents (f->first, bytes, sizeof bytes, NULL, FALSE, 0, NULL, NULL, NULL));
    select_one (f, f->first); settle (f);
    g_autofree char *text = preview_text (f);
    g_assert_cmpuint (strlen (text), ==, 0);
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "binary or invalid UTF-8"));
}

static void
test_special_file (Fixture *f, gconstpointer unused)
{
    g_autoptr (GFile) fifo = g_file_get_child (f->root, "pipe.txt");
    g_autofree char *path = g_file_get_path (fifo);
    g_assert_cmpint (mkfifo (path, 0600), ==, 0);
    gint64 start = g_get_monotonic_time ();
    select_one (f, fifo); settle (f);
    g_assert_cmpint (g_get_monotonic_time () - start, <, G_TIME_SPAN_SECOND);
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "Special file"));
    g_autoptr (GFile) link = g_file_get_child (f->root, "link.txt");
    g_autofree char *target = g_file_get_path (f->first);
    g_assert_true (g_file_make_symbolic_link (link, target, NULL, NULL));
    select_one (f, link); settle (f);
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "Symbolic link"));
    g_autofree char *text = preview_text (f);
    g_assert_cmpuint (strlen (text), ==, 0);
}

static void
test_newer_selection (Fixture *f, gconstpointer unused)
{
    select_one (f, f->first);
    select_one (f, f->second);
    settle (f);
    g_autofree char *text = preview_text (f);
    g_assert_cmpstr (text, ==, "beta");
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "second.txt"));
}

static void
test_hide_dispose (Fixture *f, gconstpointer unused)
{
    select_one (f, f->first);
    gtk_widget_set_visible (GTK_WIDGET (f->preview), FALSE);
    select_one (f, f->second);
    pump (100);
    g_assert_false (loading (f));
    g_autofree char *text = preview_text (f);
    g_assert_cmpuint (strlen (text), ==, 0);
    gtk_widget_set_visible (GTK_WIDGET (f->preview), TRUE); settle (f);
    g_clear_pointer (&text, g_free); text = preview_text (f);
    g_assert_cmpstr (text, ==, "beta");
    select_one (f, f->first);
    gpointer weak = f->preview;
    g_object_add_weak_pointer (G_OBJECT (f->preview), &weak);
    gtk_window_destroy (GTK_WINDOW (f->window)); f->window = NULL; f->preview = NULL;
    pump (100);
    g_assert_null (weak);
}

static void
test_entry_focus (Fixture *f, gconstpointer unused)
{
    gtk_widget_grab_focus (f->entry);
    GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (f->window));
    select_one (f, f->first); settle (f);
    g_assert_true (gtk_root_get_focus (GTK_ROOT (f->window)) == focus);
    select_one (f, f->second); settle (f);
    g_assert_true (gtk_root_get_focus (GTK_ROOT (f->window)) == focus);
}

typedef struct { Fixture *fixture; gboolean changed; } NewSelection;
static void
select_after_metadata (GtkLabel *label, GParamSpec *pspec, NewSelection *selection)
{
    if (!selection->changed && g_str_equal (gtk_label_get_text (label), "Loading text preview…"))
    {
        selection->changed = TRUE;
        select_one (selection->fixture, selection->fixture->second);
    }
}

static void
select_after_text (GtkTextBuffer *buffer, NewSelection *selection)
{
    g_autofree char *text = preview_text (selection->fixture);
    if (!selection->changed && g_str_equal (text, "alpha"))
    {
        selection->changed = TRUE;
        select_one (selection->fixture, selection->fixture->second);
    }
}

static void
select_after_completion (GObject *object, GParamSpec *pspec, NewSelection *selection)
{
    if (!selection->changed && !loading (selection->fixture))
    {
        selection->changed = TRUE;
        select_one (selection->fixture, selection->fixture->second);
    }
}

static void
test_reentrant_selection (Fixture *f, gconstpointer kind)
{
    NewSelection selection = { .fixture = f };
    g_autoptr (GObject) emitter = NULL;
    if (GPOINTER_TO_UINT (kind) == 0)
    {
        emitter = g_object_ref (G_OBJECT (find_label (GTK_WIDGET (f->preview), "Select a file to preview")));
        g_signal_connect (emitter, "notify::label", G_CALLBACK (select_after_metadata), &selection);
    }
    else if (GPOINTER_TO_UINT (kind) == 1)
    {
        /* Selection creates its own buffer; subscribe before async content
         * arrives, rather than to the previous target's detached buffer. */
        select_one (f, f->first);
        GtkWidget *view = find_type (GTK_WIDGET (f->preview), GTK_TYPE_TEXT_VIEW);
        emitter = g_object_ref (G_OBJECT (gtk_text_view_get_buffer (GTK_TEXT_VIEW (view))));
        g_signal_connect (emitter, "changed", G_CALLBACK (select_after_text), &selection);
    }
    else
    {
        emitter = g_object_ref (G_OBJECT (f->preview));
        g_signal_connect (emitter, "notify::loading", G_CALLBACK (select_after_completion), &selection);
    }
    if (GPOINTER_TO_UINT (kind) != 1) { select_one (f, f->first); }
    settle (f);
    g_signal_handlers_disconnect_by_data (emitter, &selection);
    g_assert_true (selection.changed);
    g_autofree char *text = preview_text (f);
    g_assert_cmpstr (text, ==, "beta");
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "second.txt"));
}

static void
test_inactive (Fixture *f, gconstpointer unused)
{
    nautilus_miller_preview_set_active (f->preview, FALSE);
    select_one (f, f->first); pump (100);
    g_assert_false (loading (f));
    g_autofree char *text = preview_text (f);
    g_assert_cmpuint (strlen (text), ==, 0);
    nautilus_miller_preview_set_active (f->preview, TRUE); settle (f);
    g_clear_pointer (&text, g_free); text = preview_text (f);
    g_assert_cmpstr (text, ==, "alpha");
    select_one (f, f->second);
    nautilus_miller_preview_set_active (f->preview, FALSE);
    pump (100);
    g_assert_false (loading (f));
    g_clear_pointer (&text, g_free); text = preview_text (f);
    g_assert_cmpuint (strlen (text), ==, 0);
}

static void
test_unknown_multiple (Fixture *f, gconstpointer unused)
{
    g_autoptr (NautilusFile) known = nautilus_file_get (f->first);
    file_load_attributes (known, NAUTILUS_FILE_ATTRIBUTE_INFO);
    g_autoptr (GFile) missing = g_file_get_child (f->root, "not-loaded.txt");
    g_autoptr (NautilusFile) unknown = nautilus_file_get (missing);
    GList second = { .data = unknown }, first = { .data = known, .next = &second };
    nautilus_miller_preview_set_selection (f->preview, &first); settle (f);
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "5 bytes"));
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "partial"));
}

static gboolean
heartbeat (gpointer data)
{
    (*(guint *) data)++;
    return G_SOURCE_CONTINUE;
}

static void
test_cached_aggregation_yields (Fixture *f, gconstpointer unused)
{
    g_autoptr (NautilusFile) known = nautilus_file_get (f->first);
    file_load_attributes (known, NAUTILUS_FILE_ATTRIBUTE_INFO);
    GList *files = NULL;
    /* Repeated cached objects exercise scheduling, not a real large directory. */
    for (guint i = 0; i < 2048; i++) { files = g_list_prepend (files, known); }
    nautilus_miller_preview_set_selection (f->preview, files);
    g_list_free (files);
    guint turns = 0;
    guint idle = g_idle_add (heartbeat, &turns);
    gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    while (loading (f))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline);
        g_main_context_iteration (NULL, FALSE);
    }
    g_source_remove (idle);
    settle (f);
    g_assert_cmpuint (turns, >=, 16);
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "2048 items"));
    g_assert_null (find_label (GTK_WIDGET (f->preview), "partial"));
}

typedef struct { Fixture *fixture; guint kind; gboolean changed; } Replacement;
static void
replace_before_open (GtkLabel *label, GParamSpec *pspec, Replacement *replacement)
{
    if (replacement->changed || !g_str_equal (gtk_label_get_text (label), "Loading text preview…")) { return; }
    replacement->changed = TRUE;
    g_autofree char *path = g_file_get_path (replacement->fixture->first);
    g_autofree char *saved = g_strconcat (path, ".saved", NULL);
    g_assert_cmpint (g_rename (path, saved), ==, 0);
    if (replacement->kind == 0) { g_assert_cmpint (mkfifo (path, 0600), ==, 0); }
    else if (replacement->kind == 1)
    {
        g_autofree char *target = g_file_get_path (replacement->fixture->second);
        g_assert_cmpint (symlink (target, path), ==, 0);
    }
    else { g_assert_true (g_file_set_contents (path, "replacement", -1, NULL)); }
}

static void
test_replacement (Fixture *f, gconstpointer kind)
{
    GtkWidget *status = find_label (GTK_WIDGET (f->preview), "Select a file to preview");
    Replacement replacement = { .fixture = f, .kind = GPOINTER_TO_UINT (kind) };
    g_signal_connect (status, "notify::label", G_CALLBACK (replace_before_open), &replacement);
    select_one (f, f->first); settle (f);
    g_signal_handlers_disconnect_by_data (status, &replacement);
    g_assert_true (replacement.changed);
    g_autofree char *text = preview_text (f);
    g_assert_cmpuint (strlen (text), ==, 0);
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "changed"));
}

static GKeyFile *
backend_state (void)
{
    const char *marker = g_getenv ("MILLER_TEST_BACKEND");
    g_autofree char *expected = g_build_filename (g_get_user_runtime_dir (), "miller-backend.ini", NULL);
    g_assert_cmpstr (marker, ==, expected);
    struct stat info;
    g_assert_cmpint (g_lstat (marker, &info), ==, 0);
    g_assert_true (S_ISREG (info.st_mode));
    g_assert_cmpuint (info.st_uid, ==, getuid ());
    g_assert_cmpuint (info.st_mode & 0777, ==, 0600);
    GKeyFile *state = g_key_file_new ();
    g_assert_true (g_key_file_load_from_file (state, marker, G_KEY_FILE_NONE, NULL));
    g_autofree char *control = g_key_file_get_string (state, "backend", "control", NULL);
    g_autofree char *expected_control = g_build_filename (g_get_user_runtime_dir (), "miller-backend.sock", NULL);
    g_assert_cmpstr (control, ==, expected_control);
    return state;
}

static GFile *
backend_file (GKeyFile *state)
{
    g_autofree char *uri = g_key_file_get_string (state, "backend", "root-uri", NULL);
    g_autoptr (GFile) root = g_file_new_for_uri (uri);
    g_assert_true (g_file_has_uri_scheme (root, "archive"));
    return g_file_resolve_relative_path (root, "A/a.txt");
}

static void
backend_control (GKeyFile *state, const char *command)
{
    g_autofree char *path = g_key_file_get_string (state, "backend", "control", NULL);
    g_autoptr (GSocketClient) client = g_socket_client_new ();
    g_socket_client_set_timeout (client, 3);
    g_autoptr (GSocketAddress) address = g_unix_socket_address_new (path);
    g_autoptr (GSocketConnection) connection = g_socket_client_connect (client, G_SOCKET_CONNECTABLE (address), NULL, NULL);
    g_assert_nonnull (connection);
    GOutputStream *output = g_io_stream_get_output_stream (G_IO_STREAM (connection));
    g_autofree char *line = g_strconcat (command, "\n", NULL);
    g_assert_true (g_output_stream_write_all (output, line, strlen (line), NULL, NULL, NULL));
    g_autoptr (GDataInputStream) input = g_data_input_stream_new (g_io_stream_get_input_stream (G_IO_STREAM (connection)));
    g_autofree char *response = g_data_input_stream_read_line (input, NULL, NULL, NULL);
    g_assert_cmpstr (response, ==, "ok");
}

static void
test_remote_off (Fixture *f, gconstpointer unused)
{
    if (g_getenv ("MILLER_TEST_BACKEND") == NULL) { g_test_skip ("Requires the actual private archive fixture"); return; }
    g_autoptr (GKeyFile) state = backend_state ();
    g_autoptr (GFile) file = backend_file (state);
    select_one (f, file); settle (f);
    g_autofree char *text = preview_text (f);
    g_assert_cmpuint (strlen (text), ==, 0);
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "Remote content preview is disabled"));
}

static void
test_backend_deadline (Fixture *f, gconstpointer unused)
{
    if (g_getenv ("MILLER_TEST_BACKEND") == NULL) { g_test_skip ("Requires the actual private archive fixture with --pause-seconds 8"); return; }
    g_autoptr (GKeyFile) state = backend_state ();
    g_assert_cmpint (g_key_file_get_integer (state, "backend", "pause-seconds", NULL), ==, 8);
    g_autoptr (GFile) file = backend_file (state);
    backend_control (state, "pause");
    gint64 start = g_get_monotonic_time ();
    select_one (f, file);
    gint64 deadline = start + 7 * G_TIME_SPAN_SECOND;
    while (loading (f))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline);
        g_main_context_iteration (NULL, FALSE);
        g_usleep (1000);
    }
    g_assert_cmpint (g_get_monotonic_time () - start, >=, 4 * G_TIME_SPAN_SECOND);
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "timed out"));
    /* Its backend must still be stopped: the application deadline, not the
     * fixture's failing watchdog, produced this state. */
    g_autofree char *pid = g_key_file_get_string (state, "backend", "pid", NULL);
    g_autofree char *status_path = g_build_filename ("/proc", pid, "status", NULL);
    g_autofree char *status = NULL;
    g_assert_true (g_file_get_contents (status_path, &status, NULL, NULL));
    g_assert_nonnull (strstr (status, "T (stopped)"));
    backend_control (state, "resume");
    select_one (f, f->first); settle (f);
    g_autofree char *text = preview_text (f);
    g_assert_cmpstr (text, ==, "alpha");
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    nautilus_register_resource (); nautilus_global_preferences_init (); nautilus_ensure_extension_points ();
    g_autoptr (NautilusTagManager) tags = nautilus_tag_manager_new_dummy ();
    test_init_config_dir ();
    g_autoptr (NautilusApplication) app = nautilus_application_new ();
    g_application_set_flags (G_APPLICATION (app), g_application_get_flags (G_APPLICATION (app)) | G_APPLICATION_NON_UNIQUE);
    g_assert_true (g_application_register (G_APPLICATION (app), NULL, NULL));
#define CASE(name, fn) g_test_add ("/miller/preview/" name, Fixture, NULL, setup, fn, teardown)
    CASE ("empty", test_empty); CASE ("folder", test_folder); CASE ("multiple-known-partial", test_multiple);
    CASE ("text-bounds", test_text_bounds); CASE ("invalid-binary", test_invalid_binary);
    CASE ("empty-file", test_empty_file); CASE ("utf8-bounds", test_utf8_bounds);
    CASE ("special-file", test_special_file); CASE ("newer-selection", test_newer_selection);
    CASE ("hide-dispose", test_hide_dispose); CASE ("entry-focus", test_entry_focus);
    CASE ("inactive", test_inactive); CASE ("unknown-multiple", test_unknown_multiple);
    CASE ("cached-aggregation-yields", test_cached_aggregation_yields);
    CASE ("remote-off", test_remote_off); CASE ("backend-deadline", test_backend_deadline);
    g_test_add ("/miller/preview/replacement-fifo", Fixture, GUINT_TO_POINTER (0), setup, test_replacement, teardown);
    g_test_add ("/miller/preview/replacement-link", Fixture, GUINT_TO_POINTER (1), setup, test_replacement, teardown);
    g_test_add ("/miller/preview/replacement-file", Fixture, GUINT_TO_POINTER (2), setup, test_replacement, teardown);
    g_test_add ("/miller/preview/reentrant-metadata", Fixture, GUINT_TO_POINTER (0), setup, test_reentrant_selection, teardown);
    g_test_add ("/miller/preview/reentrant-text", Fixture, GUINT_TO_POINTER (1), setup, test_reentrant_selection, teardown);
    g_test_add ("/miller/preview/reentrant-completion", Fixture, GUINT_TO_POINTER (2), setup, test_reentrant_selection, teardown);
    return g_test_run ();
}
