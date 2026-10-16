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
#include "nautilus-window.h"
#include "nautilus-bookmark.h"
#include "nautilus-query.h"
#include "nautilus-monitor.h"
#include "nautilus-bookmark-list.h"
#include "nautilus-pathbar.h"
#include "nautilus-view-item.h"
#include "nautilus-sidebar.h"
#include "nautilus-sidebar-row.h"
#include "miller-test-input.h"
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct
{
    NautilusWindowSlot *slot;
    NautilusMillerView *strip;
    GtkWidget *window, *entry;
    GFile *root, *child, *other, *one, *two;
    guint requests;
} Fixture;
static MillerTestInput *input;

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
settle_native (Fixture *f)
{
    gint64 deadline = g_get_monotonic_time () + 15 * G_TIME_SPAN_SECOND;
    while (nautilus_window_slot_get_loading (f->slot) || g_main_context_pending (NULL))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline);
        g_main_context_iteration (NULL, FALSE); g_usleep (1000);
    }
    pump (150);
    NautilusFilesView *view = nautilus_window_slot_get_current_view (f->slot);
    g_set_object (&f->strip, NAUTILUS_MILLER_VIEW (gtk_widget_get_ancestor (GTK_WIDGET (view), NAUTILUS_TYPE_MILLER_VIEW)));
}
static void
settle_native_location (Fixture *f, GFile *expected)
{
    gint64 deadline = g_get_monotonic_time () + 15 * G_TIME_SPAN_SECOND;
    GFile *actual = nautilus_window_slot_get_location (f->slot);
    while (actual == NULL || !g_file_equal (actual, expected))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline);
        pump (5);
        actual = nautilus_window_slot_get_location (f->slot);
    }
    settle_native (f);
}
static void
setup (Fixture *f, gconstpointer native)
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
    if (native != NULL)
    {
        g_settings_set_enum (nautilus_preferences, NAUTILUS_PREFERENCES_DEFAULT_FOLDER_VIEWER, 4);
        f->window = GTK_WIDGET (nautilus_window_new ());
        nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), f->child, 0, NULL);
        g_object_get (f->window, "active-slot", &f->slot, NULL);
    }
    else
    {
        f->slot = nautilus_window_slot_new (NAUTILUS_MODE_BROWSE);
        f->strip = g_object_ref_sink (nautilus_miller_view_new (f->slot));
        f->window = gtk_window_new ();
        GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
        f->entry = gtk_entry_new ();
        gtk_box_append (GTK_BOX (box), f->entry);
        gtk_box_append (GTK_BOX (box), GTK_WIDGET (f->strip));
        gtk_window_set_child (GTK_WINDOW (f->window), box);
        nautilus_miller_view_set_location (f->strip, f->child, NULL);
        nautilus_miller_view_set_active (f->strip, TRUE);
    }
    gtk_window_set_default_size (GTK_WINDOW (f->window), 900, 400);
    gtk_window_present (GTK_WINDOW (f->window));
    if (native != NULL) { settle_native (f); }
    else { settle (f); }
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
test_new_entry_focus_during_restore (Fixture *f, gconstpointer unused)
{
    NautilusFilesView *view = nautilus_miller_view_get_active_view (f->strip);
    gtk_widget_grab_focus (GTK_WIDGET (view)); settle (f);
    g_autoptr (GVariant) state = nautilus_miller_view_capture_state (f->strip, FALSE);
    nautilus_miller_view_restore_state (f->strip, state, TRUE);
    gtk_widget_grab_focus (f->entry);
    settle (f);
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
static void
test_native_history_positions (Fixture *f, gconstpointer unused)
{
    NautilusFilesView *view = nautilus_window_slot_get_current_view (f->slot);
    gtk_widget_grab_focus (GTK_WIDGET (view));
    select_file (view, f->one); settle_native (f);
    gtk_adjustment_set_value (vertical (view), 280);
    gtk_adjustment_set_value (horizontal (f), 200); pump (100);
    double v = gtk_adjustment_get_value (vertical (view)), h = gtk_adjustment_get_value (horizontal (f));
    nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), f->other, 0, NULL); settle_native (f);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.back", NULL)); settle_native (f);
    view = nautilus_window_slot_get_current_view (f->slot);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (view), f->child));
    assert_selection (view, f->one);
    g_assert_cmpfloat_with_epsilon (gtk_adjustment_get_value (vertical (view)), v, 2);
    g_assert_cmpfloat_with_epsilon (gtk_adjustment_get_value (horizontal (f)), h, 2);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.forward", NULL)); settle_native (f);
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->other));
}
static void
test_native_history_parent (Fixture *f, gconstpointer unused)
{
    NautilusFilesView *parent = find_view (GTK_WIDGET (f->strip), f->root);
    gtk_widget_grab_focus (GTK_WIDGET (parent)); settle_native (f);
    g_assert_true (nautilus_window_slot_get_current_view (f->slot) == parent);
    guint history = g_list_length (nautilus_window_slot_get_back_history (f->slot));
    g_settings_set_uint (nautilus_preferences, "miller-default-column-width", 300);
    gtk_widget_grab_focus (GTK_WIDGET (nautilus_miller_view_get_active_view (f->strip))); settle_native (f);
    g_assert_cmpuint (g_list_length (nautilus_window_slot_get_back_history (f->slot)), ==, history);
    nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), f->other, 0, NULL); settle_native (f);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.back", NULL)); settle_native (f);
    parent = nautilus_window_slot_get_current_view (f->slot);
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->child));
    g_assert_true (g_file_equal (nautilus_files_view_get_location (parent), f->root));
    assert_selection (parent, f->child);
    g_assert_cmpuint (g_list_length (nautilus_window_slot_get_back_history (f->slot)), ==, history);
    g_settings_reset (nautilus_preferences, "miller-default-column-width");
}
static void
test_native_closed_tab (Fixture *f, gconstpointer unused)
{
    NautilusFilesView *parent = find_view (GTK_WIDGET (f->strip), f->root);
    gtk_widget_grab_focus (GTK_WIDGET (parent)); settle_native (f);
    nautilus_window_new_tab (NAUTILUS_WINDOW (f->window)); pump (150);
    g_object_set (f->window, "active-slot", f->slot, NULL); pump (50);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (
        nautilus_window_slot_get_current_view (f->slot)), f->root));
    g_assert_true (gtk_widget_activate_action (f->window, "win.close-current-view", NULL)); pump (100);
    g_assert_true (gtk_widget_activate_action (f->window, "win.restore-tab", NULL)); pump (100);
    AdwTabView *tabs = ADW_TAB_VIEW (find_widget_type (f->window, ADW_TYPE_TAB_VIEW));
    g_assert_cmpint (adw_tab_view_get_n_pages (tabs), ==, 2);
    adw_tab_view_set_selected_page (tabs, adw_tab_view_get_nth_page (tabs, 1));
    g_clear_object (&f->slot);
    g_object_get (f->window, "active-slot", &f->slot, NULL); settle_native (f);
    parent = nautilus_window_slot_get_current_view (f->slot);
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->child));
    g_assert_true (g_file_equal (nautilus_files_view_get_location (parent), f->root));
    assert_selection (parent, f->child);
}

static void
test_native_mode_context (Fixture *f, gconstpointer unused)
{
    NautilusFilesView *parent = find_view (GTK_WIDGET (f->strip), f->root);
    gtk_widget_grab_focus (GTK_WIDGET (parent)); settle_native (f);
    guint history = g_list_length (nautilus_window_slot_get_back_history (f->slot));
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.files-view-mode", "u", 2));
    settle_native (f);
    NautilusFilesView *view = nautilus_window_slot_get_current_view (f->slot);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (view), f->child));
    g_autolist (NautilusFile) selection = nautilus_files_view_get_selection (view);
    g_assert_null (selection);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.files-view-mode", "u", 4));
    settle_native (f);
    parent = nautilus_window_slot_get_current_view (f->slot);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (parent), f->root));
    assert_selection (parent, f->child);
    g_assert_cmpuint (g_list_length (nautilus_window_slot_get_back_history (f->slot)), ==, history);
}

