/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <test-utilities.h>
#include <glib/gstdio.h>
#include "nautilus-application.h"
#include "nautilus-file.h"
#include "nautilus-file-utilities.h"
#include "nautilus-files-view.h"
#include "nautilus-global-preferences.h"
#include "nautilus-miller-view.h"
#include "nautilus-miller-widths.h"
#include "nautilus-resources.h"
#include "nautilus-tag-manager.h"
#include "nautilus-window-slot.h"
#include "nautilus-window.h"
#include "nautilus-query.h"
#include "nautilus-view-model.h"
#include "nautilus-view-controls.h"

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
    g_autoptr (NautilusFilesView) discarded = g_object_ref (after->pdata[after->len - 1]);
    select_path (parent, b, FALSE);
    settle (strip);
    g_signal_emit_by_name (discarded, "end-loading", TRUE);
    g_signal_emit_by_name (discarded, "selection-changed");
    settle (strip);
    g_assert_true (g_file_equal (nautilus_miller_view_get_location (strip), b));
    gtk_selection_model_select_all (GTK_SELECTION_MODEL (nautilus_files_view_get_private_model (parent)));
    g_signal_emit_by_name (parent, "end-loading", TRUE);
    g_autolist (NautilusFile) newest = nautilus_files_view_get_selection (parent);
    g_assert_cmpuint (g_list_length (newest), ==, 3);
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
test_view_selector_destinations (void)
{
    g_settings_set_enum (nautilus_preferences, NAUTILUS_PREFERENCES_DEFAULT_FOLDER_VIEWER, 1);
    g_autoptr (NautilusWindowSlot) slot = nautilus_window_slot_new (NAUTILUS_MODE_BROWSE);
    GtkWidget *window = gtk_window_new ();
    gtk_window_set_child (GTK_WINDOW (window), GTK_WIDGET (slot));
    g_autoptr (GFile) root = g_file_new_for_path (test_get_tmp_dir ());
    nautilus_window_slot_open_location_full (slot, root, NULL);
    settle_slot (slot);
    const guint modes[] = { 2, 4, 1 };
    const char *labels[] = { "Grid View", "Columns View", "List View" };
    const char *icons[] = { "view-grid-symbolic", "view-dual-symbolic", "view-list-symbolic" };
    g_autoptr (GtkWidget) controls = g_object_ref_sink (g_object_new (NAUTILUS_TYPE_VIEW_CONTROLS,
        "window-slot", slot, NULL));
    GtkWidget *button = adw_bin_get_child (ADW_BIN (controls));
    for (guint i = 0; i < G_N_ELEMENTS (modes); i++)
    {
        g_assert_cmpstr (nautilus_window_slot_get_tooltip (slot), ==, labels[i]);
        g_assert_cmpstr (nautilus_window_slot_get_icon_name (slot), ==, icons[i]);
        if (g_strcmp0 (g_getenv ("GTK_A11Y"), "test") == 0)
        {
            gtk_test_accessible_assert_property (button, GTK_ACCESSIBLE_PROPERTY_LABEL, labels[i]);
        }
        g_assert_true (gtk_widget_activate_action (GTK_WIDGET (slot), "slot.files-view-mode-toggle", NULL));
        settle_slot (slot);
        g_assert_cmpuint (nautilus_files_view_get_view_id (nautilus_window_slot_get_current_view (slot)), ==, modes[i]);
    }
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (slot), "slot.files-view-mode", "u", 4));
    g_autoptr (NautilusQuery) query = nautilus_query_new ();
    nautilus_query_set_text (query, "no-match");
    nautilus_query_set_location (query, root);
    nautilus_window_slot_search (slot, query);
    settle_slot (slot);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (slot), "slot.files-view-mode", "u", 2));
    settle_slot (slot);
    g_assert_cmpstr (nautilus_window_slot_get_tooltip (slot), ==, "List View");
    g_assert_cmpstr (nautilus_window_slot_get_icon_name (slot), ==, "view-list-symbolic");
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (slot), "slot.files-view-mode-toggle", NULL));
    settle_slot (slot);
    g_assert_cmpuint (nautilus_files_view_get_view_id (nautilus_window_slot_get_current_view (slot)), ==, 1);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (slot), "slot.files-view-mode", "u", 4));
    settle_slot (slot);
    g_assert_cmpuint (g_settings_get_enum (nautilus_preferences, NAUTILUS_PREFERENCES_DEFAULT_FOLDER_VIEWER), ==, 4);
    g_assert_cmpuint (nautilus_files_view_get_view_id (nautilus_window_slot_get_current_view (slot)), ==, 1);
    g_clear_object (&controls);
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

