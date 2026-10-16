/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <test-utilities.h>
#include <glib/gstdio.h>
#include "nautilus-application.h"
#include "nautilus-file.h"
#include "nautilus-file-utilities.h"
#include "nautilus-files-view.h"
#include "nautilus-global-preferences.h"
#include "nautilus-miller-view.h"
#include "nautilus-resources.h"
#include "nautilus-tag-manager.h"
#include "nautilus-window-slot.h"
#include "nautilus-window.h"
#include "nautilus-query.h"
#include "nautilus-view-model.h"

static void
collect_views (GtkWidget *widget, GPtrArray *views)
{
    if (NAUTILUS_IS_FILES_VIEW (widget))
    {
        g_ptr_array_add (views, widget);
        return;
    }
    for (GtkWidget *child = gtk_widget_get_first_child (widget); child != NULL;
         child = gtk_widget_get_next_sibling (child))
    {
        collect_views (child, views);
    }
}

static GPtrArray *
get_views (NautilusMillerView *strip)
{
    GPtrArray *views = g_ptr_array_new ();
    collect_views (GTK_WIDGET (strip), views);
    return views;
}

static void
settle (NautilusMillerView *strip)
{
    gint64 deadline = g_get_monotonic_time () + 15 * G_TIME_SPAN_SECOND;
    while (TRUE)
    {
        gboolean loading = FALSE;
        g_autoptr (GPtrArray) views = get_views (strip);
        for (guint i = 0; i < views->len; i++)
        {
            loading |= nautilus_files_view_get_loading (views->pdata[i]);
        }
        if (!loading && !g_main_context_pending (NULL)) { break; }
        g_assert_cmpint (g_get_monotonic_time (), <, deadline);
        g_main_context_iteration (NULL, FALSE);
        g_usleep (1000);
    }
}

static void
select_path (NautilusFilesView *view, GFile *location, gboolean automatic)
{
    g_autoptr (NautilusFile) file = nautilus_file_get (location);
    GList selection = { .data = file };
    nautilus_files_view_set_selection (view, &selection, automatic);
}

static void
test_branch_selection (void)
{
    g_autoptr (NautilusWindowSlot) slot = nautilus_window_slot_new (NAUTILUS_MODE_BROWSE);
    g_autoptr (NautilusMillerView) strip = g_object_ref_sink (nautilus_miller_view_new (slot));
    g_autoptr (GFile) root = g_file_new_for_path (test_get_tmp_dir ());
    g_autoptr (GFile) a = g_file_get_child (root, "A");
    g_autoptr (GFile) b = g_file_get_child (root, "B");
    g_autoptr (GFile) leaf = g_file_get_child (root, "file.txt");
    g_assert_true (g_file_make_directory (a, NULL, NULL));
    g_assert_true (g_file_make_directory (b, NULL, NULL));
    g_assert_true (g_file_replace_contents (leaf, "hello", 5, NULL, FALSE, 0, NULL, NULL, NULL));
    nautilus_miller_view_set_location (strip, root, NULL);
    settle (strip);
    g_autoptr (GPtrArray) before = get_views (strip);
    NautilusFilesView *parent = before->pdata[before->len - 1];
    g_assert_true (nautilus_miller_view_get_active_view (strip) == parent);
    select_path (parent, a, FALSE);
    settle (strip);
    g_autoptr (GPtrArray) after = get_views (strip);
    g_assert_cmpuint (after->len, ==, before->len + 1);
    g_assert_true (g_file_equal (nautilus_miller_view_get_location (strip), a));
    g_assert_true (nautilus_miller_view_get_active_view (strip) == parent);
    select_path (parent, b, FALSE);
    settle (strip);
    g_assert_true (g_file_equal (nautilus_miller_view_get_location (strip), b));
    gtk_selection_model_select_all (GTK_SELECTION_MODEL (nautilus_files_view_get_private_model (parent)));
    settle (strip);
    g_assert_true (g_file_equal (nautilus_miller_view_get_location (strip), root));
    select_path (parent, leaf, FALSE);
    settle (strip);
    g_clear_pointer (&after, g_ptr_array_unref);
    after = get_views (strip);
    g_assert_cmpuint (after->len, ==, before->len);
    g_assert_true (g_file_equal (nautilus_miller_view_get_location (strip), root));
    test_clear_tmp_dir ();
}

