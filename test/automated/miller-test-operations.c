/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "miller-test-operations.h"
#include <test-utilities.h>
#include <unistd.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include "nautilus-application.h"
#include "nautilus-window.h"
#include "nautilus-view-info.h"
#include "nautilus-view-model.h"
#include "nautilus-file.h"
#include "nautilus-global-preferences.h"
#include "nautilus-file-undo-manager.h"
#include "nautilus-progress-info.h"

static MillerTestInput *operations_input;
static const char payload[] = MILLER_TEST_OPERATION_PAYLOAD;

void
operations_set_input (MillerTestInput *input)
{
    operations_input = input;
}

typedef struct
{
    gboolean done;
    char *line;
    GError *error;
} ChildLine;

static gboolean
operations_observe_paste_key (GtkEventControllerKey *controller, guint key, guint code,
                              GdkModifierType state, OperationFixture *f)
{
    if ((key == GDK_KEY_v || key == GDK_KEY_V) && (state & GDK_CONTROL_MASK)) { f->paste_keys++; }
    return FALSE;
}

static void
operations_child_line_ready (GObject *source, GAsyncResult *result, gpointer data)
{
    ChildLine *line = data;
    line->line = g_data_input_stream_read_line_finish (G_DATA_INPUT_STREAM (source), result, NULL, &line->error);
    line->done = TRUE;
}

void
operations_pump (guint milliseconds)
{
    gint64 end = g_get_monotonic_time () + milliseconds * G_TIME_SPAN_MILLISECOND;
    while (g_get_monotonic_time () < end)
    {
        while (g_main_context_iteration (NULL, FALSE)) {}
        g_usleep (1000);
    }
}

void
operations_owned (OperationFixture *f, GFile *file)
{
    g_assert_true (g_file_equal (f->root, file) || g_file_has_prefix (file, f->root));
}

void
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

void
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

NautilusFilesView *
operations_view_for_directory (OperationFixture *f, GFile *directory)
{
    NautilusFilesView *view = operations_find_view (GTK_WIDGET (f->slot), directory);
    g_assert_nonnull (view);
    return view;
}

void
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

void
operations_wait_active (OperationFixture *f)
{
    gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    while (!gtk_window_is_active (GTK_WINDOW (f->window)))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
}

void
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

void
operations_chord (guint key, gboolean shift)
{
    g_autoptr (GError) error = NULL;
    g_assert_true (miller_test_input_key (operations_input, GDK_KEY_Control_L, TRUE, &error));
    if (shift) { g_assert_true (miller_test_input_key (operations_input, GDK_KEY_Shift_L, TRUE, &error)); }
    g_assert_true (miller_test_input_key (operations_input, key, TRUE, &error));
    g_assert_true (miller_test_input_key (operations_input, key, FALSE, &error));
    if (shift) { g_assert_true (miller_test_input_key (operations_input, GDK_KEY_Shift_L, FALSE, &error)); }
    g_assert_true (miller_test_input_key (operations_input, GDK_KEY_Control_L, FALSE, &error));
    g_assert_no_error (error); operations_pump (60);
}

void
operations_key (guint key)
{
    g_autoptr (GError) error = NULL;
    g_assert_true (miller_test_input_key (operations_input, key, TRUE, &error));
    g_assert_true (miller_test_input_key (operations_input, key, FALSE, &error));
    g_assert_no_error (error);
    operations_pump (60);
}

void
operations_type (const char *text)
{
    for (const char *p = text; *p != '\0'; p++)
    {
        g_assert_true (g_ascii_islower (*p) || *p == '-' || *p == '.');
        operations_key (*p);
    }
}

GtkWidget *
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

GtkWidget *
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

void
operations_pointer_to (OperationFixture *f, GtkWidget *widget, double widget_x, double widget_y)
{
    g_assert_nonnull (widget); g_assert_true (gtk_widget_get_mapped (widget));
    graphene_point_t point;
    g_assert_true (gtk_widget_compute_point (widget, f->window,
        &GRAPHENE_POINT_INIT (widget_x, widget_y), &point));
    g_autoptr (GError) error = NULL;
    g_assert_true (miller_test_input_motion (operations_input, -10000, -10000, &error));
    g_assert_true (miller_test_input_motion (operations_input, point.x, point.y, &error));
    operations_pump (80);
    GdkDevice *pointer = gdk_seat_get_pointer (gdk_display_get_default_seat (gdk_display_get_default ()));
    double x, y;
    GdkSurface *surface = gdk_device_get_surface_at_position (pointer, &x, &y);
    GdkSurface *expected = gtk_native_get_surface (GTK_NATIVE (f->window));
    for (guint i = 0; i < 12 && surface == expected &&
         (fabs (x - point.x) > 1 || fabs (y - point.y) > 1); i++)
    {
        g_assert_true (miller_test_input_motion (operations_input, (point.x - x) * .75, (point.y - y) * .75, &error));
        operations_pump (30);
        surface = gdk_device_get_surface_at_position (pointer, &x, &y);
    }
    g_assert_true (surface == expected);
    g_assert_cmpfloat_with_epsilon (x, point.x, 1.5);
    g_assert_cmpfloat_with_epsilon (y, point.y, 1.5);
    g_assert_no_error (error);
}