static void
test_native_history_in_flat_mode (Fixture *f, gconstpointer unused)
{
    NautilusFilesView *parent = find_view (GTK_WIDGET (f->strip), f->root);
    gtk_widget_grab_focus (GTK_WIDGET (parent)); settle_native (f);
    nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), f->other, 0, NULL); settle_native (f);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.files-view-mode", "u", 2));
    settle_native (f);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.back", NULL)); settle_native (f);
    g_assert_null (f->strip);
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->child));
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.files-view-mode", "u", 4));
    settle_native (f);
    parent = nautilus_window_slot_get_current_view (f->slot);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (parent), f->root));
    assert_selection (parent, f->child);
}

static void
test_native_flat_selection_history (Fixture *f, gconstpointer unused)
{
    NautilusFilesView *parent = find_view (GTK_WIDGET (f->strip), f->root);
    gtk_widget_grab_focus (GTK_WIDGET (parent)); settle_native (f);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.files-view-mode", "u", 2));
    settle_native (f);
    select_file (nautilus_window_slot_get_current_view (f->slot), f->two); settle_native (f);
    nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), f->other, 0, NULL); settle_native (f);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.back", NULL)); settle_native (f);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.files-view-mode", "u", 4));
    settle_native (f);
    NautilusFilesView *view = nautilus_window_slot_get_current_view (f->slot);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (view), f->child));
    assert_selection (view, f->two);
}

static void
test_native_immediate_mode_selection (Fixture *f, gconstpointer unused)
{
    NautilusFilesView *parent = find_view (GTK_WIDGET (f->strip), f->root);
    gtk_widget_grab_focus (GTK_WIDGET (parent)); settle_native (f);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.files-view-mode", "u", 2));
    settle_native (f);
    select_file (nautilus_window_slot_get_current_view (f->slot), f->two);
    /* The native FilesView selection notification is coalesced in an idle. */
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.files-view-mode", "u", 4));
    settle_native (f);
    NautilusFilesView *view = nautilus_window_slot_get_current_view (f->slot);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (view), f->child));
    assert_selection (view, f->two);
}

static void
test_native_mode_new_selection (Fixture *f, gconstpointer unused)
{
    NautilusFilesView *parent = find_view (GTK_WIDGET (f->strip), f->root);
    gtk_widget_grab_focus (GTK_WIDGET (parent)); settle_native (f);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.files-view-mode", "u", 2));
    settle_native (f);
    select_file (nautilus_window_slot_get_current_view (f->slot), f->two); settle_native (f);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.files-view-mode", "u", 4));
    settle_native (f);
    NautilusFilesView *view = nautilus_window_slot_get_current_view (f->slot);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (view), f->child));
    assert_selection (view, f->two);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.files-view-mode", "u", 1));
    settle_native (f);
    nautilus_files_view_set_selection (nautilus_window_slot_get_current_view (f->slot), NULL, FALSE);
    settle_native (f);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.files-view-mode", "u", 4));
    settle_native (f);
    view = nautilus_window_slot_get_current_view (f->slot);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (view), f->child));
    g_autolist (NautilusFile) selection = nautilus_files_view_get_selection (view);
    g_assert_null (selection);
}

static void
test_native_disclosure_history (Fixture *f, gconstpointer unused)
{
    NautilusFilesView *parent = find_view (GTK_WIDGET (f->strip), f->root);
    gtk_widget_grab_focus (GTK_WIDGET (parent)); settle_native (f);
    guint history = g_list_length (nautilus_window_slot_get_back_history (f->slot));
    select_file (parent, f->other); settle_native (f);
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->other));
    g_assert_cmpuint (g_list_length (nautilus_window_slot_get_back_history (f->slot)), ==, history + 1);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.back", NULL)); settle_native (f);
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->child));
    parent = nautilus_window_slot_get_current_view (f->slot);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (parent), f->root));
    assert_selection (parent, f->child);
    g_assert_cmpuint (g_list_length (nautilus_window_slot_get_back_history (f->slot)), ==, history);
}

static void
test_native_search_cancel (Fixture *f, gconstpointer unused)
{
    NautilusFilesView *parent = find_view (GTK_WIDGET (f->strip), f->root);
    gtk_widget_grab_focus (GTK_WIDGET (parent)); settle_native (f);
    guint history = g_list_length (nautilus_window_slot_get_back_history (f->slot));
    g_autoptr (NautilusQuery) query = nautilus_query_new ();
    nautilus_query_set_text (query, "row");
    nautilus_query_set_location (query, f->child);
    nautilus_window_slot_search (f->slot, query); settle_native (f);
    g_assert_null (f->strip);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.search-visible", NULL));
    settle_native (f);
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->child));
    parent = nautilus_window_slot_get_current_view (f->slot);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (parent), f->root));
    assert_selection (parent, f->child);
    g_assert_cmpuint (g_list_length (nautilus_window_slot_get_back_history (f->slot)), ==, history);
}

static void
test_native_search_selected_result (Fixture *f, gconstpointer unused)
{
    NautilusFilesView *parent = find_view (GTK_WIDGET (f->strip), f->root);
    gtk_widget_grab_focus (GTK_WIDGET (parent)); settle_native (f);
    g_autoptr (NautilusQuery) query = nautilus_query_new ();
    nautilus_query_set_text (query, "row");
    nautilus_query_set_location (query, f->child);
    nautilus_window_slot_search (f->slot, query); settle_native (f);
    NautilusFilesView *view = nautilus_window_slot_get_current_view (f->slot);
    select_file (view, f->two); settle_native (f);
    assert_selection (view, f->two);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.search-visible", NULL));
    settle_native (f);
    view = nautilus_window_slot_get_current_view (f->slot);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (view), f->child));
    assert_selection (view, f->two);
}

static void
test_native_explicit_selection_and_removed (Fixture *f, gconstpointer unused)
{
    NautilusFilesView *view = nautilus_window_slot_get_current_view (f->slot);
    select_file (view, f->one); settle_native (f);
    nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), f->other, 0, NULL); settle_native (f);
    g_assert_true (g_file_delete (f->one, NULL, NULL));
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.back", NULL)); settle_native (f);
    view = nautilus_window_slot_get_current_view (f->slot);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (view), f->child));
    g_autolist (NautilusFile) selection = nautilus_files_view_get_selection (view);
    g_assert_null (selection);
    NautilusFilesView *parent = find_view (GTK_WIDGET (f->strip), f->root);
    gtk_widget_grab_focus (GTK_WIDGET (parent)); settle_native (f);
    nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), f->other, 0, NULL); settle_native (f);
    g_autoptr (NautilusFile) two = nautilus_file_get (f->two);
    GList explicit = { .data = two };
    nautilus_window_slot_open_location_full (f->slot, f->child, &explicit); settle_native (f);
    view = nautilus_window_slot_get_current_view (f->slot);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (view), f->child));
    assert_selection (view, f->two);
}

