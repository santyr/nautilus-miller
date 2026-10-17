/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <test-utilities.h>
#include <unistd.h>
#include <math.h>
#include <gtk/gtk.h>
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/wayland/gdkwayland.h>
#endif
#include "nautilus-application.h"
#include "nautilus-file.h"
#include "nautilus-file-utilities.h"
#include "nautilus-files-view.h"
#include "nautilus-global-preferences.h"
#include "nautilus-miller-preview.h"
#include "nautilus-miller-view.h"
#include "nautilus-preferences-dialog.h"
#include "nautilus-previewer.h"
#include "nautilus-view-model.h"
#include "nautilus-resources.h"
#include "nautilus-tag-manager.h"
#include "nautilus-window.h"
#include "nautilus-window-slot.h"
#include "miller-test-input.h"

typedef struct
{
    GtkWidget *window;
    NautilusWindowSlot *slot;
    NautilusMillerView *strip;
    GFile *root, *child, *other, *one;
} Fixture;
static MillerTestInput *input;
static char persistent_setup_marker;
static GSubprocess *owned_sushi;

static void
pump (guint ms)
{
    gint64 end = g_get_monotonic_time () + ms * 1000;
    while (g_get_monotonic_time () < end)
    {
        while (g_main_context_iteration (NULL, FALSE)) {}
        g_usleep (1000);
    }
}
static GtkWidget *
find_type (GtkWidget *widget, GType type)
{
    if (G_TYPE_CHECK_INSTANCE_TYPE (widget, type)) { return widget; }
    for (GtkWidget *child = gtk_widget_get_first_child (widget); child; child = gtk_widget_get_next_sibling (child))
    {
        GtkWidget *found = find_type (child, type);
        if (found) { return found; }
    }
    return NULL;
}
static GtkWidget *
find_text (GtkWidget *widget, const char *text, gboolean tooltip)
{
    const char *actual = tooltip ? gtk_widget_get_tooltip_text (widget) :
        GTK_IS_LABEL (widget) ? gtk_label_get_text (GTK_LABEL (widget)) : NULL;
    if (g_strcmp0 (actual, text) == 0) { return widget; }
    for (GtkWidget *child = gtk_widget_get_first_child (widget); child; child = gtk_widget_get_next_sibling (child))
    {
        GtkWidget *found = find_text (child, text, tooltip);
        if (found) { return found; }
    }
    return NULL;
}
static NautilusMillerPreview *
preview (Fixture *f)
{
    GtkWidget *widget = find_type (GTK_WIDGET (f->strip), NAUTILUS_TYPE_MILLER_PREVIEW);
    g_assert_nonnull (widget);
    return NAUTILUS_MILLER_PREVIEW (widget);
}
static void
settle (Fixture *f)
{
    gint64 end = g_get_monotonic_time () + 15 * G_TIME_SPAN_SECOND;
    for (;;)
    {
        gboolean busy = nautilus_window_slot_get_loading (f->slot);
        NautilusFilesView *view = nautilus_window_slot_get_current_view (f->slot);
        if (view)
        {
            f->strip = NAUTILUS_MILLER_VIEW (gtk_widget_get_ancestor (GTK_WIDGET (view), NAUTILUS_TYPE_MILLER_VIEW));
            GtkWidget *pane = f->strip ? find_type (GTK_WIDGET (f->strip), NAUTILUS_TYPE_MILLER_PREVIEW) : NULL;
            gboolean loading = FALSE;
            if (pane) { g_object_get (pane, "loading", &loading, NULL); }
            busy |= loading;
        }
        if (!busy && !g_main_context_pending (NULL)) { break; }
        g_assert_cmpint (g_get_monotonic_time (), <, end);
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
        return actual && g_file_equal (actual, location) ? NAUTILUS_FILES_VIEW (widget) : NULL;
    }
    for (GtkWidget *child = gtk_widget_get_first_child (widget); child; child = gtk_widget_get_next_sibling (child))
    {
        NautilusFilesView *found = find_view (child, location);
        if (found) { return found; }
    }
    return NULL;
}
static void
select_one (NautilusFilesView *view, GFile *location)
{
    g_autoptr (NautilusFile) file = nautilus_file_get (location);
    GList files = { .data = file };
    nautilus_files_view_set_selection (view, &files, FALSE);
}
static void
setup (Fixture *f, gconstpointer persistence)
{
    if (persistence != &persistent_setup_marker)
    {
        g_settings_reset (nautilus_preferences, "miller-preview-visible");
        g_settings_reset (nautilus_preferences, "miller-preview-width");
        g_settings_reset (nautilus_preferences, "miller-preview-remote");
    }
    g_settings_set_enum (nautilus_preferences, NAUTILUS_PREFERENCES_DEFAULT_FOLDER_VIEWER, 4);
    f->root = g_file_new_for_path (test_get_tmp_dir ());
    f->child = g_file_get_child (f->root, "child");
    f->other = g_file_get_child (f->root, "other");
    f->one = g_file_get_child (f->child, "one.txt");
    g_assert_true (g_file_make_directory (f->child, NULL, NULL));
    g_assert_true (g_file_make_directory (f->other, NULL, NULL));
    g_assert_true (g_file_replace_contents (f->one, "preview alpha", 13, NULL, FALSE, 0, NULL, NULL, NULL));
    f->window = GTK_WIDGET (nautilus_window_new ());
    nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), f->child, 0, NULL);
    g_object_get (f->window, "active-slot", &f->slot, NULL);
    gtk_window_set_default_size (GTK_WINDOW (f->window), 1200, 700);
    gtk_window_present (GTK_WINDOW (f->window));
    settle (f);
}
static void
teardown (Fixture *f, gconstpointer unused)
{
    gtk_window_destroy (GTK_WINDOW (f->window)); pump (80);
    g_clear_object (&f->slot); g_clear_object (&f->root); g_clear_object (&f->child);
    g_clear_object (&f->other); g_clear_object (&f->one);
    test_clear_tmp_dir ();
}
static void
toggle (Fixture *f)
{
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.miller-preview", NULL));
    settle (f);
}
static void
assert_target (Fixture *f, const char *name)
{
    g_assert_nonnull (find_text (GTK_WIDGET (preview (f)), name, FALSE));
}
static void
assert_owner (Fixture *f, NautilusFilesView *view, GFile *endpoint)
{
    g_assert_true (nautilus_window_slot_get_current_view (f->slot) == view);
    g_assert_true (g_object_get_data (G_OBJECT (f->window), "nautilus-active-file-view") == view);
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), endpoint));
}
static void
test_active_parent (Fixture *f, gconstpointer unused)
{
    NautilusFilesView *parent = find_view (GTK_WIDGET (f->strip), f->root);
    g_assert_nonnull (parent);
    gtk_widget_grab_focus (GTK_WIDGET (parent)); settle (f);
    guint history = g_list_length (nautilus_window_slot_get_back_history (f->slot));
    GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (f->window));
    toggle (f); assert_target (f, "child"); assert_owner (f, parent, f->child);
    g_assert_true (gtk_root_get_focus (GTK_ROOT (f->window)) == focus);
    g_assert_cmpuint (g_list_length (nautilus_window_slot_get_back_history (f->slot)), ==, history);
}
static void
test_empty_child (Fixture *f, gconstpointer unused)
{
    NautilusFilesView *view = nautilus_window_slot_get_current_view (f->slot);
    toggle (f); assert_target (f, "Select a file to preview");
    select_one (view, f->one); settle (f); assert_target (f, "one.txt");
    assert_owner (f, view, f->child);
}
static void
test_history (Fixture *f, gconstpointer unused)
{
    toggle (f);
    select_one (nautilus_window_slot_get_current_view (f->slot), f->one); settle (f);
    nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), f->other, 0, NULL); settle (f);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.back", NULL)); settle (f);
    assert_target (f, "one.txt");
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->child));
}
static void
test_mode_special (Fixture *f, gconstpointer unused)
{
    toggle (f); select_one (nautilus_window_slot_get_current_view (f->slot), f->one); settle (f);
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.files-view-mode", "u", 2)); settle (f);
    g_assert_null (find_type (GTK_WIDGET (f->slot), NAUTILUS_TYPE_MILLER_PREVIEW));
    gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.miller-preview", NULL);
    g_assert_true (g_settings_get_boolean (nautilus_preferences, "miller-preview-visible"));
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.files-view-mode", "u", 4)); settle (f);
    assert_target (f, "one.txt");
    g_autoptr (GFile) starred = g_file_new_for_uri ("starred:///");
    nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), starred, 0, NULL); settle (f);
    g_assert_null (find_type (GTK_WIDGET (f->slot), NAUTILUS_TYPE_MILLER_PREVIEW));
    g_assert_true (gtk_widget_activate_action (GTK_WIDGET (f->slot), "slot.back", NULL)); settle (f);
    assert_target (f, "one.txt");
}
static void
test_tabs (Fixture *f, gconstpointer unused)
{
    toggle (f); select_one (nautilus_window_slot_get_current_view (f->slot), f->one); settle (f);
    NautilusMillerPreview *old = preview (f);
    g_object_add_weak_pointer (G_OBJECT (old), (gpointer *) &old);
    nautilus_window_new_tab (NAUTILUS_WINDOW (f->window)); pump (150);
    AdwTabView *tabs = ADW_TAB_VIEW (find_type (f->window, ADW_TYPE_TAB_VIEW));
    g_assert_cmpint (adw_tab_view_get_n_pages (tabs), ==, 2);
    gboolean loading; g_object_get (old, "loading", &loading, NULL); g_assert_false (loading);
    if (unused == NULL)
    {
        adw_tab_view_set_selected_page (tabs, adw_tab_view_get_nth_page (tabs, 0)); settle (f);
        assert_target (f, "one.txt");
    }
    AdwTabPage *closing = adw_tab_view_get_nth_page (tabs, 0);
    g_clear_object (&f->slot);
    adw_tab_view_close_page (tabs, closing); pump (150);
    f->slot = g_object_ref (NAUTILUS_WINDOW_SLOT (adw_tab_page_get_child (adw_tab_view_get_nth_page (tabs, 0))));
    settle (f);
    g_assert_null (old);
}
static void
test_controls_focus (Fixture *f, gconstpointer unused)
{
    toggle (f);
    GtkWidget *spin = find_text (GTK_WIDGET (f->strip), "Preview width", TRUE);
    GtkWidget *close = find_text (GTK_WIDGET (f->strip), "Close Preview", TRUE);
    g_assert_true (GTK_IS_SPIN_BUTTON (spin)); g_assert_nonnull (close);
    NautilusFilesView *view = nautilus_window_slot_get_current_view (f->slot);
    gtk_widget_grab_focus (spin); settle (f);
    g_assert_true (gtk_widget_is_ancestor (gtk_root_get_focus (GTK_ROOT (f->window)), spin));
    assert_owner (f, view, f->child);
    g_assert_true (gtk_widget_activate (close));
    gint64 deadline = g_get_monotonic_time () + 3 * G_TIME_SPAN_SECOND;
    while (g_settings_get_boolean (nautilus_preferences, "miller-preview-visible"))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); pump (10);
    }
    settle (f);
    g_assert_false (gtk_widget_get_mapped (GTK_WIDGET (preview (f))));
    GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (f->window));
    g_assert_true (focus && gtk_widget_is_ancestor (focus, GTK_WIDGET (view)));
}
static void
test_width (Fixture *f, gconstpointer unused)
{
    toggle (f);
    GtkSpinButton *spin = GTK_SPIN_BUTTON (find_text (GTK_WIDGET (f->strip), "Preview width", TRUE));
    g_assert_nonnull (spin);
    g_assert_cmpint (gtk_spin_button_get_value_as_int (spin), ==, 320);
    g_assert_cmpfloat (gtk_adjustment_get_lower (gtk_spin_button_get_adjustment (spin)), ==, 240);
    g_assert_cmpfloat (gtk_adjustment_get_upper (gtk_spin_button_get_adjustment (spin)), ==, 800);
    NautilusFilesView *view = nautilus_window_slot_get_current_view (f->slot);
    GtkWidget *box = gtk_widget_get_parent (GTK_WIDGET (view));
    int before; gtk_widget_get_size_request (box, &before, NULL);
    gtk_spin_button_set_value (spin, 400); settle (f);
    g_assert_cmpuint (g_settings_get_uint (nautilus_preferences, "miller-preview-width"), ==, 400);
    int after; gtk_widget_get_size_request (box, &after, NULL); g_assert_cmpint (after, ==, before);
    GtkPaned *paned = GTK_PANED (find_type (GTK_WIDGET (f->strip), GTK_TYPE_PANED));
    g_assert_nonnull (paned);
    g_assert_cmpint (gtk_widget_get_width (gtk_paned_get_end_child (paned)), ==, 400);
    gtk_window_set_default_size (GTK_WINDOW (f->window), 1300, 750); settle (f);
    g_assert_cmpuint (g_settings_get_uint (nautilus_preferences, "miller-preview-width"), ==, 400);
}
static void
key_chord (guint key, gboolean shift)
{
    g_autoptr (GError) error = NULL;
    g_assert_true (miller_test_input_key (input, GDK_KEY_Control_L, TRUE, &error));
    if (shift) { g_assert_true (miller_test_input_key (input, GDK_KEY_Shift_L, TRUE, &error)); }
    g_assert_true (miller_test_input_key (input, key, TRUE, &error));
    g_assert_true (miller_test_input_key (input, key, FALSE, &error));
    if (shift) { g_assert_true (miller_test_input_key (input, GDK_KEY_Shift_L, FALSE, &error)); }
    g_assert_true (miller_test_input_key (input, GDK_KEY_Control_L, FALSE, &error));
    g_assert_no_error (error); pump (60);
}
static void
plain_key (guint key)
{
    g_autoptr (GError) error = NULL;
    g_assert_true (miller_test_input_key (input, key, TRUE, &error));
    g_assert_true (miller_test_input_key (input, key, FALSE, &error));
    g_assert_no_error (error); pump (60);
}

