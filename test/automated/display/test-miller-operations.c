/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <test-utilities.h>
#include <unistd.h>
#include <glib-unix.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <math.h>
#include <gio/gdesktopappinfo.h>
#include "miller-test-input.h"
#include "miller-test-operations.h"
#include "nautilus-application.h"
#include "nautilus-app-chooser.h"
#include "nautilus-batch-rename-dialog.h"
#include "nautilus-clipboard.h"
#include "nautilus-compress-dialog.h"
#include "nautilus-dbus-launcher.h"
#include "nautilus-file-operations.h"
#include "nautilus-file.h"
#include "nautilus-file-utilities.h"
#include "nautilus-file-undo-manager.h"
#include "nautilus-files-view.h"
#include "nautilus-global-preferences.h"
#include "nautilus-location-entry.h"
#include "nautilus-file-conflict-dialog.h"
#include "nautilus-rename-file-popover.h"
#include "nautilus-progress-info.h"
#include "nautilus-progress-info-manager.h"
#include "nautilus-progress-info-widget.h"
#include "nautilus-resources.h"
#include "nautilus-tag-manager.h"
#include "nautilus-toolbar.h"
#include "nautilus-view-info.h"
#include "nautilus-view-model.h"
#include "nautilus-window.h"
#include "nautilus-window-slot.h"

static void test_native_multi_rename (OperationFixture *f, gconstpointer unused);
static void operations_prepare_templates (void);
static void operations_press_button (GtkWidget *dialog, const char *label);

static MillerTestInput *input;
static const char payload[] = MILLER_TEST_OPERATION_PAYLOAD;

static void
operations_observe_paste_action (GSimpleAction *action, GVariant *parameter, OperationFixture *f)
{
    f->paste_actions++;
}

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

static void
operations_activate_menu_label (NautilusFilesView *view, const char *text)
{
    gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    GtkWidget *label;
    while ((label = operations_find_label (GTK_WIDGET (view), text)) == NULL)
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    GtkWidget *item = label;
    while (item != NULL && !gtk_widget_get_focusable (item)) { item = gtk_widget_get_parent (item); }
    g_assert_nonnull (item);
    g_assert_true (gtk_widget_grab_focus (item));
    operations_key (GDK_KEY_Return);
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
test_external_clipboard (OperationFixture *f, gconstpointer late_data)
{
    gboolean delayed = GPOINTER_TO_UINT (late_data) != NAUTILUS_VIEW_MILLER_ID;
    gboolean close_origin = (GPOINTER_TO_UINT (late_data) & 0x200) != 0;
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
        if (close_origin)
        {
            /* Exercise the native tab-close pipeline with the real clipboard
             * read still awaiting the stopped foreign provider. Do not retain
             * its view or slot: disposal must cancel the pending read. */
            AdwTabView *tabs = ADW_TAB_VIEW (operations_find_type (f->window, ADW_TYPE_TAB_VIEW));
            g_assert_nonnull (tabs);
            AdwTabPage *page = adw_tab_view_get_page (tabs, GTK_WIDGET (original_slot));
            g_assert_nonnull (page);
            NautilusWindowSlot *closed_slot = original_slot;
            NautilusFilesView *closed_view = destination;
            g_object_add_weak_pointer (G_OBJECT (closed_slot), (gpointer *) &closed_slot);
            g_object_add_weak_pointer (G_OBJECT (closed_view), (gpointer *) &closed_view);
            adw_tab_view_close_page (tabs, page);
            g_clear_object (&original_slot);
            deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
            while (closed_slot != NULL || closed_view != NULL)
            {
                g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
            }
            g_assert_cmpint (adw_tab_view_get_n_pages (tabs), ==, 1);
            g_assert_cmpuint (f->jobs->len, ==, jobs_before);
            destination = NULL;
            g_test_message ("Pending real clipboard read: initiating tab and view finalized before provider resumes");
        }
        g_subprocess_send_signal (child, SIGCONT);
    }
    g_autofree char *name = g_file_get_basename (f->source_file);
    g_autoptr (GFile) copied = g_file_get_child (f->parent, name);
    g_autoptr (GFile) wrong_child = g_file_get_child (f->child, name);
    g_autoptr (GFile) wrong_sibling = g_file_get_child (f->sibling, name);
    if (!close_origin)
    {
        operations_wait_copy (f, destination, copied);
        operations_assert_bytes (f, copied, payload);
    }
    operations_assert_bytes (f, f->source_file, payload);
    g_assert_false (g_file_query_exists (wrong_child, NULL));
    g_assert_false (g_file_query_exists (wrong_sibling, NULL));
    operations_assert_sentinels (f);
    if (delayed)
    {
        operations_assert_focus (f, operations_view_for_directory (f, f->sibling));
        if (!close_origin)
        {
            g_test_message ("Real external clipboard reply arrived after genuine Ctrl+T; captured parent destination won");
        }
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
    if (close_origin)
    {
        operations_pump (300);
        g_assert_cmpuint (f->jobs->len, ==, jobs_before);
        g_assert_false (g_file_query_exists (copied, NULL));
        g_assert_false (g_file_query_exists (wrong_child, NULL));
        g_assert_false (g_file_query_exists (wrong_sibling, NULL));
        operations_assert_bytes (f, f->source_file, payload);
        operations_assert_sentinels (f);
        operations_assert_focus (f, operations_view_for_directory (f, f->sibling));
        g_test_message ("Real clipboard provider resumed and reaped after tab destruction; no job or destination writes");
    }
}

static void
test_new_folder (OperationFixture *f, gconstpointer unused)
{
    const char *test = g_test_get_path ();
    gboolean empty_child = g_str_has_suffix (test, "new-folder-empty-child");
    gboolean entry_focus = g_str_has_suffix (test, "toolbar-with-entry-focus");
    if (empty_child)
    {
        g_autoptr (GFile) sentinel = g_file_get_child (f->child, "child-sentinel.txt");
        operations_owned (f, sentinel); g_assert_true (g_file_delete (sentinel, NULL, NULL));
    }
    NautilusFilesView *parent = operations_open_parent (f);
    NautilusFilesView *target = parent;
    GFile *directory = f->parent;
    if (empty_child)
    {
        operations_key (GDK_KEY_Right); operations_settle (f);
        target = operations_view_for_directory (f, f->child); directory = f->child;
        operations_assert_focus (f, target);
    }
    if (entry_focus)
    {
        operations_chord (GDK_KEY_l, FALSE);
        GtkWidget *entry = operations_find_type (f->window, NAUTILUS_TYPE_LOCATION_ENTRY);
        g_assert_nonnull (entry);
        GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (f->window));
        g_assert_true (focus == entry || gtk_widget_is_ancestor (focus, entry));
        g_assert_true (nautilus_window_slot_get_current_view (f->slot) == parent);
    }
    /* The native managed shortcut must use the active action context even
     * while focus is in the toolbar's location entry. */
    operations_chord (GDK_KEY_n, TRUE);
    gint64 deadline = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
    AdwDialog *dialog;
    while ((dialog = adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (f->window))) == NULL)
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    GtkWidget *name = operations_find_type (GTK_WIDGET (dialog), ADW_TYPE_ENTRY_ROW);
    g_assert_nonnull (name);
    operations_type ("created-folder");
    g_assert_cmpstr (gtk_editable_get_text (GTK_EDITABLE (name)), ==, "created-folder");
    operations_key (GDK_KEY_Return);
    g_autoptr (GFile) created = g_file_get_child (directory, "created-folder");
    g_autoptr (GFile) wrong = g_file_get_child (empty_child ? f->parent : f->child, "created-folder");
    operations_wait_copy (f, target, created);
    g_assert_cmpint (g_file_query_file_type (created, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL), ==, G_FILE_TYPE_DIRECTORY);
    g_assert_false (g_file_query_exists (wrong, NULL));
    g_autoptr (GFile) sibling_wrong = g_file_get_child (f->sibling, "created-folder");
    g_assert_false (g_file_query_exists (sibling_wrong, NULL));
    operations_assert_bytes (f, f->source_file, payload);
    if (!empty_child) { operations_assert_sentinels (f); }
    else
    {
        operations_key (GDK_KEY_Left); operations_settle (f);
        operations_assert_focus (f, parent);
        g_autoptr (GFile) sibling = g_file_get_child (f->sibling, "sibling-sentinel.txt");
        operations_assert_bytes (f, sibling, "sibling unchanged");
    }
    g_test_message ("Real Ctrl+Shift+N, typed name and Return used native New Folder dialog/job; target=%s",
                    empty_child ? "empty child" : "parent with child disclosed");
}

static void
test_rename_captured (OperationFixture *f, gconstpointer unused)
{
    NautilusFilesView *view = operations_view_for_directory (f, f->source_dir);
    operations_select (f, view, f->source_file); operations_assert_focus (f, view);
    operations_key (GDK_KEY_F2);
    GtkWidget *popover = operations_find_type (GTK_WIDGET (view), NAUTILUS_TYPE_RENAME_FILE_POPOVER);
    g_assert_nonnull (popover);
    GtkWidget *entry = operations_find_type (popover, GTK_TYPE_ENTRY);
    g_assert_nonnull (entry); g_assert_true (gtk_widget_get_mapped (entry));
    operations_chord (GDK_KEY_a, FALSE); operations_type ("renamed-source.txt");
    g_assert_cmpstr (gtk_editable_get_text (GTK_EDITABLE (entry)), ==, "renamed-source.txt");
    /* Controlled selection replacement while the native rename UI retains
     * its captured file. Keep actual keyboard focus in its own entry. */
    nautilus_files_view_set_selection (view, NULL, FALSE);
    operations_pump (60);
    g_assert_true (gtk_widget_get_mapped (entry));
    operations_key (GDK_KEY_Return);
    g_autoptr (GFile) renamed = g_file_get_child (f->source_dir, "renamed-source.txt");
    operations_wait_file (f, view, renamed);
    operations_assert_bytes (f, renamed, payload);
    g_assert_false (g_file_query_exists (f->source_file, NULL));
    operations_assert_sentinels (f);
}

static GtkWidget *
operations_wait_window (OperationFixture *f, GType type)
{
    gint64 deadline = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
    for (;;)
    {
        GListModel *windows = gtk_window_get_toplevels ();
        for (guint i = 0; i < g_list_model_get_n_items (windows); i++)
        {
            g_autoptr (GtkWidget) window = g_list_model_get_item (windows, i);
            if (g_type_is_a (G_OBJECT_TYPE (window), type) && gtk_widget_get_mapped (window))
            {
                return g_steal_pointer (&window);
            }
        }
        if (g_get_monotonic_time () >= deadline)
        {
            for (guint i = 0; i < g_list_model_get_n_items (windows); i++)
            {
                g_autoptr (GtkWidget) window = g_list_model_get_item (windows, i);
                g_test_message ("Outstanding native window type=%s title=%s mapped=%d",
                    G_OBJECT_TYPE_NAME (window), gtk_window_get_title (GTK_WINDOW (window)), gtk_widget_get_mapped (window));
            }
            for (guint i = 0; i < f->jobs->len; i++)
            {
                NautilusProgressInfo *job = f->jobs->pdata[i];
                g_autofree char *status = nautilus_progress_info_get_status (job);
                g_autofree char *details = nautilus_progress_info_get_details (job);
                g_test_message ("Native job finished=%d status=%s details=%s", nautilus_progress_info_get_is_finished (job), status, details);
            }
            g_assert_not_reached ();
        }
        operations_pump (10);
    }
}

static void
test_copy_move_to (OperationFixture *f, gconstpointer unused)
{
    gboolean move = g_str_has_suffix (g_test_get_path (), "move-to-captured-selection");
    NautilusFilesView *view = operations_view_for_directory (f, f->source_dir);
    operations_select (f, view, f->source_file); operations_assert_focus (f, view);
    operations_key (GDK_KEY_Menu);
    operations_activate_menu_label (view, move ? "Move to…" : "Copy to…");
    g_autoptr (GtkWidget) dialog = operations_wait_window (f, GTK_TYPE_FILE_CHOOSER_DIALOG);
    g_assert_true (gtk_window_get_transient_for (GTK_WINDOW (dialog)) == GTK_WINDOW (f->window));
    /* Change the source selection while the real asynchronous chooser is
     * open; its native request must retain the initiating file snapshot. */
    nautilus_files_view_set_selection (view, NULL, FALSE);
    g_assert_true (gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), f->parent, NULL));
    operations_pump (250);
    g_autoptr (GFile) current = gtk_file_chooser_get_current_folder (GTK_FILE_CHOOSER (dialog));
    g_assert_true (g_file_equal (current, f->parent));
    GtkWidget *accept = gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
    g_assert_nonnull (accept); g_assert_true (gtk_widget_is_sensitive (accept));
    gtk_widget_grab_focus (accept); operations_key (GDK_KEY_Return);
    g_autofree char *name = g_file_get_basename (f->source_file);
    g_autoptr (GFile) copied = g_file_get_child (f->parent, name);
    gint64 deadline = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
    while (!g_file_query_exists (copied, NULL))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    operations_settle (f);
    operations_assert_bytes (f, copied, payload);
    if (move) { g_assert_false (g_file_query_exists (f->source_file, NULL)); }
    else { operations_assert_bytes (f, f->source_file, payload); }
    g_assert_cmpuint (f->jobs->len, >, 0);
    operations_assert_sentinels (f);
}

static void
operations_new_tab_at (OperationFixture *f, GFile *directory)
{
    nautilus_window_new_tab (NAUTILUS_WINDOW (f->window));
    g_clear_object (&f->slot);
    g_object_get (f->window, "active-slot", &f->slot, NULL);
    nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), directory, 0, NULL);
    /* A native transfer may deliberately be paused in its conflict dialog.
     * Wait for this tab's loading, independently of outstanding file jobs. */
    gint64 deadline = g_get_monotonic_time () + 15 * G_TIME_SPAN_SECOND;
    while (nautilus_window_slot_get_loading (f->slot))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    operations_pump (120);
}

