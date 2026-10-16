/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "nautilus-miller-view.h"
#include "nautilus-miller-path.h"
#include "nautilus-files-view.h"
#include "nautilus-file.h"
#include "nautilus-directory.h"
#include "nautilus-view-model.h"
#include "nautilus-view-info.h"
#include "nautilus-window-slot.h"
#include "nautilus-global-preferences.h"
#include "nautilus-miller-widths.h"
#include "nautilus-view-item.h"
#include <glib/gi18n.h>

typedef struct
{
    NautilusMillerView *owner; /* borrowed; owned by the strip */
    NautilusFilesView *view;
    GFile *location;
    NautilusFile *branch_file;
    NautilusFileList *observed_selection;
    GtkWidget *box;
    GtkWidget *grip;
    GtkMenuButton *options;
    GtkSpinButton *width_spin;
    GtkWidget *fit_button;
    GtkWidget *reset_button;
    GtkWidget *default_button;
    guint selection_idle;
    guint64 selection_scroll_generation;
    int width;
    int drag_width;
    gboolean stopped;
    gboolean branch_reload_requested;
} MillerColumn;

struct _NautilusMillerView
{
    AdwBin parent_instance;
    NautilusWindowSlot *slot; /* the slot owns this widget */
    NautilusMillerPath *path;
    GtkScrolledWindow *scroller;
    GtkBox *strip;
    GPtrArray *columns;
    NautilusFilesView *active_view; /* borrowed from columns */
    GFile *user_location;
    gboolean active;
    gboolean reconciling;
    guint scroll_tick;
    guint scroll_frames;
    guint reveal_primary;
    guint reveal_secondary;
    guint64 scroll_generation;
};
G_DEFINE_TYPE (NautilusMillerView, nautilus_miller_view, ADW_TYPE_BIN)
enum { ACTIVE_VIEW_CHANGED, LOCATION_REQUESTED, LAST_SIGNAL };
static guint signals[LAST_SIGNAL];

static void reconcile (NautilusMillerView *self, GFile *location, NautilusFileList *selection, gboolean preserve_focus, gboolean reveal);

static MillerColumn *
find_column (NautilusMillerView *self, NautilusFilesView *view, guint *position)
{
    for (guint i = 0; i < self->columns->len; i++)
    {
        MillerColumn *column = self->columns->pdata[i];
        if (column->view == view)
        {
            if (position != NULL) { *position = i; }
            return column;
        }
    }
    return NULL;
}

static void
set_active_view (NautilusMillerView *self, NautilusFilesView *view)
{
    if (self->active_view == view) { return; }
    if (self->active_view != NULL)
    {
        nautilus_files_view_set_action_context (self->active_view, FALSE);
        gtk_widget_remove_css_class (GTK_WIDGET (self->active_view), "miller-active");
    }
    self->active_view = view;
    if (view != NULL)
    {
        gtk_widget_add_css_class (GTK_WIDGET (view), "miller-active");
        nautilus_files_view_set_action_context (view, self->active);
        g_signal_emit (self, signals[ACTIVE_VIEW_CHANGED], 0, view);
    }
}

static void
cancel_reveal (NautilusMillerView *self)
{
    if (self->scroll_tick != 0)
    {
        gtk_widget_remove_tick_callback (GTK_WIDGET (self), self->scroll_tick);
        self->scroll_tick = 0;
    }
}