static GtkWidget *find_widget_type (GtkWidget *widget, GType type);

static GtkWidget *
find_button_label (GtkWidget *widget, const char *label)
{
    if (GTK_IS_BUTTON (widget) && g_strcmp0 (gtk_button_get_label (GTK_BUTTON (widget)), label) == 0)
    {
        return widget;
    }
    for (GtkWidget *child = gtk_widget_get_first_child (widget); child != NULL;
         child = gtk_widget_get_next_sibling (child))
    {
        GtkWidget *found = find_button_label (child, label);
        if (found != NULL) { return found; }
    }
    return NULL;
}

static void
test_column_width_controls (void)
{
    g_settings_reset (nautilus_preferences, MILLER_DEFAULT_WIDTH_KEY);
    g_settings_reset (nautilus_preferences, MILLER_WIDTHS_KEY);
    g_autoptr (GFile) root = g_file_new_for_path (test_get_tmp_dir ());
    g_autoptr (GFile) outside = g_file_get_parent (root);
    g_autofree char *long_name = g_strnfill (120, 'W');
    g_autoptr (GFile) long_file = g_file_get_child (root, long_name);
    g_assert_true (g_file_replace_contents (long_file, "fit", 3, NULL, FALSE, 0, NULL, NULL, NULL));
    nautilus_miller_width_set (nautilus_preferences, root, 460);
    g_autoptr (NautilusWindowSlot) slot = nautilus_window_slot_new (NAUTILUS_MODE_BROWSE);
    g_autoptr (NautilusMillerView) strip = g_object_ref_sink (nautilus_miller_view_new (slot));
    GtkWidget *window = gtk_window_new ();
    gtk_window_set_default_size (GTK_WINDOW (window), 900, 600);
    gtk_window_set_child (GTK_WINDOW (window), GTK_WIDGET (strip));
    nautilus_miller_view_set_location (strip, root, NULL);
    gtk_window_present (GTK_WINDOW (window));
    settle (strip);
    wait_frame (window);
    NautilusFilesView *view = nautilus_miller_view_get_active_view (strip);
    GtkWidget *box = gtk_widget_get_parent (GTK_WIDGET (view));
    int width;
    gtk_widget_get_size_request (box, &width, NULL);
    g_assert_cmpint (width, ==, 460);
    GtkWidget *menu = find_widget_type (box, GTK_TYPE_MENU_BUTTON);
    g_assert_nonnull (menu);
    GtkWidget *popover = GTK_WIDGET (gtk_menu_button_get_popover (GTK_MENU_BUTTON (menu)));
    GtkWidget *spin = find_widget_type (popover, GTK_TYPE_SPIN_BUTTON);
    g_assert_nonnull (spin);
    if (g_strcmp0 (g_getenv ("GTK_A11Y"), "test") == 0)
    {
        gtk_test_accessible_assert_property (spin, GTK_ACCESSIBLE_PROPERTY_LABEL, "Column Width");
        gtk_test_accessible_assert_property (menu, GTK_ACCESSIBLE_PROPERTY_LABEL, "Column Options");
    }
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin), 560);
    gtk_widget_get_size_request (box, &width, NULL);
    g_assert_cmpint (width, ==, 560);
    g_assert_cmpint (nautilus_miller_width_get (nautilus_preferences, root), ==, 560);
    GtkWidget *fit = find_button_label (popover, "Fit to Contents");
    GtkWidget *reset = find_button_label (popover, "Reset Width");
    GtkWidget *use_default = find_button_label (popover, "Use as Default");
    g_assert_nonnull (fit);
    g_assert_nonnull (reset);
    g_assert_nonnull (use_default);
    g_signal_emit_by_name (reset, "clicked");
    g_assert_cmpint (gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (spin)), ==, 280);
    g_signal_emit_by_name (fit, "clicked");
    width = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (spin));
    g_assert_cmpint (width, >, 280);
    g_assert_cmpint (width, <=, 1200);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin), 570);
    g_signal_emit_by_name (use_default, "clicked");
    g_assert_cmpuint (g_settings_get_uint (nautilus_preferences, MILLER_DEFAULT_WIDTH_KEY), ==, 570);
    g_assert_true (nautilus_miller_view_get_active_view (strip) == view);
    g_assert_true (g_file_equal (nautilus_miller_view_get_location (strip), root));

    GtkWidget *grip = gtk_widget_get_next_sibling (box);
    g_autoptr (GListModel) controllers = gtk_widget_observe_controllers (grip);
    gtk_widget_set_direction (GTK_WIDGET (strip), GTK_TEXT_DIR_RTL);
    gboolean dragged = FALSE;
    for (guint i = 0; i < g_list_model_get_n_items (controllers); i++)
    {
        g_autoptr (GtkEventController) controller = g_list_model_get_item (controllers, i);
        if (GTK_IS_GESTURE_DRAG (controller))
        {
            g_signal_emit_by_name (controller, "drag-begin", 0.0, 0.0);
            g_signal_emit_by_name (controller, "drag-update", 100.0, 0.0);
            g_signal_emit_by_name (controller, "drag-end", 100.0, 0.0);
            g_assert_cmpint (nautilus_miller_width_get (nautilus_preferences, root), ==, 470);
            dragged = TRUE;
        }
    }
    g_assert_true (dragged);
    gtk_menu_button_popup (GTK_MENU_BUTTON (menu));
    g_object_ref (spin);
    g_object_ref (reset);
    nautilus_miller_view_set_location (strip, outside, NULL);
    settle (strip);
    /* A retained old popup/control cannot publish through a freed column. */
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin), 800);
    g_signal_emit_by_name (reset, "clicked");
    g_assert_cmpint (nautilus_miller_width_get (nautilus_preferences, root), ==, 470);
    g_object_unref (spin);
    g_object_unref (reset);
    nautilus_miller_view_set_location (strip, root, NULL);
    settle (strip);
    box = gtk_widget_get_parent (GTK_WIDGET (nautilus_miller_view_get_active_view (strip)));
    gtk_widget_get_size_request (box, &width, NULL);
    g_assert_cmpint (width, ==, 470);
    gtk_window_destroy (GTK_WINDOW (window));
    g_settings_reset (nautilus_preferences, MILLER_DEFAULT_WIDTH_KEY);
    g_settings_reset (nautilus_preferences, MILLER_WIDTHS_KEY);
    test_clear_tmp_dir ();
}

