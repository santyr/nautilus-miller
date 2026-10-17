/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <test-utilities.h>
#include <unistd.h>
#include <glib-unix.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include "miller-test-input.h"
#include "nautilus-application.h"
#include "nautilus-clipboard.h"
#include "nautilus-file.h"
#include "nautilus-file-utilities.h"
#include "nautilus-file-undo-manager.h"
#include "nautilus-files-view.h"
#include "nautilus-global-preferences.h"
#include "nautilus-progress-info.h"
#include "nautilus-progress-info-manager.h"
#include "nautilus-resources.h"
#include "nautilus-tag-manager.h"
#include "nautilus-view-info.h"
#include "nautilus-view-model.h"
#include "nautilus-window.h"
#include "nautilus-window-slot.h"

typedef struct
{
    GtkWidget *window;
    NautilusWindowSlot *slot;
    GFile *root, *source_dir, *source_file, *parent, *child, *sibling;
    NautilusProgressInfoManager *manager;
    GPtrArray *jobs;
    guint view_id;
} OperationFixture;

static MillerTestInput *input;
static const char payload[] = "Native clipboard payload: 雪\n";

/* An ordinary second GTK client offers only GDK's public file-list type.
 * Nautilus must request/deserialize this through the actual Wayland clipboard. */
static gboolean
clipboard_child_copy (GtkEventControllerKey *controller, guint key, guint code,
                      GdkModifierType modifiers, GFile *file)
{
    if (key != GDK_KEY_c || !(modifiers & GDK_CONTROL_MASK)) { return FALSE; }
    GdkFileList *files = gdk_file_list_new_from_array (&file, 1);
    g_autoptr (GdkContentProvider) provider = gdk_content_provider_new_typed (GDK_TYPE_FILE_LIST, files);
    g_boxed_free (GDK_TYPE_FILE_LIST, files);
    g_assert_true (gdk_clipboard_set_content (gtk_widget_get_clipboard (
        gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (controller))), provider));
    /* A background client cannot raise itself on Wayland. Minimize this
     * owned helper after the genuine copy event to return focus naturally. */
    gtk_window_minimize (GTK_WINDOW (gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (controller))));
    g_print ("copied\n");
    return TRUE;
}

static gboolean
clipboard_child_quit (gint fd, GIOCondition condition, gpointer loop)
{
    g_main_loop_quit (loop);
    return G_SOURCE_REMOVE;
}

static int
clipboard_child_main (const char *root_path, const char *source_path)
{
    /* Reuse the input harness's bus/compositor proof, then prove the mounted
     * disposable Home and read-only host mounts before offering a file. */
    g_autoptr (GError) error = NULL;
    g_autoptr (MillerTestInput) proof = miller_test_input_new (&error);
    g_assert_no_error (error); g_assert_nonnull (proof);
    const char *masked = g_getenv ("MILLER_TEST_HOME");
    g_assert_nonnull (masked);
    struct stat home_stat, masked_stat, root_stat, file_stat;
    struct statvfs host_mount;
    g_assert_cmpint (stat (g_get_home_dir (), &home_stat), ==, 0);
    g_assert_cmpint (stat (masked, &masked_stat), ==, 0);
    g_assert_cmpuint (home_stat.st_dev, ==, masked_stat.st_dev);
    g_assert_cmpuint (home_stat.st_ino, ==, masked_stat.st_ino);
    g_assert_cmpint (statvfs ("/", &host_mount), ==, 0);
    g_assert_true ((host_mount.f_flag & ST_RDONLY) != 0);
    g_autoptr (GFile) temporary = g_file_new_for_path (g_get_tmp_dir ());
    g_autoptr (GFile) root = g_file_new_for_path (root_path);
    g_autoptr (GFile) source = g_file_new_for_path (source_path);
    g_assert_true (g_file_has_prefix (root, temporary));
    g_assert_true (g_file_has_prefix (source, root));
    g_assert_cmpint (lstat (root_path, &root_stat), ==, 0);
    g_assert_true (S_ISDIR (root_stat.st_mode));
    g_assert_cmpuint (root_stat.st_uid, ==, geteuid ());
    g_assert_cmpuint (root_stat.st_mode & 0777, ==, 0700);
    g_assert_cmpint (lstat (source_path, &file_stat), ==, 0);
    g_assert_true (S_ISREG (file_stat.st_mode));
    g_assert_cmpuint (file_stat.st_uid, ==, geteuid ());
    gtk_init ();
    GtkWidget *window = gtk_window_new ();
    gtk_window_set_title (GTK_WINDOW (window), "Owned external clipboard fixture");
    gtk_window_set_child (GTK_WINDOW (window), gtk_label_new ("Copy the owned fixture with Ctrl+C"));
    GtkEventController *keys = gtk_event_controller_key_new ();
    g_signal_connect (keys, "key-pressed", G_CALLBACK (clipboard_child_copy), source);
    gtk_widget_add_controller (window, keys);
    g_autoptr (GMainLoop) loop = g_main_loop_new (NULL, FALSE);
    guint stdin_watch = g_unix_fd_add (STDIN_FILENO, G_IO_IN | G_IO_HUP,
                                      clipboard_child_quit, loop);
    gtk_window_fullscreen (GTK_WINDOW (window));
    gtk_window_present (GTK_WINDOW (window));
    g_print ("ready\n");
    g_main_loop_run (loop);
    /* The stdin callback removes its source as it leaves the main loop. */
    (void) stdin_watch;
    gtk_window_destroy (GTK_WINDOW (window));
    return 0;
}