static void
test_native_renamed_history_endpoint (Fixture *f, gconstpointer unused)
{
    nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), f->other, 0, NULL); settle_native (f);
    NautilusBookmark *bookmark = nautilus_window_slot_get_back_history (f->slot)->data;
    g_assert_true (g_file_equal (nautilus_bookmark_get_location (bookmark), f->child));
    g_autoptr (GFile) renamed = g_file_get_child (f->root, "renamed");
    g_assert_true (g_file_move (f->child, renamed, 0, NULL, NULL, NULL, NULL));
    gint64 deadline = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
    while (!g_file_equal (nautilus_bookmark_get_location (bookmark), renamed))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline);
        pump (10);
    }
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.back", NULL));
    deadline = g_get_monotonic_time () + 5 * G_TIME_SPAN_SECOND;
    while (!g_file_equal (nautilus_window_slot_get_location (f->slot), renamed))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline);
        pump (10);
    }
    g_assert_true (g_file_equal (nautilus_miller_view_get_location (f->strip), renamed));
    settle_native (f);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (
        nautilus_window_slot_get_current_view (f->slot)), renamed));
}

static void
test_native_copied_history (Fixture *f, gconstpointer unused)
{
    nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), f->other, 0, NULL); settle_native (f);
    gtk_widget_grab_focus (GTK_WIDGET (find_view (GTK_WIDGET (f->strip), f->root))); settle_native (f);
    nautilus_window_back_or_forward_in_new_tab (NAUTILUS_WINDOW (f->window), NAUTILUS_NAVIGATION_DIRECTION_BACK);
    pump (150);
    AdwTabView *tabs = ADW_TAB_VIEW (find_widget_type (f->window, ADW_TYPE_TAB_VIEW));
    g_assert_cmpint (adw_tab_view_get_n_pages (tabs), ==, 2);
    NautilusWindowSlot *copy = NAUTILUS_WINDOW_SLOT (adw_tab_page_get_child (adw_tab_view_get_nth_page (tabs, 1)));
    g_assert_true (copy != f->slot);
    /* Change the original tab's Other state after the history copy. */
    gtk_widget_grab_focus (GTK_WIDGET (find_view (GTK_WIDGET (f->strip), f->other))); settle_native (f);
    nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), f->child, 0, NULL); settle_native (f);
    adw_tab_view_set_selected_page (tabs, adw_tab_view_get_nth_page (tabs, 1));
    g_set_object (&f->slot, copy); settle_native (f);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.forward", NULL)); settle_native (f);
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->other));
    NautilusFilesView *parent = nautilus_window_slot_get_current_view (f->slot);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (parent), f->root));
    assert_selection (parent, f->other);
}

static void
key (guint value, gboolean pressed)
{
    g_autoptr (GError) error = NULL;
    g_assert_true (miller_test_input_key (input, value, pressed, &error));
    g_assert_no_error (error);
    pump (30);
}

static void
test_native_history_keys_entry (Fixture *f, gconstpointer unused)
{
    if (input == NULL) { g_test_skip ("Requires guarded private compositor input"); return; }
    NautilusFilesView *parent = find_view (GTK_WIDGET (f->strip), f->root);
    gtk_widget_grab_focus (GTK_WIDGET (parent)); settle_native (f);
    nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), f->other, 0, NULL); settle_native (f);
    key (GDK_KEY_Alt_L, TRUE); key (GDK_KEY_Left, TRUE); key (GDK_KEY_Left, FALSE); key (GDK_KEY_Alt_L, FALSE);
    settle_native (f);
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->child));
    parent = nautilus_window_slot_get_current_view (f->slot);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (parent), f->root));
    key (GDK_KEY_Alt_L, TRUE); key (GDK_KEY_Right, TRUE); key (GDK_KEY_Right, FALSE); key (GDK_KEY_Alt_L, FALSE);
    settle_native (f);
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->other));
    key (GDK_KEY_Control_L, TRUE); key (GDK_KEY_l, TRUE); key (GDK_KEY_l, FALSE); key (GDK_KEY_Control_L, FALSE);
    GtkWidget *entry = gtk_root_get_focus (GTK_ROOT (f->window));
    g_assert_true (GTK_IS_EDITABLE (entry));
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.back", NULL)); settle_native (f);
    g_assert_true (gtk_root_get_focus (GTK_ROOT (f->window)) == entry);
    parent = nautilus_window_slot_get_current_view (f->slot);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (parent), f->root));
}

static void
test_native_cached_special_loading (Fixture *f, gconstpointer unused)
{
    /* The dummy tag manager provides an empty, immediately ready Starred
     * model. Finishing it during view replacement must not free navigation. */
    g_autoptr (GFile) starred = g_file_new_for_uri ("starred:///");
    for (guint i = 0; i < 3; i++)
    {
        NautilusFilesView *parent = find_view (GTK_WIDGET (f->strip), f->root);
        gtk_widget_grab_focus (GTK_WIDGET (parent)); settle_native (f);
        nautilus_window_slot_open_location_full (f->slot, starred, NULL); settle_native (f);
        g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), starred));
        g_assert_null (f->strip);
        g_assert_false (nautilus_window_slot_get_loading (f->slot));
        g_assert_false (nautilus_window_slot_get_allow_stop (f->slot));
        g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.back", NULL)); settle_native (f);
        g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->child));
        parent = nautilus_window_slot_get_current_view (f->slot);
        g_assert_true (g_file_equal (nautilus_files_view_get_location (parent), f->root));
        assert_selection (parent, f->child);
    }
}

static GKeyFile *
backend_metadata (void)
{
    const char *runtime = g_getenv ("XDG_RUNTIME_DIR");
    g_autofree char *expected = g_build_filename (runtime, "miller-backend.ini", NULL);
    g_assert_cmpstr (g_getenv ("MILLER_TEST_BACKEND"), ==, expected);
    struct stat info;
    g_assert_cmpint (lstat (expected, &info), ==, 0);
    g_assert_true (S_ISREG (info.st_mode));
    g_assert_cmpuint (info.st_uid, ==, getuid ());
    g_assert_cmpuint (info.st_mode & 0777, ==, 0600);
    GKeyFile *config = g_key_file_new ();
    g_assert_true (g_key_file_load_from_file (config, expected, 0, NULL));
    return config;
}

static void
backend_control (const char *request)
{
    g_autoptr (GKeyFile) config = backend_metadata ();
    g_autofree char *control = g_key_file_get_string (config, "backend", "control", NULL);
    g_autofree char *expected = g_build_filename (g_getenv ("XDG_RUNTIME_DIR"), "miller-backend.sock", NULL);
    g_assert_cmpstr (control, ==, expected);
    struct stat info;
    g_assert_cmpint (lstat (control, &info), ==, 0);
    g_assert_true (S_ISSOCK (info.st_mode));
    g_assert_cmpuint (info.st_uid, ==, getuid ());
    g_assert_cmpuint (info.st_mode & 0777, ==, 0600);
    g_autoptr (GSocketClient) client = g_socket_client_new ();
    g_socket_client_set_timeout (client, 5);
    g_autoptr (GSocketAddress) address = g_unix_socket_address_new (control);
    g_autoptr (GError) error = NULL;
    g_autoptr (GSocketConnection) connection = g_socket_client_connect (client, G_SOCKET_CONNECTABLE (address), NULL, &error);
    g_assert_no_error (error);
    g_autofree char *line = g_strconcat (request, "\n", NULL);
    g_assert_true (g_output_stream_write_all (g_io_stream_get_output_stream (G_IO_STREAM (connection)),
                                             line, strlen (line), NULL, NULL, &error));
    g_assert_no_error (error);
    g_autoptr (GDataInputStream) reply = g_data_input_stream_new (g_io_stream_get_input_stream (G_IO_STREAM (connection)));
    g_autofree char *result = g_data_input_stream_read_line (reply, NULL, NULL, &error);
    g_assert_no_error (error);
    g_assert_cmpstr (result, ==, "ok");
}

