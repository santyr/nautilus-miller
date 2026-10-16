/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <test-utilities.h>
#include <glib/gstdio.h>
#include <fcntl.h>
#include <unistd.h>
#include "miller-test-input.h"
#include "nautilus-application.h"
#include "nautilus-file.h"
#include "nautilus-file-utilities.h"
#include "nautilus-files-view.h"
#include "nautilus-global-preferences.h"
#include "nautilus-miller-typeahead.h"
#include "nautilus-miller-view.h"
#include "nautilus-resources.h"
#include "nautilus-tag-manager.h"
#include "nautilus-view-model.h"
#include "nautilus-view-item.h"
#include "nautilus-window-slot.h"

typedef struct { GtkIMContext parent; char *preedit; } TestIMContext;
typedef struct { GtkIMContextClass parent; } TestIMContextClass;
GType test_im_context_get_type (void);
G_DEFINE_TYPE (TestIMContext, test_im_context, GTK_TYPE_IM_CONTEXT)

static void
test_im_preedit (TestIMContext *self, const char *text)
{
    gboolean before = self->preedit[0] != '\0', after = text[0] != '\0';
    g_free (self->preedit);
    self->preedit = g_strdup (text);
    if (!before && after) { g_signal_emit_by_name (self, "preedit-start"); }
    g_signal_emit_by_name (self, "preedit-changed");
    if (before && !after) { g_signal_emit_by_name (self, "preedit-end"); }
}
static void test_im_reset (GtkIMContext *self) { test_im_preedit ((TestIMContext *) self, ""); }
static void
test_im_get_preedit (GtkIMContext *context, char **text, PangoAttrList **attributes, int *cursor)
{
    TestIMContext *self = (TestIMContext *) context;
    if (text != NULL) { *text = g_strdup (self->preedit); }
    if (attributes != NULL) { *attributes = pango_attr_list_new (); }
    if (cursor != NULL) { *cursor = g_utf8_strlen (self->preedit, -1); }
}
static void test_im_finalize (GObject *object) { g_free (((TestIMContext *) object)->preedit); G_OBJECT_CLASS (test_im_context_parent_class)->finalize (object); }
static void test_im_context_init (TestIMContext *self) { self->preedit = g_strdup (""); }
static void
test_im_context_class_init (TestIMContextClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = test_im_finalize;
    GTK_IM_CONTEXT_CLASS (klass)->get_preedit_string = test_im_get_preedit;
    GTK_IM_CONTEXT_CLASS (klass)->reset = test_im_reset;
}

typedef struct
{
    NautilusWindowSlot *slot;
    NautilusMillerView *strip;
    NautilusFilesView *view;
    NautilusMillerTypeahead *engine;
    TestIMContext *context;
    GtkWidget *window, *hint;
    GFile *root, *alpha, *beta;
} Fixture;
static MillerTestInput *input;

static void
drain (Fixture *f)
{
    gint64 deadline = g_get_monotonic_time () + 15 * G_TIME_SPAN_SECOND;
    while (nautilus_files_view_get_loading (f->view) ||
           nautilus_miller_typeahead_get_searching (f->engine) || g_main_context_pending (NULL))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline);
        g_main_context_iteration (NULL, FALSE);
        g_usleep (1000);
    }
}
static void
assert_selected (Fixture *f, GFile *expected)
{
    g_autolist (NautilusFile) files = nautilus_files_view_get_selection (f->view);
    g_assert_cmpuint (g_list_length (files), ==, 1);
    g_autoptr (GFile) actual = nautilus_file_get_location (files->data);
    g_assert_true (g_file_equal (actual, expected));
}
static void
select_file (Fixture *f, GFile *location)
{
    g_autoptr (NautilusFile) file = nautilus_file_get (location);
    GList selection = {.data = file};
    nautilus_files_view_set_selection (f->view, &selection, FALSE);
}
static void
setup (Fixture *f, gconstpointer large)
{
    f->root = g_file_new_for_path (test_get_tmp_dir ());
    f->alpha = g_file_get_child (f->root, "alpha.txt");
    f->beta = g_file_get_child (f->root, "beta.txt");
    g_assert_true (g_file_replace_contents (f->alpha, "alpha", 5, NULL, FALSE, 0, NULL, NULL, NULL));
    g_assert_true (g_file_replace_contents (f->beta, "beta", 4, NULL, FALSE, 0, NULL, NULL, NULL));
    if (large != NULL)
    {
        for (guint i = 0; i < 1024; i++)
        {
            g_autofree char *name = g_strdup_printf ("item-%04u", i);
            g_autofree char *path = g_build_filename (test_get_tmp_dir (), name, NULL);
            int fd = g_open (path, O_WRONLY | O_CREAT | O_EXCL, 0600);
            g_assert_cmpint (fd, >=, 0);
            close (fd);
        }
    }
    f->slot = nautilus_window_slot_new (NAUTILUS_MODE_BROWSE);
    f->strip = g_object_ref_sink (nautilus_miller_view_new (f->slot));
    f->window = gtk_window_new ();
    gtk_window_set_default_size (GTK_WINDOW (f->window), 900, 600);
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    f->hint = gtk_label_new (NULL);
    gtk_box_append (GTK_BOX (box), f->hint);
    gtk_box_append (GTK_BOX (box), GTK_WIDGET (f->strip));
    gtk_window_set_child (GTK_WINDOW (f->window), box);
    nautilus_miller_view_set_location (f->strip, f->root, NULL);
    f->view = nautilus_miller_view_get_active_view (f->strip);
    f->context = g_object_new (test_im_context_get_type (), NULL);
    f->engine = nautilus_miller_typeahead_new_with_context (f->view, GTK_LABEL (f->hint), GTK_IM_CONTEXT (f->context));
    gtk_window_present (GTK_WINDOW (f->window));
    drain (f);
    gtk_widget_grab_focus (GTK_WIDGET (f->view));
    nautilus_miller_typeahead_set_focused (f->engine, TRUE);
}
static void
teardown (Fixture *f, gconstpointer unused)
{
    g_clear_object (&f->engine);
    if (f->window != NULL) { gtk_window_destroy (GTK_WINDOW (f->window)); }
    g_clear_object (&f->strip);
    g_clear_object (&f->slot);
    g_clear_object (&f->context);
    g_clear_object (&f->root); g_clear_object (&f->alpha); g_clear_object (&f->beta);
    test_clear_tmp_dir ();
}