static gboolean
reveal_columns (GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
    NautilusMillerView *self = data;
    if (self->scroll_frames++ == 0) { return G_SOURCE_CONTINUE; }
    self->scroll_tick = 0;
    if (self->reveal_primary >= self->columns->len) { return G_SOURCE_REMOVE; }
    MillerColumn *column = self->columns->pdata[self->reveal_primary];
    graphene_rect_t bounds;
    if (!gtk_widget_compute_bounds (column->box, GTK_WIDGET (self->strip), &bounds))
    {
        return G_SOURCE_REMOVE;
    }
    GtkAdjustment *adjustment = gtk_scrolled_window_get_hadjustment (self->scroller);
    double page = gtk_adjustment_get_page_size (adjustment);
    double start = bounds.origin.x;
    double end = start + bounds.size.width;
    if (self->reveal_secondary < self->columns->len)
    {
        MillerColumn *secondary = self->columns->pdata[self->reveal_secondary];
        graphene_rect_t other;
        if (gtk_widget_compute_bounds (secondary->box, GTK_WIDGET (self->strip), &other))
        {
            double pair_start = MIN (start, other.origin.x);
            double pair_end = MAX (end, other.origin.x + other.size.width);
            if (pair_end - pair_start <= page) { start = pair_start; end = pair_end; }
        }
    }
    double value = gtk_adjustment_get_value (adjustment);
    if (end - start > page)
    {
        /* An overwide column cannot fit. Reveal its reading-order leading edge. */
        value = gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL ? end - page : start;
    }
    else if (start < value) { value = start; }
    else if (end > value + page) { value = end - page; }
    gtk_adjustment_set_value (adjustment, CLAMP (value, gtk_adjustment_get_lower (adjustment),
        MAX (gtk_adjustment_get_lower (adjustment), gtk_adjustment_get_upper (adjustment) - page)));
    return G_SOURCE_REMOVE;
}

static void
schedule_reveal (NautilusMillerView *self, guint primary, guint secondary)
{
    cancel_reveal (self);
    self->reveal_primary = primary;
    self->reveal_secondary = secondary;
    self->scroll_frames = 0;
    self->scroll_tick = gtk_widget_add_tick_callback (GTK_WIDGET (self), reveal_columns, self, NULL);
}

static void
manual_scroll_pressed (GtkGestureClick *gesture, int presses, double x, double y, NautilusMillerView *self)
{
    self->scroll_generation++;
    cancel_reveal (self);
}

static gboolean
manual_scroll (GtkEventControllerScroll *controller, double x, double y, NautilusMillerView *self)
{
    self->scroll_generation++;
    cancel_reveal (self);
    return FALSE;
}

static void
request_location (NautilusMillerView *self, GFile *location, gboolean preserve_focus, gboolean reveal)
{
    g_autoptr (GFile) target = g_object_ref (location);
    g_set_object (&self->user_location, target);
    reconcile (self, target, NULL, preserve_focus, reveal);
    g_signal_emit (self, signals[LOCATION_REQUESTED], 0, target);
}

static gboolean
selection_idle (gpointer data)
{
    MillerColumn *column = data;
    NautilusMillerView *self = column->owner;
    column->selection_idle = 0;
    if (self->reconciling || nautilus_files_view_is_selection_auto (column->view))
    {
        return G_SOURCE_REMOVE;
    }
    g_autolist (NautilusFile) selection = nautilus_files_view_get_selection (column->view);
    g_autoptr (GFile) location = NULL;
    if (selection != NULL && selection->next == NULL && nautilus_file_is_directory (selection->data))
    {
        location = nautilus_file_get_activation_location (selection->data);
    }
    else
    {
        location = g_object_ref (column->location);
    }
    set_active_view (self, column->view);
    request_location (self, location, TRUE, column->selection_scroll_generation == self->scroll_generation);
    return G_SOURCE_REMOVE;
}

static void
selection_model_changed (GtkSelectionModel *model, guint position, guint count, MillerColumn *column)
{
    /* FilesView coalesces notifications through its own idle before our idle.
     * Capture the reveal identity at the original model change, so a scroll
     * between either pair of callbacks invalidates only the older reveal. */
    if (!column->owner->reconciling && !nautilus_files_view_is_selection_auto (column->view))
    {
        column->selection_scroll_generation = column->owner->scroll_generation;
    }
}

static void
selection_changed (NautilusFilesView *view, MillerColumn *column)
{
    g_autolist (NautilusFile) selection = nautilus_files_view_get_selection (view);
    gboolean same = g_list_length (selection) == g_list_length (column->observed_selection);
    if (same && selection != NULL)
    {
        g_autoptr (GHashTable) previous = g_hash_table_new (g_direct_hash, g_direct_equal);
        for (GList *item = column->observed_selection; item != NULL; item = item->next)
        {
            g_hash_table_add (previous, item->data);
        }
        for (GList *item = selection; same && item != NULL; item = item->next)
        {
            same = g_hash_table_contains (previous, item->data);
        }
    }
    nautilus_file_list_free (column->observed_selection);
    column->observed_selection = g_steal_pointer (&selection);
    /* FilesView coalesces selection notifications and also sends them after
     * model updates. An unchanged ancestor selection is not new navigation
     * and must not reclaim action ownership from a child we just entered. */
    if (same || column->owner->reconciling || nautilus_files_view_is_selection_auto (view)) { return; }
    if (column->selection_idle == 0)
    {
        column->selection_idle = g_idle_add (selection_idle, column);
    }
}