static void
test_deep_path_and_replacement (void)
{
    g_autoptr (NautilusWindowSlot) slot = nautilus_window_slot_new (NAUTILUS_MODE_BROWSE);
    g_autoptr (NautilusMillerView) strip = g_object_ref_sink (nautilus_miller_view_new (slot));
    g_autoptr (GFile) root = g_file_new_for_path (test_get_tmp_dir ());
    g_autoptr (GFile) deep = g_file_resolve_relative_path (root, ".hidden/猫 %/child");
    g_assert_true (g_file_make_directory_with_parents (deep, NULL, NULL));
    nautilus_miller_view_set_location (strip, deep, NULL);
    settle (strip);
    g_assert_true (g_file_equal (nautilus_miller_view_get_location (strip), deep));
    g_autoptr (GPtrArray) views = get_views (strip);
    for (guint i = 0; i + 1 < views->len; i++)
    {
        g_autolist (NautilusFile) selection = nautilus_files_view_get_selection (views->pdata[i]);
        gint64 highlight_deadline = g_get_monotonic_time () + 5 * G_TIME_SPAN_SECOND;
        /* A retained /tmp model may learn about this newly created fixture
         * through its monitor after enumeration has already completed. */
        while (selection == NULL && g_get_monotonic_time () < highlight_deadline)
        {
            g_main_context_iteration (NULL, FALSE);
            g_usleep (1000);
            selection = nautilus_files_view_get_selection (views->pdata[i]);
        }
        g_autofree char *column_uri = g_file_get_uri (nautilus_files_view_get_location (views->pdata[i]));
        g_test_message ("Ancestor %u %s: %u selected, %u rows", i, column_uri, g_list_length (selection), g_list_model_get_n_items (G_LIST_MODEL (nautilus_files_view_get_private_model (views->pdata[i]))));
        g_assert_cmpuint (g_list_length (selection), ==, 1);
        g_autoptr (GFile) selected_location = nautilus_file_get_location (selection->data);
        g_assert_true (g_file_equal (selected_location,
            nautilus_files_view_get_location (views->pdata[i + 1])));
    }
    nautilus_miller_view_set_location (strip, root, NULL);
    nautilus_miller_view_set_location (strip, deep, NULL);
    nautilus_miller_view_set_location (strip, root, NULL);
    settle (strip);
    g_assert_true (g_file_equal (nautilus_miller_view_get_location (strip), root));
    test_clear_tmp_dir ();
}

static gboolean
send_arrow (NautilusFilesView *view, guint keyval)
{
    g_autoptr (GListModel) controllers = gtk_widget_observe_controllers (GTK_WIDGET (view));
    for (guint i = 0; i < g_list_model_get_n_items (controllers); i++)
    {
        g_autoptr (GtkEventController) controller = g_list_model_get_item (controllers, i);
        if (GTK_IS_EVENT_CONTROLLER_KEY (controller))
        {
            gboolean handled = FALSE;
            g_signal_emit_by_name (controller, "key-pressed", keyval, 0, 0, &handled);
            if (handled) { return TRUE; }
        }
    }
    return FALSE;
}