static GFile *
open_backend (Fixture *f)
{
    if (input == NULL || g_getenv ("MILLER_TEST_BACKEND") == NULL)
    {
        g_test_skip ("Requires with-delayed-backend.py in the guarded private compositor");
        return NULL;
    }
    g_autoptr (GKeyFile) config = backend_metadata ();
    g_autofree char *uri = g_key_file_get_string (config, "backend", "root-uri", NULL);
    GFile *root = g_file_new_for_uri (uri);
    g_assert_true (g_file_has_uri_scheme (root, "archive"));
    nautilus_window_slot_open_location_full (f->slot, root, NULL); settle_native (f);
    gtk_widget_grab_focus (GTK_WIDGET (nautilus_window_slot_get_current_view (f->slot))); pump (100);
    return root;
}

static void
test_delayed_branch (Fixture *f, gconstpointer unused)
{
    g_autoptr (GFile) root = open_backend (f);
    if (root == NULL) { return; }
    NautilusFilesView *parent = nautilus_window_slot_get_current_view (f->slot);
    g_autoptr (GFile) a = g_file_get_child (root, "A");
    g_autoptr (GFile) b = g_file_get_child (root, "B");
    backend_control ("pause");
    key (GDK_KEY_a, TRUE); key (GDK_KEY_a, FALSE); pump (150);
    assert_selection (parent, a);
    NautilusFilesView *old = find_view (GTK_WIDGET (f->strip), a);
    g_assert_nonnull (old);
    g_assert_true (nautilus_files_view_get_loading (old));
    g_object_add_weak_pointer (G_OBJECT (old), (gpointer *) &old);
    key (GDK_KEY_Escape, TRUE); key (GDK_KEY_Escape, FALSE);
    key (GDK_KEY_b, TRUE); key (GDK_KEY_b, FALSE); pump (100);
    assert_selection (parent, b);
    g_assert_true (g_file_equal (nautilus_miller_view_get_location (f->strip), b));
    g_assert_null (find_view (GTK_WIDGET (f->strip), a));
    backend_control ("resume"); settle_native (f);
    g_assert_null (old);
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), b));
    g_assert_true (nautilus_window_slot_get_current_view (f->slot) == parent);
    assert_selection (parent, b);
    GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (f->window));
    g_assert_true (focus != NULL && gtk_widget_is_ancestor (focus, GTK_WIDGET (parent)));
    g_assert_null (find_view (GTK_WIDGET (f->strip), a));
    key (GDK_KEY_Right, TRUE); key (GDK_KEY_Right, FALSE); settle_native (f);
    NautilusFilesView *child = nautilus_window_slot_get_current_view (f->slot);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (child), b));
    g_assert_cmpuint (g_list_model_get_n_items (G_LIST_MODEL (nautilus_files_view_get_private_model (child))), ==, 1);
    key (GDK_KEY_Alt_L, TRUE); key (GDK_KEY_Left, TRUE); key (GDK_KEY_Left, FALSE); key (GDK_KEY_Alt_L, FALSE);
    settle_native (f);
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), root));
}

static void
test_delayed_monitor_creation (Fixture *f, gconstpointer unused)
{
    g_autoptr (GFile) root = open_backend (f);
    if (root == NULL) { return; }
    g_autoptr (GFile) a = g_file_get_child (root, "A");
    backend_control ("pause");
    gint64 start = g_get_monotonic_time ();
    NautilusMonitor *monitor = nautilus_monitor_directory (a);
    gint64 elapsed = g_get_monotonic_time () - start;
    nautilus_monitor_cancel (monitor);
    backend_control ("resume");
    g_assert_cmpint (elapsed, <, 250 * G_TIME_SPAN_MILLISECOND);
    pump (150);
}

static void
test_delayed_navigation (Fixture *f, gconstpointer unused)
{
    g_autoptr (GFile) root = open_backend (f);
    if (root == NULL) { return; }
    g_autoptr (GFile) a = g_file_get_child (root, "A");
    backend_control ("pause");
    key (GDK_KEY_a, TRUE); key (GDK_KEY_a, FALSE); pump (100);
    NautilusFilesView *old = find_view (GTK_WIDGET (f->strip), a);
    g_assert_nonnull (old);
    g_assert_true (nautilus_files_view_get_loading (old));
    g_object_add_weak_pointer (G_OBJECT (old), (gpointer *) &old);
    nautilus_window_slot_open_location_full (f->slot, f->other, NULL);
    backend_control ("resume"); settle_native (f);
    g_assert_null (old);
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->other));
    g_assert_true (g_file_equal (nautilus_files_view_get_location (
        nautilus_window_slot_get_current_view (f->slot)), f->other));
    g_assert_null (find_view (GTK_WIDGET (f->strip), a));
}

static void
test_delayed_close (Fixture *f, gconstpointer unused)
{
    if (input == NULL || g_getenv ("MILLER_TEST_BACKEND") == NULL)
    {
        g_test_skip ("Requires with-delayed-backend.py in the guarded private compositor");
        return;
    }
    nautilus_window_new_tab (NAUTILUS_WINDOW (f->window)); pump (100);
    nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), f->other, 0, NULL); pump (100);
    g_autoptr (NautilusWindowSlot) survivor = NULL;
    g_object_get (f->window, "active-slot", &survivor, NULL);
    g_assert_true (survivor != f->slot);
    g_object_set (f->window, "active-slot", f->slot, NULL); pump (100);
    g_autoptr (GFile) root = open_backend (f);
    g_autoptr (GFile) a = g_file_get_child (root, "A");
    backend_control ("pause");
    key (GDK_KEY_a, TRUE); key (GDK_KEY_a, FALSE); pump (100);
    g_assert_true (nautilus_files_view_get_loading (find_view (GTK_WIDGET (f->strip), a)));
    NautilusWindowSlot *closed = f->slot;
    g_object_add_weak_pointer (G_OBJECT (closed), (gpointer *) &closed);
    g_clear_object (&f->strip);
    key (GDK_KEY_Control_L, TRUE); key (GDK_KEY_w, TRUE); key (GDK_KEY_w, FALSE); key (GDK_KEY_Control_L, FALSE);
    g_set_object (&f->slot, survivor);
    backend_control ("resume"); settle_native (f);
    g_assert_null (closed);
    g_assert_true (nautilus_window_slot_get_active (survivor));
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (survivor), f->other));
    g_assert_true (g_object_get_data (G_OBJECT (f->window), "nautilus-active-file-view") ==
                   nautilus_window_slot_get_current_view (survivor));
    g_assert_null (find_view (GTK_WIDGET (f->strip), a));
}

static void
skip_navigation (void)
{
    g_test_skip ("Requires with-test-home.py inside the guarded private compositor");
}

static void
navigation_ready (void)
{
    const char *masked = g_getenv ("MILLER_TEST_HOME");
    g_assert_nonnull (input);
    g_assert_nonnull (masked);
    g_autofree char *base = g_path_get_basename (masked);
    g_autofree char *parent = g_path_get_dirname (masked);
    g_autofree char *parent_base = g_path_get_basename (parent);
    g_assert_true (g_str_has_prefix (base, "navigation-home-"));
    g_assert_true (g_str_has_prefix (parent_base, "nautilus-miller-tests-"));
    struct stat home_info, masked_info, parent_info;
    g_assert_cmpint (lstat (parent, &parent_info), ==, 0);
    g_assert_true (S_ISDIR (parent_info.st_mode));
    g_assert_cmpuint (parent_info.st_uid, ==, getuid ());
    g_assert_cmpuint (parent_info.st_mode & 0777, ==, 0700);
    g_assert_cmpint (stat (g_get_home_dir (), &home_info), ==, 0);
    g_assert_cmpint (lstat (masked, &masked_info), ==, 0);
    g_assert_true (S_ISDIR (masked_info.st_mode));
    g_assert_cmpuint (masked_info.st_uid, ==, getuid ());
    g_assert_true (home_info.st_dev == masked_info.st_dev && home_info.st_ino == masked_info.st_ino);
}