static gboolean
activate_directory (NautilusFilesView *view, NautilusFile *file, MillerColumn *column)
{
    NautilusMillerView *self = column->owner;
    g_autoptr (GFile) location = nautilus_file_get_activation_location (file);
    g_clear_handle_id (&column->selection_idle, g_source_remove);
    request_location (self, location, FALSE, TRUE);
    gtk_widget_grab_focus (GTK_WIDGET (self->active_view));
    return TRUE;
}

static void
focus_enter (GtkEventControllerFocus *controller, MillerColumn *column)
{
    set_active_view (column->owner, column->view);
    guint position;
    if (find_column (column->owner, column->view, &position) != NULL)
    {
        schedule_reveal (column->owner, position, G_MAXUINT);
    }
}

static void
pointer_pressed (GtkGestureClick *gesture, int n_press, double x, double y, MillerColumn *column)
{
    set_active_view (column->owner, column->view);
}

static gboolean
key_pressed (GtkEventControllerKey *controller, guint keyval, guint keycode,
             GdkModifierType state, MillerColumn *column)
{
    NautilusMillerView *self = column->owner;
    guint position;
    GtkRoot *root = gtk_widget_get_root (GTK_WIDGET (column->view));
    GtkWidget *focused = root != NULL ? gtk_root_get_focus (root) : NULL;
    if (focused != NULL && (GTK_IS_EDITABLE (focused) ||
        gtk_widget_get_ancestor (focused, GTK_TYPE_POPOVER) != NULL)) { return FALSE; }
    if (state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK | GDK_SHIFT_MASK)) { return FALSE; }
    if (keyval != GDK_KEY_Left && keyval != GDK_KEY_Right) { return FALSE; }
    if (find_column (self, column->view, &position) == NULL) { return FALSE; }
    gboolean rtl = gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL;
    gboolean backward = keyval == (rtl ? GDK_KEY_Right : GDK_KEY_Left);
    if (backward && position > 0)
    {
        MillerColumn *previous = self->columns->pdata[position - 1];
        set_active_view (self, previous->view);
        gtk_widget_grab_focus (GTK_WIDGET (previous->view));
        schedule_reveal (self, position - 1, G_MAXUINT);
        return TRUE;
    }
    if (!backward)
    {
        g_autolist (NautilusFile) selection = nautilus_files_view_get_selection (column->view);
        if (selection != NULL && selection->next == NULL && nautilus_file_is_directory (selection->data))
        {
            return activate_directory (column->view, selection->data, column);
        }
        return TRUE;
    }
    return FALSE;
}

static void
drag_begin (GtkGestureDrag *gesture, double x, double y, MillerColumn *column)
{
    column->drag_width = column->width;
}

static void width_changed (GtkSpinButton *spin, MillerColumn *column);

static void
set_width (MillerColumn *column, int width, gboolean persist)
{
    column->width = CLAMP (width, MILLER_MIN_WIDTH, MILLER_MAX_WIDTH);
    gtk_widget_set_size_request (column->box, column->width, -1);
    g_signal_handlers_block_by_func (column->width_spin, width_changed, column);
    gtk_spin_button_set_value (column->width_spin, column->width);
    g_signal_handlers_unblock_by_func (column->width_spin, width_changed, column);
    if (persist)
    {
        nautilus_miller_width_set (nautilus_preferences, column->location, column->width);
    }
}

static void
width_changed (GtkSpinButton *spin, MillerColumn *column)
{
    set_width (column, gtk_spin_button_get_value_as_int (spin), TRUE);
}

static void
drag_update (GtkGestureDrag *gesture, double x, double y, MillerColumn *column)
{
    int direction = gtk_widget_get_direction (GTK_WIDGET (column->owner)) == GTK_TEXT_DIR_RTL ? -1 : 1;
    set_width (column, column->drag_width + direction * (int) CLAMP (x, -2400, 2400), FALSE);
}