static void
operations_accept_folder (GtkWidget *dialog, GFile *directory)
{
    g_assert_true (gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), directory, NULL));
    gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    for (;;)
    {
        g_autoptr (GFile) current = gtk_file_chooser_get_current_folder (GTK_FILE_CHOOSER (dialog));
        GtkWidget *accept = gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
        if (current != NULL && g_file_equal (current, directory) && gtk_widget_is_sensitive (accept))
        {
            gtk_widget_grab_focus (accept); operations_key (GDK_KEY_Return); return;
        }
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
}

static void
test_inactive_column_menu (OperationFixture *f, gconstpointer unused)
{
    NautilusFilesView *parent = operations_open_parent (f);
    operations_key (GDK_KEY_Right); operations_settle (f);
    NautilusFilesView *child = operations_view_for_directory (f, f->child);
    operations_assert_focus (f, child);
    GtkWidget *row_label = operations_find_label (GTK_WIDGET (parent), "child");
    operations_click (f, row_label, 273);
    operations_activate_menu_label (parent, "Copy to…");
    g_autoptr (GtkWidget) chooser = operations_wait_window (f, GTK_TYPE_FILE_CHOOSER_DIALOG);
    /* Native window APIs deliberately change context while the real chooser
     * is open. The captured inactive-column menu selected the child folder. */
    operations_new_tab_at (f, f->sibling);
    NautilusFilesView *other = operations_view_for_directory (f, f->sibling);
    operations_accept_folder (chooser, f->source_dir);
    g_autoptr (GFile) copied_dir = g_file_get_child (f->source_dir, "child");
    g_autoptr (GFile) copied = g_file_get_child (copied_dir, "child-sentinel.txt");
    gint64 deadline = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
    while (!g_file_query_exists (copied, NULL))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    operations_settle (f);
    operations_assert_bytes (f, copied, "child unchanged");
    operations_assert_bytes (f, f->source_file, payload);
    operations_assert_sentinels (f);
    g_assert_cmpuint (f->jobs->len, >, 0);
    g_assert_true (nautilus_window_slot_get_current_view (f->slot) == other);
    g_assert_true (g_object_get_data (G_OBJECT (f->window), "nautilus-active-file-view") == other);
}

static void
test_dialog_source_pruned (OperationFixture *f, gconstpointer unused)
{
    gboolean cancel = g_str_has_suffix (g_test_get_path (), "-cancel");
    NautilusFilesView *source = operations_view_for_directory (f, f->source_dir);
    operations_select (f, source, f->source_file); operations_assert_focus (f, source);
    operations_key (GDK_KEY_Menu); operations_activate_menu_label (source, "Copy to…");
    g_autoptr (GtkWidget) chooser = operations_wait_window (f, GTK_TYPE_FILE_CHOOSER_DIALOG);
    GWeakRef weak;
    g_weak_ref_init (&weak, source);
    nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), f->sibling, 0, NULL);
    operations_settle (f);
    g_autoptr (GFile) moved_dir = g_file_get_child (f->root, "renamed-source");
    operations_owned (f, moved_dir); operations_owned (f, f->source_dir);
    g_assert_true (g_file_move (f->source_dir, moved_dir, G_FILE_COPY_NONE, NULL, NULL, NULL, NULL));
    operations_pump (300);
    g_autoptr (GtkWidget) retained = g_weak_ref_get (&weak);
    g_assert_nonnull (retained);
    g_assert_null (gtk_widget_get_root (retained));
    g_test_message ("Real source branch replaced and externally renamed; old view retained=%d", retained != NULL);
    g_clear_object (&retained);
    if (cancel) { operations_key (GDK_KEY_Escape); }
    else { operations_accept_folder (chooser, f->parent); }
    g_autofree char *name = g_file_get_basename (f->source_file);
    g_autoptr (GFile) copied = g_file_get_child (f->parent, name);
    g_autoptr (GFile) moved_source = g_file_get_child (moved_dir, name);
    gint64 deadline = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
    while (!cancel && !g_file_query_exists (copied, NULL))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    operations_settle (f);
    if (cancel)
    {
        g_assert_false (g_file_query_exists (copied, NULL));
        g_assert_cmpuint (f->jobs->len, ==, 0);
    }
    else { operations_assert_bytes (f, copied, payload); }
    operations_assert_bytes (f, moved_source, payload);
    operations_assert_sentinels (f);
    g_autoptr (GFile) wrong = g_file_get_child (f->sibling, name);
    g_assert_false (g_file_query_exists (wrong, NULL));
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->sibling));
    /* The async chooser owns this reference only through response handling,
     * including cancellation; it must not retain discarded columns. */
    for (;;)
    {
        g_autoptr (GtkWidget) old_view = g_weak_ref_get (&weak);
        if (old_view == NULL) { break; }
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    operations_assert_focus (f, operations_view_for_directory (f, f->sibling));
    g_weak_ref_clear (&weak);
}

static void
test_operation_tab_switch (OperationFixture *f, gconstpointer unused)
{
    gboolean entry_focus = g_str_has_suffix (g_test_get_path (), "-entry");
    operations_copy_source (f, FALSE);
    g_autofree char *name = g_file_get_basename (f->source_file);
    g_autoptr (GFile) copied = g_file_get_child (f->parent, name);
    operations_owned (f, copied);
    g_assert_true (g_file_replace_contents (copied, "old destination", 15, NULL, FALSE, 0, NULL, NULL, NULL));
    NautilusFilesView *parent = operations_open_parent (f);
    GActionGroup *actions = nautilus_files_view_get_private_action_group (parent);
    gint64 ready = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    g_test_message ("Native paste action ready=%d before conflict input", g_action_group_get_action_enabled (actions, "paste_accel"));
    while (!g_action_group_get_action_enabled (actions, "paste_accel"))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, ready); operations_pump (10);
    }
    operations_assert_focus (f, parent); operations_assert_clipboard (f, FALSE);
    GAction *paste = g_action_map_lookup_action (G_ACTION_MAP (actions), "paste");
    g_signal_connect (paste, "activate", G_CALLBACK (operations_observe_paste_action), f);
    operations_chord (GDK_KEY_v, FALSE);
    g_test_message ("Conflict Paste observed: actual Ctrl+V keys=%u native action activations=%u jobs=%u enabled=%d",
                    f->paste_keys, f->paste_actions, f->jobs->len,
                    g_action_group_get_action_enabled (actions, "paste_accel"));
    g_autoptr (GtkWidget) conflict = operations_wait_window (f, NAUTILUS_TYPE_FILE_CONFLICT_DIALOG);
    g_assert_cmpuint (f->jobs->len, >, 0);
    g_assert_false (nautilus_progress_info_get_is_finished (f->jobs->pdata[0]));
    GtkWidget *entry = NULL;
    if (entry_focus)
    {
        GtkWidget *toolbar = operations_find_type (f->window, NAUTILUS_TYPE_TOOLBAR);
        g_assert_nonnull (toolbar);
        g_assert_true (gtk_widget_activate_action (toolbar, "toolbar.edit-location", NULL));
        entry = operations_find_type (toolbar, NAUTILUS_TYPE_LOCATION_ENTRY);
        GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (f->window));
        g_assert_nonnull (entry);
        g_assert_true (focus == entry || gtk_widget_is_ancestor (focus, entry));
    }
    operations_new_tab_at (f, f->sibling);
    NautilusFilesView *other = operations_view_for_directory (f, f->sibling);
    GtkWidget *replace_label = operations_find_label (conflict, "Replace");
    g_assert_nonnull (replace_label);
    GtkWidget *replace = gtk_widget_get_ancestor (replace_label, GTK_TYPE_BUTTON);
    g_assert_nonnull (replace);
    gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    while (!gtk_widget_is_sensitive (replace))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    gtk_widget_grab_focus (replace); operations_key (GDK_KEY_Return);
    operations_settle (f);
    operations_assert_bytes (f, copied, payload);
    operations_assert_bytes (f, f->source_file, payload);
    operations_assert_sentinels (f);
    g_autoptr (GFile) wrong = g_file_get_child (f->sibling, name);
    g_assert_false (g_file_query_exists (wrong, NULL));
    g_assert_true (nautilus_window_slot_get_current_view (f->slot) == other);
    if (f->view_id == NAUTILUS_VIEW_MILLER_ID)
    {
        g_assert_true (g_object_get_data (G_OBJECT (f->window), "nautilus-active-file-view") == other);
    }
    g_assert_cmpint (nautilus_file_conflict_dialog_get_response (NAUTILUS_FILE_CONFLICT_DIALOG (conflict)), ==, CONFLICT_RESPONSE_REPLACE);
    if (entry_focus)
    {
        operations_wait_active (f);
        GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (f->window));
        g_assert_true (focus == entry || gtk_widget_is_ancestor (focus, entry));
    }
    else { operations_assert_focus (f, other); }
}

/* Genuine monitor pruning: keep the source displayed until external GIO
 * removal makes the native slot fall back. No navigation API prunes it. */
static void
test_monitor_dialog_source_pruned (OperationFixture *f, gconstpointer unused)
{
    gboolean cancel = g_str_has_suffix (g_test_get_path (), "-cancel");
    g_autoptr (GFile) preserved_dir = g_file_get_child (f->root, "preserved");
    g_autofree char *name = g_file_get_basename (f->source_file);
    g_autoptr (GFile) preserved_file = g_file_get_child (preserved_dir, name);
    operations_owned (f, preserved_dir); operations_owned (f, preserved_file);
    g_assert_true (g_file_make_directory (preserved_dir, NULL, NULL));
    NautilusFilesView *source = operations_view_for_directory (f, f->source_dir);
    operations_select (f, source, f->source_file); operations_assert_focus (f, source);
    operations_key (GDK_KEY_Menu); operations_activate_menu_label (source, "Copy to…");
    g_autoptr (GtkWidget) chooser = operations_wait_window (f, GTK_TYPE_FILE_CHOOSER_DIALOG);
    GWeakRef weak; g_weak_ref_init (&weak, source);
    g_assert_true (gtk_widget_get_root (GTK_WIDGET (source)) == GTK_ROOT (f->window));
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->source_dir));
    operations_owned (f, f->source_file); operations_owned (f, f->source_dir);
    g_assert_true (g_file_move (f->source_file, preserved_file, G_FILE_COPY_NONE, NULL, NULL, NULL, NULL));
    g_assert_true (g_file_delete (f->source_dir, NULL, NULL));
    gint64 deadline = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
    for (;;)
    {
        g_autoptr (GtkWidget) retained = g_weak_ref_get (&weak);
        g_assert_nonnull (retained);
        if (gtk_widget_get_root (retained) == NULL &&
            g_file_equal (nautilus_window_slot_get_location (f->slot), f->root) &&
            !nautilus_window_slot_get_loading (f->slot)) { break; }
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    g_test_message ("Native monitor pruned displayed source and selected existing parent while chooser stayed open");
    guint history = g_list_length (nautilus_window_slot_get_back_history (f->slot));
    g_autoptr (GFile) copied = g_file_get_child (f->parent, name);
    gboolean error_shown = FALSE;
    if (cancel) { operations_key (GDK_KEY_Escape); }
    else
    {
        operations_accept_folder (chooser, f->parent);
        for (;;)
        {
            gboolean finished = f->jobs->len > 0;
            for (guint i = 0; i < f->jobs->len; i++)
            {
                finished &= nautilus_progress_info_get_is_finished (f->jobs->pdata[i]);
            }
            if (finished) { break; }
            GListModel *windows = gtk_window_get_toplevels ();
            for (guint i = 0; i < g_list_model_get_n_items (windows); i++)
            {
                g_autoptr (GtkWidget) window = g_list_model_get_item (windows, i);
                GtkWidget *alert = operations_find_type (window, ADW_TYPE_ALERT_DIALOG);
                if (alert != NULL && gtk_widget_get_mapped (alert))
                {
                    g_assert_false (error_shown); error_shown = TRUE;
                    g_test_message ("Native operation truthfully reports removed source: %s",
                                    adw_alert_dialog_get_heading (ADW_ALERT_DIALOG (alert)));
                    operations_press_button (alert, "Cancel"); break;
                }
            }
            g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
        }
        g_assert_cmpuint (f->jobs->len, ==, 1);
    }
    operations_settle (f);
    if (cancel || error_shown) { g_assert_false (g_file_query_exists (copied, NULL)); }
    else { operations_assert_bytes (f, copied, payload); }
    operations_assert_bytes (f, preserved_file, payload); operations_assert_sentinels (f);
    g_assert_false (g_file_query_exists (f->source_dir, NULL));
    GFile *unrelated[] = { f->root, f->child, f->sibling };
    for (guint i = 0; i < G_N_ELEMENTS (unrelated); i++)
    {
        g_autoptr (GFile) wrong = g_file_get_child (unrelated[i], name);
        g_assert_false (g_file_query_exists (wrong, NULL));
    }
    for (;;)
    {
        g_autoptr (GtkWidget) retained = g_weak_ref_get (&weak);
        if (retained == NULL) { break; }
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    g_weak_ref_clear (&weak);
    g_assert_cmpuint (g_list_length (nautilus_window_slot_get_back_history (f->slot)), ==, history);
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->root));
    operations_assert_focus (f, operations_view_for_directory (f, f->root));
}