typedef struct
{
    gboolean done;
    char *line;
    GError *error;
} ChildLine;

static void
operations_child_line_ready (GObject *source, GAsyncResult *result, gpointer data)
{
    ChildLine *line = data;
    line->line = g_data_input_stream_read_line_finish (G_DATA_INPUT_STREAM (source), result, NULL, &line->error);
    line->done = TRUE;
}

static void
operations_pump (guint milliseconds)
{
    gint64 end = g_get_monotonic_time () + milliseconds * G_TIME_SPAN_MILLISECOND;
    while (g_get_monotonic_time () < end)
    {
        while (g_main_context_iteration (NULL, FALSE)) {}
        g_usleep (1000);
    }
}

static void
operations_owned (OperationFixture *f, GFile *file)
{
    g_assert_true (g_file_equal (f->root, file) || g_file_has_prefix (file, f->root));
}

static void
operations_assert_bytes (OperationFixture *f, GFile *file, const char *expected)
{
    operations_owned (f, file);
    g_autofree char *contents = NULL;
    gsize length = 0;
    g_autoptr (GError) error = NULL;
    g_assert_true (g_file_load_contents (file, NULL, &contents, &length, NULL, &error));
    g_assert_no_error (error);
    g_assert_cmpmem (contents, length, expected, strlen (expected));
}

static void
operations_capture_job (NautilusProgressInfoManager *manager, NautilusProgressInfo *info, OperationFixture *f)
{
    g_ptr_array_add (f->jobs, g_object_ref (info));
}

static void
operations_settle (OperationFixture *f)
{
    gint64 deadline = g_get_monotonic_time () + 15 * G_TIME_SPAN_SECOND;
    for (;;)
    {
        gboolean busy = f->slot != NULL && nautilus_window_slot_get_loading (f->slot);
        busy |= nautilus_file_undo_manager_is_operating ();
        for (guint i = 0; i < f->jobs->len; i++)
        {
            busy |= !nautilus_progress_info_get_is_finished (f->jobs->pdata[i]);
        }
        if (!busy && !g_main_context_pending (NULL)) { break; }
        g_assert_cmpint (g_get_monotonic_time (), <, deadline);
        g_main_context_iteration (NULL, FALSE); g_usleep (1000);
    }
    operations_pump (120);
}

static NautilusFilesView *
operations_find_view (GtkWidget *widget, GFile *directory)
{
    if (NAUTILUS_IS_FILES_VIEW (widget))
    {
        GFile *location = nautilus_files_view_get_location (NAUTILUS_FILES_VIEW (widget));
        if (location != NULL && g_file_equal (location, directory)) { return NAUTILUS_FILES_VIEW (widget); }
    }
    for (GtkWidget *child = gtk_widget_get_first_child (widget); child; child = gtk_widget_get_next_sibling (child))
    {
        NautilusFilesView *view = operations_find_view (child, directory);
        if (view != NULL) { return view; }
    }
    return NULL;
}

