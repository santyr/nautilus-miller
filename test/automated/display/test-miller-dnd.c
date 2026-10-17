/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <test-utilities.h>
#include <gdk/wayland/gdkwayland.h>
#include "miller-test-operations.h"
#include "nautilus-application.h"
#include "nautilus-file.h"
#include "nautilus-file-utilities.h"
#include "nautilus-global-preferences.h"
#include "nautilus-progress-info.h"
#include "nautilus-resources.h"
#include "nautilus-tag-manager.h"
#include "nautilus-view-cell.h"
#include "nautilus-view-item.h"
#include "nautilus-view-info.h"
#include "nautilus-view-model.h"
#include "nautilus-window.h"

typedef struct
{
    OperationFixture files;
    GtkDragSource *source;
    GdkDrag *drag;
    guint began, ended, accepted, cancelled;
    GdkDragAction action;
    GFile *expected_source;
} DndFixture;

static MillerTestInput *input;

static GtkWidget *
dnd_cell_for_file (GtkWidget *widget, NautilusFile *file)
{
    if (NAUTILUS_IS_VIEW_CELL (widget))
    {
        g_autoptr (NautilusViewItem) item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (widget));
        if (item != NULL && nautilus_view_item_get_file (item) == file) { return widget; }
    }
    for (GtkWidget *child = gtk_widget_get_first_child (widget); child; child = gtk_widget_get_next_sibling (child))
    {
        GtkWidget *cell = dnd_cell_for_file (child, file);
        if (cell != NULL) { return cell; }
    }
    return NULL;
}

static GtkWidget *
dnd_wait_cell (DndFixture *d, NautilusFilesView *view, GFile *location)
{
    g_autoptr (NautilusFile) file = nautilus_file_get (location);
    gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    GtkWidget *cell;
    while ((cell = dnd_cell_for_file (GTK_WIDGET (view), file)) == NULL || !gtk_widget_get_mapped (cell))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    return cell;
}

static void
dnd_accepted (GdkDrag *drag, DndFixture *d)
{
    d->accepted++;
    d->action = gdk_drag_get_selected_action (drag);
    g_test_message ("Actual GdkDrag drop-performed action=%u", d->action);
}

static void
dnd_began (GtkDragSource *source, GdkDrag *drag, DndFixture *d)
{
    d->began++;
    g_auto (GValue) value = G_VALUE_INIT;
    g_value_init (&value, GDK_TYPE_FILE_LIST);
    g_autoptr (GError) error = NULL;
    g_assert_true (gdk_content_provider_get_value (gdk_drag_get_content (drag), &value, &error));
    g_assert_no_error (error);
    GSList *files = gdk_file_list_get_files (g_value_get_boxed (&value));
    g_test_message ("Actual native drag payload count=%u", g_slist_length (files));
    g_assert_cmpuint (g_slist_length (files), ==, 1);
    g_assert_true (g_file_equal (files->data, d->expected_source));
    g_assert_null (d->drag); d->drag = g_object_ref (drag);
    g_signal_connect (drag, "drop-performed", G_CALLBACK (dnd_accepted), d);
    g_test_message ("Actual GtkDragSource began; offered actions=%u", gdk_drag_get_actions (drag));
}

static void
dnd_ended (GtkDragSource *source, GdkDrag *drag, gboolean delete_data, DndFixture *d)
{
    d->ended++;
    g_test_message ("Actual GtkDragSource ended; accepted=%u cancelled=%u", d->accepted, d->cancelled);
}

static gboolean
dnd_cancelled (GtkDragSource *source, GdkDrag *drag, GdkDragCancelReason reason, DndFixture *d)
{
    d->cancelled++;
    g_test_message ("Actual GtkDragSource cancelled reason=%u", reason);
    return FALSE;
}