static void
wait_quicklook (gboolean visible)
{
    g_test_message ("Waiting for actual Sushi visible=%d (current=%d)", visible, nautilus_previewer_is_visible ());
    gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    while (nautilus_previewer_is_visible () != visible)
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); pump (10);
    }
    pump (100);
}

static guint
canonical_sushi_owner (void)
{
    g_autoptr (GDBusConnection) bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
    g_assert_nonnull (bus);
    g_autoptr (GVariant) owner = NULL;
    gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    while (owner == NULL)
    {
        owner = g_dbus_connection_call_sync (bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
            "org.freedesktop.DBus", "GetConnectionUnixProcessID",
            g_variant_new ("(s)", "org.gnome.NautilusPreviewer"), G_VARIANT_TYPE ("(u)"),
            G_DBUS_CALL_FLAGS_NONE, 1000, NULL, NULL);
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); pump (20);
    }
    guint pid; g_variant_get (owner, "(u)", &pid);
    g_autofree char *proc = g_strdup_printf ("/proc/%u/exe", pid);
    g_autofree char *exe = g_file_read_link (proc, NULL);
    g_assert_nonnull (exe); g_assert_true (g_str_has_suffix (exe, "/gjs-console"));
    g_test_message ("Actual canonical Sushi process %u (%s)", pid, exe);
    return pid;
}