static void
test_new_folder_source_pruned (OperationFixture *f, gconstpointer unused)
{
    gboolean cancel = g_str_has_suffix (g_test_get_path (), "-cancel");
    NautilusFilesView *source = operations_view_for_directory (f, f->source_dir);
    operations_select (f, source, f->source_file); operations_assert_focus (f, source);
    operations_chord (GDK_KEY_n, TRUE);
    AdwDialog *dialog = adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (f->window));
    g_assert_nonnull (dialog);
    GtkWidget *entry = operations_find_type (GTK_WIDGET (dialog), ADW_TYPE_ENTRY_ROW);
    g_assert_nonnull (entry);
    GWeakRef weak;
    g_weak_ref_init (&weak, source);
    nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), f->sibling, 0, NULL);
    operations_settle (f);
    g_autoptr (GFile) moved_dir = g_file_get_child (f->root, "moved-new-folder-source");
    operations_owned (f, moved_dir); operations_owned (f, f->source_dir);
    g_assert_true (g_file_move (f->source_dir, moved_dir, G_FILE_COPY_NONE, NULL, NULL, NULL, NULL));
    operations_pump (300);
    g_autoptr (GtkWidget) retained = g_weak_ref_get (&weak);
    g_test_message ("Native New Folder owner after real branch pruning retained=%d", retained != NULL);
    g_assert_nonnull (retained); g_assert_null (gtk_widget_get_root (retained));
    g_clear_object (&retained);
    if (cancel) { operations_key (GDK_KEY_Escape); }
    else
    {
        gtk_widget_grab_focus (entry);
        operations_type ("created-folder"); operations_key (GDK_KEY_Return);
    }
    g_autoptr (GFile) created = g_file_get_child (moved_dir, "created-folder");
    gint64 deadline = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
    while (!cancel && !g_file_query_exists (created, NULL))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    operations_settle (f);
    if (cancel)
    {
        g_assert_false (g_file_query_exists (created, NULL));
        g_assert_cmpuint (f->jobs->len, ==, 0);
    }
    g_autofree char *name = g_file_get_basename (f->source_file);
    g_autoptr (GFile) moved_source = g_file_get_child (moved_dir, name);
    operations_assert_bytes (f, moved_source, payload);
    operations_assert_sentinels (f);
    g_autoptr (GFile) wrong = g_file_get_child (f->sibling, "created-folder");
    g_assert_false (g_file_query_exists (wrong, NULL));
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->sibling));
    for (;;)
    {
        g_autoptr (GtkWidget) old_view = g_weak_ref_get (&weak);
        if (old_view == NULL) { break; }
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    operations_assert_focus (f, operations_view_for_directory (f, f->sibling));
    g_weak_ref_clear (&weak);
}

static void
test_new_folder_after_tab_switch (OperationFixture *f, gconstpointer unused)
{
    NautilusFilesView *source = operations_view_for_directory (f, f->source_dir);
    operations_select (f, source, f->source_file); operations_assert_focus (f, source);
    operations_chord (GDK_KEY_n, TRUE);
    AdwDialog *dialog = adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (f->window));
    g_assert_nonnull (dialog);
    GtkWidget *entry = operations_find_type (GTK_WIDGET (dialog), ADW_TYPE_ENTRY_ROW);
    g_assert_nonnull (entry);
    operations_new_tab_at (f, f->sibling);
    g_assert_nonnull (gtk_widget_get_root (GTK_WIDGET (source)));
    g_assert_false (gtk_widget_get_mapped (GTK_WIDGET (source)));
    gtk_widget_grab_focus (entry);
    operations_type ("created-folder"); operations_key (GDK_KEY_Return);
    g_autoptr (GFile) created = g_file_get_child (f->source_dir, "created-folder");
    gint64 deadline = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
    while (!g_file_query_exists (created, NULL))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    operations_settle (f);
    while (adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (f->window)) != NULL)
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    operations_pump (250);
    operations_assert_bytes (f, f->source_file, payload);
    operations_assert_sentinels (f);
    g_autoptr (GFile) wrong = g_file_get_child (f->sibling, "created-folder");
    g_assert_false (g_file_query_exists (wrong, NULL));
    operations_assert_focus (f, operations_view_for_directory (f, f->sibling));
}

/* Model another native window callback restoring an editor during dialog
 * closure. Setup is controlled; closure itself uses genuine Escape. */
static void
operations_restore_location_entry (AdwDialog *dialog, OperationFixture *f)
{
    GtkWidget *toolbar = operations_find_type (f->window, NAUTILUS_TYPE_TOOLBAR);
    g_assert_true (gtk_widget_activate_action (toolbar, "toolbar.edit-location", NULL));
    GtkWidget *entry = operations_find_type (toolbar, NAUTILUS_TYPE_LOCATION_ENTRY);
    GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (f->window));
    g_assert_nonnull (entry);
    g_assert_true (focus == entry || gtk_widget_is_ancestor (focus, entry));
    g_test_message ("Native close callback restored a valid location entry before after-handlers");
}

static void
test_dialog_close_preserves_entry (OperationFixture *f, gconstpointer unused)
{
    NautilusFilesView *source = operations_view_for_directory (f, f->source_dir);
    operations_select (f, source, f->source_file);
    operations_chord (GDK_KEY_l, FALSE);
    GtkWidget *toolbar = operations_find_type (f->window, NAUTILUS_TYPE_TOOLBAR);
    GtkWidget *entry = operations_find_type (toolbar, NAUTILUS_TYPE_LOCATION_ENTRY);
    g_assert_nonnull (entry);
    GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (f->window));
    g_assert_true (focus == entry || gtk_widget_is_ancestor (focus, entry));
    operations_chord (GDK_KEY_n, TRUE);
    AdwDialog *dialog = adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (f->window));
    g_assert_nonnull (dialog);
    operations_new_tab_at (f, f->sibling);
    guint history = g_list_length (nautilus_window_slot_get_back_history (f->slot));
    g_signal_connect (dialog, "closed", G_CALLBACK (operations_restore_location_entry), f);
    operations_key (GDK_KEY_Escape);
    gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    while (adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (f->window)) != NULL)
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    operations_pump (250);
    focus = gtk_root_get_focus (GTK_ROOT (f->window));
    g_test_message ("Native dialog close restored focus=%s expected location entry", focus ? G_OBJECT_TYPE_NAME (focus) : "none");
    g_assert_nonnull (focus);
    g_assert_true (focus == entry || gtk_widget_is_ancestor (focus, entry));
    g_assert_cmpuint (g_list_length (nautilus_window_slot_get_back_history (f->slot)), ==, history);
    g_assert_cmpuint (f->jobs->len, ==, 0);
    operations_assert_bytes (f, f->source_file, payload); operations_assert_sentinels (f);
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->sibling));
}

/* Catch a window-hosted matching dialog dereferencing a destroyed source,
 * or applying its result to the replacement listing instead of its source. */
static void
test_pattern_source_pruned (OperationFixture *f, gconstpointer unused)
{
    gboolean cancel = g_str_has_suffix (g_test_get_path (), "-cancel");
    NautilusFilesView *source = operations_view_for_directory (f, f->source_dir);
    operations_select (f, source, f->source_file);
    nautilus_files_view_set_selection (source, NULL, FALSE);
    operations_chord (GDK_KEY_s, FALSE);
    AdwDialog *dialog = adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (f->window));
    g_assert_nonnull (dialog);
    GtkWidget *entry = operations_find_type (GTK_WIDGET (dialog), GTK_TYPE_ENTRY);
    g_assert_nonnull (entry);
    GWeakRef weak; g_weak_ref_init (&weak, source);
    nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), f->sibling, 0, NULL);
    operations_settle (f);
    g_autoptr (GtkWidget) retained = g_weak_ref_get (&weak);
    g_test_message ("Pattern dialog source after branch pruning retained=%d", retained != NULL);
    guint history = g_list_length (nautilus_window_slot_get_back_history (f->slot));
    if (cancel) { operations_key (GDK_KEY_Escape); }
    else
    {
        gtk_editable_set_text (GTK_EDITABLE (entry), "*.txt");
        operations_press_button (GTK_WIDGET (dialog), "Select");
        g_assert_nonnull (retained);
        g_autolist (NautilusFile) selected = nautilus_files_view_get_selection (NAUTILUS_FILES_VIEW (retained));
        g_assert_cmpuint (g_list_length (selected), ==, 1);
        g_autoptr (GFile) chosen = nautilus_file_get_location (selected->data);
        g_assert_true (g_file_equal (chosen, f->source_file));
    }
    operations_pump (300);
    NautilusFilesView *current = operations_view_for_directory (f, f->sibling);
    g_autolist (NautilusFile) current_selection = nautilus_files_view_get_selection (current);
    g_assert_null (current_selection);
    g_clear_object (&retained);
    gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    for (;;)
    {
        g_autoptr (GtkWidget) old = g_weak_ref_get (&weak);
        if (old == NULL) { break; }
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    g_weak_ref_clear (&weak);
    g_assert_cmpuint (g_list_length (nautilus_window_slot_get_back_history (f->slot)), ==, history);
    g_assert_cmpuint (f->jobs->len, ==, 0);
    operations_assert_bytes (f, f->source_file, payload); operations_assert_sentinels (f);
    operations_assert_focus (f, current);
}

/* New Folder with Selection must move the files selected when its dialog
 * opened, even if another model selection supersedes them before Accept. */
static void
test_new_folder_captured_selection (OperationFixture *f, gconstpointer unused)
{
    gboolean pruned = strstr (g_test_get_path (), "-pruned") != NULL;
    gboolean cancel = g_str_has_suffix (g_test_get_path (), "-cancel");
    gboolean animations;
    g_object_get (gtk_settings_get_default (), "gtk-enable-animations", &animations, NULL);
    if (pruned) { g_object_set (gtk_settings_get_default (), "gtk-enable-animations", FALSE, NULL); }
    g_autoptr (GFile) second = g_file_get_child (f->source_dir, "second.txt");
    g_autoptr (GFile) replacement = g_file_get_child (f->source_dir, "replacement.txt");
    operations_owned (f, second); operations_owned (f, replacement);
    g_assert_true (g_file_replace_contents (second, "second member", 13, NULL, FALSE, 0, NULL, NULL, NULL));
    g_assert_true (g_file_replace_contents (replacement, "not selected", 12, NULL, FALSE, 0, NULL, NULL, NULL));
    NautilusFilesView *source = operations_view_for_directory (f, f->source_dir);
    operations_select (f, source, second);
    g_autoptr (NautilusFile) first_file = nautilus_file_get (f->source_file);
    g_autoptr (NautilusFile) second_file = nautilus_file_get (second);
    g_autoptr (NautilusFile) replacement_file = nautilus_file_get (replacement);
    GList tail = { .data = second_file }; GList head = { .data = first_file, .next = &tail }; tail.prev = &head;
    nautilus_files_view_set_selection (source, &head, FALSE); operations_settle (f);
    operations_key (GDK_KEY_Menu);
    operations_activate_menu_label (source, "New Folder with Selection (2 Items)");
    AdwDialog *dialog = adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (f->window));
    g_assert_nonnull (dialog);
    GtkWidget *entry = operations_find_type (GTK_WIDGET (dialog), ADW_TYPE_ENTRY_ROW);
    g_assert_nonnull (entry);
    GWeakRef weak, dialog_weak; g_weak_ref_init (&weak, source); g_weak_ref_init (&dialog_weak, dialog);
    if (pruned)
    {
        nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), f->sibling, 0, NULL);
        operations_settle (f);
    }
    else
    {
        GList changed = { .data = replacement_file };
        nautilus_files_view_set_selection (source, &changed, FALSE);
    }
    guint history = g_list_length (nautilus_window_slot_get_back_history (f->slot));
    if (cancel) { operations_key (GDK_KEY_Escape); }
    else
    {
        gtk_widget_grab_focus (entry); operations_chord (GDK_KEY_a, FALSE);
        operations_type ("grouped"); operations_key (GDK_KEY_Return);
    }
    g_autoptr (GFile) folder = g_file_get_child (f->source_dir, "grouped");
    gint64 deadline = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
    while (!cancel && !g_file_query_exists (folder, NULL))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    operations_settle (f);
    g_autofree char *name = g_file_get_basename (f->source_file);
    g_autoptr (GFile) moved_first = g_file_get_child (folder, name);
    g_autoptr (GFile) moved_second = g_file_get_child (folder, "second.txt");
    g_autoptr (GFile) wrong = g_file_get_child (folder, "replacement.txt");
    if (cancel)
    {
        g_assert_false (g_file_query_exists (folder, NULL));
        g_assert_cmpuint (f->jobs->len, ==, 0);
        operations_assert_bytes (f, f->source_file, payload);
        operations_assert_bytes (f, second, "second member");
        operations_assert_bytes (f, replacement, "not selected");
    }
    else
    {
        g_test_message ("Captured folder result: first=%d second=%d replacement=%d pruned=%d",
                        g_file_query_exists (moved_first, NULL), g_file_query_exists (moved_second, NULL),
                        g_file_query_exists (wrong, NULL), pruned);
        operations_assert_bytes (f, moved_first, payload);
        operations_assert_bytes (f, moved_second, "second member");
        operations_assert_bytes (f, replacement, "not selected");
        g_assert_false (g_file_query_exists (f->source_file, NULL));
        g_assert_false (g_file_query_exists (second, NULL));
        g_assert_false (g_file_query_exists (wrong, NULL));
    }
    g_autoptr (GFile) wrong_sibling = g_file_get_child (f->sibling, "grouped");
    g_autoptr (GFile) wrong_child = g_file_get_child (f->child, "grouped");
    g_assert_false (g_file_query_exists (wrong_sibling, NULL));
    g_assert_false (g_file_query_exists (wrong_child, NULL));
    operations_assert_sentinels (f);
    operations_assert_focus (f, pruned ? operations_view_for_directory (f, f->sibling) : source);
    g_assert_cmpuint (g_list_length (nautilus_window_slot_get_back_history (f->slot)), ==, history);
    if (pruned)
    {
        for (;;)
        {
            g_autoptr (GtkWidget) retained = g_weak_ref_get (&weak);
            g_autoptr (AdwDialog) retained_dialog = g_weak_ref_get (&dialog_weak);
            if (retained == NULL && retained_dialog == NULL) { break; }
            g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
        }
    }
    g_weak_ref_clear (&weak); g_weak_ref_clear (&dialog_weak);
    g_object_set (gtk_settings_get_default (), "gtk-enable-animations", animations, NULL);
}

