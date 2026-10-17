/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <test-utilities.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#include "nautilus-application.h"
#include "nautilus-file.h"
#include "nautilus-file-utilities.h"
#include "nautilus-global-preferences.h"
#include "nautilus-miller-preview.h"
#include "nautilus-metadata.h"
#include "nautilus-resources.h"
#include "nautilus-tag-manager.h"
#include "nautilus-thumbnails.h"

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
test_remote_enabled (Fixture *f, gconstpointer unused)
{
    if (g_getenv ("MILLER_TEST_BACKEND") == NULL) { g_test_skip ("Requires the actual private archive fixture"); return; }
    g_autoptr (GKeyFile) state = backend_state ();
    g_autoptr (GFile) file = backend_file (state);
    g_settings_set_boolean (nautilus_preferences, "miller-preview-remote", TRUE);
    g_settings_set_enum (nautilus_preferences, NAUTILUS_PREFERENCES_SHOW_FILE_THUMBNAILS, NAUTILUS_SPEED_TRADEOFF_LOCAL_ONLY);
    select_one (f, file); settle (f);
    g_autofree char *blocked = preview_text (f);
    g_assert_cmpstr (blocked, ==, "");
    g_settings_set_enum (nautilus_preferences, NAUTILUS_PREFERENCES_SHOW_FILE_THUMBNAILS, NAUTILUS_SPEED_TRADEOFF_ALWAYS);
    settle (f);
    g_autofree char *text = preview_text (f);
    g_assert_cmpstr (text, ==, "a");
    g_settings_set_boolean (nautilus_preferences, "miller-preview-remote", FALSE); settle (f);
    g_autofree char *cleared = preview_text (f);
    g_assert_cmpstr (cleared, ==, "");
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "Remote content preview is disabled"));
    g_settings_reset (nautilus_preferences, NAUTILUS_PREFERENCES_SHOW_FILE_THUMBNAILS);
}

static void
test_remote_bounded (Fixture *f, gconstpointer unused)
{
    if (g_getenv ("MILLER_TEST_BACKEND") == NULL) { g_test_skip ("Requires the actual private archive fixture with --preview-content"); return; }
    g_autoptr (GKeyFile) state = backend_state ();
    if (!g_key_file_get_boolean (state, "backend", "preview-content", NULL))
    {
        g_test_skip ("Requires --preview-content"); return;
    }
    g_autoptr (GFile) sibling = backend_file (state);
    g_autoptr (GFile) parent = g_file_get_parent (sibling);
    g_autoptr (GFile) file = g_file_get_child (parent, "large.txt");
    g_settings_set_boolean (nautilus_preferences, "miller-preview-remote", TRUE);
    g_settings_set_enum (nautilus_preferences, NAUTILUS_PREFERENCES_SHOW_FILE_THUMBNAILS, NAUTILUS_SPEED_TRADEOFF_ALWAYS);
    select_one (f, file); settle (f);
    g_autofree char *text = preview_text (f);
    g_assert_cmpuint (strlen (text), ==, 32 * 1024);
    for (guint i = 0; i < strlen (text); i++) { g_assert_cmpint (text[i], ==, 'x'); }
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "Truncated text preview"));
    g_settings_reset (nautilus_preferences, "miller-preview-remote");
    g_settings_reset (nautilus_preferences, NAUTILUS_PREFERENCES_SHOW_FILE_THUMBNAILS);
}

static void
test_remote_image (Fixture *f, gconstpointer unused)
{
    if (g_getenv ("MILLER_TEST_BACKEND") == NULL) { g_test_skip ("Requires the actual private archive fixture with --preview-content"); return; }
    g_autoptr (GKeyFile) state = backend_state ();
    if (!g_key_file_get_boolean (state, "backend", "preview-content", NULL))
    {
        g_test_skip ("Requires --preview-content"); return;
    }
    g_autoptr (GFile) sibling = backend_file (state);
    g_autoptr (GFile) parent = g_file_get_parent (sibling);
    g_autoptr (GFile) image = g_file_get_child (parent, "image.png");
    g_settings_set_boolean (nautilus_preferences, "miller-preview-remote", TRUE);
    g_settings_set_enum (nautilus_preferences, NAUTILUS_PREFERENCES_SHOW_FILE_THUMBNAILS, NAUTILUS_SPEED_TRADEOFF_ALWAYS);
    select_one (f, image); settle (f);
    GtkPicture *picture = GTK_PICTURE (find_type (GTK_WIDGET (f->preview), GTK_TYPE_PICTURE));
    g_assert_nonnull (picture);
    GdkPaintable *paintable = gtk_picture_get_paintable (picture);
    g_assert_true (GDK_IS_TEXTURE (paintable));
    g_assert_cmpint (gdk_texture_get_width (GDK_TEXTURE (paintable)), ==, 64);
    g_assert_cmpint (gdk_texture_get_height (GDK_TEXTURE (paintable)), ==, 32);
    g_settings_set_enum (nautilus_preferences, NAUTILUS_PREFERENCES_SHOW_FILE_THUMBNAILS, NAUTILUS_SPEED_TRADEOFF_NEVER);
    settle (f); g_assert_null (gtk_picture_get_paintable (picture));
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "Remote content preview is disabled"));
    g_settings_reset (nautilus_preferences, "miller-preview-remote");
    g_settings_reset (nautilus_preferences, NAUTILUS_PREFERENCES_SHOW_FILE_THUMBNAILS);
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