static void
start_owned_sushi (void)
{
    g_assert_null (owned_sushi);
    g_assert_nonnull (input); g_assert_nonnull (g_getenv ("MILLER_TEST_HOME"));
    g_assert_cmpstr (g_getenv ("MILLER_TEST_PREVIEWER_DISABLED"), ==, "1");
    g_autoptr (GSubprocessLauncher) launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);
    g_subprocess_launcher_setenv (launcher, "SUSHI_PERSIST", "1", TRUE);
    g_subprocess_launcher_unsetenv (launcher, "G_DBUS_DEBUG");
    g_autoptr (GError) error = NULL;
    owned_sushi = g_subprocess_launcher_spawn (launcher, &error, "/usr/libexec/org.gnome.NautilusPreviewer", NULL);
    g_assert_no_error (error); g_assert_nonnull (owned_sushi);
    guint actual = canonical_sushi_owner ();
    const char *identifier = g_subprocess_get_identifier (owned_sushi);
    g_assert_nonnull (identifier);
    g_assert_cmpuint (actual, ==, g_ascii_strtoull (identifier, NULL, 10));
}

static void
stop_owned_sushi (void)
{
    if (owned_sushi == NULL) { return; }
    /* Only the child retained by GSubprocess; never a discovered host PID. */
    g_subprocess_force_exit (owned_sushi);
    g_autoptr (GError) error = NULL;
    g_assert_true (g_subprocess_wait (owned_sushi, NULL, &error));
    g_assert_no_error (error); g_clear_object (&owned_sushi);
}

static void
test_quicklook_basic (void)
{
    if (g_strcmp0 (g_getenv ("MILLER_TEST_QUICK_LOOK"), "1") != 0)
    {
        g_test_skip ("Requires actual installed canonical Sushi in an owned private session"); return;
    }
    g_assert_nonnull (input);
    g_assert_nonnull (g_getenv ("MILLER_TEST_HOME"));
    Fixture f = {0}; setup (&f, NULL);
    canonical_sushi_owner ();
    gint64 deadline;
    g_autoptr (GFile) second = g_file_get_child (f.child, "two.txt");
    g_assert_true (g_file_replace_contents (second, "preview beta", 12, NULL, FALSE, 0, NULL, NULL, NULL));
    NautilusFilesView *view = nautilus_window_slot_get_current_view (f.slot);
    g_autoptr (NautilusFile) two = nautilus_file_get (second);
    deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    while (nautilus_view_model_get_item_for_file (nautilus_files_view_get_private_model (view), two) == NULL)
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); pump (10);
    }
    settle (&f); select_one (view, f.one); gtk_widget_grab_focus (GTK_WIDGET (view)); settle (&f);
    g_assert_true (gtk_window_is_active (GTK_WINDOW (f.window)));
    plain_key (GDK_KEY_space); wait_quicklook (TRUE);
    deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    while (gtk_window_is_active (GTK_WINDOW (f.window)))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); pump (10);
    }
    plain_key (GDK_KEY_Down);
    deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    for (;;)
    {
        g_autolist (NautilusFile) selected = nautilus_files_view_get_selection (view);
        if (g_list_length (selected) == 1 && selected->data == two) { break; }
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); pump (10);
    }
    assert_owner (&f, view, f.child);
    plain_key (GDK_KEY_Escape); wait_quicklook (FALSE);
    deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    while (!gtk_window_is_active (GTK_WINDOW (f.window)))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); pump (10);
    }
    plain_key (GDK_KEY_space); wait_quicklook (TRUE);
    plain_key (GDK_KEY_space); wait_quicklook (FALSE);
    assert_owner (&f, view, f.child);
    teardown (&f, NULL);
}