static NautilusFilesView *
operations_view_for_directory (OperationFixture *f, GFile *directory)
{
    NautilusFilesView *view = operations_find_view (GTK_WIDGET (f->slot), directory);
    g_assert_nonnull (view);
    return view;
}

static void
operations_select (OperationFixture *f, NautilusFilesView *view, GFile *location)
{
    operations_owned (f, location);
    g_autoptr (NautilusFile) file = nautilus_file_get (location);
    gint64 deadline = g_get_monotonic_time () + 15 * G_TIME_SPAN_SECOND;
    while (nautilus_view_model_get_item_for_file (nautilus_files_view_get_private_model (view), file) == NULL)
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    GList selection = { .data = file };
    nautilus_files_view_set_selection (view, &selection, FALSE);
    gtk_widget_grab_focus (GTK_WIDGET (view));
    operations_settle (f);
}

static void
operations_assert_focus (OperationFixture *f, NautilusFilesView *view)
{
    gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    while (!gtk_window_is_active (GTK_WINDOW (f->window)))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (f->window));
    g_test_message ("mode=%u active-view=%s focus=%s", f->view_id,
        G_OBJECT_TYPE_NAME (nautilus_window_slot_get_current_view (f->slot)),
        focus != NULL ? G_OBJECT_TYPE_NAME (focus) : "none");
    g_assert_nonnull (focus);
    g_assert_true (focus == GTK_WIDGET (view) || gtk_widget_is_ancestor (focus, GTK_WIDGET (view)));
    g_assert_true (nautilus_window_slot_get_current_view (f->slot) == view);
    if (f->view_id == NAUTILUS_VIEW_MILLER_ID)
    {
        g_assert_true (g_object_get_data (G_OBJECT (f->window), "nautilus-active-file-view") == view);
    }
}

static void
operations_chord (guint key, gboolean shift)
{
    g_autoptr (GError) error = NULL;
    g_assert_true (miller_test_input_key (input, GDK_KEY_Control_L, TRUE, &error));
    if (shift) { g_assert_true (miller_test_input_key (input, GDK_KEY_Shift_L, TRUE, &error)); }
    g_assert_true (miller_test_input_key (input, key, TRUE, &error));
    g_assert_true (miller_test_input_key (input, key, FALSE, &error));
    if (shift) { g_assert_true (miller_test_input_key (input, GDK_KEY_Shift_L, FALSE, &error)); }
    g_assert_true (miller_test_input_key (input, GDK_KEY_Control_L, FALSE, &error));
    g_assert_no_error (error); operations_pump (60);
}

static void
operations_assert_clipboard (OperationFixture *f, gboolean cut)
{
    GdkClipboard *clipboard = gtk_widget_get_clipboard (f->window);
    g_assert_true (gdk_clipboard_is_local (clipboard));
    g_assert_true (gdk_content_formats_contain_gtype (gdk_clipboard_get_formats (clipboard), NAUTILUS_TYPE_CLIPBOARD));
    GdkContentProvider *provider = gdk_clipboard_get_content (clipboard);
    g_assert_nonnull (provider);
    g_auto (GValue) value = G_VALUE_INIT;
    g_value_init (&value, NAUTILUS_TYPE_CLIPBOARD);
    g_autoptr (GError) error = NULL;
    g_assert_true (gdk_content_provider_get_value (provider, &value, &error));
    g_assert_no_error (error);
    NautilusClipboard *data = g_value_get_boxed (&value);
    g_assert_cmpint (nautilus_clipboard_is_cut (data), ==, cut);
    GList *uris = nautilus_clipboard_get_uri_list (data);
    g_assert_cmpuint (g_list_length (uris), ==, 1);
    g_autoptr (GFile) source = g_file_new_for_uri (uris->data);
    g_assert_true (g_file_equal (source, f->source_file));
    g_list_free_full (uris, g_free);
}

