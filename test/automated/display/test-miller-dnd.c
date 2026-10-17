/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <test-utilities.h>
#include <gdk/wayland/gdkwayland.h>
#include "miller-test-operations.h"
#include "miller-test-backend.h"
#include "nautilus-application.h"
#include "nautilus-file.h"
#include "nautilus-file-utilities.h"
#include "nautilus-global-preferences.h"
#include "nautilus-progress-info.h"
#include "nautilus-miller-view.h"
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
    double pointer_x, pointer_y; /* Proved fullscreen surface coordinates. */
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

static gboolean
dnd_layout_frame (GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
    guint *frames = data;
    return ++*frames < 3 ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
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
    /* Binding and mapping may precede the next GTK allocation. A freshly
     * inserted/sorted row must finish layout before input targets its bounds. */
    guint frames = 0;
    gtk_widget_add_tick_callback (d->files.window, dnd_layout_frame, &frames, NULL);
    while (frames < 3)
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    cell = dnd_cell_for_file (GTK_WIDGET (view), file);
    g_assert_nonnull (cell); g_assert_true (gtk_widget_get_mapped (cell));
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
dnd_clear_watch (DndFixture *d)
{
    if (d->source != NULL) { g_signal_handlers_disconnect_by_data (d->source, d); }
    if (d->drag != NULL) { g_signal_handlers_disconnect_by_data (d->drag, d); }
    g_clear_object (&d->source); g_clear_object (&d->drag); g_clear_object (&d->expected_source);
}

static void
dnd_teardown (DndFixture *d, gconstpointer unused)
{
    dnd_clear_watch (d);
    operations_teardown (&d->files, unused);
}

static void
dnd_start (DndFixture *d, GtkWidget *cell, GtkWidget *target, GdkDragAction action)
{
    OperationFixture *f = &d->files;
    dnd_watch_source (d, cell);
    int threshold; g_object_get (gtk_settings_get_default (), "gtk-dnd-drag-threshold", &threshold, NULL);
    graphene_rect_t bounds; g_assert_true (gtk_widget_compute_bounds (cell, f->window, &bounds));
    g_test_message ("Native source cell=%s bounds=(%.1f,%.1f,%.1f,%.1f) threshold=%d actions=%u",
                    G_OBJECT_TYPE_NAME (cell), bounds.origin.x, bounds.origin.y,
                    bounds.size.width, bounds.size.height, threshold, gtk_drag_source_get_actions (d->source));
    operations_pointer_to (f, cell, gtk_widget_get_width (cell) / 2.0, gtk_widget_get_height (cell) / 2.0);
    graphene_point_t source_point;
    g_assert_true (gtk_widget_compute_point (cell, f->window,
        &GRAPHENE_POINT_INIT (gtk_widget_get_width (cell) / 2.0, gtk_widget_get_height (cell) / 2.0), &source_point));
    d->pointer_x = source_point.x; d->pointer_y = source_point.y;
    g_autoptr (GError) error = NULL;
    guint modifier = action == GDK_ACTION_MOVE ? GDK_KEY_Shift_L : GDK_KEY_Control_L;
    g_assert_true (miller_test_input_key (input, modifier, TRUE, &error));
    if (action == GDK_ACTION_LINK) { g_assert_true (miller_test_input_key (input, GDK_KEY_Shift_L, TRUE, &error)); }
    g_assert_true (miller_test_input_button (input, 272, TRUE, &error));
    /* GTK requires movement after its 100 ms minimum hold interval. */
    operations_pump (140);
    g_test_message ("After primary hold: target-mapped=%d drag-began=%u", gtk_widget_get_mapped (target), d->began);
    graphene_rect_t held_bounds;
    g_assert_true (gtk_widget_compute_bounds (cell, f->window, &held_bounds));
    g_test_message ("Source allocation after hold=(%.1f,%.1f,%.1f,%.1f)",
                    held_bounds.origin.x, held_bounds.origin.y, held_bounds.size.width, held_bounds.size.height);
    g_assert_true (miller_test_input_motion (input, 18, 0, &error));
    d->pointer_x += 18;
    gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    while (d->began == 0)
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    g_test_message ("During native drag: application-window-active=%d cached-focus=%s",
                    gtk_window_is_active (GTK_WINDOW (f->window)),
                    gtk_root_get_focus (GTK_ROOT (f->window)) != NULL ?
                    G_OBJECT_TYPE_NAME (gtk_root_get_focus (GTK_ROOT (f->window))) : "none");
    g_assert_cmpuint (d->began, ==, 1);
    g_assert_true ((gdk_drag_get_actions (d->drag) & action) != 0);
    g_assert_no_error (error);
}

static void
dnd_move_to (DndFixture *d, GtkWidget *target, double target_x, double target_y)
{
    OperationFixture *f = &d->files;
    g_autoptr (GError) error = NULL;
    g_assert_true (gtk_widget_get_mapped (target));
    graphene_point_t point;
    g_assert_true (gtk_widget_compute_point (target, f->window,
        &GRAPHENE_POINT_INIT (target_x, target_y), &point));
    /* Both points are in this fixture's proved fullscreen 1280x900 window.
     * Native GdkDrag events and exact native destination bytes prove delivery;
     * ordinary pointer focus can differ while Wayland owns a drag grab. */
    /* Move continuously from the already proved position. Resetting to a
     * surface corner mid-drag would create an unintended native strip leave. */
    g_assert_true (miller_test_input_motion (input, point.x - d->pointer_x, point.y - d->pointer_y, &error));
    d->pointer_x = point.x; d->pointer_y = point.y;
    operations_pump (120);
    g_assert_no_error (error);
}

static void
dnd_finish (DndFixture *d, GdkDragAction action, gboolean cancel)
{
    OperationFixture *f = &d->files;
    g_autoptr (GError) error = NULL;
    guint modifier = action == GDK_ACTION_MOVE ? GDK_KEY_Shift_L : GDK_KEY_Control_L;
    gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    d->action = gdk_drag_get_selected_action (d->drag);
    g_test_message ("Actual drag before release: selected-action=%u", d->action);
    if (cancel) { operations_key (GDK_KEY_Escape); }
    else if (action == GDK_ACTION_LINK)
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
dnd_drag (DndFixture *d, GtkWidget *cell, GtkWidget *target, double target_x, double target_y,
          GdkDragAction action)
{
    dnd_start (d, cell, target, action);
    dnd_move_to (d, target, target_x, target_y);
    dnd_finish (d, action, FALSE);
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

static void
dnd_assert_source_selection (DndFixture *d, NautilusFilesView *source)
{
    g_autolist (NautilusFile) selected = nautilus_files_view_get_selection (source);
    g_assert_cmpuint (g_list_length (selected), ==, 1);
    g_autoptr (GFile) location = nautilus_file_get_location (selected->data);
    g_test_message ("Hover source selection still identifies original dragged file: %d",
                    g_file_equal (location, d->expected_source));
    g_assert_true (g_file_equal (location, d->expected_source));
}

static void
dnd_assert_drag_focus (DndFixture *d, NautilusFilesView *source)
{
    OperationFixture *f = &d->files;
    g_assert_nonnull (d->drag);
    GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (f->window));
    g_test_message ("Hover drag focus: window-active=%d cached-focus=%s source-context=%d",
                    gtk_window_is_active (GTK_WINDOW (f->window)),
                    focus != NULL ? G_OBJECT_TYPE_NAME (focus) : "none",
                    nautilus_window_slot_get_current_view (f->slot) == source);
    /* The compositor's native drag grab temporarily deactivates the toplevel.
     * Its cached keyboard destination and action owner must still be source. */
    g_assert_nonnull (focus);
    g_assert_true (focus == GTK_WIDGET (source) || gtk_widget_is_ancestor (focus, GTK_WIDGET (source)));
    g_assert_true (nautilus_window_slot_get_current_view (f->slot) == source);
    g_assert_true (g_object_get_data (G_OBJECT (f->window), "nautilus-active-file-view") == source);
}

static NautilusFilesView *
dnd_hover_folder (DndFixture *d, NautilusFilesView *parent, GFile *folder)
{
    GtkWidget *cell = dnd_wait_cell (d, parent, folder);
    dnd_move_to (d, cell, gtk_widget_get_width (cell) * .25, gtk_widget_get_height (cell) / 2.0);
    /* Reuse native hover scheduling: motion beyond threshold inside the row,
     * followed by the existing 500ms timeout; no emitted controller signals. */
    g_autoptr (GError) error = NULL;
    g_assert_true (miller_test_input_motion (input, 18, 0, &error));
    d->pointer_x += 18;
    g_assert_no_error (error);
    operations_pump (750);
    NautilusFilesView *view = operations_view_for_directory (&d->files, folder);
    gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    while (nautilus_files_view_get_loading (view))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    operations_pump (120);
    g_test_message ("Native hover disclosed actual directory model");
    return view;
}

static void
test_native_spring (DndFixture *d, gconstpointer unused)
{
    OperationFixture *f = &d->files;
    gboolean backtrack = g_str_has_suffix (g_test_get_path (), "spring-backtrack-ancestor");
    gboolean leave = g_str_has_suffix (g_test_get_path (), "leave-restores-branch");
    gboolean newer = g_str_has_suffix (g_test_get_path (), "newer-navigation-wins");
    gboolean nested = g_str_has_suffix (g_test_get_path (), "nested-spring-cancel") || backtrack;
    gboolean accept = g_str_has_suffix (g_test_get_path (), "spring-drop-retains-destination");
    g_autoptr (GFile) folder = g_file_get_child (f->source_dir, "spring-folder");
    g_autoptr (GFile) inner = g_file_get_child (folder, "nested-folder");
    operations_owned (f, inner);
    g_assert_true (g_file_make_directory (folder, NULL, NULL));
    g_assert_true (g_file_make_directory (inner, NULL, NULL));
    NautilusFilesView *source = operations_view_for_directory (f, f->source_dir);
    operations_select (f, source, f->source_file);
    NautilusMillerView *miller = NAUTILUS_MILLER_VIEW (operations_find_type (f->window, NAUTILUS_TYPE_MILLER_VIEW));
    g_assert_nonnull (miller);
    g_autoptr (GVariant) before = nautilus_miller_view_capture_state (miller, FALSE);
    guint history = g_list_length (nautilus_window_slot_get_back_history (f->slot));
    d->expected_source = g_object_ref (f->source_file);
    GtkWidget *target = dnd_wait_cell (d, source, folder);
    dnd_start (d, dnd_wait_cell (d, source, f->source_file), target, GDK_ACTION_COPY);
    g_autoptr (GVariant) prepared = nautilus_miller_view_capture_state (miller, FALSE);
    double initial_scroll, prepared_scroll;
    g_variant_get_child (before, 2, "d", &initial_scroll);
    g_variant_get_child (prepared, 2, "d", &prepared_scroll);
    g_test_message ("Native source press/focus scroll: initial=%.1f prepared=%.1f", initial_scroll, prepared_scroll);
    NautilusFilesView *destination = dnd_hover_folder (d, source, folder);
    dnd_assert_source_selection (d, source);
    g_assert_true (g_file_equal (nautilus_miller_view_get_location (miller), folder));
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->source_dir));
    g_assert_cmpuint (g_list_length (nautilus_window_slot_get_back_history (f->slot)), ==, history);
    g_assert_cmpuint (f->jobs->len, ==, 0);
    g_assert_cmpuint (d->accepted, ==, 0); g_assert_cmpuint (d->cancelled, ==, 0);
    dnd_assert_drag_focus (d, source);
    if (nested)
    {
        destination = dnd_hover_folder (d, destination, inner);
        dnd_assert_source_selection (d, source);
        g_assert_true (g_file_equal (nautilus_miller_view_get_location (miller), inner));
        g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->source_dir));
        g_assert_cmpuint (g_list_length (nautilus_window_slot_get_back_history (f->slot)), ==, history);
        g_assert_cmpuint (f->jobs->len, ==, 0);
        dnd_assert_drag_focus (d, source);
    }
    if (backtrack)
    {
        dnd_hover_folder (d, source, folder);
        g_assert_true (g_file_equal (nautilus_miller_view_get_location (miller), folder));
        dnd_assert_source_selection (d, source);
        g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->source_dir));
        g_assert_cmpuint (g_list_length (nautilus_window_slot_get_back_history (f->slot)), ==, history);
        g_assert_cmpuint (f->jobs->len, ==, 0);
    }
    if (leave)
    {
        /* Stay in the owned fullscreen surface, outside the column strip. */
        dnd_move_to (d, f->window, 100, 650);
        operations_pump (200);
        g_assert_true (g_file_equal (nautilus_miller_view_get_location (miller), f->source_dir));
        g_assert_cmpuint (d->ended, ==, 0);
        g_assert_cmpuint (f->jobs->len, ==, 0);
    }
    if (newer)
    {
        /* Controlled newer native navigation while a real drag owns a
         * provisional branch; old cancellation must not roll it back. */
        nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), f->sibling, 0, NULL);
        operations_settle (f);
        g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->sibling));
        g_assert_true (g_file_equal (nautilus_miller_view_get_location (miller), f->sibling));
    }
    if (accept)
    {
        dnd_move_to (d, GTK_WIDGET (destination), gtk_widget_get_width (GTK_WIDGET (destination)) / 2.0,
                     gtk_widget_get_height (GTK_WIDGET (destination)) * .78);
    }
    dnd_finish (d, GDK_ACTION_COPY, !accept);
    if (accept)
    {
        g_assert_cmpuint (d->accepted, ==, 1); g_assert_cmpuint (d->cancelled, ==, 0);
        g_assert_cmpuint (f->jobs->len, ==, 1);
        g_autofree char *name = g_file_get_basename (f->source_file);
        g_autoptr (GFile) result = g_file_get_child (folder, name);
        operations_wait_copy (f, destination, result);
        operations_assert_bytes (f, result, MILLER_TEST_OPERATION_PAYLOAD);
        g_assert_true (nautilus_progress_info_get_is_finished (f->jobs->pdata[0]));
        g_assert_false (nautilus_progress_info_get_is_cancelled (f->jobs->pdata[0]));
        g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), folder));
        g_assert_cmpuint (g_list_length (nautilus_window_slot_get_back_history (f->slot)), ==, history + 1);
    }
    else if (newer)
    {
        g_assert_cmpuint (d->accepted, ==, 0); g_assert_cmpuint (d->cancelled, ==, 1);
        g_assert_cmpuint (f->jobs->len, ==, 0);
        g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->sibling));
        g_assert_true (g_file_equal (nautilus_miller_view_get_location (miller), f->sibling));
        g_assert_cmpuint (g_list_length (nautilus_window_slot_get_back_history (f->slot)), ==, history + 1);
        operations_assert_focus (f, operations_view_for_directory (f, f->sibling));
    }
    else
    {
        g_assert_cmpuint (d->accepted, ==, 0); g_assert_cmpuint (d->cancelled, ==, 1);
        g_assert_cmpuint (f->jobs->len, ==, 0);
        g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->source_dir));
        g_assert_cmpuint (g_list_length (nautilus_window_slot_get_back_history (f->slot)), ==, history);
        g_autoptr (GVariant) after = nautilus_miller_view_capture_state (miller, FALSE);
        if (!g_variant_equal (before, after))
        {
            g_autofree char *before_text = g_variant_print (before, TRUE);
            g_autofree char *after_text = g_variant_print (after, TRUE);
            g_test_message ("Cancellation snapshot before=%s after=%s", before_text, after_text);
        }
        g_assert_true (g_variant_equal (before, after));
        operations_assert_focus (f, source);
    }
    operations_assert_bytes (f, f->source_file, MILLER_TEST_OPERATION_PAYLOAD);
    operations_assert_sentinels (f);
}