/* These acceptance cases catch wrong-destination routing or bypassing native
 * conflict policy: the original bytes must survive Skip/Cancel/Keep Both,
 * and only Replace may overwrite the captured destination. */
static void
operations_press_button (GtkWidget *dialog, const char *label)
{
    GtkWidget *text = operations_find_label (dialog, label);
    g_assert_nonnull (text);
    GtkWidget *button = gtk_widget_get_ancestor (text, GTK_TYPE_BUTTON);
    g_assert_nonnull (button);
    gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    while (!gtk_widget_is_sensitive (button))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    g_assert_true (gtk_widget_grab_focus (button));
    operations_key (GDK_KEY_Return);
}

static void
test_native_conflict (OperationFixture *f, gconstpointer unused)
{
    const char *test = g_test_get_path ();
    gboolean keep = strstr (test, "keep-both") != NULL;
    gboolean skip = strstr (test, "skip") != NULL;
    gboolean cancel = strstr (test, "cancel") != NULL;
    operations_copy_source (f, FALSE);
    g_autofree char *name = g_file_get_basename (f->source_file);
    g_autoptr (GFile) destination = g_file_get_child (f->parent, name);
    operations_owned (f, destination);
    g_assert_true (g_file_replace_contents (destination, "original bytes", 14, NULL, FALSE, 0, NULL, NULL, NULL));
    NautilusFilesView *parent = operations_open_parent (f);
    guint history = g_list_length (nautilus_window_slot_get_back_history (f->slot));
    operations_chord (GDK_KEY_v, FALSE);
    g_autoptr (GtkWidget) dialog = operations_wait_window (f, NAUTILUS_TYPE_FILE_CONFLICT_DIALOG);
    g_assert_cmpuint (f->jobs->len, ==, 1);
    NautilusProgressInfo *job = f->jobs->pdata[0];
    g_assert_true (nautilus_progress_info_get_is_started (job));
    g_assert_false (nautilus_progress_info_get_is_finished (job));
    if (keep)
    {
        GtkWidget *expander = operations_find_type (dialog, GTK_TYPE_EXPANDER);
        g_assert_nonnull (expander);
        g_assert_true (gtk_widget_grab_focus (expander));
        operations_key (GDK_KEY_space);
        g_assert_true (gtk_expander_get_expanded (GTK_EXPANDER (expander)));
        GtkWidget *entry = operations_find_type (expander, GTK_TYPE_ENTRY);
        g_assert_nonnull (entry);
        g_assert_true (gtk_widget_grab_focus (entry));
        operations_chord (GDK_KEY_a, FALSE); operations_type ("kept.txt");
        operations_press_button (dialog, "Rename");
    }
    else { operations_press_button (dialog, cancel ? "Cancel" : skip ? "Skip" : "Replace"); }
    operations_settle (f);
    g_assert_true (nautilus_progress_info_get_is_finished (job));
    g_assert_cmpint (nautilus_progress_info_get_is_cancelled (job), ==, cancel);
    ConflictResponse expected = keep ? CONFLICT_RESPONSE_RENAME : skip ? CONFLICT_RESPONSE_SKIP :
                                cancel ? CONFLICT_RESPONSE_CANCEL : CONFLICT_RESPONSE_REPLACE;
    g_assert_cmpint (nautilus_file_conflict_dialog_get_response (NAUTILUS_FILE_CONFLICT_DIALOG (dialog)), ==, expected);
    operations_assert_bytes (f, destination, keep || skip || cancel ? "original bytes" : payload);
    g_autoptr (GFile) renamed = g_file_get_child (f->parent, "kept.txt");
    if (keep) { operations_wait_file (f, parent, renamed); operations_assert_bytes (f, renamed, payload); }
    else { g_assert_false (g_file_query_exists (renamed, NULL)); }
    operations_assert_bytes (f, f->source_file, payload);
    operations_assert_sentinels (f);
    g_assert_cmpuint (g_list_length (nautilus_window_slot_get_back_history (f->slot)), ==, history);
    operations_assert_focus (f, parent);
}

static void
test_directory_merge (OperationFixture *f, gconstpointer unused)
{
    g_autoptr (GFile) source = g_file_get_child (f->source_dir, "merge");
    g_autoptr (GFile) destination = g_file_get_child (f->parent, "merge");
    operations_owned (f, source); operations_owned (f, destination);
    g_assert_true (g_file_make_directory (source, NULL, NULL));
    g_assert_true (g_file_make_directory (destination, NULL, NULL));
    g_autoptr (GFile) incoming = g_file_get_child (source, "incoming.txt");
    g_autoptr (GFile) existing = g_file_get_child (destination, "existing.txt");
    g_assert_true (g_file_replace_contents (incoming, payload, strlen (payload), NULL, FALSE, 0, NULL, NULL, NULL));
    g_assert_true (g_file_replace_contents (existing, "retained", 8, NULL, FALSE, 0, NULL, NULL, NULL));
    operations_select (f, operations_view_for_directory (f, f->source_dir), source);
    operations_chord (GDK_KEY_c, FALSE);
    NautilusFilesView *parent = operations_open_parent (f);
    operations_chord (GDK_KEY_v, FALSE);
    g_autoptr (GtkWidget) dialog = operations_wait_window (f, NAUTILUS_TYPE_FILE_CONFLICT_DIALOG);
    operations_press_button (dialog, "Merge");
    operations_settle (f);
    g_autoptr (GFile) copied = g_file_get_child (destination, "incoming.txt");
    operations_assert_bytes (f, copied, payload);
    operations_assert_bytes (f, incoming, payload);
    operations_assert_bytes (f, existing, "retained");
    operations_assert_sentinels (f);
    operations_assert_focus (f, parent);
}

static AdwDialog *
operations_wait_alert (OperationFixture *f)
{
    gint64 deadline = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
    for (;;)
    {
        AdwDialog *dialog = adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (f->window));
        if (ADW_IS_ALERT_DIALOG (dialog)) { return g_object_ref (dialog); }
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
}

/* Catch swallowed native permission errors, retargeted batches, or rollback
 * claims that erase a successfully copied member after another member fails. */
static void
test_native_permission_error (OperationFixture *f, gconstpointer unused)
{
    gboolean partial = g_str_has_suffix (g_test_get_path (), "partial-failure");
    g_autoptr (GFile) good = g_file_get_child (f->source_dir, "readable.txt");
    operations_owned (f, good);
    if (partial)
    {
        g_assert_true (g_file_replace_contents (good, "readable member", 15, NULL, FALSE, 0, NULL, NULL, NULL));
    }
    operations_owned (f, f->source_file);
    g_assert_true (g_file_set_attribute_uint32 (f->source_file, G_FILE_ATTRIBUTE_UNIX_MODE,
                                               0000, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL));
    g_autoptr (GError) error = NULL;
    GFileInputStream *unreadable = g_file_read (f->source_file, NULL, &error);
    g_assert_null (unreadable); g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
    g_clear_error (&error);
    NautilusFilesView *source = operations_view_for_directory (f, f->source_dir);
    operations_select (f, source, f->source_file);
    if (partial)
    {
        operations_select (f, source, good);
        g_autoptr (NautilusFile) first = nautilus_file_get (good);
        g_autoptr (NautilusFile) second = nautilus_file_get (f->source_file);
        GList tail = { .data = second };
        GList head = { .data = first, .next = &tail }; tail.prev = &head;
        nautilus_files_view_set_selection (source, &head, FALSE);
        operations_settle (f);
    }
    operations_chord (GDK_KEY_c, FALSE);
    NautilusFilesView *parent = operations_open_parent (f);
    operations_chord (GDK_KEY_v, FALSE);
    g_autoptr (AdwDialog) dialog = operations_wait_alert (f);
    g_test_message ("Native error heading=%s body=%s", adw_alert_dialog_get_heading (ADW_ALERT_DIALOG (dialog)),
                    adw_alert_dialog_get_body (ADW_ALERT_DIALOG (dialog)));
    GtkWidget *details = adw_alert_dialog_get_extra_child (ADW_ALERT_DIALOG (dialog));
    g_assert_true (GTK_IS_LABEL (details));
    g_test_message ("Native error details=%s", gtk_label_get_text (GTK_LABEL (details)));
    g_assert_nonnull (strstr (gtk_label_get_text (GTK_LABEL (details)), "Permission denied"));
    g_assert_cmpuint (f->jobs->len, ==, 1);
    g_assert_false (nautilus_progress_info_get_is_finished (f->jobs->pdata[0]));
    operations_press_button (GTK_WIDGET (dialog), partial ? "Skip" : "Cancel");
    operations_settle (f);
    g_assert_true (nautilus_progress_info_get_is_finished (f->jobs->pdata[0]));
    g_assert_cmpint (nautilus_progress_info_get_is_cancelled (f->jobs->pdata[0]), ==, !partial);
    /* Restore owned permissions before reading or native fixture cleanup. */
    g_assert_true (g_file_set_attribute_uint32 (f->source_file, G_FILE_ATTRIBUTE_UNIX_MODE,
                                               0600, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL));
    operations_assert_bytes (f, f->source_file, payload);
    g_autofree char *name = g_file_get_basename (f->source_file);
    g_autoptr (GFile) failed = g_file_get_child (f->parent, name);
    g_assert_false (g_file_query_exists (failed, NULL));
    if (partial)
    {
        g_autoptr (GFile) copied = g_file_get_child (f->parent, "readable.txt");
        operations_wait_file (f, parent, copied);
        operations_assert_bytes (f, copied, "readable member");
        operations_assert_bytes (f, good, "readable member");
    }
    operations_assert_sentinels (f);
    operations_assert_focus (f, parent);
}

/* A single native batch must preserve a demonstrably completed member
 * when the user cancels its later conflict. No atomic-batch assumption. */
static void
test_partial_result_cancel (OperationFixture *f, gconstpointer unused)
{
    const char *names[] = { "a-completed.txt", "b-conflict.txt", "c-unstarted.txt" };
    const char *bytes[] = { "completed bytes", "incoming conflict", "unstarted bytes" };
    g_autoptr (GPtrArray) sources = g_ptr_array_new_with_free_func (g_object_unref);
    g_autoptr (GPtrArray) targets = g_ptr_array_new_with_free_func (g_object_unref);
    g_autoptr (GPtrArray) files = g_ptr_array_new_with_free_func (g_object_unref);
    GList *selection = NULL;
    for (guint i = 0; i < G_N_ELEMENTS (names); i++)
    {
        GFile *file = g_file_get_child (f->source_dir, names[i]); operations_owned (f, file);
        g_assert_true (g_file_replace_contents (file, bytes[i], strlen (bytes[i]), NULL, FALSE, 0, NULL, NULL, NULL));
        g_ptr_array_add (sources, file);
        g_ptr_array_add (targets, g_file_get_child (f->parent, names[i]));
        g_ptr_array_add (files, nautilus_file_get (file));
        selection = g_list_append (selection, files->pdata[i]);
    }
    operations_owned (f, targets->pdata[1]);
    g_assert_true (g_file_replace_contents (targets->pdata[1], "retained conflict", 17, NULL, FALSE, 0, NULL, NULL, NULL));
    NautilusFilesView *source = operations_view_for_directory (f, f->source_dir);
    operations_select (f, source, sources->pdata[2]);
    nautilus_files_view_set_selection (source, selection, FALSE); g_list_free (selection);
    operations_settle (f); operations_chord (GDK_KEY_c, FALSE);
    NautilusFilesView *parent = operations_open_parent (f);
    guint history = g_list_length (nautilus_window_slot_get_back_history (f->slot));
    operations_chord (GDK_KEY_v, FALSE);
    g_autoptr (GtkWidget) dialog = operations_wait_window (f, NAUTILUS_TYPE_FILE_CONFLICT_DIALOG);
    g_assert_cmpuint (f->jobs->len, ==, 1);
    NautilusProgressInfo *job = f->jobs->pdata[0];
    g_assert_true (nautilus_progress_info_get_is_started (job));
    g_assert_false (nautilus_progress_info_get_is_finished (job));
    /* Establish completed bytes BEFORE allowing the later conflict to end. */
    operations_assert_bytes (f, targets->pdata[0], "completed bytes");
    operations_assert_bytes (f, targets->pdata[1], "retained conflict");
    g_assert_false (g_file_query_exists (targets->pdata[2], NULL));
    g_test_message ("Native batch completed first member before later conflict; cancel now");
    operations_press_button (dialog, "Cancel"); operations_settle (f);
    g_assert_true (nautilus_progress_info_get_is_finished (job));
    g_assert_true (nautilus_progress_info_get_is_cancelled (job));
    g_assert_cmpint (nautilus_file_conflict_dialog_get_response (NAUTILUS_FILE_CONFLICT_DIALOG (dialog)), ==, CONFLICT_RESPONSE_CANCEL);
    operations_wait_file (f, parent, targets->pdata[0]);
    operations_assert_bytes (f, targets->pdata[0], "completed bytes");
    operations_assert_bytes (f, targets->pdata[1], "retained conflict");
    g_assert_false (g_file_query_exists (targets->pdata[2], NULL));
    for (guint i = 0; i < G_N_ELEMENTS (names); i++)
    {
        operations_assert_bytes (f, sources->pdata[i], bytes[i]);
        g_autoptr (GFile) wrong_child = g_file_get_child (f->child, names[i]);
        g_autoptr (GFile) wrong_sibling = g_file_get_child (f->sibling, names[i]);
        g_assert_false (g_file_query_exists (wrong_child, NULL));
        g_assert_false (g_file_query_exists (wrong_sibling, NULL));
    }
    g_autoptr (NautilusFile) absent = nautilus_file_get (targets->pdata[2]);
    g_assert_null (nautilus_view_model_get_item_for_file (nautilus_files_view_get_private_model (parent), absent));
    operations_assert_bytes (f, f->source_file, payload); operations_assert_sentinels (f);
    operations_assert_focus (f, parent);
    g_assert_cmpuint (g_list_length (nautilus_window_slot_get_back_history (f->slot)), ==, history);
}

