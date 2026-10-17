/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <test-utilities.h>
#include <unistd.h>
#include <glib-unix.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <math.h>
#include "miller-test-input.h"
#include "nautilus-application.h"
#include "nautilus-clipboard.h"
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
#include "nautilus-resources.h"
#include "nautilus-tag-manager.h"
#include "nautilus-toolbar.h"
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
    guint paste_keys, paste_actions;
} OperationFixture;

static MillerTestInput *input;
static const char payload[] = "Native clipboard payload: 雪\n";

static gboolean
operations_observe_paste_key (GtkEventControllerKey *controller, guint key, guint code,
                              GdkModifierType state, OperationFixture *f)
{
    if ((key == GDK_KEY_v || key == GDK_KEY_V) && (state & GDK_CONTROL_MASK)) { f->paste_keys++; }
    return FALSE;
}

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
operations_wait_active (OperationFixture *f)
{
    gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    while (!gtk_window_is_active (GTK_WINDOW (f->window)))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
}

static void
operations_assert_focus (OperationFixture *f, NautilusFilesView *view)
{
    operations_wait_active (f);
    GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (f->window));
    g_autoptr (NautilusWindowSlot) active = NULL;
    g_object_get (f->window, "active-slot", &active, NULL);
    g_assert_true (active == f->slot);
    g_test_message ("mode=%u active-view=%s focus=%s", f->view_id,
        G_OBJECT_TYPE_NAME (nautilus_window_slot_get_current_view (f->slot)),
        focus != NULL ? G_OBJECT_TYPE_NAME (focus) : "none");
    g_assert_nonnull (focus);
    g_assert_true (focus == GTK_WIDGET (view) || gtk_widget_is_ancestor (focus, GTK_WIDGET (view)));
    g_assert_true (nautilus_window_slot_get_current_view (f->slot) == view);
    g_autoptr (GListModel) controllers = gtk_widget_observe_controllers (GTK_WIDGET (view));
    guint managed = 0;
    for (guint i = 0; i < g_list_model_get_n_items (controllers); i++)
    {
        g_autoptr (GtkEventController) controller = g_list_model_get_item (controllers, i);
        managed += GTK_IS_SHORTCUT_CONTROLLER (controller) &&
                   gtk_shortcut_controller_get_scope (GTK_SHORTCUT_CONTROLLER (controller)) == GTK_SHORTCUT_SCOPE_MANAGED;
    }
    g_assert_cmpuint (managed, ==, 1);
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
operations_key (guint key)
{
    g_autoptr (GError) error = NULL;
    g_assert_true (miller_test_input_key (input, key, TRUE, &error));
    g_assert_true (miller_test_input_key (input, key, FALSE, &error));
    g_assert_no_error (error);
    operations_pump (60);
}

static void
operations_type (const char *text)
{
    for (const char *p = text; *p != '\0'; p++)
    {
        g_assert_true (g_ascii_islower (*p) || *p == '-' || *p == '.');
        operations_key (*p);
    }
}

static GtkWidget *
operations_find_type (GtkWidget *widget, GType type)
{
    if (g_type_is_a (G_OBJECT_TYPE (widget), type)) { return widget; }
    for (GtkWidget *child = gtk_widget_get_first_child (widget); child; child = gtk_widget_get_next_sibling (child))
    {
        GtkWidget *found = operations_find_type (child, type);
        if (found != NULL) { return found; }
    }
    return NULL;
}

static GtkWidget *
operations_find_label (GtkWidget *widget, const char *text)
{
    if (GTK_IS_LABEL (widget) && gtk_widget_get_mapped (widget) &&
        g_strcmp0 (gtk_label_get_text (GTK_LABEL (widget)), text) == 0) { return widget; }
    for (GtkWidget *child = gtk_widget_get_first_child (widget); child; child = gtk_widget_get_next_sibling (child))
    {
        GtkWidget *found = operations_find_label (child, text);
        if (found != NULL) { return found; }
    }
    return NULL;
}