void
operations_click (OperationFixture *f, GtkWidget *widget, guint button)
{
    operations_pointer_to (f, widget, gtk_widget_get_width (widget) / 2.0,
                            gtk_widget_get_height (widget) / 2.0);
    g_autoptr (GError) error = NULL;
    g_assert_true (miller_test_input_button (operations_input, button, TRUE, &error));
    g_assert_true (miller_test_input_button (operations_input, button, FALSE, &error));
    g_assert_no_error (error); operations_pump (100);
}

void
operations_setup (OperationFixture *f, gconstpointer view_id)
{
    g_assert_nonnull (operations_input); g_assert_nonnull (g_getenv ("MILLER_TEST_HOME"));
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

void
operations_teardown (OperationFixture *f, gconstpointer unused)
{
    operations_stop_receiver (f);
    operations_settle (f);
    gdk_clipboard_set_content (gtk_widget_get_clipboard (f->window), NULL);
    gtk_window_destroy (GTK_WINDOW (f->window)); operations_pump (100);
    g_signal_handlers_disconnect_by_data (f->manager, f);
    g_clear_pointer (&f->jobs, g_ptr_array_unref); g_clear_object (&f->manager);
    g_clear_object (&f->slot); g_clear_object (&f->source_file); g_clear_object (&f->source_dir);
    g_clear_object (&f->parent); g_clear_object (&f->child); g_clear_object (&f->sibling); g_clear_object (&f->root);
    test_clear_tmp_dir ();
}

void
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

void
operations_wait_copy (OperationFixture *f, NautilusFilesView *view, GFile *destination)
{
    operations_wait_file (f, view, destination);
    g_assert_cmpuint (f->jobs->len, >, 0);
}

NautilusFilesView *
operations_open_parent (OperationFixture *f)
{
    nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), f->parent, 0, NULL);
    operations_settle (f);
    NautilusFilesView *view = operations_view_for_directory (f, f->parent);
    operations_select (f, view, f->child);
    operations_assert_focus (f, view);
    return view;
}

void
operations_assert_sentinels (OperationFixture *f)
{
    g_autoptr (GFile) child = g_file_get_child (f->child, "child-sentinel.txt");
    g_autoptr (GFile) sibling = g_file_get_child (f->sibling, "sibling-sentinel.txt");
    operations_assert_bytes (f, child, "child unchanged");
    operations_assert_bytes (f, sibling, "sibling unchanged");
}

void
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

void
operations_child_exited (GObject *source, GAsyncResult *result, gpointer data)
{
    g_autoptr (GError) error = NULL;
    g_assert_true (g_subprocess_wait_check_finish (G_SUBPROCESS (source), result, &error));
    g_assert_no_error (error);
    *(gboolean *) data = TRUE;
}

void
operations_assert_private_home (void)
{
    const char *masked = g_getenv ("MILLER_TEST_HOME");
    g_assert_nonnull (masked);
    struct stat home, backing, data;
    struct statvfs host;
    g_assert_cmpint (stat (g_get_home_dir (), &home), ==, 0);
    g_assert_cmpint (stat (masked, &backing), ==, 0);
    g_assert_cmpuint (home.st_dev, ==, backing.st_dev);
    g_assert_cmpuint (home.st_ino, ==, backing.st_ino);
    g_assert_cmpint (statvfs ("/", &host), ==, 0);
    g_assert_true ((host.f_flag & ST_RDONLY) != 0);
    g_autoptr (GFile) runtime = g_file_new_for_path (g_get_user_runtime_dir ());
    g_autoptr (GFile) private = g_file_get_parent (runtime);
    g_autoptr (GFile) expected_data = g_file_get_child (private, "data");
    g_autoptr (GFile) actual_data = g_file_new_for_path (g_get_user_data_dir ());
    g_assert_true (g_file_equal (actual_data, expected_data));
    g_assert_cmpint (lstat (g_get_user_data_dir (), &data), ==, 0);
    g_assert_true (S_ISDIR (data.st_mode));
    g_assert_cmpuint (data.st_uid, ==, geteuid ());
    g_assert_cmpuint (data.st_mode & 0777, ==, 0700);
}

void
operations_stop_receiver (OperationFixture *f)
{
    if (f->receiver == NULL) { return; }
    g_assert_true (g_output_stream_close (g_subprocess_get_stdin_pipe (f->receiver), NULL, NULL));
    gboolean exited = FALSE;
    g_subprocess_wait_check_async (f->receiver, NULL, operations_child_exited, &exited);
    gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    while (!exited)
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    g_clear_object (&f->receiver_output); g_clear_object (&f->receiver);
}