static GFile *
write_image (Fixture *f, const char *name, const char *format, int width, int height)
{
    GFile *file = g_file_get_child (f->root, name);
    g_autofree char *path = g_file_get_path (file);
    g_autoptr (GdkPixbuf) pixels = gdk_pixbuf_new (GDK_COLORSPACE_RGB, !g_str_equal (format, "jpeg"), 8, width, height);
    gdk_pixbuf_fill (pixels, 0xCC6600FF);
    g_autoptr (GError) error = NULL;
    gboolean saved = gdk_pixbuf_save (pixels, path, format, &error, NULL);
    g_assert_no_error (error);
    g_assert_true (saved);
    return file;
}

static GdkTexture *
preview_texture (Fixture *f)
{
    GtkPicture *picture = GTK_PICTURE (find_type (GTK_WIDGET (f->preview), GTK_TYPE_PICTURE));
    g_assert_nonnull (picture);
    GdkPaintable *paintable = gtk_picture_get_paintable (picture);
    g_assert_true (GDK_IS_TEXTURE (paintable));
    return GDK_TEXTURE (paintable);
}

static void
test_custom_icon_preview (Fixture *f, gconstpointer unused)
{
    g_autoptr (GFile) file = write_image (f, "custom-icon.png", "png", 64, 32);
    g_autoptr (NautilusFile) native = nautilus_file_get (file);
    nautilus_file_set_metadata (native, NAUTILUS_METADATA_KEY_CUSTOM_ICON_NAME, NULL, "folder-symbolic");
    gint64 deadline = g_get_monotonic_time () + 5 * G_TIME_SPAN_SECOND;
    while (nautilus_file_get_metadata (native, NAUTILUS_METADATA_KEY_CUSTOM_ICON_NAME, NULL) == NULL)
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); pump (10);
    }
    /* A custom row icon suppresses a row thumbnail; it is not a prohibition
     * on explicitly inspecting this file's content. */
    g_assert_false (nautilus_file_should_show_thumbnail (native));
    select_one (f, file); settle (f);
    preview_texture (f);
}

static void
test_malicious_name (Fixture *f, gconstpointer unused)
{
    const char *name = "<b>雪 & $(touch PREVIEW_EXECUTED).desktop";
    g_autoptr (GFile) file = g_file_get_child (f->root, name);
    g_autoptr (GFile) marker = g_file_get_child (f->root, "PREVIEW_EXECUTED");
    g_autofree char *path = g_file_get_path (marker);
    g_autofree char *quoted = g_shell_quote (path);
    g_autofree char *desktop = g_strdup_printf (
        "[Desktop Entry]\nType=Application\nName=Preview fixture\nExec=/usr/bin/touch %s\n", quoted);
    g_assert_true (g_file_replace_contents (file, desktop, strlen (desktop), NULL, FALSE, 0, NULL, NULL, NULL));
    g_assert_true (g_file_set_attribute_uint32 (file, G_FILE_ATTRIBUTE_UNIX_MODE, 0700,
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL));
    select_one (f, file); settle (f);
    GtkWidget *title = find_label (GTK_WIDGET (f->preview), name);
    g_assert_nonnull (title); g_assert_cmpstr (gtk_label_get_text (GTK_LABEL (title)), ==, name);
    g_assert_false (gtk_label_get_use_markup (GTK_LABEL (title)));
    g_assert_false (g_file_query_exists (marker, NULL));
    g_autoptr (GFile) shell_marker = g_file_get_child (f->root, "$(touch PREVIEW_EXECUTED)");
    g_assert_false (g_file_query_exists (shell_marker, NULL));
}

static gboolean
external_svg_request (GSocketService *service, GSocketConnection *connection, GObject *source, guint *count)
{
    (*count)++;
    return FALSE;
}