static void
dnd_watch_source (DndFixture *d, GtkWidget *cell)
{
    g_autoptr (GListModel) controllers = gtk_widget_observe_controllers (cell);
    for (guint i = 0; i < g_list_model_get_n_items (controllers); i++)
    {
        g_autoptr (GtkEventController) controller = g_list_model_get_item (controllers, i);
        if (GTK_IS_DRAG_SOURCE (controller))
        {
            g_assert_null (d->source); d->source = g_object_ref (GTK_DRAG_SOURCE (controller));
        }
    }
    g_assert_nonnull (d->source);
    g_signal_connect (d->source, "drag-begin", G_CALLBACK (dnd_began), d);
    g_signal_connect (d->source, "drag-end", G_CALLBACK (dnd_ended), d);
    g_signal_connect (d->source, "drag-cancel", G_CALLBACK (dnd_cancelled), d);
}

static void
dnd_setup (DndFixture *d, gconstpointer mode)
{
    operations_setup (&d->files, mode);
}

static void
dnd_teardown (DndFixture *d, gconstpointer unused)
{
    if (d->source != NULL) { g_signal_handlers_disconnect_by_data (d->source, d); }
    if (d->drag != NULL) { g_signal_handlers_disconnect_by_data (d->drag, d); }
    g_clear_object (&d->source); g_clear_object (&d->drag); g_clear_object (&d->expected_source);
    operations_teardown (&d->files, unused);
}

static void
dnd_drag (DndFixture *d, GtkWidget *cell, GtkWidget *target, double target_x, double target_y,
          GdkDragAction action)
{
    OperationFixture *f = &d->files;
    dnd_watch_source (d, cell);
    int threshold; g_object_get (gtk_settings_get_default (), "gtk-dnd-drag-threshold", &threshold, NULL);
    graphene_rect_t bounds; g_assert_true (gtk_widget_compute_bounds (cell, f->window, &bounds));
    g_test_message ("Native source cell=%s bounds=(%.1f,%.1f,%.1f,%.1f) threshold=%d actions=%u",
                    G_OBJECT_TYPE_NAME (cell), bounds.origin.x, bounds.origin.y,
                    bounds.size.width, bounds.size.height, threshold, gtk_drag_source_get_actions (d->source));
    operations_pointer_to (f, cell, gtk_widget_get_width (cell) / 2.0, gtk_widget_get_height (cell) / 2.0);
    g_autoptr (GError) error = NULL;
    guint modifier = action == GDK_ACTION_MOVE ? GDK_KEY_Shift_L : GDK_KEY_Control_L;
    g_assert_true (miller_test_input_key (input, modifier, TRUE, &error));
    if (action == GDK_ACTION_LINK) { g_assert_true (miller_test_input_key (input, GDK_KEY_Shift_L, TRUE, &error)); }
    g_assert_true (miller_test_input_button (input, 272, TRUE, &error));
    /* GTK requires movement after its 100 ms minimum hold interval. */
    operations_pump (140);
    g_test_message ("After primary hold: target-mapped=%d drag-began=%u", gtk_widget_get_mapped (target), d->began);
    g_assert_true (miller_test_input_motion (input, 18, 0, &error));
    gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    while (d->began == 0)
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    g_assert_cmpuint (d->began, ==, 1);
    g_assert_true ((gdk_drag_get_actions (d->drag) & action) != 0);
    g_assert_true (gtk_widget_get_mapped (target));
    graphene_point_t point;
    g_assert_true (gtk_widget_compute_point (target, f->window,
        &GRAPHENE_POINT_INIT (target_x, target_y), &point));
    /* Both points are in this fixture's proved fullscreen 1280x900 window.
     * Native GdkDrag events and exact native destination bytes prove delivery;
     * ordinary pointer focus can differ while Wayland owns a drag grab. */
    g_assert_true (miller_test_input_motion (input, -10000, -10000, &error));
    g_assert_true (miller_test_input_motion (input, point.x, point.y, &error));
    operations_pump (120);
    d->action = gdk_drag_get_selected_action (d->drag);
    g_test_message ("Actual drag before release: selected-action=%u target=(%.1f,%.1f)",
                    d->action, point.x, point.y);
    if (action == GDK_ACTION_LINK)
    {
        /* wl_data_device_manager has COPY/MOVE/ASK, no LINK action. Do not
         * drop and accidentally move an owned file while testing absence. */
        g_assert_true (GDK_IS_WAYLAND_DISPLAY (gdk_display_get_default ()));
        g_assert_cmpuint (d->action, !=, GDK_ACTION_LINK);
        operations_key (GDK_KEY_Escape);
    }
    g_assert_true (miller_test_input_button (input, 272, FALSE, &error));
    g_assert_true (miller_test_input_key (input, modifier, FALSE, &error));
    if (action == GDK_ACTION_LINK) { g_assert_true (miller_test_input_key (input, GDK_KEY_Shift_L, FALSE, &error)); }
    g_assert_no_error (error);
    while (d->ended == 0)
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    g_assert_cmpuint (d->ended, ==, 1);
    operations_settle (f);
}