static void
drag_end (GtkGestureDrag *gesture, double x, double y, MillerColumn *column)
{
    nautilus_miller_width_set (nautilus_preferences, column->location, column->width);
}

static void
fit_width (GtkButton *button, MillerColumn *column)
{
    NautilusViewModel *model = nautilus_files_view_get_private_model (column->view);
    guint count = g_list_model_get_n_items (G_LIST_MODEL (model));
    guint samples = MIN (count, 128);
    int width = MILLER_MIN_WIDTH;
    g_autoptr (PangoLayout) layout = gtk_widget_create_pango_layout (column->box, NULL);
    for (guint i = 0; i < samples; i++)
    {
        guint position = samples == 1 ? 0 : (guint64) i * (count - 1) / (samples - 1);
        g_autoptr (GtkTreeListRow) row = g_list_model_get_item (G_LIST_MODEL (model), position);
        g_autoptr (NautilusViewItem) item = gtk_tree_list_row_get_item (row);
        const char *name = nautilus_file_get_display_name (nautilus_view_item_get_file (item));
        int text_width;
        pango_layout_set_text (layout, name, -1);
        pango_layout_get_pixel_size (layout, &text_width, NULL);
        width = MAX (width, text_width + 96); /* icon, disclosure and row padding */
        if (width >= MILLER_MAX_WIDTH) { break; }
    }
    set_width (column, width, TRUE);
}

static void
reset_width (GtkButton *button, MillerColumn *column)
{
    nautilus_miller_width_reset (nautilus_preferences, column->location);
    set_width (column, nautilus_miller_width_get (nautilus_preferences, column->location), FALSE);
}

static void
use_default_width (GtkButton *button, MillerColumn *column)
{
    g_settings_set_uint (nautilus_preferences, MILLER_DEFAULT_WIDTH_KEY, column->width);
}

static GtkWidget *
column_heading (MillerColumn *column, const char *name)
{
    GtkWidget *heading = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class (heading, "miller-heading");
    GtkWidget *label = gtk_label_new (name);
    gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_tooltip_text (label, name);
    gtk_widget_set_hexpand (label, TRUE);
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_box_append (GTK_BOX (heading), label);
    column->options = GTK_MENU_BUTTON (gtk_menu_button_new ());
    gtk_menu_button_set_icon_name (column->options, "view-more-symbolic");
    gtk_widget_add_css_class (GTK_WIDGET (column->options), "flat");
    gtk_widget_set_tooltip_text (GTK_WIDGET (column->options), _("Column Options"));
    gtk_accessible_update_property (GTK_ACCESSIBLE (column->options),
                                    GTK_ACCESSIBLE_PROPERTY_LABEL, _("Column Options"), -1);
    GtkWidget *popover = gtk_popover_new ();
    GtkWidget *content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top (content, 12);
    gtk_widget_set_margin_bottom (content, 12);
    gtk_widget_set_margin_start (content, 12);
    gtk_widget_set_margin_end (content, 12);
    GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *width_label = gtk_label_new_with_mnemonic (_("_Width (pixels)"));
    column->width_spin = GTK_SPIN_BUTTON (gtk_spin_button_new_with_range (MILLER_MIN_WIDTH, MILLER_MAX_WIDTH, 10));
    gtk_spin_button_set_value (column->width_spin, column->width);
    gtk_spin_button_set_numeric (column->width_spin, TRUE);
    gtk_label_set_mnemonic_widget (GTK_LABEL (width_label), GTK_WIDGET (column->width_spin));
    gtk_accessible_update_property (GTK_ACCESSIBLE (column->width_spin),
                                    GTK_ACCESSIBLE_PROPERTY_LABEL, _("Column Width"), -1);
    gtk_box_append (GTK_BOX (row), width_label);
    gtk_box_append (GTK_BOX (row), GTK_WIDGET (column->width_spin));
    gtk_box_append (GTK_BOX (content), row);
    column->fit_button = gtk_button_new_with_label (_("Fit to Contents"));
    column->reset_button = gtk_button_new_with_label (_("Reset Width"));
    column->default_button = gtk_button_new_with_label (_("Use as Default"));
    gtk_box_append (GTK_BOX (content), column->fit_button);
    gtk_box_append (GTK_BOX (content), column->reset_button);
    gtk_box_append (GTK_BOX (content), column->default_button);
    gtk_popover_set_child (GTK_POPOVER (popover), content);
    gtk_menu_button_set_popover (column->options, popover);
    gtk_box_append (GTK_BOX (heading), GTK_WIDGET (column->options));
    g_signal_connect (column->width_spin, "value-changed", G_CALLBACK (width_changed), column);
    g_signal_connect (column->fit_button, "clicked", G_CALLBACK (fit_width), column);
    g_signal_connect (column->reset_button, "clicked", G_CALLBACK (reset_width), column);
    g_signal_connect (column->default_button, "clicked", G_CALLBACK (use_default_width), column);
    return heading;
}