static void
assert_column_visible (NautilusMillerView *strip, NautilusFilesView *view)
{
    GtkWidget *box = gtk_widget_get_parent (GTK_WIDGET (view));
    graphene_rect_t bounds;
    g_assert_true (gtk_widget_compute_bounds (box, gtk_widget_get_parent (box), &bounds));
    GtkScrolledWindow *scroller = GTK_SCROLLED_WINDOW (adw_bin_get_child (ADW_BIN (strip)));
    GtkAdjustment *adjustment = gtk_scrolled_window_get_hadjustment (scroller);
    double value = gtk_adjustment_get_value (adjustment);
    double page = gtk_adjustment_get_page_size (adjustment);
    g_test_message ("Column x=%.0f width=%.0f; viewport=%.0f+%.0f", bounds.origin.x, bounds.size.width, value, page);
    g_assert_cmpfloat (bounds.origin.x, >=, value - 1);
    g_assert_cmpfloat (bounds.origin.x + bounds.size.width, <=, value + page + 1);
}

static void
test_minimal_reveal (void)
{
    g_settings_reset (nautilus_preferences, MILLER_DEFAULT_WIDTH_KEY);
    g_settings_reset (nautilus_preferences, MILLER_WIDTHS_KEY);
    g_autoptr (GFile) root = g_file_new_for_path (test_get_tmp_dir ());
    g_autoptr (GFile) parent = g_file_resolve_relative_path (root, "one/two/three/four");
    g_autoptr (GFile) child = g_file_get_child (parent, "wide-child");
    g_assert_true (g_file_make_directory_with_parents (child, NULL, NULL));
    nautilus_miller_width_set (nautilus_preferences, child, 1100);
    g_autoptr (NautilusWindowSlot) slot = nautilus_window_slot_new (NAUTILUS_MODE_BROWSE);
    g_autoptr (NautilusMillerView) strip = g_object_ref_sink (nautilus_miller_view_new (slot));
    GtkWidget *window = gtk_window_new ();
    gtk_window_set_default_size (GTK_WINDOW (window), 900, 600);
    gtk_window_set_child (GTK_WINDOW (window), GTK_WIDGET (strip));
    nautilus_miller_view_set_location (strip, parent, NULL);
    gtk_window_present (GTK_WINDOW (window));
    settle (strip);
    wait_frame (window);
    wait_frame (window);
    NautilusFilesView *parent_view = nautilus_miller_view_get_active_view (strip);
    gtk_widget_grab_focus (GTK_WIDGET (parent_view));
    select_path (parent_view, child, FALSE);
    settle (strip);
    wait_frame (window);
    wait_frame (window);
    wait_frame (window);
    /* Disclosure must keep the focused parent visible even if child is wider
     * than the viewport. The old unconditional end-scroll hides it entirely. */
    assert_column_visible (strip, parent_view);
    GtkScrolledWindow *scroller = GTK_SCROLLED_WINDOW (adw_bin_get_child (ADW_BIN (strip)));
    GtkAdjustment *adjustment = gtk_scrolled_window_get_hadjustment (scroller);
    gtk_adjustment_set_value (adjustment, 0);
    /* Delayed slot echo of the selection-driven navigation is not a new user
     * request and must preserve intervening manual horizontal scrolling. */
    nautilus_miller_view_set_location (strip, child, NULL);
    g_signal_emit_by_name (parent_view, "end-loading", TRUE);
    settle (strip);
    wait_frame (window);
    wait_frame (window);
    g_assert_cmpfloat (gtk_adjustment_get_value (adjustment), ==, 0);
    g_assert_true (send_arrow (parent_view, GDK_KEY_Right));
    settle (strip);
    NautilusFilesView *child_view = nautilus_miller_view_get_active_view (strip);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (child_view), child));
    g_autoptr (GListModel) scroll_controllers = gtk_widget_observe_controllers (GTK_WIDGET (scroller));
    gboolean wheel_seen = FALSE;
    for (guint i = 0; i < g_list_model_get_n_items (scroll_controllers); i++)
    {
        g_autoptr (GtkEventController) controller = g_list_model_get_item (scroll_controllers, i);
        if (GTK_IS_EVENT_CONTROLLER_SCROLL (controller) &&
            gtk_event_controller_get_propagation_phase (controller) == GTK_PHASE_CAPTURE)
        {
            gboolean handled = FALSE;
            g_signal_emit_by_name (controller, "scroll", 1.0, 0.0, &handled);
            wheel_seen = TRUE;
        }
    }
    g_assert_true (wheel_seen);
    gtk_adjustment_set_value (adjustment, 0);
    wait_frame (window);
    wait_frame (window);
    wait_frame (window);
    g_assert_cmpfloat (gtk_adjustment_get_value (adjustment), ==, 0);
    g_assert_true (send_arrow (child_view, GDK_KEY_Left));
    wait_frame (window);
    wait_frame (window);
    wait_frame (window);
    assert_column_visible (strip, parent_view);
    double before = gtk_adjustment_get_value (adjustment);
    gtk_widget_grab_focus (GTK_WIDGET (parent_view));
    g_autoptr (GListModel) focus_controllers = gtk_widget_observe_controllers (GTK_WIDGET (parent_view));
    for (guint i = 0; i < g_list_model_get_n_items (focus_controllers); i++)
    {
        g_autoptr (GtkEventController) controller = g_list_model_get_item (focus_controllers, i);
        if (GTK_IS_EVENT_CONTROLLER_FOCUS (controller)) { g_signal_emit_by_name (controller, "enter"); }
    }
    wait_frame (window);
    wait_frame (window);
    wait_frame (window);
    g_assert_cmpfloat (gtk_adjustment_get_value (adjustment), ==, before);

    gtk_widget_set_default_direction (GTK_TEXT_DIR_RTL);
    g_assert_true (send_arrow (parent_view, GDK_KEY_Left));
    settle (strip);
    child_view = nautilus_miller_view_get_active_view (strip);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (child_view), child));
    gtk_widget_grab_focus (GTK_WIDGET (child_view));
    g_assert_true (send_arrow (child_view, GDK_KEY_Right));
    g_assert_true (nautilus_miller_view_get_active_view (strip) == parent_view);
    wait_frame (window);
    wait_frame (window);
    wait_frame (window);
    assert_column_visible (strip, parent_view);
    graphene_rect_t parent_bounds, child_bounds;
    GtkWidget *parent_box = gtk_widget_get_parent (GTK_WIDGET (parent_view));
    GtkWidget *child_box = gtk_widget_get_parent (GTK_WIDGET (child_view));
    g_assert_true (gtk_widget_compute_bounds (parent_box, gtk_widget_get_parent (parent_box), &parent_bounds));
    g_assert_true (gtk_widget_compute_bounds (child_box, gtk_widget_get_parent (child_box), &child_bounds));
    g_assert_cmpfloat (child_bounds.origin.x, <, parent_bounds.origin.x);
    g_assert_true (send_arrow (parent_view, GDK_KEY_Left));
    g_assert_true (g_file_equal (nautilus_files_view_get_location (nautilus_miller_view_get_active_view (strip)), child));
    /* Replace the requested column before its frame callback runs. */
    nautilus_miller_view_set_location (strip, root, NULL);
    nautilus_miller_view_set_location (strip, parent, NULL);
    gtk_window_destroy (GTK_WINDOW (window));
    gtk_widget_set_default_direction (GTK_TEXT_DIR_LTR);
    g_settings_reset (nautilus_preferences, MILLER_WIDTHS_KEY);
    test_clear_tmp_dir ();
}