static void
test_quicklook_restart (gconstpointer replace_selection)
{
    if (g_strcmp0 (g_getenv ("MILLER_TEST_QUICK_LOOK"), "restart") != 0)
    {
        g_test_skip ("Requires the owned real Sushi restart fixture"); return;
    }
    g_assert_nonnull (owned_sushi);
    Fixture f = {0}; setup (&f, NULL);
    g_autoptr (GFile) second = g_file_get_child (f.child, "two.txt");
    g_assert_true (g_file_replace_contents (second, "preview beta", 12, NULL, FALSE, 0, NULL, NULL, NULL));
    NautilusFilesView *view = nautilus_window_slot_get_current_view (f.slot);
    g_autoptr (NautilusFile) two = nautilus_file_get (second);
    gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    while (nautilus_view_model_get_item_for_file (nautilus_files_view_get_private_model (view), two) == NULL)
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); pump (10);
    }
    settle (&f); select_one (view, f.one); gtk_widget_grab_focus (GTK_WIDGET (view)); settle (&f);
    plain_key (GDK_KEY_space); wait_quicklook (TRUE);
    /* Crash only our retained real provider while its window owns focus. The
     * private activation override prevents an uncontrolled replacement. */
    stop_owned_sushi (); pump (100); wait_quicklook (FALSE);
    deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    while (!gtk_window_is_active (GTK_WINDOW (f.window)))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); pump (10);
    }
    g_assert_nonnull (gtk_root_get_focus (GTK_ROOT (f.window)));
    plain_key (GDK_KEY_space); /* Real request while no provider owns the name. */
    if (replace_selection != NULL)
    {
        plain_key (GDK_KEY_Down);
        g_autolist (NautilusFile) selected = nautilus_files_view_get_selection (view);
        g_assert_cmpuint (g_list_length (selected), ==, 1); g_assert_true (selected->data == two);
    }
    start_owned_sushi (); wait_quicklook (TRUE);
    assert_owner (&f, view, f.child);
    plain_key (GDK_KEY_Escape); wait_quicklook (FALSE);
    teardown (&f, NULL);
}

#ifdef GDK_WINDOWING_WAYLAND
static void
foreign_window_exported (GdkToplevel *toplevel, const char *handle, gpointer data)
{
    *(char **) data = g_strdup_printf ("wayland:%s", handle);
}
#endif

static void
test_quicklook_foreign_owner (gconstpointer close_owner)
{
#ifndef GDK_WINDOWING_WAYLAND
    g_test_skip ("Requires the owned Wayland compositor");
#else
    if (g_strcmp0 (g_getenv ("MILLER_TEST_QUICK_LOOK"), "foreign") != 0)
    {
        g_test_skip ("Requires actual shared Sushi ownership fixture"); return;
    }
    Fixture f = {0}; setup (&f, NULL);
    canonical_sushi_owner ();
    g_autoptr (GFile) second = g_file_get_child (f.child, "two.txt");
    g_assert_true (g_file_replace_contents (second, "preview beta", 12, NULL, FALSE, 0, NULL, NULL, NULL));
    NautilusFilesView *view = nautilus_window_slot_get_current_view (f.slot);
    g_autoptr (NautilusFile) two = nautilus_file_get (second);
    gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    while (nautilus_view_model_get_item_for_file (nautilus_files_view_get_private_model (view), two) == NULL)
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); pump (10);
    }
    select_one (view, f.one); gtk_widget_grab_focus (GTK_WIDGET (view)); settle (&f);
    plain_key (GDK_KEY_space); wait_quicklook (TRUE);
    plain_key (GDK_KEY_Escape); wait_quicklook (FALSE);
    /* A second client uses the actual installed contract and an actual GTK
     * foreign-window handle. No SelectionEvent or visibility is fabricated. */
    GtkWidget *foreign = gtk_window_new ();
    gtk_window_set_default_size (GTK_WINDOW (foreign), 300, 200);
    gtk_window_present (GTK_WINDOW (foreign)); pump (100);
    GdkSurface *surface = gtk_native_get_surface (GTK_NATIVE (foreign));
    g_autofree char *handle = NULL;
    g_assert_true (gdk_wayland_toplevel_export_handle (GDK_WAYLAND_TOPLEVEL (surface),
        foreign_window_exported, &handle, NULL));
    while (handle == NULL)
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); pump (10);
    }
    g_autoptr (GDBusConnection) bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
    g_autofree char *uri = g_file_get_uri (f.one);
    g_autoptr (GError) error = NULL;
    g_autoptr (GDBusProxy) proxy = g_dbus_proxy_new_sync (bus, G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
        NULL, "org.gnome.NautilusPreviewer", "/org/gnome/NautilusPreviewer",
        "org.gnome.NautilusPreviewer2", NULL, &error);
    g_assert_no_error (error); g_assert_nonnull (proxy);
    /* GDBusProxy addresses the actual unique owner, like the native wrapper,
     * so the wire observer can match reply sender and request destination. */
    g_autoptr (GVariant) reply = g_dbus_proxy_call_sync (proxy, "ShowFile",
        g_variant_new ("(ssbs)", uri, handle, FALSE, ""),
        G_DBUS_CALL_FLAGS_NO_AUTO_START, 2000, NULL, &error);
    g_assert_no_error (error); g_assert_nonnull (reply); pump (300);
    while (gtk_window_is_active (GTK_WINDOW (foreign)))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); pump (10);
    }
    if (close_owner != NULL)
    {
        nautilus_window_new_tab (NAUTILUS_WINDOW (f.window));
        g_clear_object (&f.slot); g_object_get (f.window, "active-slot", &f.slot, NULL); settle (&f);
        g_assert_true (nautilus_previewer_is_visible ());
    }
    else
    {
        plain_key (GDK_KEY_Down); pump (300);
        g_autolist (NautilusFile) selected = nautilus_files_view_get_selection (view);
        g_assert_cmpuint (g_list_length (selected), ==, 1);
        g_autoptr (GFile) location = nautilus_file_get_location (selected->data);
        g_assert_true (g_file_equal (location, f.one));
        assert_owner (&f, view, f.child);
    }
    /* Explicit test-client cleanup, independent of the wrapper under test. */
    g_clear_pointer (&reply, g_variant_unref);
    reply = g_dbus_proxy_call_sync (proxy, "Close", NULL, G_DBUS_CALL_FLAGS_NO_AUTO_START, 2000, NULL, &error);
    g_assert_no_error (error); wait_quicklook (FALSE);
    gdk_wayland_toplevel_drop_exported_handle (GDK_WAYLAND_TOPLEVEL (surface), handle + strlen ("wayland:"));
    gtk_window_destroy (GTK_WINDOW (foreign));
    teardown (&f, NULL);