static void
test_native_file_drag (DndFixture *d, gconstpointer unused)
{
    OperationFixture *f = &d->files;
    const char *test = g_test_get_path ();
    gboolean parent_background = g_str_has_suffix (test, "between-visible-columns-copy");
    gboolean inactive_folder = g_str_has_suffix (test, "inactive-pointer-destination");
    gboolean empty = g_str_has_suffix (test, "empty-column-background");
    gboolean reject = g_str_has_suffix (test, "reject-self-drop");
    gboolean link_unavailable = g_str_has_suffix (test, "link-unavailable-wayland");
    GdkDragAction action = g_str_has_suffix (test, "move-modifier") ? GDK_ACTION_MOVE :
                           link_unavailable ? GDK_ACTION_LINK : GDK_ACTION_COPY;
    g_autoptr (GFile) folder = g_file_get_child (f->source_dir, "drop-here");
    operations_owned (f, folder); g_assert_true (g_file_make_directory (folder, NULL, NULL));
    NautilusFilesView *source = operations_view_for_directory (f, f->source_dir);
    operations_select (f, source, f->source_file);
    GtkWidget *target;
    NautilusFilesView *destination_view = NULL;
    GFile *destination = folder;
    GFile *dragged = f->source_file;
    if (empty || reject)
    {
        operations_select (f, source, folder);
        destination_view = operations_view_for_directory (f, folder);
        target = GTK_WIDGET (destination_view);
        if (reject) { dragged = folder; }
    }
    else if (parent_background)
    {
        destination = f->root;
        destination_view = operations_view_for_directory (f, f->root);
        target = GTK_WIDGET (destination_view);
    }
    else if (inactive_folder)
    {
        destination = f->parent;
        NautilusFilesView *parent = operations_view_for_directory (f, f->root);
        target = dnd_wait_cell (d, parent, destination);
    }
    else { target = dnd_wait_cell (d, source, folder); }
    g_autoptr (GtkWidget) retained_target = g_object_ref (target);
    GtkWidget *cell = dnd_wait_cell (d, source, dragged);
    double y = (empty || reject || parent_background) ? gtk_widget_get_height (target) * .78
                                                     : gtk_widget_get_height (target) / 2.0;
    guint history = g_list_length (nautilus_window_slot_get_back_history (f->slot));
    d->expected_source = g_object_ref (dragged);
    dnd_drag (d, cell, target, gtk_widget_get_width (target) / 2.0, y, action);
    if (reject || link_unavailable)
    {
        g_assert_cmpuint (d->accepted, ==, 0); g_assert_cmpuint (d->cancelled, ==, 1);
        g_assert_cmpuint (f->jobs->len, ==, 0);
        g_autoptr (GFile) recursive = g_file_get_child (folder, "drop-here");
        g_assert_false (g_file_query_exists (recursive, NULL));
        if (link_unavailable)
        {
            g_autofree char *name = g_file_get_basename (f->source_file);
            g_autoptr (GFile) absent = g_file_get_child (folder, name);
            g_assert_false (g_file_query_exists (absent, NULL));
            g_test_message ("Wayland LINK unavailable: actual native action=%u; genuine Escape preserved source", d->action);
        }
        operations_assert_bytes (f, f->source_file, MILLER_TEST_OPERATION_PAYLOAD);
    }
    else
    {
        g_assert_cmpuint (d->accepted, ==, 1); g_assert_cmpuint (d->cancelled, ==, 0);
        g_assert_cmpuint (d->action, ==, action); g_assert_cmpuint (f->jobs->len, ==, 1);
        g_assert_true (nautilus_progress_info_get_is_finished (f->jobs->pdata[0]));
        g_assert_false (nautilus_progress_info_get_is_cancelled (f->jobs->pdata[0]));
        g_autofree char *name = g_file_get_basename (f->source_file);
        g_autoptr (GFile) result = g_file_get_child (destination, name);
        operations_owned (f, result);
        operations_assert_bytes (f, result, MILLER_TEST_OPERATION_PAYLOAD);
        if (action == GDK_ACTION_MOVE) { g_assert_false (g_file_query_exists (f->source_file, NULL)); }
        else { operations_assert_bytes (f, f->source_file, MILLER_TEST_OPERATION_PAYLOAD); }
        if (destination_view != NULL) { operations_wait_file (f, destination_view, result); }
        GFile *untouched[] = { f->root, f->parent, f->child, f->sibling, folder };
        for (guint i = 0; i < G_N_ELEMENTS (untouched); i++)
        {
            if (g_file_equal (untouched[i], destination)) { continue; }
            g_autoptr (GFile) wrong = g_file_get_child (untouched[i], name);
            g_assert_false (g_file_query_exists (wrong, NULL));
        }
    }
    operations_assert_sentinels (f);
    g_assert_cmpuint (g_list_length (nautilus_window_slot_get_back_history (f->slot)), ==, history);
    if (f->view_id == NAUTILUS_VIEW_MILLER_ID)
    {
        /* Drag finish/cancel must release its navigation guard. */
        nautilus_files_view_set_selection (source, NULL, FALSE); operations_settle (f);
        operations_select (f, source, folder);
        g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), folder));
    }
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    if (g_getenv ("MILLER_TEST_SESSION") == NULL || g_getenv ("MILLER_TEST_HOME") == NULL) { return 77; }
    g_autoptr (GError) error = NULL;
    input = miller_test_input_new (&error); g_assert_no_error (error); g_assert_nonnull (input);
    operations_set_input (input); operations_assert_private_home ();
    nautilus_register_resource (); nautilus_global_preferences_init (); nautilus_ensure_extension_points ();
    g_autoptr (NautilusTagManager) tags = nautilus_tag_manager_new_dummy ();
    test_init_config_dir ();
    g_autoptr (NautilusApplication) app = nautilus_application_new ();
    g_application_set_flags (G_APPLICATION (app), g_application_get_flags (G_APPLICATION (app)) | G_APPLICATION_NON_UNIQUE);
    g_assert_true (g_application_register (G_APPLICATION (app), NULL, NULL));
