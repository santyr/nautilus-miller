/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "nautilus-miller-typeahead.h"
#include <pango/pango.h>
#include "nautilus-file.h"
#include "nautilus-files-view.h"
#include "nautilus-list-base.h"
#include "nautilus-view-item.h"
#include "nautilus-view-model.h"
#include <glib/gi18n.h>

static gboolean
valid_text (const char *text)
{
    return text != NULL && g_utf8_validate (text, -1, NULL);
}

static char *
fold (const char *text)
{
    g_autofree char *normalized = g_utf8_normalize (text, -1, G_NORMALIZE_ALL_COMPOSE);
    g_autofree char *casefolded = g_utf8_casefold (normalized, -1);
    return g_utf8_normalize (casefolded, -1, G_NORMALIZE_ALL_COMPOSE);
}

gboolean
nautilus_miller_typeahead_matches (const char *name, const char *prefix)
{
    if (!valid_text (name) || !valid_text (prefix) || prefix[0] == '\0' ||
        g_utf8_strlen (prefix, -1) > MILLER_TYPEAHEAD_MAX_CHARACTERS) { return FALSE; }
    g_autofree char *folded_name = fold (name);
    g_autofree char *folded_prefix = fold (prefix);
    return g_str_has_prefix (folded_name, folded_prefix);
}

char *
nautilus_miller_typeahead_append (const char *prefix, const char *text)
{
    if (!valid_text (prefix) || !valid_text (text) ||
        g_utf8_strlen (prefix, -1) + g_utf8_strlen (text, -1) > MILLER_TYPEAHEAD_MAX_CHARACTERS)
    {
        return NULL;
    }
    return g_strconcat (prefix, text, NULL);
}

char *
nautilus_miller_typeahead_remove_grapheme (const char *text)
{
    if (!valid_text (text)) { return NULL; }
    glong length = g_utf8_strlen (text, -1);
    if (length > MILLER_TYPEAHEAD_MAX_CHARACTERS) { return NULL; }
    if (length == 0) { return g_strdup (""); }
    g_autofree PangoLogAttr *attributes = g_new0 (PangoLogAttr, length + 1);
    pango_get_log_attrs (text, -1, -1, pango_language_get_default (), attributes, length + 1);
    int cut = length - 1;
    while (cut > 0 && !attributes[cut].is_cursor_position) { cut--; }
    return g_strndup (text, g_utf8_offset_to_pointer (text, cut) - text);
}

struct _NautilusMillerTypeahead
{
    GObject parent_instance;
    GWeakRef view, hint;
    NautilusViewModel *model;
    GtkIMContext *context;
    char *prefix, *preedit;
    guint scan, expiry, position;
    guint64 generation, scan_generation;
    gboolean focused, preedit_active, resetting, disposing, applying, watching_view;
    gboolean no_match, limit_hit;
};
G_DEFINE_TYPE (NautilusMillerTypeahead, nautilus_miller_typeahead, G_TYPE_OBJECT)

static void start_scan (NautilusMillerTypeahead *self);

static void
update_hint (NautilusMillerTypeahead *self)
{
    g_autoptr (GtkLabel) hint = g_weak_ref_get (&self->hint);
    if (hint == NULL) { return; }
    g_autofree char *text = self->limit_hit ? g_strdup (_("Name is too long")) :
        g_strdup_printf (self->no_match ? _("No match: %s%s") : _("Select: %s%s"), self->prefix, self->preedit);
    gtk_label_set_text (hint, text);
    gtk_widget_set_visible (GTK_WIDGET (hint), self->focused &&
        (self->prefix[0] != '\0' || self->preedit_active || self->limit_hit));
    gtk_accessible_update_property (GTK_ACCESSIBLE (hint), GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, text, -1);
}

void
nautilus_miller_typeahead_reset (NautilusMillerTypeahead *self)
{
    if (self->resetting) { return; }
    self->resetting = TRUE;
    self->focused = FALSE;
    self->generation++;
    g_clear_handle_id (&self->scan, g_source_remove);
    g_clear_handle_id (&self->expiry, g_source_remove);
    if (self->context != NULL)
    {
        gtk_im_context_reset (self->context);
        gtk_im_context_focus_out (self->context);
        gtk_im_context_set_client_widget (self->context, NULL);
    }
    g_set_str (&self->prefix, "");
    g_set_str (&self->preedit, "");
    self->preedit_active = self->no_match = self->limit_hit = FALSE;
    update_hint (self);
    self->resetting = FALSE;
}

static gboolean
expire (gpointer data)
{
    g_autoptr (NautilusMillerTypeahead) self = g_object_ref (data);
    /* Input inactivity owns the deadline, even if matching has been delayed by
     * higher-priority UI work. Starting preedit already removes this timer. */
    self->expiry = 0;
    nautilus_miller_typeahead_reset (self);
    return G_SOURCE_REMOVE;
}