static void
test_svg_external_resources (Fixture *f, gconstpointer unused)
{
    g_autoptr (GFile) external = write_image (f, "external.png", "png", 64, 32);
    g_autofree char *path = g_file_get_path (external);
    g_autofree char *uri = g_file_get_uri (external);
    int watch = inotify_init1 (IN_NONBLOCK | IN_CLOEXEC);
    g_assert_cmpint (inotify_add_watch (watch, path, IN_OPEN), >=, 0);
    g_autoptr (GSocketService) service = g_socket_service_new ();
    g_autoptr (GError) error = NULL;
    g_autoptr (GSocketAddress) address = g_inet_socket_address_new_from_string ("127.0.0.1", 0);
    g_autoptr (GSocketAddress) effective = NULL;
    g_assert_true (g_socket_listener_add_address (G_SOCKET_LISTENER (service), address,
        G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP, NULL, &effective, &error));
    g_assert_no_error (error);
    guint16 port = g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (effective));
    g_assert_cmpuint (port, >, 0);
    guint requests = 0;
    g_signal_connect (service, "incoming", G_CALLBACK (external_svg_request), &requests);
    g_autoptr (GFile) file = g_file_get_child (f->root, "external.svg");
    g_autofree char *svg = g_strdup_printf ("<svg xmlns='http://www.w3.org/2000/svg' width='64' height='32'>"
        "<image href='%s' width='32' height='32'/><image href='http://127.0.0.1:%u/external.png' x='32' width='32' height='32'/></svg>", uri, port);
    g_assert_true (g_file_replace_contents (file, svg, strlen (svg), NULL, FALSE, 0, NULL, NULL, NULL));
    select_one (f, file); settle (f);
    GtkPicture *picture = GTK_PICTURE (find_type (GTK_WIDGET (f->preview), GTK_TYPE_PICTURE));
    g_assert_nonnull (picture);
    g_assert_true (gtk_picture_get_paintable (picture) != NULL ||
                   find_label (GTK_WIDGET (f->preview), "Image preview unavailable") != NULL);
    char events[1024]; g_assert_cmpint (read (watch, events, sizeof events), ==, -1);
    g_assert_cmpint (errno, ==, EAGAIN); close (watch);
    g_assert_cmpuint (requests, ==, 0);
    g_socket_service_stop (service); g_socket_listener_close (G_SOCKET_LISTENER (service));
}

static void
test_images (Fixture *f, gconstpointer unused)
{
    const char *formats[] = { "png", "jpeg" };
    const char *names[] = { "image.png", "image.jpg" };
    for (guint i = 0; i < G_N_ELEMENTS (names); i++)
    {
        g_autoptr (GFile) file = write_image (f, names[i], formats[i], 64, 32);
        select_one (f, file); settle (f);
        GdkTexture *texture = preview_texture (f);
        g_assert_cmpint (gdk_texture_get_width (texture), ==, 64);
        g_assert_cmpint (gdk_texture_get_height (texture), ==, 32);
        g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "Image preview"));
    }
}

static void
test_image_output_limit (Fixture *f, gconstpointer unused)
{
    g_autoptr (GFile) file = write_image (f, "large.png", "png", 2048, 1024);
    select_one (f, file); settle (f);
    GdkTexture *texture = preview_texture (f);
    g_assert_cmpint (gdk_texture_get_width (texture), ==, 1024);
    g_assert_cmpint (gdk_texture_get_height (texture), ==, 512);
}

static void
test_image_source_limit (Fixture *f, gconstpointer unused)
{
    g_autoptr (GFile) file = g_file_get_child (f->root, "large.svg");
    const char *svg = "<svg xmlns='http://www.w3.org/2000/svg' width='10000' height='10000'><rect width='10000' height='10000' fill='red'/></svg>";
    g_assert_true (g_file_replace_contents (file, svg, strlen (svg), NULL, FALSE, 0, NULL, NULL, NULL));
    select_one (f, file); settle (f);
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "pixel limit"));
    GtkPicture *picture = GTK_PICTURE (find_type (GTK_WIDGET (f->preview), GTK_TYPE_PICTURE));
    g_assert_nonnull (picture); g_assert_null (gtk_picture_get_paintable (picture));
}

static void
test_image_input_limit (Fixture *f, gconstpointer unused)
{
    g_autoptr (GFile) file = write_image (f, "large.png", "png", 2, 2);
    g_autofree char *bytes = NULL; gsize size;
    g_assert_true (g_file_load_contents (file, NULL, &bytes, &size, NULL, NULL));
    g_autofree char *oversized = g_malloc0 (16 * 1024 * 1024 + 1);
    memcpy (oversized, bytes, size);
    g_assert_true (g_file_replace_contents (file, oversized, 16 * 1024 * 1024 + 1, NULL, FALSE, 0, NULL, NULL, NULL));
    g_autofree char *path = g_file_get_path (file);
    int watch = inotify_init1 (IN_NONBLOCK | IN_CLOEXEC);
    g_assert_cmpint (inotify_add_watch (watch, path, IN_OPEN), >=, 0);
    select_one (f, file); settle (f);
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "input limit"));
    char events[1024]; g_assert_cmpint (read (watch, events, sizeof events), ==, -1);
    g_assert_cmpint (errno, ==, EAGAIN); close (watch);
}