#endif
}

static void
test_quicklook_pending_owner (gconstpointer action_data)
{
    if (g_strcmp0 (g_getenv ("MILLER_TEST_QUICK_LOOK"), "pending") != 0)
    {
        g_test_skip ("Requires the owned real Sushi pending-request fixture"); return;
    }
    guint action = GPOINTER_TO_UINT (action_data);
    Fixture f = {0}; setup (&f, NULL);
    NautilusFilesView *view = nautilus_window_slot_get_current_view (f.slot);
    select_one (view, f.one); gtk_widget_grab_focus (GTK_WIDGET (view)); settle (&f);
    plain_key (GDK_KEY_space); wait_quicklook (TRUE);
    stop_owned_sushi (); pump (100); wait_quicklook (FALSE);
    plain_key (GDK_KEY_space);
    GtkWidget *entry = NULL;
    NautilusWindowSlot *weak = NULL;
    AdwTabView *tabs = ADW_TAB_VIEW (find_type (f.window, ADW_TYPE_TAB_VIEW));
    AdwTabPage *old = adw_tab_view_get_selected_page (tabs);
    if (action == 0)
    {
        key_chord (GDK_KEY_l, FALSE);
        entry = gtk_root_get_focus (GTK_ROOT (f.window));
        g_assert_true (GTK_IS_EDITABLE (entry));
    }
    else
    {
        weak = f.slot;
        g_object_add_weak_pointer (G_OBJECT (weak), (gpointer *) &weak);
        nautilus_window_new_tab (NAUTILUS_WINDOW (f.window));
        g_clear_object (&f.slot);
        if (action == 2) { adw_tab_view_close_page (tabs, old); }
        g_object_get (f.window, "active-slot", &f.slot, NULL); settle (&f);
        if (action == 2) { g_assert_null (weak); }
    }
    start_owned_sushi ();
    /* Observe the complete pending-request lifetime, including the original
     * deadline. A provider appearing later must not steal entry/new-tab focus. */
    gint64 end = g_get_monotonic_time () + 5100 * 1000;
    while (g_get_monotonic_time () < end)
    {
        g_assert_false (nautilus_previewer_is_visible ()); pump (10);
    }
    if (action == 0)
    {
        g_assert_true (gtk_root_get_focus (GTK_ROOT (f.window)) == entry);
        plain_key (GDK_KEY_Escape);
        assert_owner (&f, view, f.child);
    }
    else if (action == 1)
    {
        adw_tab_view_set_selected_page (tabs, old);
        g_clear_object (&f.slot); g_object_get (f.window, "active-slot", &f.slot, NULL); settle (&f);
    }
    if (weak != NULL) { g_object_remove_weak_pointer (G_OBJECT (weak), (gpointer *) &weak); }
    if (action != 2)
    {
        plain_key (GDK_KEY_space); wait_quicklook (TRUE);
        plain_key (GDK_KEY_Escape); wait_quicklook (FALSE);
    }
    teardown (&f, NULL);
}

static void
test_quicklook_owner (gconstpointer close_owner)
{
    if (g_strcmp0 (g_getenv ("MILLER_TEST_QUICK_LOOK"), "owner") != 0)
    {
        g_test_skip ("Requires actual installed Sushi ownership fixture"); return;
    }
    g_assert_nonnull (input); g_assert_nonnull (g_getenv ("MILLER_TEST_HOME"));
    Fixture f = {0}; setup (&f, NULL);
    canonical_sushi_owner ();
    if (close_owner != NULL)
    {
        nautilus_window_open_location_full (NAUTILUS_WINDOW (f.window), f.other,
            NAUTILUS_OPEN_FLAG_NEW_TAB | NAUTILUS_OPEN_FLAG_DONT_MAKE_ACTIVE, NULL);
        settle (&f);
    }
    NautilusFilesView *view = nautilus_window_slot_get_current_view (f.slot);
    select_one (view, f.one); gtk_widget_grab_focus (GTK_WIDGET (view)); settle (&f);
    plain_key (GDK_KEY_space); wait_quicklook (TRUE);
    AdwTabView *tabs = ADW_TAB_VIEW (find_type (f.window, ADW_TYPE_TAB_VIEW));
    AdwTabPage *old = adw_tab_view_get_selected_page (tabs);
    NautilusWindowSlot *weak = f.slot;
    g_object_add_weak_pointer (G_OBJECT (weak), (gpointer *) &weak);
    if (close_owner == NULL)
    {
        nautilus_window_new_tab (NAUTILUS_WINDOW (f.window));
    }
    g_clear_object (&f.slot);
    if (close_owner != NULL) { adw_tab_view_close_page (tabs, old); }
    g_object_get (f.window, "active-slot", &f.slot, NULL); settle (&f);
    wait_quicklook (FALSE);
    g_assert_true (nautilus_window_slot_get_current_view (f.slot) ==
                   g_object_get_data (G_OBJECT (f.window), "nautilus-active-file-view"));
    if (close_owner != NULL) { g_assert_null (weak); }
    else
    {
        g_assert_nonnull (weak);
        /* Once closed on tab switch, provider input must not change this
         * background selection. Returning to its tab permits a new preview. */
        g_autolist (NautilusFile) selected = nautilus_files_view_get_selection (view);
        g_assert_cmpuint (g_list_length (selected), ==, 1);
        g_autoptr (GFile) location = nautilus_file_get_location (selected->data);
        g_assert_true (g_file_equal (location, f.one));
        adw_tab_view_set_selected_page (tabs, old);
        g_clear_object (&f.slot); g_object_get (f.window, "active-slot", &f.slot, NULL); settle (&f);
        GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (f.window));
        g_test_message ("Returned tab: window-active=%d focus=%s in-listing=%d",
            gtk_window_is_active (GTK_WINDOW (f.window)), focus ? G_OBJECT_TYPE_NAME (focus) : "none",
            focus ? gtk_widget_is_ancestor (focus, GTK_WIDGET (view)) : FALSE);
        plain_key (GDK_KEY_space); wait_quicklook (TRUE);
        plain_key (GDK_KEY_Escape); wait_quicklook (FALSE);
    }
    if (weak != NULL) { g_object_remove_weak_pointer (G_OBJECT (weak), (gpointer *) &weak); }
    teardown (&f, NULL);
}