static void
assert_native_location (Fixture *f, GFile *location)
{
    g_autofree char *want_name = g_file_get_basename (location);
    g_autofree char *actual_name = g_file_get_basename (nautilus_window_slot_get_location (f->slot));
    g_test_message ("Navigation endpoint: %s (expected %s)", actual_name, want_name);
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), location));
    g_assert_nonnull (f->strip);
    g_assert_true (g_file_equal (nautilus_miller_view_get_location (f->strip), location));
    NautilusFilesView *view = nautilus_window_slot_get_current_view (f->slot);
    g_assert_true (g_object_get_data (G_OBJECT (f->window), "nautilus-active-file-view") == view);
}

static void
shortcut (guint modifier, guint value)
{
    key (modifier, TRUE); key (value, TRUE); key (value, FALSE); key (modifier, FALSE);
}

static void
type_text (const char *text)
{
    for (const char *cursor = text; *cursor != '\0'; cursor = g_utf8_next_char (cursor))
    {
        guint value = gdk_unicode_to_keyval (g_utf8_get_char (cursor));
        g_autoptr (GError) error = NULL;
        g_assert_true (miller_test_input_key (input, value, TRUE, &error));
        g_assert_no_error (error);
        g_assert_true (miller_test_input_key (input, value, FALSE, &error));
        g_assert_no_error (error);
        pump (2);
    }
    pump (100);
}

static void
enter_location (Fixture *f, const char *text, GFile *expected)
{
    shortcut (GDK_KEY_Control_L, GDK_KEY_l);
    GtkWidget *entry = gtk_root_get_focus (GTK_ROOT (f->window));
    g_assert_true (GTK_IS_EDITABLE (entry));
    shortcut (GDK_KEY_Control_L, GDK_KEY_a);
    type_text (text);
    g_autofree char *completed = g_strconcat (text, "/", NULL);
    const char *typed = gtk_editable_get_text (GTK_EDITABLE (entry));
    g_assert_true (g_str_equal (typed, text) || g_str_equal (typed, completed));
    key (GDK_KEY_Return, TRUE); key (GDK_KEY_Return, FALSE); settle_native_location (f, expected);
    assert_native_location (f, expected);
    GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (f->window));
    NautilusFilesView *view = nautilus_window_slot_get_current_view (f->slot);
    g_assert_nonnull (focus);
    g_assert_true (focus == GTK_WIDGET (view) || gtk_widget_is_ancestor (focus, GTK_WIDGET (view)));
}

static void
test_navigation_initial_focus (Fixture *f, gconstpointer unused)
{
    navigation_ready ();
    GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (f->window));
    g_assert_nonnull (focus);
    NautilusFilesView *view = nautilus_window_slot_get_current_view (f->slot);
    g_assert_true (focus == GTK_WIDGET (view) || gtk_widget_is_ancestor (focus, GTK_WIDGET (view)));
    shortcut (GDK_KEY_Control_L, GDK_KEY_l);
    g_assert_true (GTK_IS_EDITABLE (gtk_root_get_focus (GTK_ROOT (f->window))));
    key (GDK_KEY_Escape, TRUE); key (GDK_KEY_Escape, FALSE);
    assert_native_location (f, f->child);
}

static void
test_navigation_entry (Fixture *f, gconstpointer unused)
{
    navigation_ready ();
    gtk_widget_grab_focus (GTK_WIDGET (nautilus_window_slot_get_current_view (f->slot)));
    g_autofree char *path = g_file_get_path (f->other);
    enter_location (f, path, f->other);
    g_autoptr (GFile) encoded = g_file_get_child (f->root, "space % # é");
    g_assert_true (g_file_make_directory (encoded, NULL, NULL));
    g_autofree char *uri = g_file_get_uri (encoded);
    enter_location (f, uri, encoded);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (
        nautilus_window_slot_get_current_view (f->slot)), encoded));
}

static void
test_navigation_mode_keyboard (Fixture *f, gconstpointer unused)
{
    navigation_ready ();
    NautilusFilesView *view = nautilus_window_slot_get_current_view (f->slot);
    gtk_widget_grab_focus (GTK_WIDGET (view)); select_file (view, f->one); settle_native (f);
    const guint keys[] = { GDK_KEY_2, GDK_KEY_1, GDK_KEY_3 };
    const guint modes[] = { 2, 1, 4 };
    for (guint i = 0; i < G_N_ELEMENTS (keys); i++)
    {
        shortcut (GDK_KEY_Control_L, keys[i]); settle_native (f);
        view = nautilus_window_slot_get_current_view (f->slot);
        g_assert_cmpuint (nautilus_files_view_get_view_id (view), ==, modes[i]);
        g_assert_true (g_file_equal (nautilus_files_view_get_location (view), f->child));
        assert_selection (view, f->one);
        GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (f->window));
        g_assert_nonnull (focus);
        g_assert_true (focus == GTK_WIDGET (view) || gtk_widget_is_ancestor (focus, GTK_WIDGET (view)));
    }
}

static void
test_navigation_empty_mode_keyboard (Fixture *f, gconstpointer unused)
{
    navigation_ready ();
    nautilus_window_slot_open_location_full (f->slot, f->other, NULL); settle_native (f);
    gtk_widget_grab_focus (GTK_WIDGET (nautilus_window_slot_get_current_view (f->slot)));
    const guint keys[] = { GDK_KEY_2, GDK_KEY_1, GDK_KEY_3 };
    const guint modes[] = { 2, 1, 4 };
    for (guint i = 0; i < G_N_ELEMENTS (keys); i++)
    {
        shortcut (GDK_KEY_Control_L, keys[i]); settle_native (f);
        NautilusFilesView *view = nautilus_window_slot_get_current_view (f->slot);
        g_assert_cmpuint (nautilus_files_view_get_view_id (view), ==, modes[i]);
        g_assert_true (g_file_equal (nautilus_files_view_get_location (view), f->other));
        GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (f->window));
        g_assert_nonnull (focus);
        g_assert_true (focus == GTK_WIDGET (view) || gtk_widget_is_ancestor (focus, GTK_WIDGET (view)));
    }
}

static GtkWidget *
find_label_text (GtkWidget *widget, const char *text)
{
    if (GTK_IS_LABEL (widget) && g_strcmp0 (gtk_label_get_text (GTK_LABEL (widget)), text) == 0) { return widget; }
    for (GtkWidget *child = gtk_widget_get_first_child (widget); child != NULL; child = gtk_widget_get_next_sibling (child))
    {
        GtkWidget *found = find_label_text (child, text);
        if (found != NULL) { return found; }
    }
    return NULL;
}