static void
test_progress_cancel (OperationFixture *f, gconstpointer unused)
{
    operations_copy_source (f, FALSE);
    g_autofree char *name = g_file_get_basename (f->source_file);
    g_autoptr (GFile) destination = g_file_get_child (f->parent, name);
    operations_owned (f, destination);
    g_assert_true (g_file_replace_contents (destination, "retained", 8, NULL, FALSE, 0, NULL, NULL, NULL));
    NautilusFilesView *parent = operations_open_parent (f);
    operations_chord (GDK_KEY_v, FALSE);
    g_autoptr (GtkWidget) conflict = operations_wait_window (f, NAUTILUS_TYPE_FILE_CONFLICT_DIALOG);
    g_assert_cmpuint (f->jobs->len, ==, 1);
    NautilusProgressInfo *job = f->jobs->pdata[0];
    g_assert_true (nautilus_progress_info_get_is_started (job));
    g_assert_false (nautilus_progress_info_get_is_finished (job));
    /* Display the existing native progress widget for this real paused job.
     * The extra test host keeps Cancel reachable while its conflict is modal;
     * this is widget/cancellation acceptance, not toolbar-popover coverage. */
    GtkWidget *host = gtk_window_new ();
    gtk_window_set_transient_for (GTK_WINDOW (host), GTK_WINDOW (conflict));
    GtkWidget *row = nautilus_progress_info_widget_new (job);
    gtk_window_set_child (GTK_WINDOW (host), row);
    gtk_window_present (GTK_WINDOW (host));
    gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    while (!gtk_window_is_active (GTK_WINDOW (host)))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    GtkWidget *button = operations_find_type (row, GTK_TYPE_BUTTON);
    g_assert_nonnull (button); g_assert_true (gtk_widget_grab_focus (button));
    operations_key (GDK_KEY_Return);
    g_assert_true (nautilus_progress_info_get_is_cancelled (job));
    /* Cancelling the progress token is not a completed worker. Dismiss the
     * outstanding native question through its normal response to release it. */
    g_assert_false (nautilus_progress_info_get_is_finished (job));
    gtk_window_destroy (GTK_WINDOW (host)); operations_pump (80);
    operations_press_button (conflict, "Cancel");
    operations_settle (f);
    g_assert_true (nautilus_progress_info_get_is_finished (job));
    operations_assert_bytes (f, destination, "retained");
    operations_assert_bytes (f, f->source_file, payload);
    operations_assert_sentinels (f); operations_assert_focus (f, parent);
}

static void
operations_shift_delete (void)
{
    g_autoptr (GError) error = NULL;
    g_assert_true (miller_test_input_key (input, GDK_KEY_Shift_L, TRUE, &error));
    operations_key (GDK_KEY_Delete);
    g_assert_true (miller_test_input_key (input, GDK_KEY_Shift_L, FALSE, &error));
    g_assert_no_error (error); operations_pump (60);
}

static void
operations_assert_private_trash_bytes (GFile *file)
{
    operations_assert_private_home ();
    g_autofree char *path = g_build_filename (g_get_user_data_dir (), "Trash", NULL);
    g_autoptr (GFile) trash = g_file_new_for_path (path);
    g_assert_true (g_file_has_prefix (file, trash));
    g_autofree char *contents = NULL;
    gsize length;
    g_assert_true (g_file_load_contents (file, NULL, &contents, &length, NULL, NULL));
    g_assert_cmpmem (contents, length, payload, strlen (payload));
}

/* Run before g_test_init: stock Nautilus deliberately bypasses Trash and
 * delete confirmations in GLib tests. This normal app child exercises the
 * actual policy, with the same owned compositor/Home and native controls. */
static int
operations_native_child_main (const char *kind)
{
    g_assert_false (g_test_initialized ());
    g_setenv ("GDK_DEBUG", "no-portals", TRUE);
    g_autoptr (GError) error = NULL;
    input = miller_test_input_new (&error); g_assert_no_error (error); operations_set_input (input);
    operations_assert_private_home ();
    operations_prepare_templates ();
    nautilus_register_resource (); nautilus_global_preferences_init (); nautilus_ensure_extension_points ();
    g_autoptr (NautilusTagManager) tags = nautilus_tag_manager_new_dummy ();
    test_init_config_dir ();
    g_autoptr (NautilusApplication) app = nautilus_application_new ();
    g_application_set_flags (G_APPLICATION (app), g_application_get_flags (G_APPLICATION (app)) | G_APPLICATION_NON_UNIQUE);
    g_assert_true (g_application_register (G_APPLICATION (app), NULL, NULL));
    GMainContext *context = g_main_context_default ();
    g_assert_true (g_main_context_acquire (context));
    OperationFixture fixture = { 0 }; OperationFixture *f = &fixture;
    operations_setup (f, GUINT_TO_POINTER (NAUTILUS_VIEW_MILLER_ID));
    NautilusFilesView *source = operations_view_for_directory (f, f->source_dir);
    operations_select (f, source, f->source_file); operations_assert_focus (f, source);
    if (g_str_equal (kind, "multi-rename-captured-selection"))
    {
        test_native_multi_rename (f, NULL);
    }
    else if (g_str_equal (kind, "permanent-delete-confirmation"))
    {
        operations_shift_delete ();
        g_autoptr (AdwDialog) cancel = operations_wait_alert (f);
        g_assert_nonnull (strstr (adw_alert_dialog_get_heading (ADW_ALERT_DIALOG (cancel)), "Permanently Delete"));
        operations_assert_bytes (f, f->source_file, payload);
        operations_press_button (GTK_WIDGET (cancel), "Cancel"); operations_settle (f);
        operations_assert_bytes (f, f->source_file, payload);
        operations_select (f, source, f->source_file);
        operations_shift_delete ();
        g_autoptr (AdwDialog) accept = operations_wait_alert (f);
        operations_assert_bytes (f, f->source_file, payload);
        operations_press_button (GTK_WIDGET (accept), "Delete"); operations_settle (f);
        g_assert_false (g_file_query_exists (f->source_file, NULL));
        operations_assert_sentinels (f);
    }
    else if (g_str_equal (kind, "trash-unavailable"))
    {
        /* Make only this proven-private Trash unwritable. Source deletion is
         * still possible, so the native failure must ask rather than erase. */
        g_autofree char *files_path = g_build_filename (g_get_user_data_dir (), "Trash", "files", NULL);
        g_autofree char *info_path = g_build_filename (g_get_user_data_dir (), "Trash", "info", NULL);
        g_assert_cmpint (g_mkdir_with_parents (files_path, 0700), ==, 0);
        g_assert_cmpint (g_mkdir_with_parents (info_path, 0700), ==, 0);
        g_autoptr (GFile) files = g_file_new_for_path (files_path);
        g_autoptr (GFile) info = g_file_new_for_path (info_path);
        g_assert_true (g_file_set_attribute_uint32 (files, G_FILE_ATTRIBUTE_UNIX_MODE, 0500,
                                                   G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL));
        g_assert_true (g_file_set_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_MODE, 0500,
                                                   G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL));
        operations_key (GDK_KEY_Delete);
        g_autoptr (AdwDialog) question = operations_wait_alert (f);
        g_assert_nonnull (strstr (adw_alert_dialog_get_heading (ADW_ALERT_DIALOG (question)), "can’t be put in the trash"));
        GtkWidget *details = adw_alert_dialog_get_extra_child (ADW_ALERT_DIALOG (question));
        g_assert_true (GTK_IS_LABEL (details));
        g_assert_nonnull (strstr (gtk_label_get_text (GTK_LABEL (details)), "Permission denied"));
        operations_assert_bytes (f, f->source_file, payload);
        operations_press_button (GTK_WIDGET (question), "Cancel"); operations_settle (f);
        g_assert_true (g_file_set_attribute_uint32 (files, G_FILE_ATTRIBUTE_UNIX_MODE, 0700,
                                                   G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL));
        g_assert_true (g_file_set_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_MODE, 0700,
                                                   G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL));
        operations_assert_bytes (f, f->source_file, payload);
        operations_assert_sentinels (f);
    }
    else
    {
        g_assert_cmpstr (kind, ==, "trash-undo-restore");
        g_autofree char *name = g_file_get_basename (f->source_file);
        g_autofree char *trash_path = g_build_filename (g_get_user_data_dir (), "Trash", "files", name, NULL);
        g_autoptr (GFile) local_trashed = g_file_new_for_path (trash_path);
        g_assert_false (g_file_query_exists (local_trashed, NULL));
        operations_key (GDK_KEY_Delete); operations_settle (f);
        /* No silent permanent fallback: original removal alone is not proof. */
        g_assert_false (g_file_query_exists (f->source_file, NULL));
        operations_assert_private_trash_bytes (local_trashed);
        g_assert_cmpint (nautilus_file_undo_manager_get_state (), ==, NAUTILUS_FILE_UNDO_MANAGER_STATE_UNDO);
        operations_chord (GDK_KEY_z, FALSE); operations_settle (f);
        operations_wait_file (f, source, f->source_file);
        operations_assert_bytes (f, f->source_file, payload);
        g_assert_false (g_file_query_exists (local_trashed, NULL));
        operations_select (f, source, f->source_file);
        operations_key (GDK_KEY_Delete); operations_settle (f);
        operations_assert_private_trash_bytes (local_trashed);
        g_autoptr (GFile) trash = g_file_new_for_uri ("trash:///");
        nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), trash, 0, NULL);
        operations_settle (f);
        NautilusFilesView *view = nautilus_window_slot_get_current_view (f->slot);
        g_autoptr (GFile) trashed = g_file_get_child (trash, name);
        g_autoptr (GFileInfo) info = g_file_query_info (trashed, G_FILE_ATTRIBUTE_TRASH_ORIG_PATH,
            G_FILE_QUERY_INFO_NONE, NULL, &error);
        g_assert_no_error (error); g_assert_nonnull (info);
        g_autofree char *original_path = g_file_get_path (f->source_file);
        g_assert_cmpstr (g_file_info_get_attribute_byte_string (info, G_FILE_ATTRIBUTE_TRASH_ORIG_PATH), ==, original_path);
        g_autoptr (NautilusFile) file = nautilus_file_get (trashed);
        gint64 deadline = g_get_monotonic_time () + 15 * G_TIME_SPAN_SECOND;
        while (nautilus_view_model_get_item_for_file (nautilus_files_view_get_private_model (view), file) == NULL)
        {
            g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
        }
        GList selection = { .data = file };
        nautilus_files_view_set_selection (view, &selection, FALSE);
        gtk_widget_grab_focus (GTK_WIDGET (view)); operations_settle (f);
        operations_key (GDK_KEY_Menu); operations_activate_menu_label (view, "Restore From Trash");
        operations_settle (f);
        operations_assert_bytes (f, f->source_file, payload);
        g_assert_false (g_file_query_exists (local_trashed, NULL));
        operations_assert_sentinels (f);
    }
    if (!g_str_equal (kind, "multi-rename-captured-selection"))
    {
        g_assert_cmpuint (f->jobs->len, >=, g_str_equal (kind, "trash-unavailable") ? 1 : 2);
    }
    for (guint i = 0; i < f->jobs->len; i++)
    {
        g_assert_true (nautilus_progress_info_get_is_finished (f->jobs->pdata[i]));
    }
    operations_teardown (f, NULL);
    g_main_context_release (context); g_clear_pointer (&input, miller_test_input_free);
    g_print ("Native %s acceptance: %s PASS\n",
             g_str_equal (kind, "multi-rename-captured-selection") ? "operation" : "deletion", kind);
    return 0;
}

typedef struct { gboolean done, success; GError *error; } OperationChildWait;

static void
operations_child_finished (GObject *source, GAsyncResult *result, gpointer data)
{
    OperationChildWait *wait = data;
    wait->success = g_subprocess_wait_check_finish (G_SUBPROCESS (source), result, &wait->error);
    wait->done = TRUE;
}

static void
test_native_operation_child (OperationFixture *f, gconstpointer unused)
{
    const char *kind = strrchr (g_test_get_path (), '/') + 1;
    g_autofree char *executable = g_file_read_link ("/proc/self/exe", NULL);
    g_assert_nonnull (executable);
    g_autoptr (GError) error = NULL;
    g_autoptr (GSubprocess) child = g_subprocess_new (G_SUBPROCESS_FLAGS_NONE, &error,
        executable, "--native-operation", kind, NULL);
    g_assert_no_error (error); g_assert_nonnull (child);
    OperationChildWait wait = { 0 };
    g_subprocess_wait_check_async (child, NULL, operations_child_finished, &wait);
    gint64 deadline = g_get_monotonic_time () + 45 * G_TIME_SPAN_SECOND;
    while (!wait.done && g_get_monotonic_time () < deadline) { operations_pump (10); }
    if (!wait.done)
    {
        g_subprocess_force_exit (child);
        while (!wait.done) { operations_pump (10); }
    }
    g_assert_no_error (wait.error); g_assert_true (wait.success);
    operations_assert_bytes (f, f->source_file, payload); operations_assert_sentinels (f);
}