static void
arm_expiry (NautilusMillerTypeahead *self)
{
    g_clear_handle_id (&self->expiry, g_source_remove);
    if (!self->preedit_active && self->prefix[0] != '\0')
    {
        self->expiry = g_timeout_add (1000, expire, self);
    }
}

static gboolean
scan_chunk (gpointer data)
{
    g_autoptr (NautilusMillerTypeahead) self = g_object_ref (data);
    g_autoptr (NautilusFilesView) view = g_weak_ref_get (&self->view);
    guint64 generation = self->scan_generation;
    if (view == NULL || !self->focused || self->disposing || self->preedit_active ||
        generation != self->generation || nautilus_files_view_get_loading (view))
    {
        self->scan = 0;
        return G_SOURCE_REMOVE;
    }
    gint64 deadline = g_get_monotonic_time () + 4000;
    guint count = g_list_model_get_n_items (G_LIST_MODEL (self->model));
    for (guint examined = 0; self->position < count && examined < 256; examined++)
    {
        guint position = self->position++;
        g_autoptr (GtkTreeListRow) row = g_list_model_get_item (G_LIST_MODEL (self->model), position);
        g_autoptr (NautilusViewItem) item = row != NULL ? gtk_tree_list_row_get_item (row) : NULL;
        const char *name = item != NULL ? nautilus_file_get_display_name (nautilus_view_item_get_file (item)) : NULL;
        if (name != NULL && nautilus_miller_typeahead_matches (name, self->prefix))
        {
            self->scan = 0;
            self->no_match = FALSE;
            self->applying = TRUE;
            nautilus_list_base_set_cursor (nautilus_files_view_get_private_list_base (view), position, TRUE, TRUE);
            self->applying = FALSE;
            if (generation == self->generation) { update_hint (self); }
            return G_SOURCE_REMOVE;
        }
        if (g_get_monotonic_time () >= deadline) { break; }
    }
    if (self->position < count) { return G_SOURCE_CONTINUE; }
    self->scan = 0;
    self->no_match = TRUE;
    update_hint (self);
    return G_SOURCE_REMOVE;
}

static void
start_scan (NautilusMillerTypeahead *self)
{
    self->generation++;
    g_clear_handle_id (&self->scan, g_source_remove);
    self->no_match = FALSE;
    self->position = 0;
    if (self->focused && !self->disposing && !self->preedit_active && self->model != NULL && self->prefix[0] != '\0')
    {
        self->scan_generation = self->generation;
        self->scan = g_idle_add (scan_chunk, self);
    }
    update_hint (self);
}

static void
commit (GtkIMContext *context, const char *text, NautilusMillerTypeahead *self)
{
    if (!self->focused || self->resetting || self->disposing) { return; }
    g_autofree char *next = nautilus_miller_typeahead_append (self->prefix, text);
    self->limit_hit = next == NULL;
    if (next != NULL)
    {
        g_set_str (&self->prefix, next);
        start_scan (self);
        arm_expiry (self);
    }
    update_hint (self);
}

static void
preedit_start (GtkIMContext *context, NautilusMillerTypeahead *self)
{
    if (!self->focused || self->resetting || self->disposing) { return; }
    self->preedit_active = TRUE;
    self->generation++;
    g_clear_handle_id (&self->scan, g_source_remove);
    g_clear_handle_id (&self->expiry, g_source_remove);
    update_hint (self);
}

static void
preedit_changed (GtkIMContext *context, NautilusMillerTypeahead *self)
{
    if (!self->focused || self->resetting || self->disposing) { return; }
    g_autofree char *text = NULL;
    gtk_im_context_get_preedit_string (context, &text, NULL, NULL);
    if (!valid_text (text)) { return; }
    glong length = g_utf8_strlen (text, -1);
    self->limit_hit = length > MILLER_TYPEAHEAD_MAX_CHARACTERS;
    g_free (self->preedit);
    self->preedit = g_utf8_substring (text, 0, MIN (length, MILLER_TYPEAHEAD_MAX_CHARACTERS));
    update_hint (self);
}

static void
preedit_end (GtkIMContext *context, NautilusMillerTypeahead *self)
{
    if (!self->focused || self->resetting || self->disposing) { return; }
    self->preedit_active = FALSE;
    g_set_str (&self->preedit, "");
    start_scan (self);
    arm_expiry (self);
}

static void
model_items_changed (GListModel *model, guint position, guint removed, guint added, NautilusMillerTypeahead *self)
{
    start_scan (self);
}