#define CASE(name, mode) g_test_add ("/miller/dnd/" name, DndFixture, GUINT_TO_POINTER (mode), dnd_setup, test_native_file_drag, dnd_teardown)
    CASE ("grid-folder-copy", NAUTILUS_VIEW_GRID_ID);
    CASE ("list-folder-copy", NAUTILUS_VIEW_LIST_ID);
    CASE ("column-folder-copy", NAUTILUS_VIEW_MILLER_ID);
    CASE ("between-visible-columns-copy", NAUTILUS_VIEW_MILLER_ID);
    CASE ("empty-column-background", NAUTILUS_VIEW_MILLER_ID);
    CASE ("inactive-pointer-destination", NAUTILUS_VIEW_MILLER_ID);
    CASE ("move-modifier", NAUTILUS_VIEW_MILLER_ID);
    CASE ("link-unavailable-wayland", NAUTILUS_VIEW_MILLER_ID);
    CASE ("grid-link-unavailable-wayland", NAUTILUS_VIEW_GRID_ID);
    CASE ("list-link-unavailable-wayland", NAUTILUS_VIEW_LIST_ID);
    CASE ("reject-self-drop", NAUTILUS_VIEW_MILLER_ID);
    GMainContext *context = g_main_context_default (); g_assert_true (g_main_context_acquire (context));
    int result = g_test_run (); g_main_context_release (context);
    operations_set_input (NULL); miller_test_input_free (input); return result;
}