/* Native archive extraction is supplementary verification of bytes created by
 * the real Compress dialog, not acceptance of a user Extract gesture. The
 * tiny fixture has one member; enumerate only this owned verification tree. */
static void
operations_verify_archive (OperationFixture *f, GFile *archive,
                           const char *member, const char *contents)
{
    operations_owned (f, archive);
    g_autoptr (GFile) output = g_file_get_child (f->root, "verify-archive");
    operations_owned (f, output); g_assert_true (g_file_make_directory (output, NULL, NULL));
    GList files = { .data = archive };
    nautilus_file_operations_extract_files (&files, output, GTK_WINDOW (f->window), NULL, NULL, NULL);
    operations_settle (f);
    g_autoptr (GPtrArray) directories = g_ptr_array_new_with_free_func (g_object_unref);
    g_ptr_array_add (directories, g_object_ref (output));
    guint members = 0, entries = 0;
    for (guint i = 0; i < directories->len; i++)
    {
        g_autoptr (GFileEnumerator) enumerator = g_file_enumerate_children (directories->pdata[i],
            G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE,
            G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
        g_assert_nonnull (enumerator);
        g_autoptr (GFileInfo) info = NULL;
        while ((info = g_file_enumerator_next_file (enumerator, NULL, NULL)) != NULL)
        {
            g_assert_cmpuint (++entries, <=, 8);
            GFile *file = g_file_get_child (directories->pdata[i], g_file_info_get_name (info));
            operations_owned (f, file);
            if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY) { g_ptr_array_add (directories, file); }
            else
            {
                g_assert_cmpint (g_file_info_get_file_type (info), ==, G_FILE_TYPE_REGULAR);
                g_assert_cmpstr (g_file_info_get_name (info), ==, member);
                operations_assert_bytes (f, file, contents); members++; g_object_unref (file);
            }
            g_clear_object (&info);
        }
    }
    g_assert_cmpuint (members, ==, 1);
}

static void
test_native_compress (OperationFixture *f, gconstpointer unused)
{
    gboolean pruned = strstr (g_test_get_path (), "source-pruned") != NULL;
    gboolean cancel = g_str_has_suffix (g_test_get_path (), "-cancel");
    g_settings_set_enum (nautilus_compression_preferences,
                         NAUTILUS_PREFERENCES_DEFAULT_COMPRESSION_FORMAT, NAUTILUS_COMPRESSION_ZIP);
    NautilusFilesView *source;
    if (pruned)
    {
        source = operations_view_for_directory (f, f->source_dir);
        operations_select (f, source, f->source_file);
    }
    else { source = operations_open_parent (f); }
    operations_key (GDK_KEY_Menu); operations_activate_menu_label (source, "Compress…");
    AdwDialog *dialog = adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (f->window));
    g_assert_true (NAUTILUS_IS_COMPRESS_DIALOG (dialog));
    GtkWidget *entry = operations_find_type (GTK_WIDGET (dialog), ADW_TYPE_ENTRY_ROW);
    g_assert_nonnull (entry);
    gtk_widget_grab_focus (entry); operations_chord (GDK_KEY_a, FALSE); operations_type ("archive");
    GWeakRef weak; g_weak_ref_init (&weak, source);
    g_autoptr (GFile) directory = g_object_ref (f->parent);
    if (pruned)
    {
        nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), f->sibling, 0, NULL);
        operations_settle (f);
        g_clear_object (&directory); directory = g_file_get_child (f->root, "moved-compress-source");
        operations_owned (f, directory); operations_owned (f, f->source_dir);
        g_assert_true (g_file_move (f->source_dir, directory, G_FILE_COPY_NONE, NULL, NULL, NULL, NULL));
        operations_pump (300);
        g_autoptr (GtkWidget) retained = g_weak_ref_get (&weak);
        g_test_message ("Compress source after actual branch prune retained=%d", retained != NULL);
    }
    operations_press_button (GTK_WIDGET (dialog), cancel ? "Cancel" : "Compress");
    operations_settle (f);
    g_autoptr (GFile) archive = g_file_get_child (directory, "archive.zip");
    if (cancel)
    {
        g_assert_false (g_file_query_exists (archive, NULL)); g_assert_cmpuint (f->jobs->len, ==, 0);
    }
    else
    {
        g_assert_true (g_file_query_exists (archive, NULL));
        g_autofree char *member = pruned ? g_file_get_basename (f->source_file) : g_strdup ("child-sentinel.txt");
        operations_verify_archive (f, archive, member, pruned ? payload : "child unchanged");
    }
    operations_assert_sentinels (f);
    g_autoptr (GFile) wrong = g_file_get_child (pruned ? f->sibling : f->child, "archive.zip");
    g_assert_false (g_file_query_exists (wrong, NULL));
    if (pruned)
    {
        gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
        for (;;)
        {
            g_autoptr (GtkWidget) retained = g_weak_ref_get (&weak);
            if (retained == NULL) { break; }
            g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
        }
        operations_assert_focus (f, operations_view_for_directory (f, f->sibling));
    }
    else { operations_assert_focus (f, source); }
    g_weak_ref_clear (&weak);
}

static void
test_native_extract (OperationFixture *f, gconstpointer unused)
{
    gboolean pruned = strstr (g_test_get_path (), "source-pruned") != NULL;
    gboolean cancel = g_str_has_suffix (g_test_get_path (), "-cancel");
    g_autoptr (GFile) archive = g_file_get_child (f->source_dir, "input.zip");
    operations_owned (f, archive);
    /* Create only the archive fixture through the existing native worker.
     * Actual extraction below enters through its menu and real chooser. */
    GList files = { .data = f->source_file };
    nautilus_file_operations_compress (&files, archive, AUTOAR_FORMAT_ZIP, AUTOAR_FILTER_NONE,
                                       NULL, GTK_WINDOW (f->window), NULL, NULL, NULL);
    operations_settle (f); g_assert_true (g_file_query_exists (archive, NULL));
    NautilusFilesView *source = operations_view_for_directory (f, f->source_dir);
    operations_select (f, source, archive);
    operations_key (GDK_KEY_Menu);
    if (g_str_has_suffix (g_test_get_path (), "extract-here"))
    {
        operations_activate_menu_label (source, "Extract"); operations_settle (f);
        g_autoptr (GFile) output = g_file_get_child (f->source_dir, "input");
        g_autofree char *name = g_file_get_basename (f->source_file);
        g_autoptr (GFile) member = g_file_get_child (output, name);
        operations_wait_file (f, source, output); operations_assert_bytes (f, member, payload);
        operations_assert_bytes (f, f->source_file, payload); operations_assert_sentinels (f);
        g_autoptr (GFile) wrong = g_file_get_child (f->parent, "input");
        g_assert_false (g_file_query_exists (wrong, NULL));
        g_assert_cmpuint (f->jobs->len, ==, 2); operations_assert_focus (f, source);
        return;
    }
    operations_activate_menu_label (source, "Extract to…");
    g_autoptr (GtkWidget) chooser = operations_wait_window (f, GTK_TYPE_FILE_CHOOSER_DIALOG);
    GWeakRef weak; g_weak_ref_init (&weak, source);
    g_autoptr (GFile) moved = NULL;
    if (pruned)
    {
        nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), f->sibling, 0, NULL);
        operations_settle (f);
        moved = g_file_get_child (f->root, "moved-extract-source");
        operations_owned (f, moved); operations_owned (f, f->source_dir);
        g_assert_true (g_file_move (f->source_dir, moved, G_FILE_COPY_NONE, NULL, NULL, NULL, NULL));
        operations_pump (300);
        g_autoptr (GtkWidget) retained = g_weak_ref_get (&weak);
        g_test_message ("Extract To source after real branch prune retained=%d", retained != NULL);
    }
    else
    {
        /* Keep the source tab alive while a different parent/action owner
         * discloses its child. Only the chosen parent may receive contents. */
        operations_new_tab_at (f, f->parent);
        operations_select (f, operations_view_for_directory (f, f->parent), f->child);
    }
    if (cancel) { operations_key (GDK_KEY_Escape); }
    else { operations_accept_folder (chooser, f->parent); }
    operations_settle (f);
    g_autofree char *name = g_file_get_basename (f->source_file);
    g_autoptr (GFile) extracted_root = g_file_get_child (f->parent, "input");
    g_autoptr (GFile) extracted = g_file_get_child (extracted_root, name);
    if (cancel)
    {
        g_assert_false (g_file_query_exists (extracted_root, NULL)); g_assert_cmpuint (f->jobs->len, ==, 1);
    }
    else
    {
        operations_assert_bytes (f, extracted, payload);
        g_assert_cmpuint (f->jobs->len, ==, 2);
        if (!pruned) { operations_wait_file (f, operations_view_for_directory (f, f->parent), extracted_root); }
    }
    g_autoptr (GFile) wrong_child = g_file_get_child (f->child, "input");
    g_autoptr (GFile) wrong_sibling = g_file_get_child (f->sibling, "input");
    g_assert_false (g_file_query_exists (wrong_child, NULL));
    g_assert_false (g_file_query_exists (wrong_sibling, NULL));
    g_autoptr (GFile) original = pruned ? g_file_get_child (moved, name) : g_object_ref (f->source_file);
    operations_assert_bytes (f, original, payload); operations_assert_sentinels (f);
    if (pruned)
    {
        gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
        for (;;)
        {
            g_autoptr (GtkWidget) retained = g_weak_ref_get (&weak);
            if (retained == NULL) { break; }
            g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
        }
    }
    operations_assert_focus (f, operations_view_for_directory (f, pruned ? f->sibling : f->parent));
    g_weak_ref_clear (&weak);
}

static void
test_native_link (OperationFixture *f, gconstpointer unused)
{
    gboolean in_place = g_str_has_suffix (g_test_get_path (), "in-place-supported");
    g_settings_set_boolean (nautilus_preferences, NAUTILUS_PREFERENCES_SHOW_CREATE_LINK, TRUE);
    operations_copy_source (f, FALSE);
    NautilusFilesView *view = in_place ? operations_view_for_directory (f, f->source_dir) : operations_open_parent (f);
    operations_chord (GDK_KEY_m, in_place);
    operations_settle (f);
    GFile *directory = in_place ? f->source_dir : f->parent;
    g_autoptr (GFileEnumerator) enumerator = g_file_enumerate_children (directory,
        G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK "," G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET,
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
    g_assert_nonnull (enumerator);
    guint count = 0;
    g_autoptr (GFileInfo) info = NULL;
    while ((info = g_file_enumerator_next_file (enumerator, NULL, NULL)) != NULL)
    {
        if (g_file_info_get_is_symlink (info))
        {
            g_autoptr (GFile) link = g_file_get_child (directory, g_file_info_get_name (info));
            operations_owned (f, link);
            const char *target = g_file_info_get_symlink_target (info);
            g_assert_nonnull (target);
            g_autoptr (GFile) resolved = g_path_is_absolute (target) ? g_file_new_for_path (target) :
                                       g_file_resolve_relative_path (directory, target);
            g_assert_true (g_file_equal (resolved, f->source_file));
            operations_assert_bytes (f, link, payload); operations_wait_file (f, view, link);
            g_autoptr (GFile) wrong = g_file_get_child (f->child, g_file_info_get_name (info));
            g_assert_false (g_file_query_exists (wrong, NULL));
            g_autoptr (GFile) sibling = g_file_get_child (f->sibling, g_file_info_get_name (info));
            g_assert_false (g_file_query_exists (sibling, NULL)); count++;
        }
        g_clear_object (&info);
    }
    g_assert_cmpuint (count, ==, 1); g_assert_cmpuint (f->jobs->len, ==, 1);
    operations_assert_bytes (f, f->source_file, payload);
    operations_assert_sentinels (f); operations_assert_focus (f, view);
}

static GtkWidget *
operations_focused_entry (OperationFixture *f)
{
    GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (f->window));
    g_assert_nonnull (focus);
    GtkWidget *entry = GTK_IS_ENTRY (focus) ? focus : gtk_widget_get_ancestor (focus, GTK_TYPE_ENTRY);
    g_assert_nonnull (entry); return entry;
}

static void
test_native_multi_rename (OperationFixture *f, gconstpointer unused)
{
    g_autoptr (GFile) first = g_file_get_child (f->source_dir, "old-one.txt");
    g_autoptr (GFile) second = g_file_get_child (f->source_dir, "old-two.txt");
    operations_owned (f, first); operations_owned (f, second);
    g_assert_true (g_file_replace_contents (first, "first member", 12, NULL, FALSE, 0, NULL, NULL, NULL));
    g_assert_true (g_file_replace_contents (second, "second member", 13, NULL, FALSE, 0, NULL, NULL, NULL));
    NautilusFilesView *source = operations_view_for_directory (f, f->source_dir);
    operations_select (f, source, first); operations_select (f, source, second);
    g_autoptr (NautilusFile) one = nautilus_file_get (first);
    g_autoptr (NautilusFile) two = nautilus_file_get (second);
    GList tail = { .data = two }; GList head = { .data = one, .next = &tail }; tail.prev = &head;
    nautilus_files_view_set_selection (source, &head, FALSE); operations_settle (f);
    operations_key (GDK_KEY_F2);
    AdwDialog *dialog = adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (f->window));
    g_assert_true (NAUTILUS_IS_BATCH_RENAME_DIALOG (dialog));
    GtkWidget *label = operations_find_label (GTK_WIDGET (dialog), "Find and replace text");
    g_assert_nonnull (label);
    GtkWidget *check = gtk_widget_get_ancestor (label, GTK_TYPE_CHECK_BUTTON);
    g_assert_nonnull (check); gtk_widget_grab_focus (check); operations_key (GDK_KEY_space);
    g_assert_true (gtk_check_button_get_active (GTK_CHECK_BUTTON (check)));
    GtkWidget *find = operations_focused_entry (f);
    operations_type ("old"); g_assert_cmpstr (gtk_editable_get_text (GTK_EDITABLE (find)), ==, "old");
    operations_key (GDK_KEY_Tab);
    GtkWidget *replace = operations_focused_entry (f); g_assert_true (replace != find);
    operations_type ("new"); g_assert_cmpstr (gtk_editable_get_text (GTK_EDITABLE (replace)), ==, "new");
    /* A native API tab change cannot retarget the batch captured by F2. */
    operations_new_tab_at (f, f->sibling);
    operations_press_button (GTK_WIDGET (dialog), "Rename");
    g_autoptr (GFile) renamed_one = g_file_get_child (f->source_dir, "new-one.txt");
    g_autoptr (GFile) renamed_two = g_file_get_child (f->source_dir, "new-two.txt");
    operations_wait_file (f, source, renamed_one); operations_wait_file (f, source, renamed_two);
    operations_settle (f);
    operations_assert_bytes (f, renamed_one, "first member");
    operations_assert_bytes (f, renamed_two, "second member");
    g_assert_false (g_file_query_exists (first, NULL)); g_assert_false (g_file_query_exists (second, NULL));
    g_autoptr (GFile) wrong_one = g_file_get_child (f->sibling, "new-one.txt");
    g_autoptr (GFile) wrong_two = g_file_get_child (f->sibling, "new-two.txt");
    g_assert_false (g_file_query_exists (wrong_one, NULL)); g_assert_false (g_file_query_exists (wrong_two, NULL));
    operations_assert_bytes (f, f->source_file, payload); operations_assert_sentinels (f);
    operations_assert_focus (f, operations_view_for_directory (f, f->sibling));
}