static void
operations_setup (OperationFixture *f, gconstpointer view_id)
{
    g_assert_nonnull (input); g_assert_nonnull (g_getenv ("MILLER_TEST_HOME"));
    g_assert_cmpuint (geteuid (), !=, 0);
    f->view_id = GPOINTER_TO_UINT (view_id) & 0xff;
    g_settings_set_enum (nautilus_preferences, NAUTILUS_PREFERENCES_DEFAULT_FOLDER_VIEWER, f->view_id);
    g_settings_set_boolean (nautilus_preferences, NAUTILUS_PREFERENCES_MILLER_PREVIEW_VISIBLE, FALSE);
    nautilus_file_undo_manager_set_action (NULL);
    f->root = g_file_new_for_path (test_get_tmp_dir ());
    f->source_dir = g_file_get_child (f->root, "source");
    f->parent = g_file_get_child (f->root, "destination");
    f->child = g_file_get_child (f->parent, "child");
    f->sibling = g_file_get_child (f->root, "unrelated");
    GFile *directories[] = { f->source_dir, f->parent, f->child, f->sibling };
    for (guint i = 0; i < G_N_ELEMENTS (directories); i++)
    {
        operations_owned (f, directories[i]);
        g_assert_true (g_file_make_directory (directories[i], NULL, NULL));
    }
    f->source_file = g_file_get_child (f->source_dir, "payload – 雪.txt");
    g_assert_true (g_file_replace_contents (f->source_file, payload, strlen (payload), NULL, FALSE, 0, NULL, NULL, NULL));
    g_autoptr (GFile) child_sentinel = g_file_get_child (f->child, "child-sentinel.txt");
    g_autoptr (GFile) sibling_sentinel = g_file_get_child (f->sibling, "sibling-sentinel.txt");
    g_assert_true (g_file_replace_contents (child_sentinel, "child unchanged", 15, NULL, FALSE, 0, NULL, NULL, NULL));
    g_assert_true (g_file_replace_contents (sibling_sentinel, "sibling unchanged", 17, NULL, FALSE, 0, NULL, NULL, NULL));
    f->jobs = g_ptr_array_new_with_free_func (g_object_unref);
    f->manager = nautilus_progress_info_manager_dup_singleton ();
    g_signal_connect (f->manager, "new-progress-info", G_CALLBACK (operations_capture_job), f);
    f->window = GTK_WIDGET (nautilus_window_new ());
    nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), f->source_dir, 0, NULL);
    g_object_get (f->window, "active-slot", &f->slot, NULL);
    gtk_window_fullscreen (GTK_WINDOW (f->window));
    gtk_window_present (GTK_WINDOW (f->window));
    operations_settle (f);
}

static void
operations_teardown (OperationFixture *f, gconstpointer unused)
{
    operations_settle (f);
    gdk_clipboard_set_content (gtk_widget_get_clipboard (f->window), NULL);
    gtk_window_destroy (GTK_WINDOW (f->window)); operations_pump (100);
    g_signal_handlers_disconnect_by_data (f->manager, f);
    g_clear_pointer (&f->jobs, g_ptr_array_unref); g_clear_object (&f->manager);
    g_clear_object (&f->slot); g_clear_object (&f->source_file); g_clear_object (&f->source_dir);
    g_clear_object (&f->parent); g_clear_object (&f->child); g_clear_object (&f->sibling); g_clear_object (&f->root);
    test_clear_tmp_dir ();
}

static void
operations_wait_copy (OperationFixture *f, NautilusFilesView *view, GFile *destination)
{
    operations_owned (f, destination);
    gint64 deadline = g_get_monotonic_time () + 15 * G_TIME_SPAN_SECOND;
    while (!g_file_query_exists (destination, NULL))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    operations_settle (f);
    g_autoptr (NautilusFile) file = nautilus_file_get (destination);
    while (nautilus_view_model_get_item_for_file (nautilus_files_view_get_private_model (view), file) == NULL)
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    g_assert_cmpuint (f->jobs->len, >, 0);
}