static void
test_corrupt_image (Fixture *f, gconstpointer unused)
{
    g_autoptr (GFile) file = g_file_get_child (f->root, "corrupt.png");
    g_assert_true (g_file_replace_contents (file, "not PNG", 7, NULL, FALSE, 0, NULL, NULL, NULL));
    select_one (f, file); settle (f);
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "Image preview unavailable"));
    GtkPicture *picture = GTK_PICTURE (find_type (GTK_WIDGET (f->preview), GTK_TYPE_PICTURE));
    g_assert_nonnull (picture); g_assert_null (gtk_picture_get_paintable (picture));
}

static void
test_image_privacy (Fixture *f, gconstpointer unused)
{
    g_autoptr (GFile) file = write_image (f, "private.png", "png", 64, 32);
    g_settings_set_enum (nautilus_preferences, NAUTILUS_PREFERENCES_SHOW_FILE_THUMBNAILS, NAUTILUS_SPEED_TRADEOFF_NEVER);
    g_autofree char *path = g_file_get_path (file);
    int watch = inotify_init1 (IN_NONBLOCK | IN_CLOEXEC);
    g_assert_cmpint (inotify_add_watch (watch, path, IN_OPEN), >=, 0);
    select_one (f, file); settle (f);
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "disabled"));
    char events[1024]; g_assert_cmpint (read (watch, events, sizeof events), ==, -1);
    g_assert_cmpint (errno, ==, EAGAIN); close (watch);
    g_settings_reset (nautilus_preferences, NAUTILUS_PREFERENCES_SHOW_FILE_THUMBNAILS); settle (f);
    preview_texture (f);
    g_settings_set_enum (nautilus_preferences, NAUTILUS_PREFERENCES_SHOW_FILE_THUMBNAILS, NAUTILUS_SPEED_TRADEOFF_NEVER);
    settle (f);
    GtkPicture *picture = GTK_PICTURE (find_type (GTK_WIDGET (f->preview), GTK_TYPE_PICTURE));
    g_assert_null (gtk_picture_get_paintable (picture));
    g_settings_reset (nautilus_preferences, NAUTILUS_PREFERENCES_SHOW_FILE_THUMBNAILS);
}

static void
test_thumbnail_size_preference (Fixture *f, gconstpointer unused)
{
    g_autoptr (GFile) file = write_image (f, "limited.png", "png", 64, 32);
    g_autofree char *bytes = NULL; gsize length;
    g_assert_true (g_file_load_contents (file, NULL, &bytes, &length, NULL, NULL));
    g_autofree char *padded = g_malloc0 (2 * 1024 * 1024);
    memcpy (padded, bytes, length);
    g_assert_true (g_file_replace_contents (file, padded, 2 * 1024 * 1024, NULL, FALSE, 0, NULL, NULL, NULL));
    g_settings_set_uint64 (nautilus_preferences, NAUTILUS_PREFERENCES_FILE_THUMBNAIL_LIMIT, 1);
    int watch = inotify_init1 (IN_NONBLOCK | IN_CLOEXEC);
    g_autofree char *path = g_file_get_path (file);
    g_assert_cmpint (inotify_add_watch (watch, path, IN_OPEN), >=, 0);
    select_one (f, file); settle (f);
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "disabled"));
    char events[1024]; g_assert_cmpint (read (watch, events, sizeof events), ==, -1);
    g_assert_cmpint (errno, ==, EAGAIN); close (watch);
    g_settings_reset (nautilus_preferences, NAUTILUS_PREFERENCES_FILE_THUMBNAIL_LIMIT);
}