#define OWNED_RECEIVER_ID "org.example.MillerOwnedReceiver"

typedef struct { GFile *root; guint calls; gboolean directory; } OperationReceiver;

static void
operations_receiver_open (GApplication *app, GFile **files, gint count,
                           const char *hint, OperationReceiver *receiver)
{
    g_assert_cmpint (count, >, 0); g_assert_cmpint (count, <=, 8);
    g_autoptr (GKeyFile) record = g_key_file_new ();
    g_auto (GStrv) uris = g_new0 (char *, count + 1);
    for (gint i = 0; i < count; i++)
    {
        g_assert_true (g_file_has_prefix (files[i], receiver->root));
        g_autoptr (GFileInfo) info = g_file_query_info (files[i],
            G_FILE_ATTRIBUTE_STANDARD_TYPE "," G_FILE_ATTRIBUTE_UNIX_UID,
            G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
        g_assert_nonnull (info);
        g_assert_cmpint (g_file_info_get_file_type (info), ==, receiver->directory ? G_FILE_TYPE_DIRECTORY : G_FILE_TYPE_REGULAR);
        g_assert_cmpuint (g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_UID), ==, geteuid ());
        uris[i] = g_file_get_uri (files[i]);
    }
    g_key_file_set_string_list (record, "Open", "uris", (const char * const *) uris, count);
    g_autofree char *name = g_strdup_printf ("open-receipt-%u.ini", ++receiver->calls);
    g_autoptr (GFile) target = g_file_get_child (receiver->root, name);
    gsize length; g_autofree char *text = g_key_file_to_data (record, &length, NULL);
    g_assert_true (g_file_replace_contents (target, text, length, NULL, FALSE,
                                           G_FILE_CREATE_PRIVATE, NULL, NULL, NULL));
}

static int
operations_receiver_main (const char *root_path, gboolean console)
{
    g_autoptr (GError) error = NULL;
    g_autoptr (MillerTestInput) proof = miller_test_input_new (&error);
    g_assert_no_error (error); g_assert_nonnull (proof); operations_assert_private_home ();
    struct stat info; g_assert_cmpint (lstat (root_path, &info), ==, 0);
    g_assert_true (S_ISDIR (info.st_mode)); g_assert_cmpuint (info.st_uid, ==, geteuid ());
    g_assert_cmpuint (info.st_mode & 0777, ==, 0700);
    g_autoptr (GFile) root = g_file_new_for_path (root_path);
    g_autoptr (GFile) temporary = g_file_new_for_path (g_get_tmp_dir ());
    g_assert_true (g_file_has_prefix (root, temporary));
    OperationReceiver receiver = { .root = root, .directory = console };
    g_autoptr (GApplication) app = g_application_new (console ? "org.gnome.Console" : OWNED_RECEIVER_ID,
        G_APPLICATION_HANDLES_OPEN | G_APPLICATION_IS_SERVICE);
    g_signal_connect (app, "open", G_CALLBACK (operations_receiver_open), &receiver);
    g_assert_true (g_application_register (app, NULL, &error)); g_assert_no_error (error);
    g_autoptr (GMainLoop) loop = g_main_loop_new (NULL, FALSE);
    g_unix_fd_add (STDIN_FILENO, G_IO_IN | G_IO_HUP, clipboard_child_quit, loop);
    g_print ("ready\n"); g_main_loop_run (loop);
    return 0;
}

static void
operations_start_receiver (OperationFixture *f, gboolean console)
{
    operations_assert_private_home ();
    if (!console)
    {
        g_autofree char *applications = g_build_filename (g_get_user_data_dir (), "applications", NULL);
        g_assert_cmpint (g_mkdir_with_parents (applications, 0700), ==, 0);
        g_autofree char *desktop = g_build_filename (applications, OWNED_RECEIVER_ID ".desktop", NULL);
        const char entry[] = "[Desktop Entry]\nType=Application\nName=Owned file receiver\n"
            "Exec=/usr/bin/false %U\nDBusActivatable=true\nTerminal=false\n"
            "MimeType=text/plain;\nNoDisplay=false\n";
        g_assert_true (g_file_set_contents (desktop, entry, -1, NULL));
        g_autoptr (GDesktopAppInfo) app = g_desktop_app_info_new_from_filename (desktop);
        g_assert_nonnull (app); g_assert_true (g_app_info_set_as_default_for_type (G_APP_INFO (app), "text/plain", NULL));
        g_autoptr (GAppInfo) selected = g_app_info_get_default_for_type ("text/plain", FALSE);
        g_assert_nonnull (selected); g_assert_true (g_app_info_equal (selected, G_APP_INFO (app)));
    }
    g_autofree char *executable = g_file_read_link ("/proc/self/exe", NULL);
    g_autofree char *path = g_file_get_path (f->root);
    f->receiver = g_subprocess_new (G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE,
        NULL, executable, console ? "--console-receiver" : "--open-receiver", path, NULL);
    g_assert_nonnull (f->receiver);
    f->receiver_output = g_data_input_stream_new (g_subprocess_get_stdout_pipe (f->receiver));
    operations_child_marker (f->receiver_output, "ready");
}

static void
operations_assert_open_receipt (OperationFixture *f, GFile *expected_file)
{
    g_autoptr (GFile) receipt = g_file_get_child (f->root, "open-receipt-1.ini");
    operations_owned (f, receipt);
    gint64 deadline = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
    while (!g_file_query_exists (receipt, NULL))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    g_autofree char *contents = NULL;
    g_assert_true (g_file_load_contents (receipt, NULL, &contents, NULL, NULL, NULL));
    g_autoptr (GKeyFile) record = g_key_file_new ();
    g_assert_true (g_key_file_load_from_data (record, contents, -1, 0, NULL));
    gsize count; g_auto (GStrv) uris = g_key_file_get_string_list (record, "Open", "uris", &count, NULL);
    g_assert_cmpuint (count, ==, 1);
    g_autofree char *expected = g_file_get_uri (expected_file);
    g_assert_cmpstr (uris[0], ==, expected);
    /* Drain queued native activation work before checking duplicate launch. */
    operations_pump (500);
    g_autoptr (GFile) duplicate = g_file_get_child (f->root, "open-receipt-2.ini");
    g_assert_false (g_file_query_exists (duplicate, NULL));
}

static void
test_native_open (OperationFixture *f, gconstpointer unused)
{
    gboolean chooser = g_str_has_suffix (g_test_get_path (), "open-with-captured-selection");
    g_autoptr (GFile) hostile = g_file_get_child (f->source_dir, "quote' $(never-run); 雪.txt");
    operations_owned (f, hostile);
    g_assert_true (g_file_move (f->source_file, hostile, G_FILE_COPY_NONE, NULL, NULL, NULL, NULL));
    g_set_object (&f->source_file, hostile);
    operations_start_receiver (f, FALSE);
    NautilusFilesView *source = operations_view_for_directory (f, f->source_dir);
    operations_select (f, source, f->source_file); operations_assert_focus (f, source);
    if (chooser)
    {
        operations_key (GDK_KEY_Menu); operations_activate_menu_label (source, "Open With…");
        AdwDialog *dialog = adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (f->window));
        g_assert_true (NAUTILUS_IS_APP_CHOOSER (dialog));
        GtkWidget *label; gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
        while ((label = operations_find_label (GTK_WIDGET (dialog), "Owned file receiver")) == NULL)
        {
            g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
        }
        operations_click (f, label, 272);
        g_autoptr (GAppInfo) selected = nautilus_app_chooser_get_app_info (NAUTILUS_APP_CHOOSER (dialog));
        g_assert_nonnull (selected); g_assert_cmpstr (g_app_info_get_id (selected), ==, OWNED_RECEIVER_ID ".desktop");
        operations_new_tab_at (f, f->sibling);
        operations_press_button (GTK_WIDGET (dialog), "Open");
    }
    else { operations_key (GDK_KEY_Return); }
    operations_assert_open_receipt (f, f->source_file); operations_settle (f);
    operations_assert_bytes (f, f->source_file, payload); operations_assert_sentinels (f);
    operations_assert_focus (f, chooser ? operations_view_for_directory (f, f->sibling) : source);
}

static void
operations_reload_bus_services (void)
{
    g_autoptr (GDBusConnection) bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
    g_assert_nonnull (bus);
    g_autoptr (GError) error = NULL;
    g_autoptr (GVariant) reply = g_dbus_connection_call_sync (bus, "org.freedesktop.DBus",
        "/org/freedesktop/DBus", "org.freedesktop.DBus", "ReloadConfig", NULL, NULL,
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &error);
    g_assert_no_error (error); g_assert_nonnull (reply); operations_pump (100);
}

static void
test_native_terminal (OperationFixture *f, gconstpointer unused)
{
    operations_assert_private_home ();
    NautilusDBusLauncher *launcher = nautilus_dbus_launcher_get ();
    g_assert_nonnull (launcher);
    /* This host has no Console provider. Assert native unavailability before
     * exposing a controlled provider on the proved-private session bus. */
    g_assert_false (nautilus_dbus_launcher_is_available (launcher, NAUTILUS_DBUS_LAUNCHER_CONSOLE));
    g_autofree char *services = g_build_filename (g_get_user_data_dir (), "dbus-1", "services", NULL);
    g_assert_cmpint (g_mkdir_with_parents (services, 0700), ==, 0);
    g_autofree char *service_path = g_build_filename (services, "org.gnome.Console.service", NULL);
    g_autoptr (GFile) service = g_file_new_for_path (service_path);
    const char record[] = "[D-BUS Service]\nName=org.gnome.Console\nExec=/usr/bin/false\n";
    g_assert_true (g_file_replace_contents (service, record, strlen (record), NULL, FALSE,
                                           G_FILE_CREATE_PRIVATE, NULL, NULL, NULL));
    operations_start_receiver (f, TRUE); operations_reload_bus_services ();
    gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    while (!nautilus_dbus_launcher_is_available (launcher, NAUTILUS_DBUS_LAUNCHER_CONSOLE))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    NautilusFilesView *parent = operations_open_parent (f);
    GActionGroup *actions = nautilus_files_view_get_private_action_group (parent);
    g_assert_true (g_action_group_get_action_enabled (actions, "current-directory-console"));
    guint history = g_list_length (nautilus_window_slot_get_back_history (f->slot));
    operations_chord (GDK_KEY_period, FALSE);
    operations_assert_open_receipt (f, f->parent);
    g_assert_cmpuint (g_list_length (nautilus_window_slot_get_back_history (f->slot)), ==, history);
    operations_assert_bytes (f, f->source_file, payload); operations_assert_sentinels (f);
    operations_assert_focus (f, parent);
    operations_stop_receiver (f); g_assert_true (g_file_delete (service, NULL, NULL));
    operations_reload_bus_services ();
    deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    while (nautilus_dbus_launcher_is_available (launcher, NAUTILUS_DBUS_LAUNCHER_CONSOLE))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    nautilus_files_view_set_selection (parent, NULL, FALSE);
    operations_select (f, parent, f->child);
    g_assert_false (g_action_group_get_action_enabled (actions, "current-directory-console"));
}

static AdwDialog *
operations_wait_dialog_label (OperationFixture *f, const char *text)
{
    gint64 deadline = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
    for (;;)
    {
        AdwDialog *dialog = adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (f->window));
        if (dialog != NULL && operations_find_label (GTK_WIDGET (dialog), text) != NULL) { return g_object_ref (dialog); }
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
}

static void
operations_alt_return (void)
{
    g_autoptr (GError) error = NULL;
    g_assert_true (miller_test_input_key (input, GDK_KEY_Alt_L, TRUE, &error));
    operations_key (GDK_KEY_Return);
    g_assert_true (miller_test_input_key (input, GDK_KEY_Alt_L, FALSE, &error));
    g_assert_no_error (error); operations_pump (60);
}