static void
test_clipboard_copy (OperationFixture *f, gconstpointer unused)
{
    NautilusFilesView *source = operations_view_for_directory (f, f->source_dir);
    operations_select (f, source, f->source_file); operations_assert_focus (f, source);
    operations_chord (GDK_KEY_c, FALSE); operations_assert_clipboard (f, FALSE);
    nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), f->parent, 0, NULL);
    operations_settle (f);
    NautilusFilesView *destination = operations_view_for_directory (f, f->parent);
    if (f->view_id == NAUTILUS_VIEW_MILLER_ID)
    {
        operations_select (f, destination, f->child);
        g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->child));
    }
    else { gtk_widget_grab_focus (GTK_WIDGET (destination)); operations_settle (f); }
    operations_assert_focus (f, destination);
    guint history = g_list_length (nautilus_window_slot_get_back_history (f->slot));
    operations_chord (GDK_KEY_v, FALSE);
    g_autofree char *basename = g_file_get_basename (f->source_file);
    g_autoptr (GFile) copied = g_file_get_child (f->parent, basename);
    operations_wait_copy (f, destination, copied);
    operations_assert_bytes (f, copied, payload); operations_assert_bytes (f, f->source_file, payload);
    g_autoptr (GFile) wrong_child = g_file_get_child (f->child, basename);
    g_autoptr (GFile) wrong_sibling = g_file_get_child (f->sibling, basename);
    g_assert_false (g_file_query_exists (wrong_child, NULL)); g_assert_false (g_file_query_exists (wrong_sibling, NULL));
    g_autoptr (GFile) child_sentinel = g_file_get_child (f->child, "child-sentinel.txt");
    g_autoptr (GFile) sibling_sentinel = g_file_get_child (f->sibling, "sibling-sentinel.txt");
    operations_assert_bytes (f, child_sentinel, "child unchanged");
    operations_assert_bytes (f, sibling_sentinel, "sibling unchanged");
    g_assert_true (nautilus_window_slot_get_current_view (f->slot) == destination);
    g_assert_cmpuint (g_list_length (nautilus_window_slot_get_back_history (f->slot)), ==, history);
    g_test_message ("Real Ctrl+C/Ctrl+V copied exact Unicode-named bytes to mode%u target; native jobs=%u", f->view_id, f->jobs->len);
}

static void
operations_copy_source (OperationFixture *f, gboolean cut)
{
    NautilusFilesView *view = operations_view_for_directory (f, f->source_dir);
    operations_select (f, view, f->source_file);
    operations_assert_focus (f, view);
    operations_chord (cut ? GDK_KEY_x : GDK_KEY_c, FALSE);
    operations_assert_clipboard (f, cut);
}

static NautilusFilesView *
operations_open_parent (OperationFixture *f)
{
    nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), f->parent, 0, NULL);
    operations_settle (f);
    NautilusFilesView *view = operations_view_for_directory (f, f->parent);
    operations_select (f, view, f->child);
    operations_assert_focus (f, view);
    return view;
}

static void
operations_assert_sentinels (OperationFixture *f)
{
    g_autoptr (GFile) child = g_file_get_child (f->child, "child-sentinel.txt");
    g_autoptr (GFile) sibling = g_file_get_child (f->sibling, "sibling-sentinel.txt");
    operations_assert_bytes (f, child, "child unchanged");
    operations_assert_bytes (f, sibling, "sibling unchanged");
}

static void
test_cut_across_columns (OperationFixture *f, gconstpointer unused)
{
    operations_copy_source (f, TRUE);
    NautilusFilesView *parent = operations_open_parent (f);
    operations_chord (GDK_KEY_v, FALSE);
    g_autofree char *name = g_file_get_basename (f->source_file);
    g_autoptr (GFile) copied = g_file_get_child (f->parent, name);
    g_autoptr (GFile) wrong = g_file_get_child (f->child, name);
    operations_wait_copy (f, parent, copied);
    operations_assert_bytes (f, copied, payload);
    g_assert_false (g_file_query_exists (f->source_file, NULL));
    g_assert_false (g_file_query_exists (wrong, NULL));
    g_assert_cmpint (nautilus_file_undo_manager_get_state (), ==, NAUTILUS_FILE_UNDO_MANAGER_STATE_UNDO);
    operations_assert_sentinels (f);
}