static void
click_widget (Fixture *f, GtkWidget *widget)
{
    g_assert_nonnull (widget);
    g_assert_true (gtk_widget_get_mapped (widget));
    graphene_point_t point;
    g_assert_true (gtk_widget_compute_point (widget, f->window,
        &GRAPHENE_POINT_INIT (gtk_widget_get_width (widget) / 2.0, gtk_widget_get_height (widget) / 2.0), &point));
    g_autoptr (GError) error = NULL;
    g_assert_true (miller_test_input_motion (input, -10000, -10000, &error));
    g_assert_no_error (error);
    g_assert_true (miller_test_input_motion (input, point.x, point.y, &error));
    g_assert_no_error (error);
    pump (80);
    GdkDevice *pointer = gdk_seat_get_pointer (gdk_display_get_default_seat (gdk_display_get_default ()));
    double x, y;
    GdkSurface *surface = gdk_device_get_surface_at_position (pointer, &x, &y);
    GdkSurface *expected_surface = gtk_native_get_surface (GTK_NATIVE (f->window));
    for (guint attempt = 0; attempt < 12 && surface == expected_surface &&
         (fabs (x - point.x) > 1 || fabs (y - point.y) > 1); attempt++)
    {
        /* Relative virtual input and compositor window transitions can shift
         * the initial position. Correct using actual private Wayland motion. */
        g_assert_true (miller_test_input_motion (input, (point.x - x) * .75, (point.y - y) * .75, &error));
        g_assert_no_error (error);
        pump (30);
        surface = gdk_device_get_surface_at_position (pointer, &x, &y);
    }
    g_assert_true (surface == expected_surface);
    g_assert_cmpfloat_with_epsilon (x, point.x, 1.5);
    g_assert_cmpfloat_with_epsilon (y, point.y, 1.5);
    g_assert_true (miller_test_input_button (input, 272, TRUE, &error));
    g_assert_no_error (error);
    g_assert_true (miller_test_input_button (input, 272, FALSE, &error));
    g_assert_no_error (error);
    pump (100);
}

static void
test_navigation_breadcrumb (Fixture *f, gconstpointer unused)
{
    navigation_ready ();
    select_file (nautilus_window_slot_get_current_view (f->slot), f->one); settle_native (f);
    gtk_window_fullscreen (GTK_WINDOW (f->window)); pump (200);
    GtkWidget *bar = find_widget_type (f->window, NAUTILUS_TYPE_PATH_BAR);
    g_assert_nonnull (bar);
    g_autofree char *name = g_file_get_basename (f->root);
    GtkWidget *label = find_label_text (bar, name);
    g_assert_nonnull (label);
    GtkWidget *button = gtk_widget_get_ancestor (label, GTK_TYPE_BUTTON);
    click_widget (f, button); settle_native_location (f, f->root);
    assert_native_location (f, f->root);
    assert_selection (nautilus_window_slot_get_current_view (f->slot), f->child);
    GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (f->window));
    g_test_message ("After breadcrumb: focus %s; window active %d; back entries %u",
                    focus != NULL ? G_OBJECT_TYPE_NAME (focus) : "none",
                    gtk_window_is_active (GTK_WINDOW (f->window)),
                    g_list_length (nautilus_window_slot_get_back_history (f->slot)));
    shortcut (GDK_KEY_Alt_L, GDK_KEY_Left); settle_native_location (f, f->child);
    assert_native_location (f, f->child);
    assert_selection (nautilus_window_slot_get_current_view (f->slot), f->one);
}

static GtkWidget *
find_sidebar_location (GtkWidget *widget, GFile *location)
{
    if (NAUTILUS_IS_SIDEBAR_ROW (widget))
    {
        g_autofree char *uri = NULL;
        g_object_get (widget, "uri", &uri, NULL);
        if (uri != NULL)
        {
            g_autoptr (GFile) file = g_file_new_for_uri (uri);
            if (g_file_equal (file, location)) { return widget; }
        }
    }
    for (GtkWidget *child = gtk_widget_get_first_child (widget); child != NULL; child = gtk_widget_get_next_sibling (child))
    {
        GtkWidget *found = find_sidebar_location (child, location);
        if (found != NULL) { return found; }
    }
    return NULL;
}

static void
test_navigation_bookmark (Fixture *f, gconstpointer unused)
{
    navigation_ready ();
    select_file (nautilus_window_slot_get_current_view (f->slot), f->one); settle_native (f);
    NautilusBookmarkList *bookmarks = nautilus_application_get_bookmarks (nautilus_application_get_default ());
    nautilus_bookmark_list_add (bookmarks, f->other, -1);
    gtk_window_fullscreen (GTK_WINDOW (f->window)); pump (250);
    GtkWidget *sidebar = find_widget_type (f->window, NAUTILUS_TYPE_PLACES_SIDEBAR);
    g_assert_nonnull (sidebar);
    click_widget (f, find_sidebar_location (sidebar, f->other)); settle_native_location (f, f->other);
    assert_native_location (f, f->other);
    shortcut (GDK_KEY_Alt_L, GDK_KEY_Left); settle_native_location (f, f->child);
    assert_native_location (f, f->child);
    assert_selection (nautilus_window_slot_get_current_view (f->slot), f->one);
    nautilus_bookmark_list_remove (bookmarks, f->other);
}

static void
test_navigation_home (Fixture *f, gconstpointer unused)
{
    navigation_ready ();
    NautilusFilesView *parent = find_view (GTK_WIDGET (f->strip), f->root);
    gtk_widget_grab_focus (GTK_WIDGET (parent)); settle_native (f);
    g_autoptr (GFile) home = g_file_new_for_path (g_get_home_dir ());
    shortcut (GDK_KEY_Alt_L, GDK_KEY_Home); settle_native_location (f, home);
    assert_native_location (f, home);
    g_autoptr (GFile) fixture = g_file_get_child (home, "Home fixture");
    g_assert_true (g_file_query_exists (fixture, NULL));
    select_file (nautilus_window_slot_get_current_view (f->slot), fixture); settle_native_location (f, fixture);
    assert_native_location (f, fixture);
    shortcut (GDK_KEY_Alt_L, GDK_KEY_Left); settle_native_location (f, home);
    assert_native_location (f, home);
    shortcut (GDK_KEY_Alt_L, GDK_KEY_Left); settle_native_location (f, f->child);
    assert_native_location (f, f->child);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (
        nautilus_window_slot_get_current_view (f->slot)), f->root));
}

static void
test_navigation_tab_window (Fixture *f, gconstpointer unused)
{
    navigation_ready ();
    NautilusFilesView *parent = find_view (GTK_WIDGET (f->strip), f->root);
    gtk_widget_grab_focus (GTK_WIDGET (parent)); select_file (parent, f->other); settle_native (f);
    shortcut (GDK_KEY_Control_L, GDK_KEY_Return); pump (150);
    AdwTabView *tabs = ADW_TAB_VIEW (find_widget_type (f->window, ADW_TYPE_TAB_VIEW));
    g_assert_cmpint (adw_tab_view_get_n_pages (tabs), ==, 2);
    g_autoptr (NautilusWindowSlot) active = NULL;
    g_object_get (f->window, "active-slot", &active, NULL);
    g_assert_true (active == f->slot);
    Fixture background = { .window = f->window, .slot = NAUTILUS_WINDOW_SLOT (
        adw_tab_page_get_child (adw_tab_view_get_nth_page (tabs, 1))) };
    settle_native_location (&background, f->other);
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (background.slot), f->other));
    g_assert_true (g_file_equal (nautilus_files_view_get_location (
        nautilus_window_slot_get_current_view (background.slot)), f->other));
    g_clear_object (&background.strip);
    g_assert_true (nautilus_window_slot_get_current_view (f->slot) == parent);
    shortcut (GDK_KEY_Shift_L, GDK_KEY_Return); pump (200);
    GList *windows = nautilus_application_get_windows (nautilus_application_get_default ());
    g_assert_cmpuint (g_list_length (windows), ==, 2);
    GtkWidget *created = windows->data == f->window ? windows->next->data : windows->data;
    Fixture separate = { .window = created };
    g_object_get (created, "active-slot", &separate.slot, NULL); settle_native_location (&separate, f->other);
    assert_native_location (&separate, f->other);
    g_clear_object (&separate.strip); g_clear_object (&separate.slot);
    gtk_window_destroy (GTK_WINDOW (created)); pump (100);
    g_assert_true (nautilus_window_slot_get_current_view (f->slot) == parent);
    assert_selection (parent, f->other);
}