static void
test_native_properties (OperationFixture *f, gconstpointer unused)
{
    NautilusFilesView *source = operations_view_for_directory (f, f->source_dir);
    operations_select (f, source, f->source_file); operations_alt_return ();
    g_autofree char *name = g_file_get_basename (f->source_file);
    g_autoptr (AdwDialog) selected = operations_wait_dialog_label (f, name);
    operations_new_tab_at (f, f->sibling);
    g_assert_nonnull (operations_find_label (GTK_WIDGET (selected), name));
    operations_key (GDK_KEY_Escape); operations_settle (f);
    g_assert_null (adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (f->window)));
    operations_assert_focus (f, operations_view_for_directory (f, f->sibling));
    NautilusFilesView *parent = operations_open_parent (f);
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->child));
    /* Genuine background click enters native directory Properties while the
     * parent had its child disclosed; no replacement properties surface. */
    operations_click (f, GTK_WIDGET (parent), 273);
    operations_activate_menu_label (parent, "Properties");
    g_autoptr (AdwDialog) directory = operations_wait_dialog_label (f, "destination");
    operations_new_tab_at (f, f->sibling);
    g_assert_nonnull (operations_find_label (GTK_WIDGET (directory), "destination"));
    operations_key (GDK_KEY_Escape); operations_settle (f);
    g_assert_null (adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (f->window)));
    operations_assert_bytes (f, f->source_file, payload); operations_assert_sentinels (f);
    operations_assert_focus (f, operations_view_for_directory (f, f->sibling));
}

static guint32
operations_mode (OperationFixture *f, GFile *file)
{
    operations_owned (f, file);
    g_autoptr (GFileInfo) info = g_file_query_info (file, G_FILE_ATTRIBUTE_UNIX_MODE,
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
    g_assert_nonnull (info); return g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_MODE);
}

static void
operations_permission_choice (OperationFixture *f, AdwComboRow *row, gboolean writable)
{
    g_assert_true (gtk_widget_grab_focus (GTK_WIDGET (row)));
    operations_key (GDK_KEY_Return); operations_key (GDK_KEY_Home);
    if (writable) { operations_key (GDK_KEY_Down); }
    operations_key (GDK_KEY_Return);
    guint32 expected = writable ? S_IWUSR : 0;
    gint64 deadline = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
    while ((operations_mode (f, f->source_file) & S_IWUSR) != expected)
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    g_assert_cmpuint (adw_combo_row_get_selected (row), ==, writable ? 1 : 0);
}

static void
test_native_permissions (OperationFixture *f, gconstpointer unused)
{
    guint32 original = operations_mode (f, f->source_file);
    g_assert_true ((original & S_IWUSR) != 0);
    NautilusFilesView *source = operations_view_for_directory (f, f->source_dir);
    operations_select (f, source, f->source_file); operations_alt_return ();
    g_autofree char *name = g_file_get_basename (f->source_file);
    g_autoptr (AdwDialog) dialog = operations_wait_dialog_label (f, name);
    GtkWidget *label = operations_find_label (GTK_WIDGET (dialog), "Permissions");
    g_assert_nonnull (label);
    GtkWidget *navigation = gtk_widget_get_ancestor (label, ADW_TYPE_ACTION_ROW);
    g_assert_nonnull (navigation); gtk_widget_grab_focus (navigation); operations_key (GDK_KEY_Return);
    g_autoptr (AdwDialog) loaded = operations_wait_dialog_label (f, "Owner");
    GtkWidget *owner = operations_find_label (GTK_WIDGET (dialog), "Owner");
    GtkWidget *owner_row = gtk_widget_get_ancestor (owner, ADW_TYPE_COMBO_ROW);
    g_assert_nonnull (owner_row);
    GtkWidget *access = gtk_widget_get_next_sibling (owner_row);
    while (access != NULL && !gtk_widget_get_visible (access)) { access = gtk_widget_get_next_sibling (access); }
    g_assert_true (ADW_IS_COMBO_ROW (access));
    g_assert_cmpstr (adw_preferences_row_get_title (ADW_PREFERENCES_ROW (access)), ==, "Access");
    operations_new_tab_at (f, f->sibling);
    operations_permission_choice (f, ADW_COMBO_ROW (access), FALSE);
    g_assert_cmpuint (operations_mode (f, f->source_file), ==, original & ~S_IWUSR);
    operations_permission_choice (f, ADW_COMBO_ROW (access), TRUE);
    g_assert_cmpuint (operations_mode (f, f->source_file), ==, original);
    operations_key (GDK_KEY_Escape);
    if (adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (f->window)) != NULL)
    {
        operations_key (GDK_KEY_Escape);
    }
    operations_settle (f);
    g_assert_null (adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (f->window)));
    operations_assert_bytes (f, f->source_file, payload); operations_assert_sentinels (f);
    operations_assert_focus (f, operations_view_for_directory (f, f->sibling));
}

static void
operations_prepare_templates (void)
{
    operations_assert_private_home ();
    g_autoptr (GFile) runtime = g_file_new_for_path (g_get_user_runtime_dir ());
    g_autoptr (GFile) private = g_file_get_parent (runtime);
    g_autoptr (GFile) expected_config = g_file_get_child (private, "config");
    g_autoptr (GFile) config = g_file_new_for_path (g_get_user_config_dir ());
    g_assert_true (g_file_equal (config, expected_config));
    g_autofree char *dirs = g_build_filename (g_get_user_config_dir (), "user-dirs.dirs", NULL);
    const char setting[] = "XDG_TEMPLATES_DIR=\"$HOME/Templates\"\n";
    g_assert_true (g_file_set_contents (dirs, setting, -1, NULL));
    g_reload_user_special_dirs_cache ();
    g_autofree char *templates = g_build_filename (g_get_home_dir (), "Templates", NULL);
    g_assert_cmpstr (g_get_user_special_dir (G_USER_DIRECTORY_TEMPLATES), ==, templates);
    g_assert_cmpint (g_mkdir_with_parents (templates, 0700), ==, 0);
    g_autofree char *path = g_build_filename (templates, "owned-template.txt", NULL);
    g_assert_true (g_file_set_contents (path, "Owned template payload\n", -1, NULL));
}

static void
test_native_template (OperationFixture *f, gconstpointer unused)
{
    NautilusFilesView *parent = operations_open_parent (f);
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->child));
    operations_click (f, GTK_WIDGET (parent), 273);
    guint history = g_list_length (nautilus_window_slot_get_back_history (f->slot));
    operations_activate_menu_label (parent, "New Document");
    operations_activate_menu_label (parent, "owned-template.txt");
    g_autoptr (GFile) created = g_file_get_child (f->parent, "owned-template.txt");
    operations_wait_file (f, parent, created); operations_settle (f);
    operations_assert_bytes (f, created, "Owned template payload\n");
    g_assert_cmpuint (f->jobs->len, ==, 1);
    g_autoptr (GFile) wrong_child = g_file_get_child (f->child, "owned-template.txt");
    g_autoptr (GFile) wrong_sibling = g_file_get_child (f->sibling, "owned-template.txt");
    g_assert_false (g_file_query_exists (wrong_child, NULL)); g_assert_false (g_file_query_exists (wrong_sibling, NULL));
    g_assert_cmpuint (g_list_length (nautilus_window_slot_get_back_history (f->slot)), ==, history);
    operations_assert_sentinels (f); operations_assert_focus (f, parent);
}

int
main (int argc, char **argv)
{
    if (argc == 4 && g_str_equal (argv[1], "--clipboard-source"))
    {
        return clipboard_child_main (argv[2], argv[3]);
    }
    if (argc == 3 && g_str_equal (argv[1], "--native-operation"))
    {
        return operations_native_child_main (argv[2]);
    }
    if (argc == 3 && g_str_equal (argv[1], "--open-receiver"))
    {
        return operations_receiver_main (argv[2], FALSE);
    }
    if (argc == 3 && g_str_equal (argv[1], "--console-receiver"))
    {
        return operations_receiver_main (argv[2], TRUE);
    }
    g_test_init (&argc, &argv, NULL);
    if (g_getenv ("MILLER_TEST_SESSION") == NULL || g_getenv ("MILLER_TEST_HOME") == NULL)
    {
        g_print ("Requires the owned private compositor and mounted disposable Home\n"); return 77;
    }
    /* Exercise GtkFileDialog's existing in-process GTK backend. Portal
     * implementation acceptance remains in its separate native test. */
    g_setenv ("GDK_DEBUG", "no-portals", TRUE);
    g_autoptr (GError) error = NULL; input = miller_test_input_new (&error); g_assert_no_error (error); operations_set_input (input);
    operations_prepare_templates ();
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
    g_test_add ("/miller/operations/clipboard-after-tab-destruction", OperationFixture,
        GUINT_TO_POINTER (NAUTILUS_VIEW_MILLER_ID | 0x200), operations_setup,
        test_external_clipboard, operations_teardown);
    COLUMN_CASE ("new-folder-parent", test_new_folder);
    COLUMN_CASE ("new-folder-empty-child", test_new_folder);
    COLUMN_CASE ("toolbar-with-entry-focus", test_new_folder);
    COLUMN_CASE ("rename-captured-selection", test_rename_captured);
    COLUMN_CASE ("copy-to-captured-selection", test_copy_move_to);
    COLUMN_CASE ("move-to-captured-selection", test_copy_move_to);
    COLUMN_CASE ("inactive-column-menu", test_inactive_column_menu);
    COLUMN_CASE ("dialog-source-pruned", test_dialog_source_pruned);
    COLUMN_CASE ("dialog-source-pruned-cancel", test_dialog_source_pruned);
    COLUMN_CASE ("monitor-dialog-source-pruned", test_monitor_dialog_source_pruned);
    COLUMN_CASE ("monitor-dialog-source-pruned-cancel", test_monitor_dialog_source_pruned);
    COLUMN_CASE ("operation-tab-switch", test_operation_tab_switch);
    COLUMN_CASE ("new-folder-source-pruned", test_new_folder_source_pruned);
    COLUMN_CASE ("new-folder-source-pruned-cancel", test_new_folder_source_pruned);
    COLUMN_CASE ("new-folder-after-tab-switch", test_new_folder_after_tab_switch);
    COLUMN_CASE ("dialog-close-preserves-entry", test_dialog_close_preserves_entry);
    COLUMN_CASE ("pattern-source-pruned", test_pattern_source_pruned);
    COLUMN_CASE ("pattern-source-pruned-cancel", test_pattern_source_pruned);
    COLUMN_CASE ("new-folder-captured-selection", test_new_folder_captured_selection);
    COLUMN_CASE ("new-folder-captured-selection-pruned", test_new_folder_captured_selection);
    COLUMN_CASE ("new-folder-captured-selection-pruned-cancel", test_new_folder_captured_selection);
    g_test_add ("/miller/operations/operation-tab-switch-list", OperationFixture,
        GUINT_TO_POINTER (NAUTILUS_VIEW_LIST_ID), operations_setup, test_operation_tab_switch, operations_teardown);
    g_test_add ("/miller/operations/operation-tab-switch-grid", OperationFixture,
        GUINT_TO_POINTER (NAUTILUS_VIEW_GRID_ID), operations_setup, test_operation_tab_switch, operations_teardown);
    COLUMN_CASE ("operation-tab-switch-entry", test_operation_tab_switch);
    COLUMN_CASE ("conflict-replace", test_native_conflict);
    COLUMN_CASE ("conflict-keep-both", test_native_conflict);
    COLUMN_CASE ("conflict-skip", test_native_conflict);
    COLUMN_CASE ("conflict-cancel", test_native_conflict);
    COLUMN_CASE ("directory-merge", test_directory_merge);
    g_test_add ("/miller/operations/conflict-replace-list", OperationFixture,
        GUINT_TO_POINTER (NAUTILUS_VIEW_LIST_ID), operations_setup, test_native_conflict, operations_teardown);
    g_test_add ("/miller/operations/conflict-replace-grid", OperationFixture,
        GUINT_TO_POINTER (NAUTILUS_VIEW_GRID_ID), operations_setup, test_native_conflict, operations_teardown);
    COLUMN_CASE ("permission-denied", test_native_permission_error);
    COLUMN_CASE ("partial-failure", test_native_permission_error);
    COLUMN_CASE ("progress-cancel", test_progress_cancel);
    COLUMN_CASE ("partial-result-cancel", test_partial_result_cancel);
    COLUMN_CASE ("permanent-delete-confirmation", test_native_operation_child);
    COLUMN_CASE ("trash-undo-restore", test_native_operation_child);
    COLUMN_CASE ("trash-unavailable", test_native_operation_child);
    COLUMN_CASE ("compress-parent-target", test_native_compress);
    COLUMN_CASE ("compress-source-pruned", test_native_compress);
    COLUMN_CASE ("compress-source-pruned-cancel", test_native_compress);
    COLUMN_CASE ("extract-parent-target", test_native_extract);
    COLUMN_CASE ("extract-source-pruned", test_native_extract);
    COLUMN_CASE ("extract-source-pruned-cancel", test_native_extract);
    COLUMN_CASE ("link-supported", test_native_link);
    COLUMN_CASE ("link-in-place-supported", test_native_link);
    COLUMN_CASE ("multi-rename-captured-selection", test_native_operation_child);
    COLUMN_CASE ("open-once", test_native_open);
    COLUMN_CASE ("open-with-captured-selection", test_native_open);
    COLUMN_CASE ("terminal-directory", test_native_terminal);
    COLUMN_CASE ("properties-selection-and-directory", test_native_properties);
    COLUMN_CASE ("permissions-native", test_native_permissions);
    COLUMN_CASE ("template-parent-target", test_native_template);
    COLUMN_CASE ("extract-here", test_native_extract);
    /* A real GMainLoop keeps its context owned while running. Match that
     * contract while these tests manually iterate/pump GTK: native operation
     * UI dispatch must never acquire the default context on a worker. */
    GMainContext *context = g_main_context_default ();
    g_assert_true (g_main_context_acquire (context));
    int result = g_test_run ();
    g_main_context_release (context);
    miller_test_input_free (input); return result;
}