static void
test_keyboard_and_focus (void)
{
    g_autoptr (NautilusWindowSlot) slot = nautilus_window_slot_new (NAUTILUS_MODE_BROWSE);
    g_autoptr (NautilusMillerView) strip = g_object_ref_sink (nautilus_miller_view_new (slot));
    g_autoptr (GFile) root = g_file_new_for_path (test_get_tmp_dir ());
    g_autoptr (GFile) child = g_file_get_child (root, "child");
    g_assert_true (g_file_make_directory (child, NULL, NULL));
    GtkWidget *window = gtk_window_new ();
    gtk_window_set_default_size (GTK_WINDOW (window), 1100, 600);
    gtk_window_set_child (GTK_WINDOW (window), GTK_WIDGET (strip));
    nautilus_miller_view_set_location (strip, root, NULL);
    gtk_window_present (GTK_WINDOW (window));
    settle (strip);
    NautilusFilesView *parent = nautilus_miller_view_get_active_view (strip);
    gtk_widget_grab_focus (GTK_WIDGET (parent));
    select_path (parent, child, FALSE);
    settle (strip);
    g_assert_true (nautilus_miller_view_get_active_view (strip) == parent);
    GtkWidget *focused = gtk_root_get_focus (GTK_ROOT (window));
    g_assert_true (focused == GTK_WIDGET (parent) || gtk_widget_is_ancestor (focused, GTK_WIDGET (parent)));
    g_assert_true (send_arrow (parent, GDK_KEY_Right));
    settle (strip);
    NautilusFilesView *child_view = nautilus_miller_view_get_active_view (strip);
    g_autofree char *actual_child = g_file_get_uri (nautilus_files_view_get_location (child_view));
    g_autofree char *expected_child = g_file_get_uri (child);
    g_assert_cmpstr (actual_child, ==, expected_child);
    /* Metadata/model updates may rebroadcast an unchanged parent selection.
     * They must not make that parent the action owner after entering a child. */
    g_signal_emit_by_name (parent, "selection-changed");
    settle (strip);
    g_assert_true (nautilus_miller_view_get_active_view (strip) == child_view);
    g_assert_true (send_arrow (child_view, GDK_KEY_Left));
    g_assert_true (nautilus_miller_view_get_active_view (strip) == parent);
    GtkWidget *column_box = gtk_widget_get_parent (GTK_WIDGET (parent));
    GtkWidget *grip = gtk_widget_get_next_sibling (column_box);
    g_autoptr (GListModel) controllers = gtk_widget_observe_controllers (grip);
    for (guint i = 0; i < g_list_model_get_n_items (controllers); i++)
    {
        g_autoptr (GtkEventController) controller = g_list_model_get_item (controllers, i);
        if (GTK_IS_GESTURE_DRAG (controller))
        {
            int before, after;
            gtk_widget_get_size_request (column_box, &before, NULL);
            g_signal_emit_by_name (controller, "drag-begin", 0.0, 0.0);
            g_signal_emit_by_name (controller, "drag-update", 100.0, 0.0);
            gtk_widget_get_size_request (column_box, &after, NULL);
            g_assert_cmpint (after, ==, before + 100);
            g_signal_emit_by_name (controller, "drag-update", -1000.0, 0.0);
            gtk_widget_get_size_request (column_box, &after, NULL);
            g_assert_cmpint (after, >=, 180);
        }
    }
    /* Programmatic branch reconstruction must not take focus from an entry. */
    GtkWidget *entry = gtk_entry_new ();
    GtkWidget *outer = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    g_object_ref (strip);
    gtk_window_set_child (GTK_WINDOW (window), NULL);
    gtk_box_append (GTK_BOX (outer), entry);
    gtk_box_append (GTK_BOX (outer), GTK_WIDGET (strip));
    g_object_unref (strip);
    gtk_window_set_child (GTK_WINDOW (window), outer);
    gtk_widget_grab_focus (entry);
    nautilus_miller_view_set_location (strip, root, NULL);
    nautilus_miller_view_set_location (strip, child, NULL);
    settle (strip);
    focused = gtk_root_get_focus (GTK_ROOT (window));
    g_assert_true (focused == entry || gtk_widget_is_ancestor (focused, entry));
    gtk_window_destroy (GTK_WINDOW (window));
    test_clear_tmp_dir ();
}

static GtkWidget *
find_status (GtkWidget *widget)
{
    if (ADW_IS_STATUS_PAGE (widget)) { return widget; }
    for (GtkWidget *child = gtk_widget_get_first_child (widget); child != NULL;
         child = gtk_widget_get_next_sibling (child))
    {
        GtkWidget *found = find_status (child);
        if (found != NULL) { return found; }
    }
    return NULL;
}