typedef struct { GFile *file; gboolean grown; } ImageGrowth;
static void
grow_image_before_open (GtkLabel *label, GParamSpec *pspec, ImageGrowth *growth)
{
    if (growth->grown || !g_str_equal (gtk_label_get_text (label), "Loading image preview…")) { return; }
    growth->grown = TRUE;
    g_autofree char *path = g_file_get_path (growth->file);
    int fd = g_open (path, O_WRONLY | O_APPEND | O_CLOEXEC, 0);
    g_assert_cmpint (fd, >=, 0);
    char bytes[4096] = {0};
    for (guint i = 0; i < 512; i++) { g_assert_cmpint (write (fd, bytes, sizeof bytes), ==, sizeof bytes); }
    close (fd);
}
static void
test_image_grows_beyond_preference (Fixture *f, gconstpointer unused)
{
    g_autoptr (GFile) file = write_image (f, "growing.png", "png", 64, 32);
    g_settings_set_uint64 (nautilus_preferences, NAUTILUS_PREFERENCES_FILE_THUMBNAIL_LIMIT, 1);
    GtkWidget *status = find_label (GTK_WIDGET (f->preview), "Select a file to preview");
    ImageGrowth growth = { .file = file };
    g_signal_connect (status, "notify::label", G_CALLBACK (grow_image_before_open), &growth);
    select_one (f, file); settle (f);
    g_signal_handlers_disconnect_by_data (status, &growth);
    g_assert_true (growth.grown);
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "input limit"));
    GtkPicture *picture = GTK_PICTURE (find_type (GTK_WIDGET (f->preview), GTK_TYPE_PICTURE));
    g_assert_null (gtk_picture_get_paintable (picture));
    g_settings_reset (nautilus_preferences, NAUTILUS_PREFERENCES_FILE_THUMBNAIL_LIMIT);
}

typedef struct { GdkPixbuf *pixels; gboolean done; GError *error; } ThumbnailResult;
static void
thumbnail_done (GObject *source, GAsyncResult *result, gpointer data)
{
    ThumbnailResult *thumbnail = data;
    thumbnail->pixels = nautilus_create_thumbnail_finish (result, &thumbnail->error);
    thumbnail->done = TRUE;
}
static void
test_native_uncached_thumbnail (Fixture *f, gconstpointer unused)
{
    g_autoptr (GFile) file = write_image (f, "native-thumbnail.png", "png", 64, 32);
    g_autofree char *uri = g_file_get_uri (file);
    g_autoptr (GFileInfo) info = g_file_query_info (file, G_FILE_ATTRIBUTE_TIME_MODIFIED, 0, NULL, NULL);
    time_t mtime = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
    g_autofree char *cache = nautilus_thumbnail_get_path_for_uri (uri);
    for (guint cached = 0; cached < 2; cached++)
    {
        ThumbnailResult thumbnail = {0};
        if (cached) { nautilus_create_thumbnail_async (uri, "image/png", mtime, NULL, thumbnail_done, &thumbnail); }
        else { nautilus_create_thumbnail_uncached_async (uri, "image/png", mtime, NULL, thumbnail_done, &thumbnail); }
        gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
        while (!thumbnail.done)
        {
            g_assert_cmpint (g_get_monotonic_time (), <, deadline);
            g_main_context_iteration (NULL, FALSE); g_usleep (1000);
        }
        g_assert_no_error (thumbnail.error); g_assert_nonnull (thumbnail.pixels);
        g_assert_cmpint (gdk_pixbuf_get_width (thumbnail.pixels), <=, nautilus_thumbnail_get_max_size ());
        g_assert_cmpint (g_file_test (cache, G_FILE_TEST_EXISTS), ==, cached != 0);
        g_clear_object (&thumbnail.pixels); g_clear_error (&thumbnail.error);
    }
}

