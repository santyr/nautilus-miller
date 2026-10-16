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

int
main (int argc, char **argv)
{
    gtk_test_init (&argc, &argv, NULL);
    nautilus_register_resource ();
    gtk_icon_theme_add_resource_path (gtk_icon_theme_get_for_display (gdk_display_get_default ()),
                                      "/org/gnome/nautilus/icons");
    GtkCssProvider *css = gtk_css_provider_new ();
    gtk_css_provider_load_from_resource (css, "/org/gnome/nautilus/style.css");
    gtk_style_context_add_provider_for_display (gdk_display_get_default (), GTK_STYLE_PROVIDER (css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref (css);
    nautilus_ensure_extension_points ();
    nautilus_global_preferences_init ();
    g_autoptr (NautilusTagManager) tags = nautilus_tag_manager_new_dummy ();
    test_init_config_dir ();
    g_autoptr (NautilusApplication) app = nautilus_application_new ();
    g_test_add_func ("/miller/view/error-is-local", test_error_is_local);
    g_test_add_func ("/miller/view/keyboard-and-focus", test_keyboard_and_focus);
    g_test_add_func ("/miller/view/branch-selection", test_branch_selection);
    g_test_add_func ("/miller/view/deep-path-and-replacement", test_deep_path_and_replacement);
    return g_test_run ();
}