static void
test_commit_preedit (Fixture *f, gconstpointer unused)
{
    select_file (f, f->alpha);
    drain (f);
    nautilus_miller_typeahead_set_focused (f->engine, TRUE);
    test_im_preedit (f->context, "beta");
    g_assert_true (nautilus_miller_typeahead_has_preedit (f->engine));
    g_assert_nonnull (strstr (gtk_label_get_text (GTK_LABEL (f->hint)), "beta"));
    drain (f);
    assert_selected (f, f->alpha);
    g_signal_emit_by_name (f->context, "commit", "beta");
    test_im_preedit (f->context, "");
    drain (f);
    assert_selected (f, f->beta);
    g_assert_cmpstr (nautilus_miller_typeahead_get_prefix (f->engine), ==, "beta");
    g_assert_false (nautilus_miller_typeahead_has_preedit (f->engine));
}
static void
test_reset_and_new_selection (Fixture *f, gconstpointer unused)
{
    select_file (f, f->alpha); drain (f);
    nautilus_miller_typeahead_set_focused (f->engine, TRUE);
    g_signal_emit_by_name (f->context, "commit", "beta");
    g_assert_true (nautilus_miller_typeahead_get_searching (f->engine));
    nautilus_miller_typeahead_reset (f->engine);
    g_signal_emit_by_name (f->context, "commit", "beta");
    drain (f);
    assert_selected (f, f->alpha);
    g_assert_cmpstr (nautilus_miller_typeahead_get_prefix (f->engine), ==, "");
    nautilus_miller_typeahead_set_focused (f->engine, TRUE);
    g_signal_emit_by_name (f->context, "commit", "beta");
    gtk_selection_model_unselect_all (GTK_SELECTION_MODEL (nautilus_files_view_get_private_model (f->view)));
    select_file (f, f->alpha);
    drain (f);
    assert_selected (f, f->alpha);
    g_assert_cmpstr (nautilus_miller_typeahead_get_prefix (f->engine), ==, "");
}
static gboolean heartbeat (gpointer data) { (*(guint *) data)++; return G_SOURCE_CONTINUE; }
static void
test_bounded_miss_and_dispose (Fixture *f, gconstpointer unused)
{
    guint count = g_list_model_get_n_items (G_LIST_MODEL (nautilus_files_view_get_private_model (f->view)));
    g_assert_cmpuint (count, >=, 1024);
    guint beats = 0;
    guint source = g_idle_add (heartbeat, &beats);
    g_signal_emit_by_name (f->context, "commit", "zzzz-no-match");
    g_assert_true (nautilus_miller_typeahead_get_searching (f->engine));
    gint64 deadline = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
    while (nautilus_miller_typeahead_get_searching (f->engine))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline);
        g_main_context_iteration (NULL, FALSE);
    }
    g_source_remove (source);
    g_assert_cmpuint (beats, >=, 2);
    nautilus_miller_typeahead_reset (f->engine);
    nautilus_miller_typeahead_set_focused (f->engine, TRUE);
    g_signal_emit_by_name (f->context, "commit", "beta");
    gpointer alive = f->engine;
    g_object_add_weak_pointer (G_OBJECT (f->engine), &alive);
    g_clear_object (&f->engine);
    g_assert_null (alive);
    g_signal_emit_by_name (f->context, "commit", "beta");
    while (g_main_context_pending (NULL)) { g_main_context_iteration (NULL, FALSE); }
}
static void
test_location_change (Fixture *f, gconstpointer unused)
{
    g_autoptr (GFile) child = g_file_get_child (f->root, "child");
    g_assert_true (g_file_make_directory (child, NULL, NULL));
    g_signal_emit_by_name (f->context, "commit", "beta");
    nautilus_files_view_set_location (f->view, child);
    drain (f);
    g_assert_cmpstr (nautilus_miller_typeahead_get_prefix (f->engine), ==, "");
    g_assert_false (nautilus_miller_typeahead_get_searching (f->engine));
}
static void
test_large_loaded_model (Fixture *f, gconstpointer unused)
{
    NautilusViewModel *model = nautilus_files_view_get_private_model (f->view);
    g_autolist (NautilusViewItem) items = NULL;
    /* Exercise matching over 100k already-loaded names, without conflating
     * this algorithm check with Stage5's real directory/monitor benchmarks. */
    for (guint i = 0; i < 100000; i++)
    {
        g_autofree char *name = g_strdup_printf ("generated-%06u", i);
        g_autoptr (GFile) location = g_file_get_child (f->root, name);
        g_autoptr (NautilusFile) file = nautilus_file_get (location);
        items = g_list_prepend (items, nautilus_view_item_new (file));
    }
    nautilus_view_model_add_items (model, items);
    g_assert_cmpuint (g_list_model_get_n_items (G_LIST_MODEL (model)), >=, 100000);
    drain (f);
    nautilus_miller_typeahead_set_focused (f->engine, TRUE);
    guint beats = 0;
    guint source = g_idle_add (heartbeat, &beats);
    g_signal_emit_by_name (f->context, "commit", "zzzz-no-match");
    gint64 deadline = g_get_monotonic_time () + 3 * G_TIME_SPAN_SECOND;
    while (nautilus_miller_typeahead_get_searching (f->engine))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline);
        g_main_context_iteration (NULL, FALSE);
    }
    g_source_remove (source);
    g_assert_cmpuint (beats, >=, 300);
    g_assert_nonnull (strstr (gtk_label_get_text (GTK_LABEL (f->hint)), "No match"));
}
static void
test_empty_and_expiry (Fixture *f, gconstpointer unused)
{
    g_autoptr (GFile) child = g_file_get_child (f->root, "empty");
    g_assert_true (g_file_make_directory (child, NULL, NULL));
    nautilus_files_view_set_location (f->view, child);
    drain (f);
    nautilus_miller_typeahead_set_focused (f->engine, TRUE);
    g_signal_emit_by_name (f->context, "commit", "beta");
    drain (f);
    g_assert_cmpstr (nautilus_miller_typeahead_get_prefix (f->engine), ==, "beta");
    g_assert_nonnull (strstr (gtk_label_get_text (GTK_LABEL (f->hint)), "No match"));
    gint64 deadline = g_get_monotonic_time () + 3 * G_TIME_SPAN_SECOND;
    while (nautilus_miller_typeahead_get_prefix (f->engine)[0] != '\0')
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline);
        g_main_context_iteration (NULL, FALSE); g_usleep (1000);
    }
    g_assert_false (gtk_widget_get_visible (f->hint));
}
static void
test_view_destroyed (Fixture *f, gconstpointer unused)
{
    g_signal_emit_by_name (f->context, "commit", "beta");
    g_assert_true (nautilus_miller_typeahead_get_searching (f->engine));
    gpointer alive = f->view;
    g_object_add_weak_pointer (G_OBJECT (f->view), &alive);
    gtk_window_destroy (GTK_WINDOW (f->window));
    f->window = NULL;
    g_clear_object (&f->strip);
    f->view = NULL;
    g_assert_null (alive);
    g_assert_false (nautilus_miller_typeahead_get_searching (f->engine));
    g_assert_cmpstr (nautilus_miller_typeahead_get_prefix (f->engine), ==, "");
    g_signal_emit_by_name (f->context, "commit", "beta");
    while (g_main_context_pending (NULL)) { g_main_context_iteration (NULL, FALSE); }
    g_assert_cmpstr (nautilus_miller_typeahead_get_prefix (f->engine), ==, "");
}
static void
test_expiry_cancels_pending_match (Fixture *f, gconstpointer unused)
{
    guint beats = 0;
    /* Model idle work may be delayed by higher-priority UI work. The user's
     * inactivity deadline still owns the prefix, rather than the scan. */
    guint source = g_idle_add_full (G_PRIORITY_DEFAULT, heartbeat, &beats, NULL);
    g_signal_emit_by_name (f->context, "commit", "beta");
    g_assert_true (nautilus_miller_typeahead_get_searching (f->engine));
    gint64 deadline = g_get_monotonic_time () + 3 * G_TIME_SPAN_SECOND;
    while (nautilus_miller_typeahead_get_prefix (f->engine)[0] != '\0' &&
           g_get_monotonic_time () < deadline)
    {
        g_main_context_iteration (NULL, FALSE); g_usleep (1000);
    }
    g_source_remove (source);
    g_assert_cmpstr (nautilus_miller_typeahead_get_prefix (f->engine), ==, "");
    g_assert_false (nautilus_miller_typeahead_get_searching (f->engine));
}
static gboolean
forward_event (GtkEventControllerLegacy *controller, GdkEvent *event, Fixture *f)
{
    return nautilus_miller_typeahead_handle_event (f->engine, event);
}
static void
test_genuine_keys (Fixture *f, gconstpointer unused)
{
    if (input == NULL) { g_test_skip ("Requires guarded private compositor"); return; }
    g_clear_object (&f->engine);
    f->engine = nautilus_miller_typeahead_new (f->view, GTK_LABEL (f->hint));
    GtkEventController *controller = gtk_event_controller_legacy_new ();
    gtk_event_controller_set_propagation_phase (controller, GTK_PHASE_CAPTURE);
    g_signal_connect (controller, "event", G_CALLBACK (forward_event), f);
    gtk_widget_add_controller (GTK_WIDGET (f->strip), controller);
    gtk_widget_grab_focus (GTK_WIDGET (f->view));
    gint64 deadline = g_get_monotonic_time () + 5 * G_TIME_SPAN_SECOND;
    while (!gtk_window_is_active (GTK_WINDOW (f->window)) && g_get_monotonic_time () < deadline)
    {
        g_main_context_iteration (NULL, FALSE); g_usleep (1000);
    }
    g_assert_true (gtk_window_is_active (GTK_WINDOW (f->window)));
    g_autoptr (GError) error = NULL;
    g_assert_true (miller_test_input_key (input, GDK_KEY_b, TRUE, &error));
    g_assert_true (miller_test_input_key (input, GDK_KEY_b, FALSE, &error));
    g_assert_no_error (error);
    deadline = g_get_monotonic_time () + 3 * G_TIME_SPAN_SECOND;
    while (nautilus_miller_typeahead_get_prefix (f->engine)[0] == '\0' && g_get_monotonic_time () < deadline)
    {
        g_main_context_iteration (NULL, FALSE); g_usleep (1000);
    }
    drain (f);
    assert_selected (f, f->beta);
    g_assert_cmpstr (nautilus_miller_typeahead_get_prefix (f->engine), ==, "b");
    g_assert_true (miller_test_input_key (input, GDK_KEY_BackSpace, TRUE, &error));
    g_assert_true (miller_test_input_key (input, GDK_KEY_BackSpace, FALSE, &error));
    deadline = g_get_monotonic_time () + 3 * G_TIME_SPAN_SECOND;
    while (nautilus_miller_typeahead_get_prefix (f->engine)[0] != '\0' && g_get_monotonic_time () < deadline)
    {
        g_main_context_iteration (NULL, FALSE); g_usleep (1000);
    }
    g_assert_cmpstr (nautilus_miller_typeahead_get_prefix (f->engine), ==, "");
    g_assert_no_error (error);
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
#define CASE(name, data, function) g_test_add ("/miller/typeahead-engine/" name, Fixture, data, setup, function, teardown)
    CASE ("commit-preedit", NULL, test_commit_preedit);
    CASE ("reset-new-selection", NULL, test_reset_and_new_selection);
    CASE ("bounded-miss-dispose", GINT_TO_POINTER (1), test_bounded_miss_and_dispose);
    CASE ("location-change", NULL, test_location_change);
    CASE ("100k-loaded-model", NULL, test_large_loaded_model);
    CASE ("empty-expiry", NULL, test_empty_and_expiry);
    CASE ("view-destroyed", NULL, test_view_destroyed);
    CASE ("pending-match-expiry", NULL, test_expiry_cancels_pending_match);
    CASE ("genuine-keys", NULL, test_genuine_keys);
    int result = g_test_run ();
    miller_test_input_free (input);
    return result;
}