static void
test_quicklook_missing (void)
{
    if (g_strcmp0 (g_getenv ("MILLER_TEST_QUICK_LOOK"), "missing") != 0)
    {
        g_test_skip ("Requires the private previewer-absence fixture"); return;
    }
    g_assert_nonnull (input); g_assert_nonnull (g_getenv ("MILLER_TEST_HOME"));
    g_assert_cmpstr (g_getenv ("MILLER_TEST_PREVIEWER_DISABLED"), ==, "1");
    Fixture f = {0}; setup (&f, NULL);
    NautilusFilesView *view = nautilus_window_slot_get_current_view (f.slot);
    select_one (view, f.one); gtk_widget_grab_focus (GTK_WIDGET (view)); settle (&f);
    GtkWidget *focus = gtk_root_get_focus (GTK_ROOT (f.window));
    g_assert_nonnull (focus);
    plain_key (GDK_KEY_space);
    gint64 deadline = g_get_monotonic_time () + 5 * G_TIME_SPAN_SECOND;
    while (find_text (f.window, "Preview unavailable. Check that Sushi is installed and running.", FALSE) == NULL)
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); pump (10);
    }
    g_assert_false (nautilus_previewer_is_visible ());
    g_assert_true (gtk_root_get_focus (GTK_ROOT (f.window)) == focus);
    assert_owner (&f, view, f.child);
    teardown (&f, NULL);
}

static void
test_toggle_keys (Fixture *f, gconstpointer unused)
{
    if (!input) { g_test_skip ("Requires owned private compositor input"); return; }
    key_chord (GDK_KEY_p, TRUE); settle (f);
    g_assert_true (g_settings_get_boolean (nautilus_preferences, "miller-preview-visible"));
    g_assert_true (gtk_widget_get_mapped (GTK_WIDGET (preview (f))));
    key_chord (GDK_KEY_l, FALSE); settle (f);
    GtkWidget *entry = gtk_root_get_focus (GTK_ROOT (f->window));
    g_assert_true (GTK_IS_EDITABLE (entry));
    key_chord (GDK_KEY_p, TRUE); settle (f);
    g_assert_false (g_settings_get_boolean (nautilus_preferences, "miller-preview-visible"));
    g_assert_true (gtk_root_get_focus (GTK_ROOT (f->window)) == entry);
    key_chord (GDK_KEY_p, TRUE); settle (f);
    g_assert_true (g_settings_get_boolean (nautilus_preferences, "miller-preview-visible"));
    g_assert_true (gtk_root_get_focus (GTK_ROOT (f->window)) == entry);
}

static void
test_background_close_entry_focus (Fixture *f, gconstpointer unused)
{
    if (!input) { g_test_skip ("Requires owned private compositor input"); return; }
    toggle (f);
    NautilusMillerPreview *old = preview (f);
    g_object_add_weak_pointer (G_OBJECT (old), (gpointer *) &old);
    AdwTabView *tabs = ADW_TAB_VIEW (find_type (f->window, ADW_TYPE_TAB_VIEW));
    AdwTabPage *closing = adw_tab_view_get_selected_page (tabs);
    nautilus_window_new_tab (NAUTILUS_WINDOW (f->window));
    g_clear_object (&f->slot);
    g_object_get (f->window, "active-slot", &f->slot, NULL); settle (f);
    key_chord (GDK_KEY_l, FALSE); settle (f);
    GtkWidget *entry = gtk_root_get_focus (GTK_ROOT (f->window));
    g_assert_true (GTK_IS_EDITABLE (entry));
    adw_tab_view_close_page (tabs, closing); settle (f);
    g_assert_null (old);
    g_assert_true (gtk_root_get_focus (GTK_ROOT (f->window)) == entry);
    g_assert_true (nautilus_window_slot_get_current_view (f->slot) ==
                   g_object_get_data (G_OBJECT (f->window), "nautilus-active-file-view"));
}