typedef struct
{
    GFile *location; /* Borrowed until the real ready callback completes. */
    gboolean ready;
} DelayedOrigin;

static void
delayed_origin_ready (NautilusFile *file, gpointer data)
{
    DelayedOrigin *origin = data;
    g_autoptr (GFile) location = nautilus_file_get_location (file);
    g_assert_true (g_file_equal (location, origin->location));
    origin->ready = TRUE;
    g_test_message ("Actual paused origin metadata completed after backend resume");
}

static void
test_late_origin_completion (DndFixture *d, gconstpointer unused)
{
    if (g_getenv ("MILLER_TEST_BACKEND") == NULL)
    {
        g_test_skip ("Requires actual guarded with-delayed-backend.py");
        return;
    }
    OperationFixture *f = &d->files;
    g_autoptr (GKeyFile) config = miller_test_backend_metadata ();
    g_autofree char *uri = g_key_file_get_string (config, "backend", "root-uri", NULL);
    g_autoptr (GFile) backend = g_file_new_for_uri (uri);
    g_assert_true (g_file_has_uri_scheme (backend, "archive"));
    g_autoptr (GFile) old_origin = g_file_get_child (backend, "A");
    g_autoptr (GFile) folder = g_file_get_child (f->source_dir, "late-spring-folder");
    operations_owned (f, folder); g_assert_true (g_file_make_directory (folder, NULL, NULL));
    NautilusFilesView *source = operations_view_for_directory (f, f->source_dir);
    operations_select (f, source, f->source_file);
    g_autoptr (NautilusFilesView) retained_source = g_object_ref (source);
    NautilusMillerView *miller = NAUTILUS_MILLER_VIEW (operations_find_type (f->window, NAUTILUS_TYPE_MILLER_VIEW));
    g_autoptr (GVariant) before = nautilus_miller_view_capture_state (miller, FALSE);
    GtkWidget *target = dnd_wait_cell (d, source, folder);
    GtkWidget *cell = dnd_wait_cell (d, source, f->source_file);
    guint history = g_list_length (nautilus_window_slot_get_back_history (f->slot));
    miller_test_backend_control ("pause");
    nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), old_origin, 0, NULL);
    g_autoptr (NautilusFile) origin_file = nautilus_file_get (old_origin);
    DelayedOrigin origin = { .location = old_origin };
    nautilus_file_call_when_ready (origin_file, NAUTILUS_FILE_ATTRIBUTE_INFO, delayed_origin_ready, &origin);
    operations_pump (100);
    g_assert_false (origin.ready);
    g_assert_true (g_file_equal (nautilus_window_slot_get_pending_location (f->slot), old_origin));
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->source_dir));
    g_test_message ("Real origin request remains pending while archive backend is paused");
    d->expected_source = g_object_ref (f->source_file);
    dnd_start (d, cell, target, GDK_ACTION_COPY);
    dnd_hover_folder (d, source, folder);
    g_assert_false (origin.ready);
    g_assert_true (g_file_equal (nautilus_miller_view_get_location (miller), folder));
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->source_dir));
    g_assert_cmpuint (f->jobs->len, ==, 0);
    miller_test_backend_control ("resume");
    gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    while (!origin.ready)
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); operations_pump (10);
    }
    operations_settle (f);
    g_assert_cmpuint (d->ended, ==, 0);
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->source_dir));
    g_assert_true (g_file_equal (nautilus_miller_view_get_location (miller), folder));
    g_assert_cmpuint (g_list_length (nautilus_window_slot_get_back_history (f->slot)), ==, history);
    dnd_assert_source_selection (d, source); dnd_assert_drag_focus (d, source);
    dnd_finish (d, GDK_ACTION_COPY, TRUE);
    g_assert_cmpuint (d->accepted, ==, 0); g_assert_cmpuint (d->cancelled, ==, 1);
    g_assert_cmpuint (f->jobs->len, ==, 0);
    g_autoptr (GVariant) after = nautilus_miller_view_capture_state (miller, FALSE);
    g_assert_true (g_variant_equal (before, after));
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->source_dir));
    operations_assert_focus (f, source);
    operations_assert_bytes (f, f->source_file, MILLER_TEST_OPERATION_PAYLOAD);
    operations_assert_sentinels (f);
}