static void
wait_thumbnail (ThumbnailResult *result)
{
    gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    while (!result->done)
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline);
        g_main_context_iteration (NULL, FALSE); g_usleep (1000);
    }
}
static void
test_thumbnail_coalesced_cache (Fixture *f, gconstpointer unused)
{
    g_autoptr (GFile) file = write_image (f, "shared-thumbnail.png", "png", 64, 32);
    g_autofree char *uri = g_file_get_uri (file);
    time_t mtime = g_get_real_time () / G_TIME_SPAN_SECOND;
    ThumbnailResult first = {0}, second = {0};
    nautilus_create_thumbnail_uncached_async (uri, "image/png", mtime, NULL, thumbnail_done, &first);
    nautilus_create_thumbnail_async (uri, "image/png", mtime, NULL, thumbnail_done, &second);
    wait_thumbnail (&first); wait_thumbnail (&second);
    g_assert_no_error (first.error); g_assert_no_error (second.error);
    g_assert_nonnull (first.pixels); g_assert_nonnull (second.pixels);
    g_autofree char *cache = nautilus_thumbnail_get_path_for_uri (uri);
    g_assert_true (g_file_test (cache, G_FILE_TEST_EXISTS));
    g_clear_object (&first.pixels); g_clear_object (&second.pixels);
}
static void
test_thumbnail_failure_cache (Fixture *f, gconstpointer unused)
{
    g_autoptr (GFile) file = g_file_get_child (f->root, "broken-thumbnail.png");
    g_assert_true (g_file_replace_contents (file, "invalid", 7, NULL, FALSE, 0, NULL, NULL, NULL));
    g_autofree char *uri = g_file_get_uri (file);
    time_t mtime = g_get_real_time () / G_TIME_SPAN_SECOND;
    for (guint cached = 0; cached < 2; cached++)
    {
        ThumbnailResult result = {0};
        if (cached) { nautilus_create_thumbnail_async (uri, "image/png", mtime, NULL, thumbnail_done, &result); }
        else { nautilus_create_thumbnail_uncached_async (uri, "image/png", mtime, NULL, thumbnail_done, &result); }
        wait_thumbnail (&result); g_assert_nonnull (result.error); g_assert_null (result.pixels);
        g_clear_error (&result.error);
        g_autoptr (GFileInfo) info = g_file_query_info (file, G_FILE_ATTRIBUTE_THUMBNAILING_FAILED, 0, NULL, NULL);
        g_assert_nonnull (info);
        gboolean failed = g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_THUMBNAILING_FAILED) &&
            g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_THUMBNAILING_FAILED);
        g_assert_cmpint (failed, ==, cached != 0);
    }
}
static GFile *
write_pdf (Fixture *f)
{
    g_autoptr (GString) pdf = g_string_new ("%PDF-1.4\n");
    const char *objects[] = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 200] /Resources << >> /Contents 4 0 R >>",
        "<< /Length 27 >>\nstream\n1 0 0 rg 20 20 200 120 re f\nendstream"
    };
    gsize offsets[G_N_ELEMENTS (objects)];
    for (guint i = 0; i < G_N_ELEMENTS (objects); i++)
    {
        offsets[i] = pdf->len; g_string_append_printf (pdf, "%u 0 obj\n%s\nendobj\n", i+1, objects[i]);
    }
    gsize xref = pdf->len;
    g_string_append (pdf, "xref\n0 5\n0000000000 65535 f \n");
    for (guint i = 0; i < G_N_ELEMENTS (objects); i++)
    {
        g_string_append_printf (pdf, "%010" G_GSIZE_FORMAT " 00000 n \n", offsets[i]);
    }
    g_string_append_printf (pdf, "trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n%" G_GSIZE_FORMAT "\n%%%%EOF\n", xref);
    GFile *file = g_file_get_child (f->root, "page.pdf");
    g_assert_true (g_file_replace_contents (file, pdf->str, pdf->len, NULL, FALSE, 0, NULL, NULL, NULL));
    return file;
}
static void
test_pdf_thumbnail (Fixture *f, gconstpointer unused)
{
    g_autoptr (GFile) file = write_pdf (f);
    int watch = inotify_init1 (IN_NONBLOCK | IN_CLOEXEC);
    g_assert_cmpint (inotify_add_watch (watch, g_get_user_runtime_dir (), IN_CREATE | IN_DELETE), >=, 0);
    select_one (f, file); settle (f);
    char events[16384];
    ssize_t count = read (watch, events, sizeof events);
    guint created = 0, deleted = 0;
    g_assert_cmpint (count, >, 0);
    for (gsize offset = 0; offset < (gsize) count;)
    {
        struct inotify_event *event = (struct inotify_event *) (events + offset);
        if (event->len > 0 && g_str_has_prefix (event->name, "nautilus-inspector-"))
        {
            if (event->mask & IN_CREATE) { created++; }
            if (event->mask & IN_DELETE) { deleted++; }
        }
        offset += sizeof *event + event->len;
    }
    close (watch);
    g_assert_cmpuint (created, ==, 1); g_assert_cmpuint (deleted, ==, 1);
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "Document thumbnail"));
    GdkTexture *texture = preview_texture (f);
    g_assert_cmpint (gdk_texture_get_width (texture), >, 0);
    g_assert_cmpint (gdk_texture_get_width (texture), <=, 1024);
    g_assert_cmpint (gdk_texture_get_height (texture), <=, 1024);
}