static void
model_selection_changed (GtkSelectionModel *model, guint position, guint count, NautilusMillerTypeahead *self)
{
    if (self->applying || self->resetting || !self->focused) { return; }
    g_autoptr (NautilusFilesView) view = g_weak_ref_get (&self->view);
    if (view == NULL || !nautilus_files_view_is_selection_auto (view))
    {
        nautilus_miller_typeahead_reset (self);
    }
}

static void
view_loading (NautilusFilesView *view, NautilusMillerTypeahead *self)
{
    nautilus_miller_typeahead_reset (self);
}

static void
view_gone (gpointer data, GObject *view)
{
    NautilusMillerTypeahead *self = data;
    self->watching_view = FALSE;
    nautilus_miller_typeahead_reset (self);
    if (self->model != NULL) { g_signal_handlers_disconnect_by_data (self->model, self); }
    g_clear_object (&self->model);
}

void
nautilus_miller_typeahead_set_focused (NautilusMillerTypeahead *self, gboolean focused)
{
    if (!focused) { nautilus_miller_typeahead_reset (self); return; }
    if (self->disposing) { return; }
    g_autoptr (NautilusFilesView) view = g_weak_ref_get (&self->view);
    if (view == NULL || gtk_widget_get_root (GTK_WIDGET (view)) == NULL) { return; }
    self->focused = TRUE;
    gtk_im_context_set_client_widget (self->context, GTK_WIDGET (view));
    gtk_im_context_focus_in (self->context);
    /* The popup-position helper assumes a selection and scrolls it into view.
     * IM positioning must also work in empty columns and must not scroll on
     * every key release. Use the already realized row when one is available. */
    g_autoptr (GtkBitset) selection = gtk_selection_model_get_selection (GTK_SELECTION_MODEL (self->model));
    g_autoptr (GtkTreeListRow) selected = gtk_bitset_is_empty (selection) ? NULL :
        g_list_model_get_item (G_LIST_MODEL (self->model), gtk_bitset_get_minimum (selection));
    g_autoptr (NautilusViewItem) item = selected != NULL ? gtk_tree_list_row_get_item (selected) : NULL;
    GtkWidget *row = item != NULL ? nautilus_view_item_get_item_ui (item) : NULL;
    graphene_rect_t bounds;
    GdkRectangle cursor = { 8, 0, 1, 1 };
    if (row != NULL && gtk_widget_compute_bounds (row, GTK_WIDGET (view), &bounds))
    {
        cursor.y = bounds.origin.y + bounds.size.height;
    }
    gtk_im_context_set_cursor_location (self->context, &cursor);
}

gboolean
nautilus_miller_typeahead_handle_event (NautilusMillerTypeahead *self, GdkEvent *event)
{
    GdkEventType type = gdk_event_get_event_type (event);
    if (type != GDK_KEY_PRESS && type != GDK_KEY_RELEASE) { return FALSE; }
    g_autoptr (NautilusFilesView) view = g_weak_ref_get (&self->view);
    GtkRoot *root = view != NULL ? gtk_widget_get_root (GTK_WIDGET (view)) : NULL;
    GtkWidget *focus = root != NULL ? gtk_root_get_focus (root) : NULL;
    if (self->disposing || focus == NULL || view == NULL || GTK_IS_EDITABLE (focus) ||
        gtk_widget_get_ancestor (focus, GTK_TYPE_POPOVER) != NULL ||
        (focus != GTK_WIDGET (view) && !gtk_widget_is_ancestor (focus, GTK_WIDGET (view))))
    {
        nautilus_miller_typeahead_set_focused (self, FALSE);
        return FALSE;
    }
    nautilus_miller_typeahead_set_focused (self, TRUE);
    guint keyval = gdk_key_event_get_keyval (event);
    GdkModifierType state = gdk_event_get_modifier_state (event);
    gboolean composing = self->preedit_active;
    if (type == GDK_KEY_PRESS && keyval == GDK_KEY_space && !composing &&
        !(state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK)))
    {
        nautilus_miller_typeahead_reset (self);
        return FALSE;
    }
    if (gtk_im_context_filter_keypress (self->context, event)) { return TRUE; }
    if (type != GDK_KEY_PRESS) { return FALSE; }
    if (composing && !(state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK)))
    {
        if (keyval == GDK_KEY_Escape) { nautilus_miller_typeahead_reset (self); }
        /* IMEs may commit and decline Enter/Space so a text editor can insert
         * them. Here neither may fall through to file activation/preview. */
        return TRUE;
    }
    if (state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK))
    {
        nautilus_miller_typeahead_reset (self);
        return FALSE;
    }
    if (keyval == GDK_KEY_BackSpace && self->prefix[0] != '\0')
    {
        g_autofree char *next = nautilus_miller_typeahead_remove_grapheme (self->prefix);
        g_set_str (&self->prefix, next);
        start_scan (self);
        arm_expiry (self);
        return TRUE;
    }
    gboolean clear = keyval == GDK_KEY_Escape && self->prefix[0] != '\0';
    nautilus_miller_typeahead_reset (self);
    return clear;
}