static void
test_error_is_local (void)
{
    g_autoptr (NautilusWindowSlot) slot = nautilus_window_slot_new (NAUTILUS_MODE_BROWSE);
    g_autoptr (NautilusMillerView) strip = g_object_ref_sink (nautilus_miller_view_new (slot));
    g_autoptr (GFile) root = g_file_new_for_path (test_get_tmp_dir ());
    g_autoptr (GFile) denied = g_file_get_child (root, "denied");
    g_assert_true (g_file_make_directory (denied, NULL, NULL));
    g_autofree char *path = g_file_get_path (denied);
    g_assert_cmpint (g_chmod (path, 0000), ==, 0);
    GtkWidget *window = gtk_window_new ();
    gtk_window_set_child (GTK_WINDOW (window), GTK_WIDGET (strip));
    nautilus_miller_view_set_location (strip, denied, NULL);
    settle (strip);
    g_assert_cmpint (g_chmod (path, 0700), ==, 0);
    NautilusFilesView *last = nautilus_miller_view_get_active_view (strip);
    GtkWidget *status = find_status (GTK_WIDGET (last));
    g_assert_nonnull (status);
    g_assert_true (gtk_widget_get_visible (status));
    g_assert_nonnull (adw_status_page_get_description (ADW_STATUS_PAGE (status)));
    g_assert_cmpuint (strlen (adw_status_page_get_description (ADW_STATUS_PAGE (status))), >, 0);
    nautilus_miller_view_set_location (strip, root, NULL);
    settle (strip);
    g_assert_true (g_file_equal (nautilus_miller_view_get_location (strip), root));
    gtk_window_destroy (GTK_WINDOW (window));
    test_clear_tmp_dir ();
}

static void
settle_slot (NautilusWindowSlot *slot)
{
    gint64 deadline = g_get_monotonic_time () + 15 * G_TIME_SPAN_SECOND;
    while (TRUE)
    {
        NautilusFilesView *view = nautilus_window_slot_get_current_view (slot);
        gboolean loading = nautilus_window_slot_get_loading (slot) || nautilus_window_slot_get_pending_location (slot) != NULL || view == NULL ||
                           nautilus_files_view_get_loading (view);
        if (!loading && !g_main_context_pending (NULL)) { return; }
        g_assert_cmpint (g_get_monotonic_time (), <, deadline);
        g_main_context_iteration (NULL, FALSE);
        g_usleep (1000);
    }
}

static void
test_slot_navigation_and_switching (void)
{
    g_settings_set_enum (nautilus_preferences, NAUTILUS_PREFERENCES_DEFAULT_FOLDER_VIEWER, 1);
    g_autoptr (NautilusWindowSlot) slot = nautilus_window_slot_new (NAUTILUS_MODE_BROWSE);
    GtkWidget *window = gtk_window_new ();
    gtk_window_set_child (GTK_WINDOW (window), GTK_WIDGET (slot));
    g_autoptr (GFile) root = g_file_new_for_path (test_get_tmp_dir ());
    g_autoptr (GFile) child = g_file_get_child (root, "child");
    g_assert_true (g_file_make_directory (child, NULL, NULL));
    nautilus_window_slot_open_location_full (slot, root, NULL);
    settle_slot (slot);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (slot), "slot.files-view-mode", "u", 4));
    settle_slot (slot);
    NautilusFilesView *parent = nautilus_window_slot_get_current_view (slot);
    g_assert_cmpuint (nautilus_files_view_get_view_id (parent), ==, 4);
    select_path (parent, child, FALSE);
    settle_slot (slot);
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (slot), child));
    g_assert_true (nautilus_window_slot_get_current_view (slot) == parent);
    g_assert_false (nautilus_window_slot_get_loading (slot));
    g_assert_false (nautilus_window_slot_get_allow_stop (slot));
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (slot), "slot.back", NULL));
    settle_slot (slot);
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (slot), root));
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (slot), "slot.forward", NULL));
    settle_slot (slot);
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (slot), child));
    const guint modes[] = { 2, 1, 4 };
    for (guint i = 0; i < G_N_ELEMENTS (modes); i++)
    {
        g_assert_true (gtk_widget_activate_action (GTK_WIDGET (slot), "slot.files-view-mode", "u", modes[i]));
        settle_slot (slot);
        g_assert_cmpuint (nautilus_files_view_get_view_id (nautilus_window_slot_get_current_view (slot)), ==, modes[i]);
        g_assert_true (g_file_equal (nautilus_window_slot_get_location (slot), child));
    }
    gtk_window_destroy (GTK_WINDOW (window));
    test_clear_tmp_dir ();
}