typedef struct
{
    Fixture *fixture;
    GFile *first, *second;
    guint mode, decoded;
    gboolean changed;
} ImageTransition;
static void
change_during_image_frame (GtkLabel *label, GParamSpec *pspec, ImageTransition *transition)
{
    if (!g_str_equal (gtk_label_get_text (label), "Decoding image preview…")) { return; }
    transition->decoded++;
    if (transition->changed) { return; }
    Fixture *f = transition->fixture;
    g_assert_true (loading (f));
    GtkPicture *picture = GTK_PICTURE (find_type (GTK_WIDGET (f->preview), GTK_TYPE_PICTURE));
    g_assert_null (gtk_picture_get_paintable (picture));
    transition->changed = TRUE;
    if (transition->mode == 0) { select_one (f, transition->second); }
    else if (transition->mode == 1)
    {
        g_settings_set_enum (nautilus_preferences, NAUTILUS_PREFERENCES_SHOW_FILE_THUMBNAILS, NAUTILUS_SPEED_TRADEOFF_NEVER);
    }
    else if (transition->mode == 2)
    {
        gtk_window_destroy (GTK_WINDOW (f->window)); f->window = NULL;
    }
    else
    {
        for (guint i = 0; i < 1000; i++) { select_one (f, i % 2 == 0 ? transition->first : transition->second); }
    }
}
static void
test_image_transition (Fixture *f, gconstpointer mode)
{
    g_autoptr (GFile) first = write_image (f, "image-a.png", "png", 2048, 1024);
    g_autoptr (GFile) second = write_image (f, "image-b.png", "png", 32, 64);
    g_autoptr (GtkWidget) status = g_object_ref (find_label (GTK_WIDGET (f->preview), "Select a file to preview"));
    ImageTransition transition = { .fixture = f, .first = first, .second = second, .mode = GPOINTER_TO_UINT (mode) };
    NautilusMillerPreview *weak = f->preview;
    g_object_add_weak_pointer (G_OBJECT (weak), (gpointer *) &weak);
    g_signal_connect (status, "notify::label", G_CALLBACK (change_during_image_frame), &transition);
    select_one (f, first);
    if (transition.mode == 2)
    {
        gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
        while (f->window != NULL)
        {
            g_assert_cmpint (g_get_monotonic_time (), <, deadline); pump (10);
        }
        pump (200); g_assert_null (weak);
    }
    else
    {
        settle (f);
        if (transition.mode == 1)
        {
            g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "disabled"));
            GtkPicture *picture = GTK_PICTURE (find_type (GTK_WIDGET (f->preview), GTK_TYPE_PICTURE));
            g_assert_null (gtk_picture_get_paintable (picture));
        }
        else
        {
            GdkTexture *texture = preview_texture (f);
            g_assert_cmpint (gdk_texture_get_width (texture), ==, 32);
            g_assert_cmpint (gdk_texture_get_height (texture), ==, 64);
            g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "image-b.png"));
            g_assert_cmpuint (transition.decoded, ==, 2);
        }
        g_object_remove_weak_pointer (G_OBJECT (weak), (gpointer *) &weak);
    }
    g_signal_handlers_disconnect_by_data (status, &transition);
    g_assert_true (transition.changed);
    g_settings_reset (nautilus_preferences, NAUTILUS_PREFERENCES_SHOW_FILE_THUMBNAILS);
}