static void
disconnect_column_controllers (GtkWidget *widget, MillerColumn *column)
{
    g_autoptr (GListModel) controllers = gtk_widget_observe_controllers (widget);
    for (guint i = 0; i < g_list_model_get_n_items (controllers); i++)
    {
        g_autoptr (GtkEventController) controller = g_list_model_get_item (controllers, i);
        g_signal_handlers_disconnect_by_data (controller, column);
    }
}

static void
column_free (gpointer data)
{
    MillerColumn *column = data;
    g_clear_handle_id (&column->selection_idle, g_source_remove);
    g_signal_handlers_disconnect_by_data (column->view, column);
    g_signal_handlers_disconnect_by_data (nautilus_files_view_get_private_model (column->view), column);
    disconnect_column_controllers (GTK_WIDGET (column->view), column);
    disconnect_column_controllers (column->grip, column);
    g_signal_handlers_disconnect_by_data (column->width_spin, column);
    g_signal_handlers_disconnect_by_data (column->fit_button, column);
    g_signal_handlers_disconnect_by_data (column->reset_button, column);
    g_signal_handlers_disconnect_by_data (column->default_button, column);
    GtkRoot *root = gtk_widget_get_root (column->box);
    GtkWidget *focused = root != NULL ? gtk_root_get_focus (root) : NULL;
    if (focused != NULL && gtk_widget_is_ancestor (focused, column->box))
    {
        gtk_root_set_focus (root, NULL);
    }
    /* No popdown animation may outlive the directory and its focused editor. */
    gtk_widget_set_visible (GTK_WIDGET (gtk_menu_button_get_popover (column->options)), FALSE);
    nautilus_files_view_set_action_context (column->view, FALSE);
    nautilus_files_view_stop_loading (column->view);
    gtk_box_remove (column->owner->strip, column->box);
    gtk_box_remove (column->owner->strip, column->grip);
    g_object_unref (column->view);
    g_object_unref (column->location);
    g_clear_object (&column->branch_file);
    nautilus_file_list_free (column->observed_selection);
    g_free (column);
}

static void
restore_branch_selection (MillerColumn *column)
{
    if (column->branch_file == NULL || column->selection_idle != 0) { return; }
    g_autolist (NautilusFile) selection = nautilus_files_view_get_selection (column->view);
    if (selection != NULL && !nautilus_files_view_is_selection_auto (column->view)) { return; }
    if (selection != NULL && selection->next == NULL && selection->data == column->branch_file) { return; }
    NautilusViewModel *model = nautilus_files_view_get_private_model (column->view);
    if (!nautilus_files_view_get_loading (column->view) &&
        nautilus_view_model_get_item_for_file (model, column->branch_file) == NULL &&
        !column->branch_reload_requested)
    {
        /* A directory may have been cached while no monitor was installed.
         * An explicitly opened child is evidence that the cache needs refresh. */
        column->branch_reload_requested = TRUE;
        g_autoptr (NautilusDirectory) directory = nautilus_directory_get (column->location);
        nautilus_directory_force_reload (directory);
    }
    GList branch = { .data = column->branch_file };
    nautilus_files_view_set_selection (column->view, &branch, TRUE);
}

static void
column_files_added (NautilusFilesView *view, GList *files, MillerColumn *column)
{
    restore_branch_selection (column);
}

static void
column_end_loading (NautilusFilesView *view, gboolean all_seen, MillerColumn *column)
{
    if (all_seen) { restore_branch_selection (column); }
}