static void
test_paste_empty_child (OperationFixture *f, gconstpointer unused)
{
    g_autoptr (GFile) sentinel = g_file_get_child (f->child, "child-sentinel.txt");
    operations_owned (f, sentinel);
    g_assert_true (g_file_delete (sentinel, NULL, NULL));
    operations_copy_source (f, FALSE);
    NautilusFilesView *parent = operations_open_parent (f);
    /* Real Right enters the disclosed empty child; real Left must leave it. */
    g_autoptr (GError) error = NULL;
    g_assert_true (miller_test_input_key (input, GDK_KEY_Right, TRUE, &error));
    g_assert_true (miller_test_input_key (input, GDK_KEY_Right, FALSE, &error));
    operations_settle (f);
    NautilusFilesView *child = operations_view_for_directory (f, f->child);
    operations_assert_focus (f, child);
    operations_chord (GDK_KEY_v, FALSE);
    g_autofree char *name = g_file_get_basename (f->source_file);
    g_autoptr (GFile) copied = g_file_get_child (f->child, name);
    g_autoptr (GFile) wrong = g_file_get_child (f->parent, name);
    operations_wait_copy (f, child, copied);
    operations_assert_bytes (f, copied, payload);
    operations_assert_bytes (f, f->source_file, payload);
    g_assert_false (g_file_query_exists (wrong, NULL));
    g_assert_true (miller_test_input_key (input, GDK_KEY_Left, TRUE, &error));
    g_assert_true (miller_test_input_key (input, GDK_KEY_Left, FALSE, &error));
    g_assert_no_error (error);
    operations_settle (f);
    operations_assert_focus (f, parent);
    g_autoptr (GFile) sibling = g_file_get_child (f->sibling, "sibling-sentinel.txt");
    operations_assert_bytes (f, sibling, "sibling unchanged");
}

static void
test_copy_undo_redo (OperationFixture *f, gconstpointer unused)
{
    test_clipboard_copy (f, unused);
    NautilusFilesView *view = operations_view_for_directory (f, f->parent);
    g_autofree char *name = g_file_get_basename (f->source_file);
    g_autoptr (GFile) copied = g_file_get_child (f->parent, name);
    g_assert_cmpint (nautilus_file_undo_manager_get_state (), ==, NAUTILUS_FILE_UNDO_MANAGER_STATE_UNDO);
    operations_chord (GDK_KEY_z, FALSE);
    operations_settle (f);
    g_assert_false (g_file_query_exists (copied, NULL));
    g_assert_cmpint (nautilus_file_undo_manager_get_state (), ==, NAUTILUS_FILE_UNDO_MANAGER_STATE_REDO);
    operations_assert_bytes (f, f->source_file, payload);
    operations_assert_sentinels (f);
    operations_chord (GDK_KEY_z, TRUE);
    operations_wait_copy (f, view, copied);
    operations_assert_bytes (f, copied, payload);
    operations_assert_bytes (f, f->source_file, payload);
    g_assert_cmpint (nautilus_file_undo_manager_get_state (), ==, NAUTILUS_FILE_UNDO_MANAGER_STATE_UNDO);
    operations_assert_sentinels (f);
}

static void
test_same_directory_duplicate (OperationFixture *f, gconstpointer unused)
{
    operations_copy_source (f, FALSE);
    NautilusFilesView *view = operations_view_for_directory (f, f->source_dir);
    operations_chord (GDK_KEY_v, FALSE);
    gint64 deadline = g_get_monotonic_time () + 15 * G_TIME_SPAN_SECOND;
    g_autoptr (GFile) duplicate = NULL;
    while (duplicate == NULL)
    {
        g_autoptr (GFileEnumerator) enumerator = g_file_enumerate_children (f->source_dir,
            G_FILE_ATTRIBUTE_STANDARD_NAME, G_FILE_QUERY_INFO_NONE, NULL, NULL);
        g_assert_nonnull (enumerator);
        g_autoptr (GFileInfo) info = NULL;
        while ((info = g_file_enumerator_next_file (enumerator, NULL, NULL)) != NULL)
        {
            GFile *file = g_file_get_child (f->source_dir, g_file_info_get_name (info));
            if (!g_file_equal (file, f->source_file)) { g_assert_null (duplicate); duplicate = file; }
            else { g_object_unref (file); }
            g_clear_object (&info);
        }
        g_assert_cmpint (g_get_monotonic_time (), <, deadline);
        if (duplicate == NULL) { operations_pump (10); }
    }
    operations_wait_copy (f, view, duplicate);
    operations_assert_bytes (f, duplicate, payload);
    operations_assert_bytes (f, f->source_file, payload);
    operations_assert_sentinels (f);
    g_autofree char *name = g_file_get_basename (duplicate);
    g_test_message ("Native same-directory Copy/Paste generated name: %s", name);
}