typedef struct
{
    DndFixture *old, *next;
    GFile *next_file, *next_folder;
    GVariant *before;
    guint calls;
} DragOverlap;

static void
start_drag_during_old_end (GtkDragSource *source, GdkDrag *drag, gboolean delete_data, DragOverlap *overlap)
{
    OperationFixture *f = &overlap->next->files;
    overlap->calls++;
    g_assert_cmpuint (overlap->calls, ==, 1);
    g_assert_true (drag == overlap->old->drag);
    g_assert_cmpuint (overlap->old->ended, ==, 1);
    /* The old native grab has ended; its drag-end emission and later native
     * callbacks are still on the stack. Deliver a real new pointer gesture. */
    g_autoptr (GError) error = NULL;
    g_assert_true (miller_test_input_button (input, 272, FALSE, &error));
    g_assert_true (miller_test_input_key (input, GDK_KEY_Control_L, FALSE, &error));
    g_assert_no_error (error);
    nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), f->sibling, 0, NULL);
    operations_settle (f);
    NautilusFilesView *view = operations_view_for_directory (f, f->sibling);
    operations_select (f, view, overlap->next_file);
    GtkWidget *target = dnd_wait_cell (overlap->next, view, overlap->next_folder);
    GtkWidget *cell = dnd_wait_cell (overlap->next, view, overlap->next_file);
    NautilusMillerView *miller = NAUTILUS_MILLER_VIEW (operations_find_type (f->window, NAUTILUS_TYPE_MILLER_VIEW));
    overlap->before = nautilus_miller_view_capture_state (miller, FALSE);
    overlap->next->expected_source = g_object_ref (overlap->next_file);
    dnd_start (overlap->next, cell, target, GDK_ACTION_COPY);
    g_assert_true (overlap->next->drag != drag);
    dnd_hover_folder (overlap->next, view, overlap->next_folder);
    g_assert_cmpuint (overlap->next->ended, ==, 0);
    g_test_message ("Second actual native drag is spring-loaded while old drag-end emission remains in flight");
}