static void
after_paint (GdkFrameClock *clock, gboolean *painted)
{
    *painted = TRUE;
}

static void
wait_frame (GtkWidget *widget)
{
    GdkFrameClock *clock = gtk_widget_get_frame_clock (widget);
    gboolean painted = FALSE;
    gulong handler = g_signal_connect (clock, "after-paint", G_CALLBACK (after_paint), &painted);
    gtk_widget_queue_draw (widget);
    gdk_frame_clock_request_phase (clock, GDK_FRAME_CLOCK_PHASE_PAINT);
    gint64 deadline = g_get_monotonic_time () + 3 * G_TIME_SPAN_SECOND;
    while (!painted && g_get_monotonic_time () < deadline)
    {
        g_main_context_iteration (NULL, FALSE);
        g_usleep (1000);
    }
    g_signal_handler_disconnect (clock, handler);
    if (!painted) { g_test_message ("The session did not paint a frame within the snapshot timeout"); }
}

static void
save_window_snapshot (GtkWidget *window)
{
    const char *filename = g_getenv ("MILLER_SCREENSHOT_PATH");
    if (filename == NULL) { return; }
    g_autoptr (GdkPaintable) paintable = gtk_widget_paintable_new (window);
    GtkSnapshot *snapshot = gtk_snapshot_new ();
    gdk_paintable_snapshot (paintable, GDK_SNAPSHOT (snapshot),
        gtk_widget_get_width (window), gtk_widget_get_height (window));
    g_autoptr (GskRenderNode) node = gtk_snapshot_free_to_node (snapshot);
    g_assert_nonnull (node);
    GskRenderer *renderer = gtk_native_get_renderer (GTK_NATIVE (window));
    g_autoptr (GdkTexture) texture = gsk_renderer_render_texture (renderer, node, NULL);
    g_assert_true (gdk_texture_save_to_png (texture, filename));
}

static GtkWidget *
find_list (GtkWidget *widget)
{
    if (GTK_IS_LIST_VIEW (widget)) { return widget; }
    for (GtkWidget *child = gtk_widget_get_first_child (widget); child != NULL;
         child = gtk_widget_get_next_sibling (child))
    {
        GtkWidget *found = find_list (child);
        if (found != NULL) { return found; }
    }
    return NULL;
}

static void
test_automatic_selection_revealed (void)
{
    g_autoptr (NautilusWindowSlot) slot = nautilus_window_slot_new (NAUTILUS_MODE_BROWSE);
    g_autoptr (NautilusMillerView) strip = g_object_ref_sink (nautilus_miller_view_new (slot));
    g_autoptr (GFile) root = g_file_new_for_path (test_get_tmp_dir ());
    for (guint i = 0; i < 200; i++)
    {
        g_autofree char *name = g_strdup_printf ("row-%03u.txt", i);
        g_autoptr (GFile) file = g_file_get_child (root, name);
        g_assert_true (g_file_replace_contents (file, "", 0, NULL, FALSE, 0, NULL, NULL, NULL));
    }
    GtkWidget *window = gtk_window_new ();
    GtkWidget *outer = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *entry = gtk_entry_new ();
    gtk_box_append (GTK_BOX (outer), entry);
    gtk_box_append (GTK_BOX (outer), GTK_WIDGET (strip));
    gtk_widget_set_vexpand (GTK_WIDGET (strip), TRUE);
    gtk_window_set_child (GTK_WINDOW (window), outer);
    gtk_window_set_default_size (GTK_WINDOW (window), 1100, 400);
    nautilus_miller_view_set_location (strip, root, NULL);
    gtk_window_present (GTK_WINDOW (window));
    settle (strip);
    wait_frame (window);
    NautilusFilesView *view = nautilus_miller_view_get_active_view (strip);
    GtkWidget *list = find_list (GTK_WIDGET (view));
    g_assert_nonnull (list);
    GtkAdjustment *adjustment = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (list));
    gtk_adjustment_set_value (adjustment, 0);
    gtk_widget_grab_focus (entry);
    g_autoptr (GFile) target = g_file_get_child (root, "row-199.txt");
    g_autoptr (NautilusFile) target_file = nautilus_file_get (target);
    GList selection = { .data = target_file };
    /* This is the endpoint-selection path used by FileManager1 ShowItems. */
    nautilus_miller_view_set_location (strip, root, &selection);
    settle (strip);
    wait_frame (window);
    gint64 deadline = g_get_monotonic_time () + 5 * G_TIME_SPAN_SECOND;
    while (gtk_adjustment_get_value (adjustment) == 0 && g_get_monotonic_time () < deadline)
    {
        g_main_context_iteration (NULL, FALSE);
        g_usleep (1000);
    }
    g_autolist (NautilusFile) actual_selection = nautilus_files_view_get_selection (view);
    g_autoptr (GtkBitset) selected_positions = gtk_selection_model_get_selection (
        GTK_SELECTION_MODEL (nautilus_files_view_get_private_model (view)));
    g_test_message ("Selected model position: %u", gtk_bitset_get_minimum (selected_positions));
    g_assert_nonnull (actual_selection);
    g_autoptr (GFile) actual_file = nautilus_file_get_location (actual_selection->data);
    g_assert_true (g_file_equal (actual_file, target));
    g_test_message ("Reveal model=%u selection=%u loading=%d list=%dx%d value=%.0f upper=%.0f page=%.0f",
        g_list_model_get_n_items (G_LIST_MODEL (nautilus_files_view_get_private_model (view))),
        g_list_length (actual_selection), nautilus_files_view_get_loading (view),
        gtk_widget_get_width (list), gtk_widget_get_height (list),
        gtk_adjustment_get_value (adjustment), gtk_adjustment_get_upper (adjustment),
        gtk_adjustment_get_page_size (adjustment));
    g_assert_cmpfloat (gtk_adjustment_get_value (adjustment), >, 0);
    GtkWidget *focused = gtk_root_get_focus (GTK_ROOT (window));
    g_assert_true (focused == entry || gtk_widget_is_ancestor (focused, entry));
    gtk_window_destroy (GTK_WINDOW (window));
    test_clear_tmp_dir ();
}