static void
column_loading_changed (NautilusFilesView *view, GParamSpec *pspec, MillerColumn *column)
{
    if (!column->owner->reconciling)
    {
        g_object_notify (G_OBJECT (column->owner), "loading");
    }
}

static MillerColumn *
column_new (NautilusMillerView *self, GFile *location)
{
    MillerColumn *column = g_new0 (MillerColumn, 1);
    column->owner = self;
    column->location = g_object_ref (location);
    column->width = nautilus_miller_width_get (nautilus_preferences, location);
    column->view = nautilus_files_view_new (NAUTILUS_VIEW_MILLER_ID, self->slot);
    nautilus_files_view_set_managed_context (column->view, TRUE);
    gtk_widget_add_css_class (GTK_WIDGET (column->view), "miller-file-view");
    column->box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request (column->box, column->width, -1);
    gtk_widget_set_vexpand (column->box, TRUE);
    gtk_widget_add_css_class (column->box, "miller-directory");
    g_autofree char *basename = g_file_get_basename (location);
    gtk_box_append (GTK_BOX (column->box), column_heading (column,
        basename != NULL && basename[0] != '\0' ? basename : "/"));
    gtk_box_append (GTK_BOX (column->box), GTK_WIDGET (column->view));
    gtk_widget_set_vexpand (GTK_WIDGET (column->view), TRUE);
    gtk_accessible_update_property (GTK_ACCESSIBLE (column->view), GTK_ACCESSIBLE_PROPERTY_LABEL,
                                    basename != NULL ? basename : "/", -1);
    column->grip = gtk_separator_new (GTK_ORIENTATION_VERTICAL);
    gtk_widget_add_css_class (column->grip, "miller-separator");
    gtk_widget_set_cursor_from_name (column->grip, "col-resize");
    GtkGesture *drag = gtk_gesture_drag_new ();
    g_signal_connect (drag, "drag-begin", G_CALLBACK (drag_begin), column);
    g_signal_connect (drag, "drag-update", G_CALLBACK (drag_update), column);
    g_signal_connect (drag, "drag-end", G_CALLBACK (drag_end), column);
    gtk_widget_add_controller (column->grip, GTK_EVENT_CONTROLLER (drag));
    GtkEventController *focus = gtk_event_controller_focus_new ();
    g_signal_connect (focus, "enter", G_CALLBACK (focus_enter), column);
    gtk_widget_add_controller (GTK_WIDGET (column->view), focus);
    GtkGesture *click = gtk_gesture_click_new ();
    gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click), 0);
    gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (click), GTK_PHASE_CAPTURE);
    g_signal_connect (click, "pressed", G_CALLBACK (pointer_pressed), column);
    gtk_widget_add_controller (GTK_WIDGET (column->view), GTK_EVENT_CONTROLLER (click));
    GtkEventController *keys = gtk_event_controller_key_new ();
    gtk_event_controller_set_propagation_phase (keys, GTK_PHASE_CAPTURE);
    g_signal_connect (keys, "key-pressed", G_CALLBACK (key_pressed), column);
    gtk_widget_add_controller (GTK_WIDGET (column->view), keys);
    g_signal_connect_after (column->view, "add-files", G_CALLBACK (column_files_added), column);
    g_signal_connect_after (column->view, "end-loading", G_CALLBACK (column_end_loading), column);
    g_signal_connect (column->view, "notify::loading", G_CALLBACK (column_loading_changed), column);
    g_signal_connect (column->view, "selection-changed", G_CALLBACK (selection_changed), column);
    g_signal_connect (nautilus_files_view_get_private_model (column->view), "selection-changed",
                      G_CALLBACK (selection_model_changed), column);
    g_signal_connect (column->view, "column-activate-directory", G_CALLBACK (activate_directory), column);
    gtk_box_append (self->strip, column->box);
    gtk_box_append (self->strip, column->grip);
    return column;
}