gboolean nautilus_miller_typeahead_has_preedit (NautilusMillerTypeahead *self) { return self->preedit_active; }
gboolean nautilus_miller_typeahead_get_searching (NautilusMillerTypeahead *self) { return self->scan != 0; }
const char *nautilus_miller_typeahead_get_prefix (NautilusMillerTypeahead *self) { return self->prefix; }

static void
nautilus_miller_typeahead_dispose (GObject *object)
{
    NautilusMillerTypeahead *self = NAUTILUS_MILLER_TYPEAHEAD (object);
    self->disposing = TRUE;
    nautilus_miller_typeahead_reset (self);
    g_autoptr (NautilusFilesView) view = g_weak_ref_get (&self->view);
    if (view != NULL)
    {
        g_signal_handlers_disconnect_by_data (view, self);
        if (self->watching_view) { g_object_weak_unref (G_OBJECT (view), view_gone, self); }
    }
    self->watching_view = FALSE;
    if (self->model != NULL) { g_signal_handlers_disconnect_by_data (self->model, self); }
    if (self->context != NULL) { g_signal_handlers_disconnect_by_data (self->context, self); }
    g_clear_object (&self->model);
    g_clear_object (&self->context);
    G_OBJECT_CLASS (nautilus_miller_typeahead_parent_class)->dispose (object);
}

static void
nautilus_miller_typeahead_finalize (GObject *object)
{
    NautilusMillerTypeahead *self = NAUTILUS_MILLER_TYPEAHEAD (object);
    g_weak_ref_clear (&self->view);
    g_weak_ref_clear (&self->hint);
    g_free (self->prefix); g_free (self->preedit);
    G_OBJECT_CLASS (nautilus_miller_typeahead_parent_class)->finalize (object);
}

static void
nautilus_miller_typeahead_class_init (NautilusMillerTypeaheadClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = nautilus_miller_typeahead_dispose;
    G_OBJECT_CLASS (klass)->finalize = nautilus_miller_typeahead_finalize;
}

static void
nautilus_miller_typeahead_init (NautilusMillerTypeahead *self)
{
    g_weak_ref_init (&self->view, NULL);
    g_weak_ref_init (&self->hint, NULL);
    self->prefix = g_strdup (""); self->preedit = g_strdup ("");
}

NautilusMillerTypeahead *
nautilus_miller_typeahead_new_with_context (NautilusFilesView *view, GtkLabel *hint, GtkIMContext *context)
{
    NautilusMillerTypeahead *self = g_object_new (NAUTILUS_TYPE_MILLER_TYPEAHEAD, NULL);
    g_weak_ref_set (&self->view, view);
    g_weak_ref_set (&self->hint, hint);
    self->context = g_object_ref (context);
    self->model = g_object_ref (nautilus_files_view_get_private_model (view));
    g_signal_connect (context, "commit", G_CALLBACK (commit), self);
    g_signal_connect (context, "preedit-start", G_CALLBACK (preedit_start), self);
    g_signal_connect (context, "preedit-changed", G_CALLBACK (preedit_changed), self);
    g_signal_connect (context, "preedit-end", G_CALLBACK (preedit_end), self);
    g_signal_connect (self->model, "items-changed", G_CALLBACK (model_items_changed), self);
    g_signal_connect (self->model, "selection-changed", G_CALLBACK (model_selection_changed), self);
    g_signal_connect (view, "begin-loading", G_CALLBACK (view_loading), self);
    g_object_weak_ref (G_OBJECT (view), view_gone, self);
    self->watching_view = TRUE;
    gtk_label_set_single_line_mode (hint, TRUE);
    gtk_label_set_ellipsize (hint, PANGO_ELLIPSIZE_END);
    gtk_widget_set_focusable (GTK_WIDGET (hint), FALSE);
    gtk_accessible_update_property (GTK_ACCESSIBLE (hint), GTK_ACCESSIBLE_PROPERTY_LABEL, _("Type to Select"), -1);
    update_hint (self);
    return self;
}

NautilusMillerTypeahead *
nautilus_miller_typeahead_new (NautilusFilesView *view, GtkLabel *hint)
{
    g_autoptr (GtkIMContext) context = gtk_im_multicontext_new ();
    return nautilus_miller_typeahead_new_with_context (view, hint, context);
}