static void
test_navigation_specials (Fixture *f, gconstpointer unused)
{
    navigation_ready ();
    const char *uris[] = { "recent:///", "starred:///", "trash:///", "x-network-view:///" };
    for (guint i = 0; i < G_N_ELEMENTS (uris); i++)
    {
        NautilusFilesView *parent = find_view (GTK_WIDGET (f->strip), f->root);
        gtk_widget_grab_focus (GTK_WIDGET (parent)); settle_native (f);
        g_autoptr (GFile) location = g_file_new_for_uri (uris[i]);
        g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.open-location", "s", uris[i]));
        settle_native_location (f, location);
        g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), location));
        NautilusFilesView *special = nautilus_window_slot_get_current_view (f->slot);
        g_test_message ("Special %s: native view %u; cached strip %p; actual strip %p", uris[i],
            nautilus_files_view_get_view_id (special), (void *) f->strip,
            (void *) gtk_widget_get_ancestor (GTK_WIDGET (special), NAUTILUS_TYPE_MILLER_VIEW));
        g_assert_null (f->strip);
        g_assert_cmpuint (g_settings_get_enum (nautilus_preferences, NAUTILUS_PREFERENCES_DEFAULT_FOLDER_VIEWER), ==, 4);
        g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.files-view-mode", "u", 4));
        settle_native (f);
        g_assert_null (f->strip);
        g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.back", NULL)); settle_native_location (f, f->child);
        assert_native_location (f, f->child);
        parent = nautilus_window_slot_get_current_view (f->slot);
        g_assert_true (g_file_equal (nautilus_files_view_get_location (parent), f->root));
        assert_selection (parent, f->child);
    }
}

static void
test_navigation_mount (Fixture *f, gconstpointer unused)
{
    navigation_ready ();
    if (g_getenv ("MILLER_TEST_BACKEND") == NULL)
    {
        g_test_skip ("Requires the actual private GVfs mount fixture too");
        return;
    }
    g_autoptr (GKeyFile) config = backend_metadata ();
    g_autofree char *uri = g_key_file_get_string (config, "backend", "root-uri", NULL);
    g_autoptr (GFile) root = g_file_new_for_uri (uri);
    g_autoptr (GVolumeMonitor) monitor = g_volume_monitor_get ();
    g_autolist (GMount) mounts = g_volume_monitor_get_mounts (monitor);
    gboolean found = FALSE;
    for (GList *item = mounts; item != NULL; item = item->next)
    {
        g_autoptr (GFile) mounted = g_mount_get_root (item->data);
        if (g_file_equal (mounted, root)) { found = TRUE; }
    }
    g_assert_true (found);
    enter_location (f, uri, root);
    NautilusFilesView *view = nautilus_window_slot_get_current_view (f->slot);
    g_assert_cmpuint (g_list_model_get_n_items (G_LIST_MODEL (nautilus_files_view_get_private_model (view))), ==, 2);
    shortcut (GDK_KEY_Alt_L, GDK_KEY_Left); settle_native_location (f, f->child);
    assert_native_location (f, f->child);
}

static void
test_native_pending_background_capture (Fixture *f, gconstpointer unused)
{
    NautilusFilesView *parent = find_view (GTK_WIDGET (f->strip), f->root);
    gtk_widget_grab_focus (GTK_WIDGET (parent)); settle_native (f);
    nautilus_window_new_tab (NAUTILUS_WINDOW (f->window)); pump (150);
    g_object_set (f->window, "active-slot", f->slot, NULL); pump (50);
    double expected_horizontal = gtk_adjustment_get_value (horizontal (f));
    g_assert_true (gtk_widget_activate_action (f->window, "win.close-current-view", NULL)); pump (100);
    for (guint i = 0; i < 2; i++)
    {
        g_assert_true (gtk_widget_activate_action (f->window, "win.restore-tab", NULL));
        AdwTabView *tabs = ADW_TAB_VIEW (find_widget_type (f->window, ADW_TYPE_TAB_VIEW));
        g_assert_cmpint (adw_tab_view_get_n_pages (tabs), ==, 2);
        AdwTabPage *page = adw_tab_view_get_nth_page (tabs, 1);
        NautilusWindowSlot *restored = NAUTILUS_WINDOW_SLOT (adw_tab_page_get_child (page));
        Fixture background = { .window = f->window, .slot = restored };
        settle_native_location (&background, f->child);
        g_assert_false (nautilus_window_slot_get_active (restored));
        NautilusNavigationState *saved = nautilus_window_slot_get_navigation_state (restored);
        GVariant *value = g_object_get_data (G_OBJECT (saved->current_location_bookmark), "nautilus-miller-history-state");
        g_assert_nonnull (value);
        const char *active;
        double position;
        g_variant_get_child (value, 1, "&s", &active);
        g_variant_get_child (value, 2, "d", &position);
        g_autofree char *expected = g_file_get_uri (f->root);
        g_assert_cmpstr (active, ==, expected);
        g_assert_cmpfloat_with_epsilon (position, expected_horizontal, 2);
        free_navigation_state (saved);
        if (i == 0)
        {
            adw_tab_view_close_page (tabs, page); pump (150);
            g_assert_cmpint (adw_tab_view_get_n_pages (tabs), ==, 1);
        }
        else
        {
            adw_tab_view_set_selected_page (tabs, page); settle_native (&background);
            parent = nautilus_window_slot_get_current_view (restored);
            g_assert_true (g_file_equal (nautilus_files_view_get_location (parent), f->root));
            assert_selection (parent, f->child);
            g_assert_true (g_object_get_data (G_OBJECT (f->window), "nautilus-active-file-view") == parent);
        }
        g_clear_object (&background.strip);
    }
}

static void
test_native_loading_flat_selection (Fixture *f, gconstpointer unused)
{
    const guint modes[] = { 2, 1 };
    for (guint i = 0; i < G_N_ELEMENTS (modes); i++)
    {
        NautilusFilesView *parent = find_view (GTK_WIDGET (f->strip), f->root);
        gtk_widget_grab_focus (GTK_WIDGET (parent)); settle_native (f);
        g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.files-view-mode", "u", modes[i]));
        NautilusFilesView *view = nautilus_window_slot_get_current_view (f->slot);
        g_assert_true (nautilus_files_view_get_loading (view));
        select_file (view, f->two); settle_native (f);
        assert_selection (view, f->two);
        g_assert_false (nautilus_files_view_is_selection_auto (view));
        g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.files-view-mode", "u", 4));
        settle_native (f);
        view = nautilus_window_slot_get_current_view (f->slot);
        g_assert_true (g_file_equal (nautilus_files_view_get_location (view), f->child));
        assert_selection (view, f->two);
    }
}


typedef struct
{
    GFile *target;
    gboolean selected;
} LoadingSelection;

static void
select_loaded_row (NautilusFilesView *view, NautilusFileList *files, LoadingSelection *selection)
{
    if (selection->selected) { return; }
    GtkSelectionModel *model = GTK_SELECTION_MODEL (nautilus_files_view_get_private_model (view));
    for (guint i = 0; i < g_list_model_get_n_items (G_LIST_MODEL (model)); i++)
    {
        g_autoptr (GtkTreeListRow) row = g_list_model_get_item (G_LIST_MODEL (model), i);
        g_autoptr (NautilusViewItem) item = gtk_tree_list_row_get_item (row);
        g_autoptr (GFile) location = nautilus_file_get_location (nautilus_view_item_get_file (item));
        if (g_file_equal (location, selection->target))
        {
            g_assert_true (nautilus_files_view_get_loading (view));
            g_assert_true (gtk_selection_model_select_item (model, i, TRUE));
            g_assert_false (nautilus_files_view_is_selection_auto (view));
            selection->selected = TRUE;
            return;
        }
    }
}