static void
test_entry_arrows_do_not_navigate (void)
{
    g_autoptr (GFile) root = g_file_new_for_path (test_get_tmp_dir ());
    g_autoptr (GFile) child = g_file_get_child (root, "child");
    g_assert_true (g_file_make_directory (child, NULL, NULL));
    g_autoptr (NautilusWindowSlot) slot = nautilus_window_slot_new (NAUTILUS_MODE_BROWSE);
    g_autoptr (NautilusMillerView) strip = g_object_ref_sink (nautilus_miller_view_new (slot));
    GtkWidget *window = gtk_window_new ();
    gtk_window_set_default_size (GTK_WINDOW (window), 900, 600);
    gtk_window_set_child (GTK_WINDOW (window), GTK_WIDGET (strip));
    nautilus_miller_view_set_location (strip, root, NULL);
    gtk_window_present (GTK_WINDOW (window));
    settle (strip);
    NautilusFilesView *view = nautilus_miller_view_get_active_view (strip);
    select_path (view, child, FALSE);
    settle (strip);
    wait_frame (window);
    wait_frame (window);
    /* Rename-style editors are popover descendants of FilesView, so the
     * strip's capture controller sees their keys before the editor does. */
    GtkWidget *popover = gtk_popover_new ();
    GtkWidget *entry = gtk_entry_new ();
    gtk_popover_set_child (GTK_POPOVER (popover), entry);
    gtk_widget_set_parent (popover, GTK_WIDGET (view));
    gtk_popover_popup (GTK_POPOVER (popover));
    gtk_widget_grab_focus (entry);
    wait_frame (window);
    g_assert_false (send_arrow (view, GDK_KEY_Right));
    g_assert_true (nautilus_miller_view_get_active_view (strip) == view);
    gtk_root_set_focus (GTK_ROOT (window), NULL);
    gtk_widget_set_visible (popover, FALSE);
    gtk_widget_unparent (popover);
    gtk_window_destroy (GTK_WINDOW (window));
    test_clear_tmp_dir ();
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
emit_column_focus (NautilusFilesView *view)
{
    g_autoptr (GListModel) controllers = gtk_widget_observe_controllers (GTK_WIDGET (view));
    for (guint i = 0; i < g_list_model_get_n_items (controllers); i++)
    {
        g_autoptr (GtkEventController) controller = g_list_model_get_item (controllers, i);
        if (GTK_IS_EVENT_CONTROLLER_FOCUS (controller))
        {
            g_signal_emit_by_name (controller, "enter");
        }
    }
}

static GtkWidget *
find_widget_type (GtkWidget *widget, GType type)
{
    if (G_TYPE_CHECK_INSTANCE_TYPE (widget, type)) { return widget; }
    for (GtkWidget *child = gtk_widget_get_first_child (widget); child != NULL;
         child = gtk_widget_get_next_sibling (child))
    {
        GtkWidget *found = find_widget_type (child, type);
        if (found != NULL) { return found; }
    }
    return NULL;
}

static void
test_context_menu_owns_its_actions (void)
{
    g_autoptr (NautilusWindowSlot) slot = nautilus_window_slot_new (NAUTILUS_MODE_BROWSE);
    g_autoptr (NautilusMillerView) strip = g_object_ref_sink (nautilus_miller_view_new (slot));
    g_autoptr (GFile) root = g_file_new_for_path (test_get_tmp_dir ());
    g_autoptr (GFile) a = g_file_get_child (root, "A");
    g_autoptr (GFile) b = g_file_get_child (root, "B");
    g_assert_true (g_file_make_directory (a, NULL, NULL));
    g_assert_true (g_file_make_directory (b, NULL, NULL));
    GtkWidget *window = gtk_window_new ();
    gtk_window_set_default_size (GTK_WINDOW (window), 1100, 600);
    gtk_window_set_child (GTK_WINDOW (window), GTK_WIDGET (strip));
    nautilus_miller_view_set_location (strip, root, NULL);
    nautilus_miller_view_set_active (strip, TRUE);
    gtk_window_present (GTK_WINDOW (window));
    settle (strip);
    wait_frame (window);
    NautilusFilesView *parent = nautilus_miller_view_get_active_view (strip);
    select_path (parent, a, FALSE);
    settle (strip);
    g_autoptr (GPtrArray) views = get_views (strip);
    g_autoptr (NautilusFilesView) child = g_object_ref (views->pdata[views->len - 1]);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (parent), "view.popup-menu", NULL));
    GtkWidget *menu = find_widget_type (GTK_WIDGET (parent), GTK_TYPE_POPOVER_MENU);
    g_assert_nonnull (menu);
    /* Unit-level controller event. Actual compositor input is a separate gate. */
    emit_column_focus (child);
    g_assert_true (nautilus_miller_view_get_active_view (strip) == child);
    gtk_widget_grab_focus (GTK_WIDGET (child));
    gtk_popover_popdown (GTK_POPOVER (menu));
    wait_frame (window);
    settle (strip);
    g_assert_true (nautilus_miller_view_get_active_view (strip) == child);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (parent), "view.popup-menu", NULL));
    menu = find_widget_type (GTK_WIDGET (parent), GTK_TYPE_POPOVER_MENU);
    g_assert_nonnull (menu);
    emit_column_focus (child);
    g_assert_true (gtk_widget_activate_action (menu, "view.select-all", NULL));
    g_autolist (NautilusFile) selected = nautilus_files_view_get_selection (parent);
    g_assert_cmpuint (g_list_length (selected), ==, 2);
    g_autolist (NautilusFile) child_selection = nautilus_files_view_get_selection (child);
    g_assert_null (child_selection);
    gtk_popover_popdown (GTK_POPOVER (menu));
    settle (strip);
    gtk_window_destroy (GTK_WINDOW (window));
    test_clear_tmp_dir ();
}