static void
operations_click (OperationFixture *f, GtkWidget *widget, guint button)
{
    g_assert_nonnull (widget); g_assert_true (gtk_widget_get_mapped (widget));
    graphene_point_t point;
    g_assert_true (gtk_widget_compute_point (widget, f->window,
        &GRAPHENE_POINT_INIT (gtk_widget_get_width (widget) / 2.0, gtk_widget_get_height (widget) / 2.0), &point));
    g_autoptr (GError) error = NULL;
    g_assert_true (miller_test_input_motion (input, -10000, -10000, &error));
    g_assert_true (miller_test_input_motion (input, point.x, point.y, &error));
    operations_pump (80);
    GdkDevice *pointer = gdk_seat_get_pointer (gdk_display_get_default_seat (gdk_display_get_default ()));
    double x, y;
    GdkSurface *surface = gdk_device_get_surface_at_position (pointer, &x, &y);
    GdkSurface *expected = gtk_native_get_surface (GTK_NATIVE (f->window));
    for (guint i = 0; i < 12 && surface == expected &&
         (fabs (x - point.x) > 1 || fabs (y - point.y) > 1); i++)
    {
        g_assert_true (miller_test_input_motion (input, (point.x - x) * .75, (point.y - y) * .75, &error));
        operations_pump (30);
        surface = gdk_device_get_surface_at_position (pointer, &x, &y);
    }
    g_assert_true (surface == expected);
    g_assert_cmpfloat_with_epsilon (x, point.x, 1.5);
    g_assert_cmpfloat_with_epsilon (y, point.y, 1.5);
    g_assert_true (miller_test_input_button (input, button, TRUE, &error));
    g_assert_true (miller_test_input_button (input, button, FALSE, &error));
    g_assert_no_error (error); operations_pump (100);
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
    GtkEventController *observer = gtk_event_controller_key_new ();
    gtk_event_controller_set_propagation_phase (observer, GTK_PHASE_CAPTURE);
    g_signal_connect (observer, "key-pressed", G_CALLBACK (operations_observe_paste_key), f);
    gtk_widget_add_controller (f->window, observer);
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
operations_wait_file (OperationFixture *f, NautilusFilesView *view, GFile *destination)
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
}

static void
operations_wait_copy (OperationFixture *f, NautilusFilesView *view, GFile *destination)
{
    operations_wait_file (f, view, destination);
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
    /* Exercise GtkFileDialog's existing in-process GTK backend. Portal
     * implementation acceptance remains in its separate native test. */
    g_setenv ("GDK_DEBUG", "no-portals", TRUE);
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
    COLUMN_CASE ("new-folder-parent", test_new_folder);
    COLUMN_CASE ("new-folder-empty-child", test_new_folder);
    COLUMN_CASE ("toolbar-with-entry-focus", test_new_folder);
    COLUMN_CASE ("rename-captured-selection", test_rename_captured);
    COLUMN_CASE ("copy-to-captured-selection", test_copy_move_to);
    COLUMN_CASE ("move-to-captured-selection", test_copy_move_to);
    COLUMN_CASE ("inactive-column-menu", test_inactive_column_menu);
    COLUMN_CASE ("dialog-source-pruned", test_dialog_source_pruned);
    COLUMN_CASE ("dialog-source-pruned-cancel", test_dialog_source_pruned);
    COLUMN_CASE ("operation-tab-switch", test_operation_tab_switch);
    COLUMN_CASE ("new-folder-source-pruned", test_new_folder_source_pruned);
    COLUMN_CASE ("new-folder-source-pruned-cancel", test_new_folder_source_pruned);
    COLUMN_CASE ("new-folder-after-tab-switch", test_new_folder_after_tab_switch);
    g_test_add ("/miller/operations/operation-tab-switch-list", OperationFixture,
        GUINT_TO_POINTER (NAUTILUS_VIEW_LIST_ID), operations_setup, test_operation_tab_switch, operations_teardown);
    g_test_add ("/miller/operations/operation-tab-switch-grid", OperationFixture,
        GUINT_TO_POINTER (NAUTILUS_VIEW_GRID_ID), operations_setup, test_operation_tab_switch, operations_teardown);
    COLUMN_CASE ("operation-tab-switch-entry", test_operation_tab_switch);
    /* A real GMainLoop keeps its context owned while running. Match that
     * contract while these tests manually iterate/pump GTK: native operation
     * UI dispatch must never acquire the default context on a worker. */
    GMainContext *context = g_main_context_default ();
    g_assert_true (g_main_context_acquire (context));
    int result = g_test_run ();
    g_main_context_release (context);
    miller_test_input_free (input); return result;
}