static void
reconcile (NautilusMillerView *self, GFile *location, NautilusFileList *selection, gboolean preserve_focus, gboolean reveal)
{
    self->reconciling = TRUE;
    GFile *previous_location = nautilus_miller_view_get_location (self);
    gboolean changed = previous_location == NULL || !g_file_equal (previous_location, location);
    guint common = nautilus_miller_path_set_location (self->path, location);
    guint count = nautilus_miller_path_get_n_columns (self->path);
    guint active_index = 0;
    if (self->active_view != NULL && find_column (self, self->active_view, &active_index) != NULL && active_index >= common)
    {
        set_active_view (self, NULL);
    }
    g_ptr_array_set_size (self->columns, common);
    for (guint i = common; i < count; i++)
    {
        g_ptr_array_add (self->columns, column_new (self, nautilus_miller_path_get_column (self->path, i)));
    }
    for (guint i = 0; i < count; i++)
    {
        MillerColumn *column = self->columns->pdata[i];
        g_autoptr (NautilusFile) child = i + 1 < count ? nautilus_file_get (
            nautilus_miller_path_get_column (self->path, i + 1)) : NULL;
        if (g_set_object (&column->branch_file, child)) { column->branch_reload_requested = FALSE; }
        nautilus_files_view_set_branch_file (column->view, child);
        if (i >= common || column->stopped)
        {
            nautilus_files_view_set_location (column->view, column->location);
            column->stopped = FALSE;
        }
        if (child != NULL)
        {
            GList branch_selection = { .data = child };
            nautilus_files_view_set_selection (column->view, &branch_selection, TRUE);
        }
        else if (selection != NULL && !preserve_focus)
        {
            nautilus_files_view_set_selection (column->view, selection, TRUE);
        }
    }
    if (!preserve_focus || self->active_view == NULL)
    {
        MillerColumn *last = self->columns->pdata[count - 1];
        set_active_view (self, last->view);
    }
    self->reconciling = FALSE;
    g_object_notify (G_OBJECT (self), "loading");
    if (reveal && (changed || !preserve_focus))
    {
        if (find_column (self, self->active_view, &active_index) != NULL)
        {
            schedule_reveal (self, active_index, preserve_focus && active_index + 2 == count ? count - 1 : G_MAXUINT);
        }
    }
}

void
nautilus_miller_view_set_location (NautilusMillerView *self, GFile *location, NautilusFileList *selection)
{
    gboolean preserve = self->user_location != NULL && g_file_equal (self->user_location, location);
    g_clear_object (&self->user_location);
    reconcile (self, location, selection, preserve, TRUE);
}

NautilusFilesView *nautilus_miller_view_get_active_view (NautilusMillerView *self) { return self->active_view; }
GFile *nautilus_miller_view_get_location (NautilusMillerView *self)
{
    guint count = nautilus_miller_path_get_n_columns (self->path);
    return count > 0 ? nautilus_miller_path_get_column (self->path, count - 1) : NULL;
}

gboolean
nautilus_miller_view_get_loading (NautilusMillerView *self)
{
    for (guint i = 0; i < self->columns->len; i++)
    {
        MillerColumn *column = self->columns->pdata[i];
        if (nautilus_files_view_get_loading (column->view)) { return TRUE; }
    }
    return FALSE;
}

void
nautilus_miller_view_set_active (NautilusMillerView *self, gboolean active)
{
    self->active = active;
    if (self->active_view != NULL) { nautilus_files_view_set_action_context (self->active_view, active); }
}

void
nautilus_miller_view_stop_loading (NautilusMillerView *self)
{
    for (guint i = 0; i < self->columns->len; i++)
    {
        MillerColumn *column = self->columns->pdata[i];
        column->stopped = TRUE;
        nautilus_files_view_stop_loading (column->view);
    }
}

void
nautilus_miller_view_reload (NautilusMillerView *self)
{
    for (guint i = 0; i < self->columns->len; i++)
    {
        MillerColumn *column = self->columns->pdata[i];
        g_autolist (NautilusFile) selection = nautilus_files_view_get_selection (column->view);
        nautilus_files_view_set_location (column->view, column->location);
        nautilus_files_view_set_selection (column->view, selection, TRUE);
    }
}

static void
nautilus_miller_view_dispose (GObject *object)
{
    NautilusMillerView *self = NAUTILUS_MILLER_VIEW (object);
    cancel_reveal (self);
    self->active_view = NULL;
    g_ptr_array_set_size (self->columns, 0);
    g_clear_object (&self->path);
    g_clear_object (&self->user_location);
    G_OBJECT_CLASS (nautilus_miller_view_parent_class)->dispose (object);
}

