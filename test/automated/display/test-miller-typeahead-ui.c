/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <test-utilities.h>
#include "miller-test-input.h"
#include "nautilus-application.h"
#include "nautilus-file.h"
#include "nautilus-file-utilities.h"
#include "nautilus-files-view.h"
#include "nautilus-global-preferences.h"
#include "nautilus-miller-view.h"
#include "nautilus-preferences-dialog.h"
#include "nautilus-resources.h"
#include "nautilus-tag-manager.h"
#include "nautilus-window.h"
#include "nautilus-window-slot.h"

typedef struct
{
    NautilusWindow *window;
    NautilusWindowSlot *slot;
    NautilusFilesView *view;
    GFile *root, *beta, *unicode, *branch;
} Fixture;
static MillerTestInput *input;

static void
pump (guint milliseconds)
{
    gint64 end = g_get_monotonic_time () + milliseconds * G_TIME_SPAN_MILLISECOND;
    do
    {
        while (g_main_context_iteration (NULL, FALSE)) {}
        g_usleep (1000);
    } while (g_get_monotonic_time () < end);
}
static void
key (guint keysym, gboolean pressed)
{
    g_autoptr (GError) error = NULL;
    g_assert_true (miller_test_input_key (input, keysym, pressed, &error));
    g_assert_no_error (error);
}
static void tap (guint keysym) { key (keysym, TRUE); key (keysym, FALSE); }
static void shortcut (guint modifier, guint keysym)
{
    key (modifier, TRUE); tap (keysym); key (modifier, FALSE); pump (100);
}
static void
settle (Fixture *f)
{
    gint64 end = g_get_monotonic_time () + 15 * G_TIME_SPAN_SECOND;
    while (nautilus_window_slot_get_loading (f->slot) || g_main_context_pending (NULL))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, end);
        g_main_context_iteration (NULL, FALSE); g_usleep (1000);
    }
}
static void
select_file (Fixture *f, GFile *location)
{
    g_autoptr (NautilusFile) file = nautilus_file_get (location);
    GList selection = { .data = file };
    nautilus_files_view_set_selection (f->view, &selection, FALSE);
}
static gboolean
is_selected (Fixture *f, GFile *location)
{
    g_autolist (NautilusFile) files = nautilus_files_view_get_selection (f->view);
    if (files == NULL || files->next != NULL) { return FALSE; }
    g_autoptr (GFile) actual = nautilus_file_get_location (files->data);
    return g_file_equal (actual, location);
}
static void
wait_selected (Fixture *f, GFile *location)
{
    gint64 end = g_get_monotonic_time () + 3 * G_TIME_SPAN_SECOND;
    while (!is_selected (f, location) && g_get_monotonic_time () < end) { pump (5); }
    g_assert_true (is_selected (f, location));
    g_assert_false (nautilus_window_slot_get_search_visible (f->slot));
}
static void
setup (Fixture *f, gconstpointer unused)
{
    if (input == NULL) { return; }
    g_settings_set_enum (nautilus_preferences, NAUTILUS_PREFERENCES_DEFAULT_FOLDER_VIEWER, 4);
    f->root = g_file_new_for_path (test_get_tmp_dir ());
    f->beta = g_file_get_child (f->root, "beta.txt");
    f->unicode = g_file_get_child (f->root, "α-folder");
    f->branch = g_file_get_child (f->root, "projects");
    g_assert_true (g_file_replace_contents (f->beta, "beta", 4, NULL, FALSE, 0, NULL, NULL, NULL));
    g_assert_true (g_file_make_directory (f->unicode, NULL, NULL));
    g_assert_true (g_file_make_directory (f->branch, NULL, NULL));
    f->window = nautilus_window_new ();
    gtk_window_set_default_size (GTK_WINDOW (f->window), 1100, 680);
    nautilus_window_open_location_full (f->window, f->root, 0, NULL);
    gtk_window_present (GTK_WINDOW (f->window));
    g_object_get (f->window, "active-slot", &f->slot, NULL);
    settle (f);
    f->view = nautilus_window_slot_get_current_view (f->slot);
    gtk_widget_grab_focus (GTK_WIDGET (f->view));
    gint64 end = g_get_monotonic_time () + 5 * G_TIME_SPAN_SECOND;
    while (!gtk_window_is_active (GTK_WINDOW (f->window)) && g_get_monotonic_time () < end) { pump (5); }
    g_assert_true (gtk_window_is_active (GTK_WINDOW (f->window)));
    pump (100);
}
static void
teardown (Fixture *f, gconstpointer unused)
{
    if (input == NULL) { return; }
    gtk_window_destroy (GTK_WINDOW (f->window));
    g_clear_object (&f->slot);
    g_clear_object (&f->root); g_clear_object (&f->beta);
    g_clear_object (&f->unicode); g_clear_object (&f->branch);
    test_clear_tmp_dir ();
    pump (50);
}
static void
test_ascii_and_unicode (Fixture *f, gconstpointer unused)
{
    if (input == NULL) { g_test_skip ("Requires private compositor"); return; }
    tap (GDK_KEY_b);
    wait_selected (f, f->beta);
    tap (GDK_KEY_Escape); pump (50);
    /* GTK's Unicode entry is genuine IM preedit/commit, separate from the
     * external IBus conversion acceptance required by the input plan. */
    key (GDK_KEY_Control_L, TRUE); key (GDK_KEY_Shift_L, TRUE); tap (GDK_KEY_u);
    key (GDK_KEY_Shift_L, FALSE); key (GDK_KEY_Control_L, FALSE);
    tap (GDK_KEY_0); tap (GDK_KEY_3); tap (GDK_KEY_b); tap (GDK_KEY_1);
    pump (100);
    g_assert_true (is_selected (f, f->beta));
    tap (GDK_KEY_Return);
    wait_selected (f, f->unicode);
    settle (f);
    g_assert_true (nautilus_window_slot_get_current_view (f->slot) == f->view);
    GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (f->window));
    g_assert_true (focus == GTK_WIDGET (f->view) || gtk_widget_is_ancestor (focus, GTK_WIDGET (f->view)));
}
static void
test_native_shortcuts (Fixture *f, gconstpointer unused)
{
    if (input == NULL) { g_test_skip ("Requires private compositor"); return; }
    tap (GDK_KEY_b); wait_selected (f, f->beta);
    shortcut (GDK_KEY_Control_L, GDK_KEY_f);
    g_assert_true (nautilus_window_slot_get_search_visible (f->slot));
    tap (GDK_KEY_Escape); pump (100); settle (f);
    g_assert_false (nautilus_window_slot_get_search_visible (f->slot));
    gtk_widget_grab_focus (GTK_WIDGET (f->view));
    shortcut (GDK_KEY_Control_L, GDK_KEY_l);
    GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (f->window));
    g_assert_true (GTK_IS_EDITABLE (focus));
    tap (GDK_KEY_z); pump (100);
    g_assert_nonnull (strstr (gtk_editable_get_text (GTK_EDITABLE (focus)), "z"));
    g_assert_true (is_selected (f, f->beta));
    tap (GDK_KEY_Escape); pump (100);
    gtk_widget_grab_focus (GTK_WIDGET (f->view));
    tap (GDK_KEY_F2); pump (100);
    focus = gtk_root_get_focus (GTK_ROOT (f->window));
    g_assert_true (GTK_IS_EDITABLE (focus));
    g_assert_nonnull (gtk_widget_get_ancestor (focus, GTK_TYPE_POPOVER));
    tap (GDK_KEY_z); pump (100);
    g_assert_true (is_selected (f, f->beta));
    tap (GDK_KEY_Escape); pump (100);
    select_file (f, f->branch); settle (f);
    gtk_widget_grab_focus (GTK_WIDGET (f->view));
    tap (GDK_KEY_Return); pump (100); settle (f);
    NautilusFilesView *child = nautilus_window_slot_get_current_view (f->slot);
    g_assert_true (child != f->view);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (child), f->branch));
}
static void
test_preference_off (Fixture *f, gconstpointer unused)
{
    if (input == NULL) { g_test_skip ("Requires private compositor"); return; }
    g_assert_true (g_settings_get_boolean (nautilus_preferences, "miller-type-to-select"));
    g_settings_set_boolean (nautilus_preferences, "miller-type-to-select", FALSE);
    tap (GDK_KEY_b); pump (100);
    g_assert_true (nautilus_window_slot_get_search_visible (f->slot));
    tap (GDK_KEY_Escape); pump (100); settle (f);
    g_settings_set_boolean (nautilus_preferences, "miller-type-to-select", TRUE);
    /* Typing in stock search can replace the Columns widget. Returning from
     * results creates a new listing; do not retain the old borrowed view. */
    f->view = nautilus_window_slot_get_current_view (f->slot);
    gtk_widget_grab_focus (GTK_WIDGET (f->view));
    tap (GDK_KEY_b); wait_selected (f, f->beta);
}
static void
test_immediate_right (Fixture *f, gconstpointer unused)
{
    if (input == NULL) { g_test_skip ("Requires private compositor"); return; }
    select_file (f, f->beta); settle (f);
    select_file (f, f->branch);
    /* No main-loop settling between the model selection and genuine Right. */
    tap (GDK_KEY_Right); pump (150); settle (f);
    NautilusFilesView *child = nautilus_window_slot_get_current_view (f->slot);
    g_assert_true (child != f->view);
    g_assert_true (g_file_equal (nautilus_files_view_get_location (child), f->branch));
    g_assert_true (g_object_get_data (G_OBJECT (f->window), "nautilus-active-file-view") == child);
}
static GtkWidget *
preference_row (GtkWidget *widget)
{
    if (ADW_IS_SWITCH_ROW (widget) &&
        g_strcmp0 (adw_preferences_row_get_title (ADW_PREFERENCES_ROW (widget)), "Type to Select in Columns") == 0)
    {
        return widget;
    }
    for (GtkWidget *child = gtk_widget_get_first_child (widget); child != NULL;
         child = gtk_widget_get_next_sibling (child))
    {
        GtkWidget *found = preference_row (child);
        if (found != NULL) { return found; }
    }
    return NULL;
}
static void
test_tab_focus_and_preference (Fixture *f, gconstpointer unused)
{
    if (input == NULL) { g_test_skip ("Requires private compositor"); return; }
    tap (GDK_KEY_b); wait_selected (f, f->beta);
    GtkWidget *hint = gtk_widget_get_last_child (gtk_widget_get_parent (GTK_WIDGET (f->view)));
    g_assert_true (GTK_IS_LABEL (hint));
    g_assert_true (gtk_widget_get_visible (hint));
    nautilus_window_new_tab (f->window); pump (100);
    g_assert_false (gtk_widget_get_visible (hint));
    g_object_set (f->window, "active-slot", f->slot, NULL); settle (f);
    gtk_widget_grab_focus (GTK_WIDGET (f->view));
    tap (GDK_KEY_e); pump (100);
    g_assert_nonnull (strstr (gtk_label_get_text (GTK_LABEL (hint)), ": e"));
    shortcut (GDK_KEY_Control_L, GDK_KEY_l);
    g_assert_false (gtk_widget_get_visible (hint));
    tap (GDK_KEY_Escape); pump (100);
    nautilus_preferences_dialog_show (GTK_WIDGET (f->window)); pump (100);
    AdwDialog *dialog = adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (f->window));
    g_assert_nonnull (dialog);
    GtkWidget *row = preference_row (GTK_WIDGET (dialog));
    g_assert_nonnull (row);
    g_assert_true (adw_switch_row_get_active (ADW_SWITCH_ROW (row)));
    adw_switch_row_set_active (ADW_SWITCH_ROW (row), FALSE);
    g_assert_false (g_settings_get_boolean (nautilus_preferences, "miller-type-to-select"));
    g_settings_set_boolean (nautilus_preferences, "miller-type-to-select", TRUE);
    g_assert_true (adw_switch_row_get_active (ADW_SWITCH_ROW (row)));
    adw_dialog_force_close (dialog); pump (100);
}
static void
test_grid_list_keep_search (Fixture *f, gconstpointer unused)
{
    if (input == NULL) { g_test_skip ("Requires private compositor"); return; }
    for (guint mode = 1; mode <= 2; mode++)
    {
        g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.files-view-mode", "u", mode));
        settle (f);
        f->view = nautilus_window_slot_get_current_view (f->slot);
        gtk_widget_grab_focus (GTK_WIDGET (f->view));
        tap (GDK_KEY_b); pump (100);
        g_assert_true (nautilus_window_slot_get_search_visible (f->slot));
        tap (GDK_KEY_Escape); pump (100); settle (f);
    }
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
    nautilus_register_resource ();
    nautilus_global_preferences_init ();
    g_autoptr (NautilusTagManager) tags = nautilus_tag_manager_new_dummy ();
    test_init_config_dir ();
    g_autoptr (NautilusApplication) app = nautilus_application_new ();
    g_application_set_flags (G_APPLICATION (app), g_application_get_flags (G_APPLICATION (app)) | G_APPLICATION_NON_UNIQUE);
    g_assert_true (g_application_register (G_APPLICATION (app), NULL, NULL));
    gtk_icon_theme_add_resource_path (gtk_icon_theme_get_for_display (gdk_display_get_default ()), "/org/gnome/nautilus/icons");
    nautilus_ensure_extension_points ();
#define CASE(name, function) g_test_add ("/miller/typeahead-ui/" name, Fixture, NULL, setup, function, teardown)
    CASE ("ascii-unicode", test_ascii_and_unicode);
    CASE ("native-shortcuts", test_native_shortcuts);
    CASE ("preference-off", test_preference_off);
    CASE ("immediate-right", test_immediate_right);
    CASE ("tab-focus-preference", test_tab_focus_and_preference);
    CASE ("grid-list-search", test_grid_list_keep_search);
    int result = g_test_run ();
    miller_test_input_free (input);
    return result;
}