static void
test_new_folder_captures_parent_context (void)
{
    g_autoptr (NautilusWindowSlot) slot = nautilus_window_slot_new (NAUTILUS_MODE_BROWSE);
    g_autoptr (NautilusMillerView) strip = g_object_ref_sink (nautilus_miller_view_new (slot));
    g_autoptr (GFile) root = g_file_new_for_path (test_get_tmp_dir ());
    g_autoptr (GFile) child = g_file_get_child (root, "child");
    g_autoptr (GFile) sibling = g_file_get_child (root, "untouched");
    g_assert_true (g_file_make_directory (child, NULL, NULL));
    g_assert_true (g_file_replace_contents (sibling, "sentinel", 8, NULL, FALSE, 0, NULL, NULL, NULL));
    GtkWidget *window = adw_window_new ();
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *entry = gtk_entry_new ();
    gtk_box_append (GTK_BOX (box), entry);
    gtk_box_append (GTK_BOX (box), GTK_WIDGET (strip));
    gtk_widget_set_vexpand (GTK_WIDGET (strip), TRUE);
    adw_window_set_content (ADW_WINDOW (window), box);
    gtk_window_set_default_size (GTK_WINDOW (window), 1100, 600);
    nautilus_miller_view_set_location (strip, root, NULL);
    nautilus_miller_view_set_active (strip, TRUE);
    gtk_window_present (GTK_WINDOW (window));
    settle (strip);
    wait_frame (window);
    NautilusFilesView *parent = nautilus_miller_view_get_active_view (strip);
    select_path (parent, child, FALSE);
    settle (strip);
    g_autoptr (GPtrArray) views = get_views (strip);
    NautilusFilesView *child_view = views->pdata[views->len - 1];
    gtk_widget_grab_focus (entry);
    g_assert_true (nautilus_miller_view_get_active_view (strip) == parent);
    g_assert_true (gtk_widget_activate_action (window, "view.new-folder", NULL));
    AdwDialog *dialog = adw_window_get_visible_dialog (ADW_WINDOW (window));
    g_assert_nonnull (dialog);
    GtkWidget *name = find_widget_type (GTK_WIDGET (dialog), ADW_TYPE_ENTRY_ROW);
    g_assert_nonnull (name);
    gtk_editable_set_text (GTK_EDITABLE (name), "Created in parent");
    emit_column_focus (child_view);
    g_signal_emit_by_name (name, "entry-activated");
    g_autoptr (GFile) created = g_file_get_child (root, "Created in parent");
    g_autoptr (GFile) wrong = g_file_get_child (child, "Created in parent");
    gint64 deadline = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
    while (!g_file_query_exists (created, NULL) && g_get_monotonic_time () < deadline)
    {
        g_main_context_iteration (NULL, FALSE);
        g_usleep (1000);
    }
    g_assert_true (g_file_query_exists (created, NULL));
    g_assert_false (g_file_query_exists (wrong, NULL));
    g_autofree char *contents = NULL;
    g_assert_true (g_file_load_contents (sibling, NULL, &contents, NULL, NULL, NULL));
    g_assert_cmpstr (contents, ==, "sentinel");
    settle (strip);
    /* The empty child's own toolbar context must be ready as well. */
    emit_column_focus (child_view);
    gtk_widget_grab_focus (entry);
    g_assert_true (gtk_widget_activate_action (window, "view.new-folder", NULL));
    dialog = adw_window_get_visible_dialog (ADW_WINDOW (window));
    g_assert_nonnull (dialog);
    name = find_widget_type (GTK_WIDGET (dialog), ADW_TYPE_ENTRY_ROW);
    g_assert_nonnull (name);
    gtk_editable_set_text (GTK_EDITABLE (name), "Created in child");
    g_signal_emit_by_name (name, "entry-activated");
    g_autoptr (GFile) child_created = g_file_get_child (child, "Created in child");
    g_autoptr (GFile) parent_wrong = g_file_get_child (root, "Created in child");
    deadline = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
    while (!g_file_query_exists (child_created, NULL) && g_get_monotonic_time () < deadline)
    {
        g_main_context_iteration (NULL, FALSE);
        g_usleep (1000);
    }
    g_assert_true (g_file_query_exists (child_created, NULL));
    g_assert_false (g_file_query_exists (parent_wrong, NULL));
    settle (strip);
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
    g_assert_true (g_object_get_data (G_OBJECT (window), "nautilus-active-file-view") ==
        nautilus_window_slot_get_current_view (second_slot));
    g_signal_emit_by_name (destination, "selection-changed");
    settle_slot (second_slot);
    g_assert_true (g_object_get_data (G_OBJECT (window), "nautilus-active-file-view") ==
        nautilus_window_slot_get_current_view (second_slot));
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
    g_test_add_func ("/miller/slot/view-selector-destinations", test_view_selector_destinations);
    g_test_add_func ("/miller/view/error-is-local", test_error_is_local);
    g_test_add_func ("/miller/view/keyboard-and-focus", test_keyboard_and_focus);
    g_test_add_func ("/miller/view/automatic-selection-revealed", test_automatic_selection_revealed);
    g_test_add_func ("/miller/view/width-controls", test_column_width_controls);
    g_test_add_func ("/miller/view/minimal-reveal", test_minimal_reveal);
    g_test_add_func ("/miller/view/entry-arrows", test_entry_arrows_do_not_navigate);
    g_test_add_func ("/miller/view/context-menu-action-owner", test_context_menu_owns_its_actions);
    g_test_add_func ("/miller/view/new-folder-parent-context", test_new_folder_captures_parent_context);
    g_test_add_func ("/miller/view/branch-selection", test_branch_selection);
    g_test_add_func ("/miller/view/deep-path-and-replacement", test_deep_path_and_replacement);
    return g_test_run ();
}
