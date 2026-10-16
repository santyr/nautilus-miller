/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "nautilus-miller-view.h"
#include "nautilus-miller-path.h"
#include "nautilus-files-view.h"
#include "nautilus-file.h"
#include "nautilus-view-info.h"
#include "nautilus-window-slot.h"
#include <glib/gi18n.h>

typedef struct
{
    NautilusMillerView *owner; /* borrowed; owned by the strip */
    NautilusFilesView *view;
    GFile *location;
    GtkWidget *box;
    GtkWidget *grip;
    guint selection_idle;
    int width;
    int drag_width;
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
};
G_DEFINE_TYPE (NautilusMillerView, nautilus_miller_view, ADW_TYPE_BIN)
enum { ACTIVE_VIEW_CHANGED, LOCATION_REQUESTED, LAST_SIGNAL };
static guint signals[LAST_SIGNAL];

static void reconcile (NautilusMillerView *self, GFile *location, NautilusFileList *selection, gboolean preserve_focus);

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

static gboolean
scroll_to_end (GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
    NautilusMillerView *self = data;
    GtkAdjustment *adjustment = gtk_scrolled_window_get_hadjustment (self->scroller);
    gtk_adjustment_set_value (adjustment, gtk_adjustment_get_upper (adjustment) -
                             gtk_adjustment_get_page_size (adjustment));
    self->scroll_tick = 0;
    return G_SOURCE_REMOVE;
}

static void
request_location (NautilusMillerView *self, GFile *location, gboolean preserve_focus)
{
    g_autoptr (GFile) target = g_object_ref (location);
    g_set_object (&self->user_location, target);
    reconcile (self, target, NULL, preserve_focus);
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
    request_location (self, location, TRUE);
    return G_SOURCE_REMOVE;
}

static void
selection_changed (NautilusFilesView *view, MillerColumn *column)
{
    if (column->owner->reconciling || nautilus_files_view_is_selection_auto (view)) { return; }
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
    request_location (self, location, FALSE);
    gtk_widget_grab_focus (GTK_WIDGET (self->active_view));
    return TRUE;
}

static void
focus_enter (GtkEventControllerFocus *controller, MillerColumn *column)
{
    set_active_view (column->owner, column->view);
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
    if (state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK | GDK_SHIFT_MASK)) { return FALSE; }
    if (keyval != GDK_KEY_Left && keyval != GDK_KEY_Right) { return FALSE; }
    if (find_column (self, column->view, &position) == NULL) { return FALSE; }
    if (keyval == GDK_KEY_Left && position > 0)
    {
        MillerColumn *previous = self->columns->pdata[position - 1];
        set_active_view (self, previous->view);
        gtk_widget_grab_focus (GTK_WIDGET (previous->view));
        return TRUE;
    }
    if (keyval == GDK_KEY_Right)
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

static void
drag_update (GtkGestureDrag *gesture, double x, double y, MillerColumn *column)
{
    column->width = CLAMP (column->drag_width + (int) x, 180, 1200);
    gtk_widget_set_size_request (column->box, column->width, -1);
}

static void
column_free (gpointer data)
{
    MillerColumn *column = data;
    g_clear_handle_id (&column->selection_idle, g_source_remove);
    g_signal_handlers_disconnect_by_data (column->view, column);
    nautilus_files_view_set_action_context (column->view, FALSE);
    nautilus_files_view_stop_loading (column->view);
    gtk_box_remove (column->owner->strip, column->box);
    gtk_box_remove (column->owner->strip, column->grip);
    g_object_unref (column->view);
    g_object_unref (column->location);
    g_free (column);
}

static MillerColumn *
column_new (NautilusMillerView *self, GFile *location)
{
    MillerColumn *column = g_new0 (MillerColumn, 1);
    column->owner = self;
    column->location = g_object_ref (location);
    column->width = 280;
    column->view = nautilus_files_view_new (NAUTILUS_VIEW_MILLER_ID, self->slot);
    nautilus_files_view_set_managed_context (column->view, TRUE);
    gtk_widget_add_css_class (GTK_WIDGET (column->view), "miller-file-view");
    column->box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request (column->box, column->width, -1);
    gtk_widget_set_vexpand (column->box, TRUE);
    gtk_widget_add_css_class (column->box, "miller-directory");
    g_autofree char *basename = g_file_get_basename (location);
    GtkWidget *label = gtk_label_new (basename != NULL && basename[0] != '\0' ? basename : "/");
    gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_add_css_class (label, "miller-heading");
    gtk_box_append (GTK_BOX (column->box), label);
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
    g_signal_connect (column->view, "selection-changed", G_CALLBACK (selection_changed), column);
    g_signal_connect (column->view, "column-activate-directory", G_CALLBACK (activate_directory), column);
    gtk_box_append (self->strip, column->box);
    gtk_box_append (self->strip, column->grip);
    return column;
}

static void
reconcile (NautilusMillerView *self, GFile *location, NautilusFileList *selection, gboolean preserve_focus)
{
    self->reconciling = TRUE;
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
        nautilus_files_view_set_branch_file (column->view, child);
        if (i >= common)
        {
            nautilus_files_view_set_location (column->view, column->location);
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
    if (self->scroll_tick != 0) { gtk_widget_remove_tick_callback (GTK_WIDGET (self), self->scroll_tick); }
    self->scroll_tick = gtk_widget_add_tick_callback (GTK_WIDGET (self), scroll_to_end, self, NULL);
}

void
nautilus_miller_view_set_location (NautilusMillerView *self, GFile *location, NautilusFileList *selection)
{
    gboolean preserve = self->user_location != NULL && g_file_equal (self->user_location, location);
    g_clear_object (&self->user_location);
    reconcile (self, location, selection, preserve);
}

NautilusFilesView *nautilus_miller_view_get_active_view (NautilusMillerView *self) { return self->active_view; }
GFile *nautilus_miller_view_get_location (NautilusMillerView *self)
{
    guint count = nautilus_miller_path_get_n_columns (self->path);
    return count > 0 ? nautilus_miller_path_get_column (self->path, count - 1) : NULL;
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
    if (self->scroll_tick != 0)
    {
        gtk_widget_remove_tick_callback (GTK_WIDGET (self), self->scroll_tick);
        self->scroll_tick = 0;
    }
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
nautilus_miller_view_class_init (NautilusMillerViewClass *klass)
{
    GTK_WIDGET_CLASS (klass)->grab_focus = nautilus_miller_view_grab_focus;
    GTK_WIDGET_CLASS (klass)->focus = nautilus_miller_view_focus;
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