static void
test_old_drag_cannot_restore_new_drag (DndFixture *d, gconstpointer unused)
{
    OperationFixture *f = &d->files;
    g_autoptr (GFile) old_folder = g_file_get_child (f->source_dir, "old-drag-folder");
    g_autoptr (GFile) next_folder = g_file_get_child (f->sibling, "new-drag-folder");
    g_autoptr (GFile) next_file = g_file_get_child (f->sibling, "new-drag.txt");
    GFile *owned[] = { old_folder, next_folder, next_file };
    for (guint i = 0; i < G_N_ELEMENTS (owned); i++) { operations_owned (f, owned[i]); }
    g_assert_true (g_file_make_directory (old_folder, NULL, NULL));
    g_assert_true (g_file_make_directory (next_folder, NULL, NULL));
    const char *next_payload = "Second actual native drag\n";
    g_assert_true (g_file_replace_contents (next_file, next_payload, strlen (next_payload), NULL, FALSE, 0, NULL, NULL, NULL));
    NautilusFilesView *view = operations_view_for_directory (f, f->source_dir);
    operations_select (f, view, f->source_file);
    GtkWidget *target = dnd_wait_cell (d, view, old_folder);
    d->expected_source = g_object_ref (f->source_file);
    guint history = g_list_length (nautilus_window_slot_get_back_history (f->slot));
    dnd_start (d, dnd_wait_cell (d, view, f->source_file), target, GDK_ACTION_COPY);
    dnd_hover_folder (d, view, old_folder);
    /* Share the already owned operation fixture without another setup/teardown.
     * Only the second observer's drag/source/expected-source refs are owned. */
    DndFixture next = { .files = *f };
    DragOverlap overlap = { .old = d, .next = &next, .next_file = next_file, .next_folder = next_folder };
    gulong handler = g_signal_connect (d->source, "drag-end", G_CALLBACK (start_drag_during_old_end), &overlap);
    operations_key (GDK_KEY_Escape);
    g_signal_handler_disconnect (d->source, handler);
    g_assert_cmpuint (overlap.calls, ==, 1);
    g_assert_cmpuint (d->cancelled, ==, 1); g_assert_cmpuint (d->accepted, ==, 0);
    g_assert_cmpuint (next.began, ==, 1); g_assert_cmpuint (next.ended, ==, 0);
    NautilusMillerView *miller = NAUTILUS_MILLER_VIEW (operations_find_type (f->window, NAUTILUS_TYPE_MILLER_VIEW));
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->sibling));
    g_assert_true (g_file_equal (nautilus_miller_view_get_location (miller), next_folder));
    g_assert_cmpuint (g_list_length (nautilus_window_slot_get_back_history (f->slot)), ==, history + 1);
    NautilusFilesView *new_source = operations_view_for_directory (f, f->sibling);
    dnd_assert_source_selection (&next, new_source); dnd_assert_drag_focus (&next, new_source);
    g_assert_cmpuint (f->jobs->len, ==, 0);
    dnd_finish (&next, GDK_ACTION_COPY, TRUE);
    g_assert_cmpuint (next.accepted, ==, 0); g_assert_cmpuint (next.cancelled, ==, 1);
    g_autoptr (GVariant) before = g_steal_pointer (&overlap.before);
    g_autoptr (GVariant) after = nautilus_miller_view_capture_state (miller, FALSE);
    g_assert_true (g_variant_equal (before, after));
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->sibling));
    operations_assert_focus (f, new_source);
    g_assert_cmpuint (f->jobs->len, ==, 0);
    operations_assert_bytes (f, f->source_file, MILLER_TEST_OPERATION_PAYLOAD);
    operations_assert_bytes (f, next_file, next_payload); operations_assert_sentinels (f);
    g_autofree char *old_name = g_file_get_basename (f->source_file);
    g_autoptr (GFile) old_result = g_file_get_child (old_folder, old_name);
    g_autoptr (GFile) new_result = g_file_get_child (next_folder, "new-drag.txt");
    g_assert_false (g_file_query_exists (old_result, NULL));
    g_assert_false (g_file_query_exists (new_result, NULL));
    dnd_clear_watch (&next);
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
#define SPRING(name) g_test_add ("/miller/dnd/" name, DndFixture, GUINT_TO_POINTER (NAUTILUS_VIEW_MILLER_ID), dnd_setup, test_native_spring, dnd_teardown)
    SPRING ("hover-preserves-source-selection");
    SPRING ("nested-spring-cancel");
    SPRING ("spring-drop-retains-destination");
    SPRING ("spring-backtrack-ancestor");
    SPRING ("leave-restores-branch");
    SPRING ("newer-navigation-wins");
    g_test_add ("/miller/dnd/old-drag-cannot-restore-new-drag", DndFixture, GUINT_TO_POINTER (NAUTILUS_VIEW_MILLER_ID),
                dnd_setup, test_old_drag_cannot_restore_new_drag, dnd_teardown);
    g_test_add ("/miller/dnd/late-origin-completion", DndFixture, GUINT_TO_POINTER (NAUTILUS_VIEW_MILLER_ID),
                dnd_setup, test_late_origin_completion, dnd_teardown);
    GMainContext *context = g_main_context_default (); g_assert_true (g_main_context_acquire (context));
    int result = g_test_run (); g_main_context_release (context);
    operations_set_input (NULL); miller_test_input_free (input); return result;
}
