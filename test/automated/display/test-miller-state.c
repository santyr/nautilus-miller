/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <test-utilities.h>
#include "nautilus-application.h"
#include "nautilus-file.h"
#include "nautilus-file-utilities.h"
#include "nautilus-files-view.h"
#include "nautilus-global-preferences.h"
#include "nautilus-list-base.h"
#include "nautilus-miller-view.h"
#include "nautilus-resources.h"
#include "nautilus-tag-manager.h"
#include "nautilus-window-slot.h"

typedef struct
{
    NautilusWindowSlot *slot;
    NautilusMillerView *strip;
    GtkWidget *window, *entry;
    GFile *root, *child, *other, *one, *two;
    guint requests;
} Fixture;

static void
pump (guint milliseconds)
{
    gint64 end = g_get_monotonic_time () + milliseconds * G_TIME_SPAN_MILLISECOND;
    while (g_get_monotonic_time () < end)
    {
        while (g_main_context_iteration (NULL, FALSE)) {}
        g_usleep (1000);
    }
}
static void
settle (Fixture *f)
{
    gint64 deadline = g_get_monotonic_time () + 15 * G_TIME_SPAN_SECOND;
    while (nautilus_miller_view_get_loading (f->strip) || g_main_context_pending (NULL))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline);
        g_main_context_iteration (NULL, FALSE); g_usleep (1000);
    }
    pump (120);
}
static NautilusFilesView *
find_view (GtkWidget *widget, GFile *location)
{
    if (NAUTILUS_IS_FILES_VIEW (widget))
    {
        GFile *actual = nautilus_files_view_get_location (NAUTILUS_FILES_VIEW (widget));
        return actual != NULL && g_file_equal (actual, location) ? NAUTILUS_FILES_VIEW (widget) : NULL;
    }
    for (GtkWidget *child = gtk_widget_get_first_child (widget); child != NULL;
         child = gtk_widget_get_next_sibling (child))
    {
        NautilusFilesView *view = find_view (child, location);
        if (view != NULL) { return view; }
    }
    return NULL;
}
static GtkAdjustment *
vertical (NautilusFilesView *view)
{
    return gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (
        nautilus_list_base_get_scrolled_window (nautilus_files_view_get_private_list_base (view))));
}
static GtkAdjustment *horizontal (Fixture *f)
{
    return gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW (adw_bin_get_child (ADW_BIN (f->strip))));
}
static void
select_file (NautilusFilesView *view, GFile *location)
{
    g_autoptr (NautilusFile) file = nautilus_file_get (location);
    GList selection = { .data = file };
    nautilus_files_view_set_selection (view, &selection, FALSE);
}
static void
assert_selection (NautilusFilesView *view, GFile *location)
{
    g_autolist (NautilusFile) files = nautilus_files_view_get_selection (view);
    g_assert_cmpuint (g_list_length (files), ==, 1);
    g_autoptr (GFile) actual = nautilus_file_get_location (files->data);
    g_assert_true (g_file_equal (actual, location));
}
static void requested (NautilusMillerView *strip, GFile *location, Fixture *f) { f->requests++; }
static void
setup (Fixture *f, gconstpointer unused)
{
    f->root = g_file_new_for_path (test_get_tmp_dir ());
    f->child = g_file_get_child (f->root, "child");
    f->other = g_file_get_child (f->root, "other");
    f->one = g_file_get_child (f->child, "row-119.txt");
    f->two = g_file_get_child (f->child, "row-118.txt");
    g_assert_true (g_file_make_directory (f->child, NULL, NULL));
    g_assert_true (g_file_make_directory (f->other, NULL, NULL));
    for (guint i = 0; i < 120; i++)
    {
        g_autofree char *name = g_strdup_printf ("row-%03u.txt", i);
        g_autoptr (GFile) file = g_file_get_child (f->child, name);
        g_assert_true (g_file_replace_contents (file, "fixture", 7, NULL, FALSE, 0, NULL, NULL, NULL));
    }
    f->slot = nautilus_window_slot_new (NAUTILUS_MODE_BROWSE);
    f->strip = g_object_ref_sink (nautilus_miller_view_new (f->slot));
    f->window = gtk_window_new ();
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    f->entry = gtk_entry_new ();
    gtk_box_append (GTK_BOX (box), f->entry);
    gtk_box_append (GTK_BOX (box), GTK_WIDGET (f->strip));
    gtk_window_set_child (GTK_WINDOW (f->window), box);
    gtk_window_set_default_size (GTK_WINDOW (f->window), 900, 400);
    nautilus_miller_view_set_location (f->strip, f->child, NULL);
    nautilus_miller_view_set_active (f->strip, TRUE);
    gtk_window_present (GTK_WINDOW (f->window));
    settle (f);
    g_signal_connect (f->strip, "location-requested", G_CALLBACK (requested), f);
}
static void
teardown (Fixture *f, gconstpointer unused)
{
    if (f->window != NULL) { gtk_window_destroy (GTK_WINDOW (f->window)); }
    g_clear_object (&f->strip); g_clear_object (&f->slot);
    g_clear_object (&f->root); g_clear_object (&f->child); g_clear_object (&f->other);
    g_clear_object (&f->one); g_clear_object (&f->two);
    test_clear_tmp_dir ();
}
static void
test_roundtrip (Fixture *f, gconstpointer unused)
{
    NautilusFilesView *view = nautilus_miller_view_get_active_view (f->strip);
    gtk_widget_grab_focus (GTK_WIDGET (view));
    select_file (view, f->one); settle (f);
    gtk_adjustment_set_value (vertical (view), 280);
    gtk_adjustment_set_value (horizontal (f), 200);
    pump (100);
    double v = gtk_adjustment_get_value (vertical (view)), h = gtk_adjustment_get_value (horizontal (f));
    g_assert_cmpfloat (v, >, 0); g_assert_cmpfloat (h, >, 0);
    g_autoptr (GVariant) state = nautilus_miller_view_capture_state (f->strip, FALSE);
    g_assert_true (g_variant_is_of_type (state, G_VARIANT_TYPE ("(ssda(sasd))")));
    nautilus_miller_view_set_location (f->strip, f->other, NULL); settle (f);
    guint requests = f->requests;
    nautilus_miller_view_restore_state (f->strip, state, TRUE); settle (f);
    view = nautilus_miller_view_get_active_view (f->strip);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (view), f->child));
    assert_selection (view, f->one);
    g_assert_cmpfloat_with_epsilon (gtk_adjustment_get_value (vertical (view)), v, 2);
    g_assert_cmpfloat_with_epsilon (gtk_adjustment_get_value (horizontal (f)), h, 2);
    g_assert_cmpuint (f->requests, ==, requests);
}
static void
test_parent_context_and_entry (Fixture *f, gconstpointer unused)
{
    NautilusFilesView *parent = find_view (GTK_WIDGET (f->strip), f->root);
    g_assert_nonnull (parent);
    gtk_widget_grab_focus (GTK_WIDGET (parent));
    settle (f);
    g_assert_true (nautilus_miller_view_get_active_view (f->strip) == parent);
    g_autoptr (GVariant) state = nautilus_miller_view_capture_state (f->strip, FALSE);
    nautilus_miller_view_set_location (f->strip, f->other, NULL); settle (f);
    gtk_widget_grab_focus (f->entry);
    nautilus_miller_view_restore_state (f->strip, state, FALSE); settle (f);
    parent = nautilus_miller_view_get_active_view (f->strip);
    g_assert_true (g_file_equal (nautilus_miller_view_get_location (f->strip), f->child));
    g_assert_true (g_file_equal (nautilus_files_view_get_location (parent), f->root));
    assert_selection (parent, f->child);
    GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (f->window));
    g_assert_true (focus == f->entry || gtk_widget_is_ancestor (focus, f->entry));
}
static void
test_newer_selection_and_navigation (Fixture *f, gconstpointer unused)
{
    NautilusFilesView *view = nautilus_miller_view_get_active_view (f->strip);
    select_file (view, f->one); settle (f);
    g_autoptr (GVariant) state = nautilus_miller_view_capture_state (f->strip, FALSE);
    nautilus_miller_view_restore_state (f->strip, state, FALSE);
    select_file (view, f->two); settle (f);
    assert_selection (view, f->two);
    nautilus_miller_view_restore_state (f->strip, state, FALSE);
    nautilus_miller_view_set_location (f->strip, f->other, NULL); settle (f);
    g_assert_true (g_file_equal (nautilus_miller_view_get_location (f->strip), f->other));
    nautilus_miller_view_restore_state (f->strip, state, FALSE);
    gtk_window_destroy (GTK_WINDOW (f->window)); f->window = NULL;
    g_clear_object (&f->strip);
    pump (50);
}
static void
test_removed_and_multiple_selection (Fixture *f, gconstpointer unused)
{
    NautilusFilesView *view = nautilus_miller_view_get_active_view (f->strip);
    g_autoptr (NautilusFile) one = nautilus_file_get (f->one);
    g_autoptr (NautilusFile) two = nautilus_file_get (f->two);
    GList b = { .data = two }, a = { .data = one, .next = &b };
    nautilus_files_view_set_selection (view, &a, FALSE); settle (f);
    g_autoptr (GVariant) state = nautilus_miller_view_capture_state (f->strip, FALSE);
    nautilus_miller_view_set_location (f->strip, f->other, NULL); settle (f);
    nautilus_miller_view_restore_state (f->strip, state, FALSE); settle (f);
    view = nautilus_miller_view_get_active_view (f->strip);
    g_autolist (NautilusFile) files = nautilus_files_view_get_selection (view);
    g_assert_cmpuint (g_list_length (files), ==, 2);
    nautilus_miller_view_set_location (f->strip, f->other, NULL); settle (f);
    g_assert_true (g_file_delete (f->one, NULL, NULL));
    nautilus_miller_view_restore_state (f->strip, state, FALSE); settle (f);
    assert_selection (nautilus_miller_view_get_active_view (f->strip), f->two);
}
static void
test_leaving_parent_branch (Fixture *f, gconstpointer unused)
{
    NautilusFilesView *parent = find_view (GTK_WIDGET (f->strip), f->root);
    gtk_widget_grab_focus (GTK_WIDGET (parent));
    select_file (parent, f->other);
    /* Capture the old history entry before the pending sibling disclosure. */
    g_autoptr (GVariant) state = nautilus_miller_view_capture_state (f->strip, TRUE);
    settle (f);
    g_assert_true (g_file_equal (nautilus_miller_view_get_location (f->strip), f->other));
    nautilus_miller_view_restore_state (f->strip, state, FALSE); settle (f);
    g_assert_true (g_file_equal (nautilus_miller_view_get_location (f->strip), f->child));
    parent = nautilus_miller_view_get_active_view (f->strip);
    assert_selection (parent, f->child);
}
static void
test_manual_scroll_cancels (Fixture *f, gconstpointer unused)
{
    gtk_adjustment_set_value (horizontal (f), 200); pump (50);
    g_autoptr (GVariant) state = nautilus_miller_view_capture_state (f->strip, FALSE);
    nautilus_miller_view_restore_state (f->strip, state, FALSE);
    GtkWidget *scroller = adw_bin_get_child (ADW_BIN (f->strip));
    g_autoptr (GListModel) controllers = gtk_widget_observe_controllers (scroller);
    gboolean sent = FALSE;
    for (guint i = 0; i < g_list_model_get_n_items (controllers); i++)
    {
        g_autoptr (GtkEventController) controller = g_list_model_get_item (controllers, i);
        if (GTK_IS_EVENT_CONTROLLER_SCROLL (controller))
        {
            gboolean handled;
            g_signal_emit_by_name (controller, "scroll", 1.0, 0.0, &handled);
            sent = TRUE;
        }
    }
    g_assert_true (sent);
    gtk_adjustment_set_value (horizontal (f), 10);
    settle (f);
    g_assert_cmpfloat_with_epsilon (gtk_adjustment_get_value (horizontal (f)), 10, 2);
}
static void
test_activation_cancels_pending_context (Fixture *f, gconstpointer unused)
{
    NautilusFilesView *parent = find_view (GTK_WIDGET (f->strip), f->root);
    gtk_widget_grab_focus (GTK_WIDGET (parent)); settle (f);
    g_autoptr (GVariant) state = nautilus_miller_view_capture_state (f->strip, FALSE);
    nautilus_miller_view_restore_state (f->strip, state, FALSE);
    g_autoptr (NautilusFile) child = nautilus_file_get (f->child);
    gboolean handled = FALSE;
    g_signal_emit_by_name (parent, "column-activate-directory", child, &handled);
    g_assert_true (handled);
    settle (f);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (
        nautilus_miller_view_get_active_view (f->strip)), f->child));
}
typedef struct { Fixture *fixture; gboolean redirected; } Redirect;
static void
redirect_during_restore (NautilusMillerView *strip, NautilusFilesView *view, Redirect *redirect)
{
    if (!redirect->redirected && g_file_equal (nautilus_files_view_get_location (view), redirect->fixture->child))
    {
        redirect->redirected = TRUE;
        nautilus_miller_view_set_location (strip, redirect->fixture->other, NULL);
    }
}
static void
test_reentrant_navigation (Fixture *f, gconstpointer unused)
{
    NautilusFilesView *parent = find_view (GTK_WIDGET (f->strip), f->root);
    gtk_widget_grab_focus (GTK_WIDGET (parent)); settle (f);
    g_autoptr (GVariant) state = nautilus_miller_view_capture_state (f->strip, FALSE);
    nautilus_miller_view_set_location (f->strip, f->other, NULL); settle (f);
    Redirect redirect = { .fixture = f };
    gulong handler = g_signal_connect (f->strip, "active-view-changed", G_CALLBACK (redirect_during_restore), &redirect);
    nautilus_miller_view_restore_state (f->strip, state, FALSE); settle (f);
    g_signal_handler_disconnect (f->strip, handler);
    g_assert_true (redirect.redirected);
    g_assert_true (g_file_equal (nautilus_miller_view_get_location (f->strip), f->other));
    g_assert_true (g_file_equal (nautilus_files_view_get_location (
        nautilus_miller_view_get_active_view (f->strip)), f->other));
    parent = find_view (GTK_WIDGET (f->strip), f->root);
    assert_selection (parent, f->other);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    nautilus_register_resource (); nautilus_global_preferences_init ();
    g_autoptr (NautilusTagManager) tags = nautilus_tag_manager_new_dummy ();
    test_init_config_dir ();
    g_autoptr (NautilusApplication) app = nautilus_application_new ();
    g_application_set_flags (G_APPLICATION (app), g_application_get_flags (G_APPLICATION (app)) | G_APPLICATION_NON_UNIQUE);
    g_assert_true (g_application_register (G_APPLICATION (app), NULL, NULL));
    gtk_icon_theme_add_resource_path (gtk_icon_theme_get_for_display (gdk_display_get_default ()), "/org/gnome/nautilus/icons");
    nautilus_ensure_extension_points ();
#define CASE(name, function) g_test_add ("/miller/state/" name, Fixture, NULL, setup, function, teardown)
    CASE ("roundtrip", test_roundtrip);
    CASE ("parent-entry", test_parent_context_and_entry);
    CASE ("newer-selection-navigation-dispose", test_newer_selection_and_navigation);
    CASE ("multiple-removed", test_removed_and_multiple_selection);
    CASE ("leaving-parent-branch", test_leaving_parent_branch);
    CASE ("manual-scroll", test_manual_scroll_cancels);
    CASE ("activation-cancels-context", test_activation_cancels_pending_context);
    CASE ("reentrant-navigation", test_reentrant_navigation);
    return g_test_run ();
}