static void
nautilus_miller_view_finalize (GObject *object)
{
    g_ptr_array_unref (NAUTILUS_MILLER_VIEW (object)->columns);
    G_OBJECT_CLASS (nautilus_miller_view_parent_class)->finalize (object);
}

static gboolean
nautilus_miller_view_grab_focus (GtkWidget *widget)
{
    NautilusMillerView *self = NAUTILUS_MILLER_VIEW (widget);
    return self->active_view != NULL && gtk_widget_grab_focus (GTK_WIDGET (self->active_view));
}

static gboolean
nautilus_miller_view_focus (GtkWidget *widget, GtkDirectionType direction)
{
    GtkRoot *root = gtk_widget_get_root (widget);
    GtkWidget *focused = root != NULL ? gtk_root_get_focus (root) : NULL;
    if (focused == NULL || !gtk_widget_is_ancestor (focused, widget))
    {
        return nautilus_miller_view_grab_focus (widget);
    }
    return GTK_WIDGET_CLASS (nautilus_miller_view_parent_class)->focus (widget, direction);
}

static void
nautilus_miller_view_get_property (GObject *object, guint id, GValue *value, GParamSpec *pspec)
{
    if (id == 1)
    {
        g_value_set_boolean (value, nautilus_miller_view_get_loading (NAUTILUS_MILLER_VIEW (object)));
    }
    else { G_OBJECT_WARN_INVALID_PROPERTY_ID (object, id, pspec); }
}

static void
nautilus_miller_view_class_init (NautilusMillerViewClass *klass)
{
    GTK_WIDGET_CLASS (klass)->grab_focus = nautilus_miller_view_grab_focus;
    GTK_WIDGET_CLASS (klass)->focus = nautilus_miller_view_focus;
    G_OBJECT_CLASS (klass)->get_property = nautilus_miller_view_get_property;
    g_object_class_install_property (G_OBJECT_CLASS (klass), 1,
        g_param_spec_boolean ("loading", NULL, NULL, FALSE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
    G_OBJECT_CLASS (klass)->dispose = nautilus_miller_view_dispose;
    G_OBJECT_CLASS (klass)->finalize = nautilus_miller_view_finalize;
    signals[ACTIVE_VIEW_CHANGED] = g_signal_new ("active-view-changed", G_TYPE_FROM_CLASS (klass),
        G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, NAUTILUS_TYPE_FILES_VIEW);
    signals[LOCATION_REQUESTED] = g_signal_new ("location-requested", G_TYPE_FROM_CLASS (klass),
        G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_FILE);
}

static void
nautilus_miller_view_init (NautilusMillerView *self)
{
    self->path = nautilus_miller_path_new ();
    self->columns = g_ptr_array_new_with_free_func (column_free);
    self->strip = GTK_BOX (gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0));
    self->scroller = GTK_SCROLLED_WINDOW (gtk_scrolled_window_new ());
    gtk_scrolled_window_set_policy (self->scroller, GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_child (self->scroller, GTK_WIDGET (self->strip));
    GtkEventController *scroll = gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
    gtk_event_controller_set_propagation_phase (scroll, GTK_PHASE_CAPTURE);
    g_signal_connect_object (scroll, "scroll", G_CALLBACK (manual_scroll), self, 0);
    gtk_widget_add_controller (GTK_WIDGET (self->scroller), scroll);
    GtkGesture *press = gtk_gesture_click_new ();
    gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (press), 0);
    gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (press), GTK_PHASE_CAPTURE);
    g_signal_connect_object (press, "pressed", G_CALLBACK (manual_scroll_pressed), self, 0);
    gtk_widget_add_controller (GTK_WIDGET (self->scroller), GTK_EVENT_CONTROLLER (press));
    adw_bin_set_child (ADW_BIN (self), GTK_WIDGET (self->scroller));
    gtk_widget_add_css_class (GTK_WIDGET (self), "nautilus-miller-view");
}

NautilusMillerView *
nautilus_miller_view_new (NautilusWindowSlot *slot)
{
    NautilusMillerView *self = g_object_new (NAUTILUS_TYPE_MILLER_VIEW, NULL);
    self->slot = slot;
    return self;
}