static void
test_native_loading_model_selection (Fixture *f, gconstpointer unused)
{
    NautilusFilesView *parent = find_view (GTK_WIDGET (f->strip), f->root);
    gtk_widget_grab_focus (GTK_WIDGET (parent)); settle_native (f);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.files-view-mode", "u", 2));
    NautilusFilesView *view = nautilus_window_slot_get_current_view (f->slot);
    LoadingSelection selection = { .target = f->two };
    g_signal_connect_after (view, "add-files", G_CALLBACK (select_loaded_row), &selection);
    settle_native (f);
    g_signal_handlers_disconnect_by_data (view, &selection);
    g_assert_true (selection.selected);
    assert_selection (view, f->two);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.files-view-mode", "u", 4));
    settle_native (f);
    view = nautilus_window_slot_get_current_view (f->slot);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (view), f->child));
    assert_selection (view, f->two);
}

static void
test_native_automatic_model_clear (Fixture *f, gconstpointer unused)
{
    NautilusFilesView *parent = find_view (GTK_WIDGET (f->strip), f->root);
    gtk_widget_grab_focus (GTK_WIDGET (parent)); settle_native (f);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.files-view-mode", "u", 2));
    settle_native (f);
    NautilusFilesView *view = nautilus_window_slot_get_current_view (f->slot);
    g_autoptr (NautilusFile) file = nautilus_file_get (f->two);
    GList automatic = { .data = file };
    nautilus_files_view_set_selection (view, &automatic, TRUE);
    assert_selection (view, f->two);
    g_assert_true (nautilus_files_view_is_selection_auto (view));
    /* Exercise the real reload clear without a slot commit replacing its
     * bookmark afterward and hiding the model's selection provenance. */
    nautilus_files_view_set_location (view, f->child); settle_native (f);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.files-view-mode", "u", 4));
    settle_native (f);
    view = nautilus_window_slot_get_current_view (f->slot);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (view), f->root));
    assert_selection (view, f->child);
}


static void
test_pending_same_view_focus (Fixture *f, gconstpointer unused)
{
    NautilusFilesView *parent = find_view (GTK_WIDGET (f->strip), f->root);
    gtk_widget_grab_focus (GTK_WIDGET (parent)); settle (f);
    g_autoptr (GVariant) saved = nautilus_miller_view_capture_state (f->strip, FALSE);
    nautilus_miller_view_set_location (f->strip, f->other, NULL); settle (f);
    nautilus_miller_view_restore_state (f->strip, saved, FALSE);
    NautilusFilesView *child = find_view (GTK_WIDGET (f->strip), f->child);
    gtk_widget_grab_focus (f->entry);
    g_assert_true (gtk_widget_grab_focus (GTK_WIDGET (child)));
    g_assert_true (nautilus_miller_view_get_active_view (f->strip) == child);
    settle (f);
    GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (f->window));
    g_assert_true (focus != NULL && gtk_widget_is_ancestor (focus, GTK_WIDGET (child)));
    g_assert_true (nautilus_miller_view_get_active_view (f->strip) == child);
    g_assert_true (g_object_get_data (G_OBJECT (f->window), "nautilus-active-file-view") == child);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    if (g_getenv ("MILLER_TEST_SESSION") != NULL)
    {
        g_autoptr (GError) error = NULL;
        input = miller_test_input_new (&error);
        g_assert_no_error (error);
    }
    nautilus_register_resource (); nautilus_global_preferences_init ();
    g_autoptr (NautilusTagManager) tags = nautilus_tag_manager_new_dummy ();
    test_init_config_dir ();
    g_autoptr (NautilusApplication) app = nautilus_application_new ();
    g_application_set_flags (G_APPLICATION (app), g_application_get_flags (G_APPLICATION (app)) | G_APPLICATION_NON_UNIQUE);
    g_assert_true (g_application_register (G_APPLICATION (app), NULL, NULL));
    gtk_icon_theme_add_resource_path (gtk_icon_theme_get_for_display (gdk_display_get_default ()), "/org/gnome/nautilus/icons");
    nautilus_ensure_extension_points ();
#define CASE(name, function) g_test_add ("/miller/state/" name, Fixture, NULL, setup, function, teardown)
    CASE ("pending-same-view-focus", test_pending_same_view_focus);
    CASE ("roundtrip", test_roundtrip);
    CASE ("parent-entry", test_parent_context_and_entry);
    CASE ("new-entry-focus", test_new_entry_focus_during_restore);
    CASE ("newer-selection-navigation-dispose", test_newer_selection_and_navigation);
    CASE ("multiple-removed", test_removed_and_multiple_selection);
    CASE ("leaving-parent-branch", test_leaving_parent_branch);
    CASE ("manual-scroll", test_manual_scroll_cancels);
    CASE ("activation-cancels-context", test_activation_cancels_pending_context);
    CASE ("reentrant-navigation", test_reentrant_navigation);
#define NATIVE(name, function) g_test_add ("/miller/history/" name, Fixture, GINT_TO_POINTER (1), setup, function, teardown)
    NATIVE ("pending-background-capture", test_native_pending_background_capture);
    NATIVE ("loading-flat-selection", test_native_loading_flat_selection);
    NATIVE ("loading-model-selection", test_native_loading_model_selection);
    NATIVE ("automatic-model-clear", test_native_automatic_model_clear);
    NATIVE ("positions", test_native_history_positions);
    NATIVE ("parent-context", test_native_history_parent);
    NATIVE ("disclosure-history", test_native_disclosure_history);
    NATIVE ("closed-tab", test_native_closed_tab);
    NATIVE ("mode-context", test_native_mode_context);
    NATIVE ("history-in-flat-mode", test_native_history_in_flat_mode);
    NATIVE ("flat-selection-history", test_native_flat_selection_history);
    NATIVE ("immediate-mode-selection", test_native_immediate_mode_selection);
    NATIVE ("mode-new-selection", test_native_mode_new_selection);
    NATIVE ("search-cancel", test_native_search_cancel);
    NATIVE ("search-selection", test_native_search_selected_result);
    NATIVE ("explicit-removed", test_native_explicit_selection_and_removed);
    NATIVE ("renamed-endpoint", test_native_renamed_history_endpoint);
    NATIVE ("copied-history", test_native_copied_history);
    NATIVE ("keys-entry", test_native_history_keys_entry);
    NATIVE ("cached-special-loading", test_native_cached_special_loading);
    NATIVE ("delayed/branch", test_delayed_branch);
    NATIVE ("delayed/monitor", test_delayed_monitor_creation);
    NATIVE ("delayed/navigation", test_delayed_navigation);
    NATIVE ("delayed/close", test_delayed_close);
#define NAV(name, function) do { \
    if (input != NULL && g_getenv ("MILLER_TEST_HOME") != NULL) \
        g_test_add ("/miller/navigation/" name, Fixture, GINT_TO_POINTER (1), setup, function, teardown); \
    else g_test_add_func ("/miller/navigation/" name, skip_navigation); \
} while (0)
    NAV ("initial-focus", test_navigation_initial_focus);
    NAV ("entry", test_navigation_entry);
    NAV ("mode-keyboard", test_navigation_mode_keyboard);
    NAV ("empty-mode-keyboard", test_navigation_empty_mode_keyboard);
    NAV ("breadcrumb", test_navigation_breadcrumb);
    NAV ("bookmark", test_navigation_bookmark);
    NAV ("home", test_navigation_home);
    NAV ("tab-window", test_navigation_tab_window);
    NAV ("specials", test_navigation_specials);
    NAV ("mount", test_navigation_mount);
    int result = g_test_run ();
    miller_test_input_free (input);
    return result;
}