static void
test_drag_width (Fixture *f, gconstpointer unused)
{
    if (!input) { g_test_skip ("Requires owned private compositor input"); return; }
    if (unused == NULL) { toggle (f); }
    gtk_window_fullscreen (GTK_WINDOW (f->window)); pump (250);
    GtkPaned *paned = GTK_PANED (find_type (GTK_WIDGET (f->strip), GTK_TYPE_PANED));
    GtkWidget *handle = NULL;
    for (GtkWidget *child = gtk_widget_get_first_child (GTK_WIDGET (paned)); child; child = gtk_widget_get_next_sibling (child))
    {
        if (child != gtk_paned_get_start_child (paned) && child != gtk_paned_get_end_child (paned)) { handle = child; break; }
    }
    g_assert_nonnull (handle);
    graphene_point_t point;
    g_assert_true (gtk_widget_compute_point (handle, f->window,
        &GRAPHENE_POINT_INIT (gtk_widget_get_width (handle) / 2.0, gtk_widget_get_height (handle) / 2.0), &point));
    g_autoptr (GError) error = NULL;
    g_assert_true (miller_test_input_motion (input, -10000, -10000, &error));
    g_assert_true (miller_test_input_motion (input, point.x, point.y, &error)); pump (80);
    GdkDevice *pointer = gdk_seat_get_pointer (gdk_display_get_default_seat (gdk_display_get_default ()));
    double x, y;
    GdkSurface *expected = gtk_native_get_surface (GTK_NATIVE (f->window));
    GdkSurface *surface = gdk_device_get_surface_at_position (pointer, &x, &y);
    for (guint i = 0; i < 12 && surface == expected && (fabs (x-point.x)>1 || fabs (y-point.y)>1); i++)
    {
        g_assert_true (miller_test_input_motion (input, (point.x-x)*.75, (point.y-y)*.75, &error)); pump (30);
        surface = gdk_device_get_surface_at_position (pointer, &x, &y);
    }
    g_assert_no_error (error); g_assert_true (surface == expected);
    g_assert_cmpfloat_with_epsilon (x, point.x, 1.5); g_assert_cmpfloat_with_epsilon (y, point.y, 1.5);
    int before = gtk_widget_get_width (gtk_paned_get_end_child (paned));
    g_assert_true (miller_test_input_button (input, 272, TRUE, &error)); pump (40);
    for (guint i = 0; i < 8; i++)
    {
        g_assert_true (miller_test_input_motion (input, unused == NULL ? -5 : 5, 0, &error)); pump (25);
    }
    g_assert_true (miller_test_input_button (input, 272, FALSE, &error)); g_assert_no_error (error);
    settle (f);
    int after = gtk_widget_get_width (gtk_paned_get_end_child (paned));
    g_test_message ("Actual inspector drag width %d -> %d; saved %u", before, after,
        g_settings_get_uint (nautilus_preferences, "miller-preview-width"));
    if (unused == NULL) { g_assert_cmpint (after, >, before + 10); }
    else if (unused == GINT_TO_POINTER (1)) { g_assert_cmpint (after, ==, 240); }
    else { g_assert_cmpint (after, <, before - 10); }
    g_assert_cmpuint (g_settings_get_uint (nautilus_preferences, "miller-preview-width"), ==, after);
    g_assert_true (g_file_equal (nautilus_window_slot_get_location (f->slot), f->child));
    if (unused == NULL)
    {
        g_settings_set_uint (nautilus_preferences, "miller-preview-width", 400); settle (f);
        test_drag_width (f, GINT_TO_POINTER (2));
        g_settings_set_uint (nautilus_preferences, "miller-preview-width", 240); settle (f);
        test_drag_width (f, GINT_TO_POINTER (1));
    }
}

typedef struct { Fixture *fixture; gboolean closed, create_tab; } DecodeClose;
static void
close_decoding_tab (GtkLabel *label, GParamSpec *pspec, DecodeClose *close)
{
    if (close->closed || !g_str_equal (gtk_label_get_text (label), "Decoding image preview…")) { return; }
    close->closed = TRUE;
    Fixture *f = close->fixture;
    AdwTabView *tabs = ADW_TAB_VIEW (find_type (f->window, ADW_TYPE_TAB_VIEW));
    AdwTabPage *old = adw_tab_view_get_selected_page (tabs);
    if (close->create_tab) { nautilus_window_new_tab (NAUTILUS_WINDOW (f->window)); }
    g_clear_object (&f->slot);
    adw_tab_view_close_page (tabs, old);
    g_object_get (f->window, "active-slot", &f->slot, NULL);
}
static void
test_decode_tab_close (Fixture *f, gconstpointer unused)
{
    if (unused == NULL)
    {
        nautilus_window_open_location_full (NAUTILUS_WINDOW (f->window), f->child,
            NAUTILUS_OPEN_FLAG_NEW_TAB | NAUTILUS_OPEN_FLAG_DONT_MAKE_ACTIVE, NULL);
        settle (f);
    }
    toggle (f);
    g_assert_true (gtk_widget_get_mapped (GTK_WIDGET (preview (f))));
    g_autoptr (GFile) image = g_file_get_child (f->child, "decoding.png");
    g_autofree char *path = g_file_get_path (image);
    g_autoptr (GdkPixbuf) pixels = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, 2048, 1024);
    gdk_pixbuf_fill (pixels, 0xCC6600FF);
    g_autoptr (GError) error = NULL;
    gboolean saved = gdk_pixbuf_save (pixels, path, "png", &error, NULL);
    g_assert_no_error (error); g_assert_true (saved);
    NautilusFilesView *view = nautilus_window_slot_get_current_view (f->slot);
    g_autoptr (NautilusFile) file = nautilus_file_get (image);
    gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;
    while (nautilus_view_model_get_item_for_file (nautilus_files_view_get_private_model (view), file) == NULL)
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); pump (10);
    }
    g_test_message ("Image row became available after %.3fs", (g_get_monotonic_time () - deadline + 8 * G_TIME_SPAN_SECOND) / 1e6);
    NautilusMillerPreview *weak = preview (f);
    g_object_add_weak_pointer (G_OBJECT (weak), (gpointer *) &weak);
    g_autoptr (GtkWidget) status = g_object_ref (find_text (GTK_WIDGET (weak), "Select a file to preview", FALSE));
    g_assert_nonnull (status);
    DecodeClose close = { .fixture = f, .create_tab = unused != NULL };
    g_signal_connect (status, "notify::label", G_CALLBACK (close_decoding_tab), &close);
    select_one (view, image);
    while (!close.closed)
    {
        if (g_get_monotonic_time () >= deadline)
        {
            gboolean busy; g_object_get (weak, "loading", &busy, NULL);
            g_test_message ("Decode close phase: %s; loading=%d", gtk_label_get_text (GTK_LABEL (status)), busy);
        }
        g_assert_cmpint (g_get_monotonic_time (), <, deadline); pump (10);
    }
    settle (f); pump (100);
    g_signal_handlers_disconnect_by_data (status, &close);
    g_clear_object (&status);
    gint64 release_deadline = g_get_monotonic_time () + 3 * G_TIME_SPAN_SECOND;
    while (weak != NULL && g_get_monotonic_time () < release_deadline) { pump (10); }
    g_assert_null (weak);
    g_assert_true (nautilus_window_slot_get_current_view (f->slot) ==
                   g_object_get_data (G_OBJECT (f->window), "nautilus-active-file-view"));
}