static GFile *
write_media (Fixture *f, gboolean audio)
{
    g_autofree char *ffmpeg = g_find_program_in_path ("ffmpeg");
    g_assert_nonnull (ffmpeg);
    GFile *file = g_file_get_child (f->root, audio ? "audio.wav" : "video.webm");
    g_autofree char *path = g_file_get_path (file);
    g_autoptr (GError) error = NULL;
    g_autoptr (GSubprocess) process = g_subprocess_new (G_SUBPROCESS_FLAGS_STDERR_PIPE | G_SUBPROCESS_FLAGS_STDOUT_SILENCE,
        &error, ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin", "-threads", "1", "-f", "lavfi", "-i",
        audio ? "sine=frequency=440:sample_rate=8000:duration=1" : "testsrc=size=96x64:rate=4:duration=1",
        audio ? "-c:a" : "-c:v", audio ? "pcm_s16le" : "libvpx", "-threads", "1", "-y", path, NULL);
    g_assert_no_error (error);
    g_autofree char *stderr_output = NULL;
    g_assert_true (g_subprocess_communicate_utf8 (process, NULL, NULL, NULL, &stderr_output, &error));
    g_assert_no_error (error);
    if (!g_subprocess_get_successful (process)) { g_test_message ("Media fixture encoder: %s", stderr_output); }
    g_assert_true (g_subprocess_get_successful (process));
    return file;
}
static void
test_media_poster (Fixture *f, gconstpointer unused)
{
    g_autoptr (GFile) file = write_media (f, FALSE);
    select_one (f, file); settle (f);
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "Media poster"));
    GdkTexture *texture = preview_texture (f);
    g_assert_cmpint (gdk_texture_get_width (texture), >, 0);
    g_assert_cmpint (gdk_texture_get_width (texture), <=, 1024);
    g_assert_cmpint (gdk_texture_get_height (texture), <=, 1024);
}
static void
test_audio_metadata (Fixture *f, gconstpointer unused)
{
    g_autoptr (GFile) file = write_media (f, TRUE);
    select_one (f, file); settle (f);
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "audio.wav"));
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "Open or press Space"));
    /* Installed providers may offer artwork; lack of artwork is a truthful
     * metadata fallback, with no embedded player or automatic activation. */
}
static void
test_unsupported_media (Fixture *f, gconstpointer unused)
{
    g_autoptr (GFile) file = g_file_get_child (f->root, "unsupported.webm");
    g_assert_true (g_file_replace_contents (file, "not a media container", strlen ("not a media container"), NULL, FALSE, 0, NULL, NULL, NULL));
    select_one (f, file); settle (f);
    g_assert_nonnull (find_label (GTK_WIDGET (f->preview), "Thumbnail unavailable"));
    GtkPicture *picture = GTK_PICTURE (find_type (GTK_WIDGET (f->preview), GTK_TYPE_PICTURE));
    g_assert_null (gtk_picture_get_paintable (picture));
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
    CASE ("png-jpeg-still", test_images); CASE ("image-output-limit", test_image_output_limit);
    CASE ("malicious-name", test_malicious_name); CASE ("svg-external-resources", test_svg_external_resources);
    CASE ("custom-icon-preview", test_custom_icon_preview);
    CASE ("image-source-limit", test_image_source_limit); CASE ("image-input-limit", test_image_input_limit);
    CASE ("corrupt-image", test_corrupt_image); CASE ("image-privacy", test_image_privacy);
    CASE ("thumbnail-size-preference", test_thumbnail_size_preference);
    CASE ("image-grows-past-policy", test_image_grows_beyond_preference);
    CASE ("native-uncached-thumbnail", test_native_uncached_thumbnail);
    CASE ("thumbnail-coalesced-cache", test_thumbnail_coalesced_cache);
    CASE ("thumbnail-failure-cache", test_thumbnail_failure_cache);
    CASE ("pdf-thumbnail", test_pdf_thumbnail);
    CASE ("media-poster", test_media_poster); CASE ("audio-metadata", test_audio_metadata);
    CASE ("unsupported-media", test_unsupported_media);
    CASE ("empty", test_empty); CASE ("folder", test_folder); CASE ("multiple-known-partial", test_multiple);
    CASE ("text-bounds", test_text_bounds); CASE ("invalid-binary", test_invalid_binary);
    CASE ("empty-file", test_empty_file); CASE ("utf8-bounds", test_utf8_bounds);
    CASE ("special-file", test_special_file); CASE ("newer-selection", test_newer_selection);
    CASE ("hide-dispose", test_hide_dispose); CASE ("entry-focus", test_entry_focus);
    CASE ("inactive", test_inactive); CASE ("unknown-multiple", test_unknown_multiple);
    CASE ("cached-aggregation-yields", test_cached_aggregation_yields);
    CASE ("remote-off", test_remote_off); CASE ("backend-deadline", test_backend_deadline);
    CASE ("remote-enabled-policy", test_remote_enabled);
    CASE ("remote-bounded-read", test_remote_bounded);
    CASE ("remote-image-policy", test_remote_image);
    g_test_add ("/miller/preview/replacement-fifo", Fixture, GUINT_TO_POINTER (0), setup, test_replacement, teardown);
    g_test_add ("/miller/preview/replacement-link", Fixture, GUINT_TO_POINTER (1), setup, test_replacement, teardown);
    g_test_add ("/miller/preview/replacement-file", Fixture, GUINT_TO_POINTER (2), setup, test_replacement, teardown);
    g_test_add ("/miller/preview/reentrant-metadata", Fixture, GUINT_TO_POINTER (0), setup, test_reentrant_selection, teardown);
    g_test_add ("/miller/preview/reentrant-text", Fixture, GUINT_TO_POINTER (1), setup, test_reentrant_selection, teardown);
    g_test_add ("/miller/preview/reentrant-completion", Fixture, GUINT_TO_POINTER (2), setup, test_reentrant_selection, teardown);
    g_test_add ("/miller/preview/image-late-a-b", Fixture, GUINT_TO_POINTER (0), setup, test_image_transition, teardown);
    g_test_add ("/miller/preview/image-privacy-during-frame", Fixture, GUINT_TO_POINTER (1), setup, test_image_transition, teardown);
    g_test_add ("/miller/preview/image-dispose-during-frame", Fixture, GUINT_TO_POINTER (2), setup, test_image_transition, teardown);
    g_test_add ("/miller/preview/rapid-replacement-budget", Fixture, GUINT_TO_POINTER (3), setup, test_image_transition, teardown);
    return g_test_run ();
}