static void
operations_child_marker (GDataInputStream *stream, const char *expected)
{
    gint64 deadline = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
    for (;;)
    {
        ChildLine line = { 0 };
        g_data_input_stream_read_line_async (stream, G_PRIORITY_DEFAULT, NULL,
                                             operations_child_line_ready, &line);
        while (!line.done)
        {
            g_assert_cmpint (g_get_monotonic_time (), <, deadline);
            operations_pump (10);
        }
        g_assert_no_error (line.error); g_assert_nonnull (line.line);
        gboolean match = g_str_equal (line.line, expected);
        g_free (line.line);
        if (match) { return; }
    }
}

static void
operations_child_exited (GObject *source, GAsyncResult *result, gpointer data)
{
    g_autoptr (GError) error = NULL;
    g_assert_true (g_subprocess_wait_check_finish (G_SUBPROCESS (source), result, &error));
    g_assert_no_error (error);
    *(gboolean *) data = TRUE;
}

static void
test_external_clipboard (OperationFixture *f, gconstpointer late_data)
{
    gboolean delayed = GPOINTER_TO_UINT (late_data) != NAUTILUS_VIEW_MILLER_ID;
    NautilusFilesView *destination = operations_open_parent (f);
    g_autofree char *executable = g_file_read_link ("/proc/self/exe", NULL);
    g_autofree char *root_path = g_file_get_path (f->root);
    g_autofree char *source_path = g_file_get_path (f->source_file);
    g_autoptr (GError) error = NULL;
    g_autoptr (GSubprocess) child = g_subprocess_new (G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE,
        &error, executable, "--clipboard-source", root_path, source_path, NULL);
    g_assert_no_error (error); g_assert_nonnull (child);
    g_autoptr (GDataInputStream) output = g_data_input_stream_new (g_subprocess_get_stdout_pipe (child));
    operations_child_marker (output, "ready");
    gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    while (gtk_window_is_active (GTK_WINDOW (f->window)))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    operations_chord (GDK_KEY_c, FALSE);
    operations_child_marker (output, "copied");
    /* Wayland sends the new offer when this client regains keyboard focus. */
    gtk_window_present (GTK_WINDOW (f->window));
    gtk_widget_grab_focus (GTK_WIDGET (destination));
    operations_assert_focus (f, destination);
    GdkClipboard *clipboard = gtk_widget_get_clipboard (f->window);
    /* Foreign offers advertise wire MIME types; GDK deserializes the public
     * URI list to GdkFileList on read, rather than advertising a local GType. */
    while (gdk_clipboard_is_local (clipboard) ||
           !gdk_content_formats_contain_mime_type (gdk_clipboard_get_formats (clipboard), "text/uri-list"))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    g_assert_false (gdk_content_formats_contain_gtype (gdk_clipboard_get_formats (clipboard), NAUTILUS_TYPE_CLIPBOARD));
    g_autoptr (NautilusWindowSlot) original_slot = g_object_ref (f->slot);
    if (delayed)
    {
        /* Pause only the retained owned provider process. The compositor and
         * Nautilus keep running; the real clipboard transfer cannot reply. */
        g_subprocess_send_signal (child, SIGSTOP);
        g_autofree char *status_path = g_strdup_printf ("/proc/%s/status", g_subprocess_get_identifier (child));
        for (;;)
        {
            g_autofree char *status = NULL;
            g_assert_true (g_file_get_contents (status_path, &status, NULL, NULL));
            if (strstr (status, "State:\tT") != NULL) { break; }
            g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
        }
    }
    guint jobs_before = f->jobs->len;
    operations_chord (GDK_KEY_v, FALSE);
    if (delayed)
    {
        g_assert_cmpuint (f->jobs->len, ==, jobs_before);
        operations_chord (GDK_KEY_t, FALSE);
        NautilusWindowSlot *next_slot = NULL;
        g_object_get (f->window, "active-slot", &next_slot, NULL);
        g_assert_true (next_slot != original_slot);
        g_set_object (&f->slot, next_slot); g_object_unref (next_slot);
        nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), f->sibling, 0, NULL);
        operations_settle (f);
        NautilusFilesView *other = operations_view_for_directory (f, f->sibling);
        gtk_widget_grab_focus (GTK_WIDGET (other)); operations_assert_focus (f, other);
        g_subprocess_send_signal (child, SIGCONT);
    }
    g_autofree char *name = g_file_get_basename (f->source_file);
    g_autoptr (GFile) copied = g_file_get_child (f->parent, name);
    g_autoptr (GFile) wrong_child = g_file_get_child (f->child, name);
    g_autoptr (GFile) wrong_sibling = g_file_get_child (f->sibling, name);
    operations_wait_copy (f, destination, copied);
    operations_assert_bytes (f, copied, payload);
    operations_assert_bytes (f, f->source_file, payload);
    g_assert_false (g_file_query_exists (wrong_child, NULL));
    g_assert_false (g_file_query_exists (wrong_sibling, NULL));
    operations_assert_sentinels (f);
    if (delayed)
    {
        operations_assert_focus (f, operations_view_for_directory (f, f->sibling));
        g_test_message ("Real external clipboard reply arrived after genuine Ctrl+T; captured parent destination won");
    }
    g_assert_true (g_output_stream_write_all (g_subprocess_get_stdin_pipe (child), "quit\n", 5, NULL, NULL, &error));
    g_assert_no_error (error);
    gboolean exited = FALSE;
    g_subprocess_wait_check_async (child, NULL, operations_child_exited, &exited);
    deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    while (!exited)
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
}