static void
test_remote_preference (Fixture *f, gconstpointer unused)
{
    nautilus_preferences_dialog_show (f->window); pump (150);
    GtkWidget *label = find_text (f->window, "Preview Remote Contents", FALSE);
    g_assert_nonnull (label);
    AdwSwitchRow *row = ADW_SWITCH_ROW (gtk_widget_get_ancestor (label, ADW_TYPE_SWITCH_ROW));
    g_assert_nonnull (row); g_assert_false (adw_switch_row_get_active (row));
    adw_switch_row_set_active (row, TRUE); pump (30);
    g_assert_true (g_settings_get_boolean (nautilus_preferences, "miller-preview-remote"));
    g_settings_set_boolean (nautilus_preferences, "miller-preview-remote", FALSE); pump (30);
    g_assert_false (adw_switch_row_get_active (row));
    AdwDialog *dialog = adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (f->window));
    g_assert_nonnull (dialog); g_assert_true (adw_dialog_close (dialog)); pump (150);
}

static void
test_process (void)
{
    const char *phase = g_getenv ("MILLER_PREVIEW_STATE_PHASE");
    if (!phase) { g_test_skip ("Requires separate process dconf writer/reader"); return; }
    g_assert_cmpstr (g_getenv ("GSETTINGS_BACKEND"), ==, "dconf");
    Fixture f = {0}; setup (&f, &persistent_setup_marker);
    if (g_str_equal (phase, "write"))
    {
        toggle (&f);
        GtkSpinButton *spin = GTK_SPIN_BUTTON (find_text (GTK_WIDGET (f.strip), "Preview width", TRUE));
        g_assert_nonnull (spin); gtk_spin_button_set_value (spin, 410);
        g_settings_set_boolean (nautilus_preferences, "miller-preview-remote", TRUE);
        g_settings_sync ();
    }
    else
    {
        g_assert_true (g_settings_get_boolean (nautilus_preferences, "miller-preview-visible"));
        g_assert_true (g_settings_get_boolean (nautilus_preferences, "miller-preview-remote"));
        GtkSpinButton *spin = GTK_SPIN_BUTTON (find_text (GTK_WIDGET (f.strip), "Preview width", TRUE));
        g_assert_nonnull (spin); g_assert_cmpint (gtk_spin_button_get_value_as_int (spin), ==, 410);
        g_assert_true (gtk_widget_get_mapped (GTK_WIDGET (preview (&f))));
        GtkPaned *paned = GTK_PANED (find_type (GTK_WIDGET (f.strip), GTK_TYPE_PANED));
        g_assert_cmpint (gtk_widget_get_width (gtk_paned_get_end_child (paned)), ==, 410);
    }
    g_test_message ("Preview persistence %s process %d", phase, getpid ());
    teardown (&f, NULL);
}
int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    if (g_getenv ("MILLER_TEST_SESSION"))
    {
        g_autoptr (GError) error = NULL; input = miller_test_input_new (&error); g_assert_no_error (error);
    }
    if (g_getenv ("MILLER_TEST_OWN_SUSHI")) { start_owned_sushi (); }
    nautilus_register_resource (); nautilus_global_preferences_init (); nautilus_ensure_extension_points ();
    g_autoptr (NautilusTagManager) tags = nautilus_tag_manager_new_dummy ();
    test_init_config_dir ();
    g_autoptr (NautilusApplication) app = nautilus_application_new ();
    g_application_set_flags (G_APPLICATION (app), g_application_get_flags (G_APPLICATION (app)) | G_APPLICATION_NON_UNIQUE);
    g_assert_true (g_application_register (G_APPLICATION (app), NULL, NULL));
#define CASE(name, fn) g_test_add ("/miller/preview-ui/" name, Fixture, NULL, setup, fn, teardown)
    CASE ("decode-tab-close", test_decode_tab_close);
    g_test_add ("/miller/preview-ui/decode-new-tab-close", Fixture, GINT_TO_POINTER (1), setup, test_decode_tab_close, teardown);
    CASE ("remote-preference", test_remote_preference);
    CASE ("background-close-entry-focus", test_background_close_entry_focus);
    CASE ("active-parent", test_active_parent); CASE ("empty-child", test_empty_child);
    CASE ("history-selection", test_history); CASE ("mode-special-return", test_mode_special);
    CASE ("tab-ownership", test_tabs); CASE ("controls-focus", test_controls_focus);
    g_test_add ("/miller/preview-ui/inactive-tab-close", Fixture, GINT_TO_POINTER (1), setup, test_tabs, teardown);
    CASE ("width-independent", test_width); CASE ("drag-width", test_drag_width); CASE ("toggle-keys", test_toggle_keys);
    g_test_add_func ("/miller/preview-ui/process-state", test_process);
    g_test_add_func ("/miller/quicklook/actual-basic", test_quicklook_basic);
    g_test_add_func ("/miller/quicklook/missing-provider", test_quicklook_missing);
    g_test_add_data_func ("/miller/quicklook/tab-switch", NULL, test_quicklook_owner);
    g_test_add_data_func ("/miller/quicklook/tab-close", GINT_TO_POINTER (1), test_quicklook_owner);
    g_test_add_data_func ("/miller/quicklook/restart", NULL, test_quicklook_restart);
    g_test_add_data_func ("/miller/quicklook/restart-latest-selection", GINT_TO_POINTER (1), test_quicklook_restart);
    g_test_add_data_func ("/miller/quicklook/pending-entry", NULL, test_quicklook_pending_owner);
    g_test_add_data_func ("/miller/quicklook/pending-tab-switch", GUINT_TO_POINTER (1), test_quicklook_pending_owner);
    g_test_add_data_func ("/miller/quicklook/pending-tab-close", GUINT_TO_POINTER (2), test_quicklook_pending_owner);
    g_test_add_data_func ("/miller/quicklook/foreign-selection", NULL, test_quicklook_foreign_owner);
    g_test_add_data_func ("/miller/quicklook/foreign-close", GINT_TO_POINTER (1), test_quicklook_foreign_owner);
    int result = g_test_run (); stop_owned_sushi (); miller_test_input_free (input); return result;
}