static void
test_full_window_operations (void)
{
    g_settings_set_enum (nautilus_preferences, NAUTILUS_PREFERENCES_DEFAULT_FOLDER_VIEWER, 4);
    NautilusWindow *window = nautilus_window_new ();
    gtk_window_set_default_size (GTK_WINDOW (window), 1280, 680);
    g_autoptr (GFile) root = g_file_new_for_path (test_get_tmp_dir ());
    g_autoptr (GFile) child = g_file_get_child (root, "Projects");
    g_autoptr (GFile) source = g_file_get_child (root, "notes.txt");
    g_assert_true (g_file_make_directory (child, NULL, NULL));
    g_assert_true (g_file_replace_contents (source, "miller-test", 11, NULL, FALSE, 0, NULL, NULL, NULL));
    nautilus_window_open_location_full (window, root, 0, NULL);
    gtk_window_present (GTK_WINDOW (window));
    g_autoptr (NautilusWindowSlot) slot = NULL;
    g_object_get (window, "active-slot", &slot, NULL);
    settle_slot (slot);
    wait_frame (GTK_WIDGET (window));
    g_autoptr (GPtrArray) columns = g_ptr_array_new ();
    collect_views (GTK_WIDGET (slot), columns);
    NautilusFilesView *parent = NULL;
    for (guint i = 0; i < columns->len; i++)
    {
        NautilusFilesView *candidate = columns->pdata[i];
        if (g_file_equal (nautilus_files_view_get_location (candidate), root)) { parent = candidate; }
    }
    g_assert_nonnull (parent);
    gtk_widget_grab_focus (GTK_WIDGET (parent));
    select_path (parent, source, FALSE);
    settle_slot (slot);
    GAction *copy_action = g_action_map_lookup_action (G_ACTION_MAP (nautilus_files_view_get_private_action_group (parent)), "copy");
    g_assert_true (g_action_get_enabled (copy_action));
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (parent), "view.copy", NULL));
    select_path (parent, child, FALSE);
    settle_slot (slot);
    g_assert_true (send_arrow (parent, GDK_KEY_Right));
    settle_slot (slot);
    NautilusFilesView *destination = nautilus_window_slot_get_current_view (slot);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (destination), child));
    g_autofree char *target_uri = nautilus_files_view_get_backing_uri (destination);
    g_autofree char *expected_target = g_file_get_uri (child);
    g_assert_cmpstr (target_uri, ==, expected_target);
    g_autofree char *source_uri = g_file_get_uri (source);
    GList source_uris = { .data = source_uri };
    /* Wayland clipboard ownership needs a real user-input serial. Exercise
     * the shared transfer implementation with the active column's backing URI. */
    nautilus_files_view_move_copy_items (destination, &source_uris, target_uri, GDK_ACTION_COPY);
    g_autoptr (GFile) copied = g_file_get_child (child, "notes.txt");
    gint64 deadline = g_get_monotonic_time () + 15 * G_TIME_SPAN_SECOND;
    while (!g_file_query_exists (copied, NULL))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline);
        g_main_context_iteration (NULL, FALSE);
        g_usleep (1000);
    }
    g_autoptr (NautilusFile) copied_file = nautilus_file_get (copied);
    while (nautilus_view_model_get_item_for_file (nautilus_files_view_get_private_model (destination), copied_file) == NULL)
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline);
        g_main_context_iteration (NULL, FALSE);
        g_usleep (1000);
    }
    settle_slot (slot);
    g_assert_true (g_file_query_exists (source, NULL));
    if (g_getenv ("MILLER_SCREENSHOT_PATH") != NULL)
    {
        wait_frame (GTK_WIDGET (window));
        wait_frame (GTK_WIDGET (window));
        save_window_snapshot (GTK_WIDGET (window));
    }
    nautilus_window_new_tab (window);
    g_autoptr (NautilusWindowSlot) second_slot = NULL;
    g_object_get (window, "active-slot", &second_slot, NULL);
    g_assert_true (second_slot != slot);
    settle_slot (second_slot);
    g_object_set (window, "active-slot", slot, NULL);
    g_assert_true (nautilus_window_slot_get_active (slot));
    g_assert_false (nautilus_window_slot_get_active (second_slot));
    g_autoptr (NautilusQuery) query = nautilus_query_new ();
    nautilus_query_set_text (query, "notes");
    nautilus_query_set_location (query, child);
    nautilus_window_slot_search (slot, query);
    settle_slot (slot);
    g_assert_cmpuint (nautilus_files_view_get_view_id (nautilus_window_slot_get_current_view (slot)), !=, 4);
    g_assert_cmpuint (g_settings_get_enum (nautilus_preferences, NAUTILUS_PREFERENCES_DEFAULT_FOLDER_VIEWER), ==, 4);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (slot), "slot.search-visible", NULL));
    settle_slot (slot);
    g_assert_cmpuint (nautilus_files_view_get_view_id (nautilus_window_slot_get_current_view (slot)), ==, 4);
    gtk_window_destroy (GTK_WINDOW (window));
    test_clear_tmp_dir ();
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    nautilus_register_resource ();
    nautilus_global_preferences_init ();
    g_autoptr (NautilusTagManager) tags = nautilus_tag_manager_new_dummy ();
    test_init_config_dir ();
    g_autoptr (NautilusApplication) app = nautilus_application_new ();
    g_application_set_flags (G_APPLICATION (app), g_application_get_flags (G_APPLICATION (app)) | G_APPLICATION_NON_UNIQUE);
    g_assert_true (g_application_register (G_APPLICATION (app), NULL, NULL));
    gtk_icon_theme_add_resource_path (gtk_icon_theme_get_for_display (gdk_display_get_default ()),
                                      "/org/gnome/nautilus/icons");
    nautilus_ensure_extension_points ();
    g_test_add_func ("/miller/window/operations-tabs-search", test_full_window_operations);
    g_test_add_func ("/miller/slot/navigation-and-switching", test_slot_navigation_and_switching);
    g_test_add_func ("/miller/view/error-is-local", test_error_is_local);
    g_test_add_func ("/miller/view/keyboard-and-focus", test_keyboard_and_focus);
    g_test_add_func ("/miller/view/automatic-selection-revealed", test_automatic_selection_revealed);
    g_test_add_func ("/miller/view/branch-selection", test_branch_selection);
    g_test_add_func ("/miller/view/deep-path-and-replacement", test_deep_path_and_replacement);
    return g_test_run ();
}