int
main (int argc, char **argv)
{
    if (argc == 4 && g_str_equal (argv[1], "--clipboard-source"))
    {
        return clipboard_child_main (argv[2], argv[3]);
    }
    g_test_init (&argc, &argv, NULL);
    if (g_getenv ("MILLER_TEST_SESSION") == NULL || g_getenv ("MILLER_TEST_HOME") == NULL)
    {
        g_print ("Requires the owned private compositor and mounted disposable Home\n"); return 77;
    }
    g_autoptr (GError) error = NULL; input = miller_test_input_new (&error); g_assert_no_error (error);
    nautilus_register_resource (); nautilus_global_preferences_init (); nautilus_ensure_extension_points ();
    g_autoptr (NautilusTagManager) tags = nautilus_tag_manager_new_dummy ();
    test_init_config_dir ();
    g_autoptr (NautilusApplication) app = nautilus_application_new ();
    g_application_set_flags (G_APPLICATION (app), g_application_get_flags (G_APPLICATION (app)) | G_APPLICATION_NON_UNIQUE);
    g_assert_true (g_application_register (G_APPLICATION (app), NULL, NULL));
#define CASE(name, mode) g_test_add ("/miller/operations/" name, OperationFixture, GUINT_TO_POINTER (mode), operations_setup, test_clipboard_copy, operations_teardown)
    CASE ("clipboard-list", NAUTILUS_VIEW_LIST_ID);
    CASE ("clipboard-grid", NAUTILUS_VIEW_GRID_ID);
    CASE ("paste-focused-parent", NAUTILUS_VIEW_MILLER_ID);
#define COLUMN_CASE(name, func) g_test_add ("/miller/operations/" name, OperationFixture, GUINT_TO_POINTER (NAUTILUS_VIEW_MILLER_ID), operations_setup, func, operations_teardown)
    COLUMN_CASE ("paste-empty-child", test_paste_empty_child);
    COLUMN_CASE ("cut-across-columns", test_cut_across_columns);
    COLUMN_CASE ("copy-undo-redo", test_copy_undo_redo);
    COLUMN_CASE ("same-directory-duplicate", test_same_directory_duplicate);
    COLUMN_CASE ("external-clipboard-source", test_external_clipboard);
    /* The extra bit is fixture data, not a native view identifier. */
    g_test_add ("/miller/operations/clipboard-after-tab-switch", OperationFixture,
        GUINT_TO_POINTER (NAUTILUS_VIEW_MILLER_ID | 0x100), operations_setup,
        test_external_clipboard, operations_teardown);
    int result = g_test_run (); miller_test_input_free (input); return result;
}
