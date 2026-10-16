/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "nautilus-miller-column.h"
#include "nautilus-list-base-private.h"
#include "nautilus-file.h"
#include "nautilus-global-preferences.h"
#include "nautilus-name-cell.h"
#include "nautilus-view-item.h"
#include "nautilus-view-model.h"
#include <glib/gi18n.h>

struct _NautilusMillerColumn
{
    NautilusListBase parent_instance;
    GtkListView *list;
    GQuark sort_attribute;
    gboolean reversed;
    int zoom;
};
G_DEFINE_TYPE (NautilusMillerColumn, nautilus_miller_column, NAUTILUS_TYPE_LIST_BASE)

static NautilusViewInfo
get_view_info (NautilusListBase *base)
{
    return (NautilusViewInfo) { NAUTILUS_VIEW_MILLER_ID, NAUTILUS_LIST_ZOOM_LEVEL_SMALL,
        NAUTILUS_LIST_ZOOM_LEVEL_LARGE, NAUTILUS_LIST_ZOOM_LEVEL_MEDIUM };
}

static guint
get_icon_size (NautilusListBase *base)
{
    const guint sizes[] = { NAUTILUS_LIST_ICON_SIZE_SMALL, NAUTILUS_LIST_ICON_SIZE_MEDIUM,
                            NAUTILUS_LIST_ICON_SIZE_LARGE };
    return sizes[NAUTILUS_MILLER_COLUMN (base)->zoom];
}

static int get_zoom_level (NautilusListBase *base) { return NAUTILUS_MILLER_COLUMN (base)->zoom; }

static void
set_zoom_level (NautilusListBase *base, int zoom)
{
    NAUTILUS_MILLER_COLUMN (base)->zoom = CLAMP (zoom, NAUTILUS_LIST_ZOOM_LEVEL_SMALL,
                                               NAUTILUS_LIST_ZOOM_LEVEL_LARGE);
    g_object_notify (G_OBJECT (base), "icon-size");
}

static GVariant *
get_sort_state (NautilusListBase *base)
{
    NautilusMillerColumn *self = NAUTILUS_MILLER_COLUMN (base);
    return g_variant_ref_sink (g_variant_new ("(sb)", g_quark_to_string (self->sort_attribute), self->reversed));
}

static gint
sort_items (gconstpointer a, gconstpointer b, gpointer data)
{
    NautilusMillerColumn *self = data;
    return nautilus_file_compare_for_sort_by_attribute_q (
        nautilus_view_item_get_file ((NautilusViewItem *) a),
        nautilus_view_item_get_file ((NautilusViewItem *) b), self->sort_attribute,
        g_settings_get_boolean (gtk_filechooser_preferences, NAUTILUS_PREFERENCES_SORT_DIRECTORIES_FIRST),
        self->reversed);
}

static void
set_sort_state (NautilusListBase *base, GVariant *value)
{
    NautilusMillerColumn *self = NAUTILUS_MILLER_COLUMN (base);
    const char *attribute;
    g_variant_get (value, "(&sb)", &attribute, &self->reversed);
    self->sort_attribute = g_quark_from_string (attribute);
    NautilusViewModel *model = nautilus_list_base_get_model (base);
    if (model != NULL)
    {
        g_autoptr (GtkCustomSorter) sorter = gtk_custom_sorter_new (sort_items, self, NULL);
        nautilus_view_model_set_sorter (model, GTK_SORTER (sorter));
    }
}

static void
scroll_to (NautilusListBase *base, guint position, GtkListScrollFlags flags, GtkScrollInfo *scroll)
{
    gtk_list_view_scroll_to (NAUTILUS_MILLER_COLUMN (base)->list, position, flags, scroll);
}

static void
set_enable_rubberband (NautilusListBase *base, gboolean enabled)
{
    NautilusViewModel *model = nautilus_list_base_get_model (base);
    gtk_list_view_set_enable_rubberband (NAUTILUS_MILLER_COLUMN (base)->list,
        enabled && model != NULL && !nautilus_view_model_get_single_selection (model));
}

static void
activate (GtkListView *list, guint position, NautilusMillerColumn *self)
{
    nautilus_list_base_activate_selection (NAUTILUS_LIST_BASE (self), FALSE);
}

static void
setup_cell (GtkSignalListItemFactory *factory, GtkListItem *listitem, NautilusMillerColumn *self)
{
    NautilusViewCell *cell = nautilus_name_cell_new (NAUTILUS_LIST_BASE (self));
    GtkWidget *content = nautilus_name_cell_get_content (NAUTILUS_NAME_CELL (cell));
    GtkWidget *arrow = gtk_image_new_from_icon_name ("go-next-symbolic");
    gtk_widget_add_css_class (arrow, "dim-label");
    gtk_box_append (GTK_BOX (content), arrow);
    g_object_set_data (G_OBJECT (listitem), "miller-arrow", arrow);
    gtk_list_item_set_child (listitem, GTK_WIDGET (cell));
    setup_cell_common (G_OBJECT (listitem), cell);
    setup_cell_hover (cell);
    g_object_bind_property (self, "icon-size", cell, "icon-size", G_BINDING_SYNC_CREATE);
    GtkExpression *expression = gtk_property_expression_new (GTK_TYPE_LIST_ITEM, NULL, "item");
    expression = gtk_property_expression_new (GTK_TYPE_TREE_LIST_ROW, expression, "item");
    expression = gtk_property_expression_new (NAUTILUS_TYPE_VIEW_ITEM, expression, "file");
    expression = gtk_property_expression_new (NAUTILUS_TYPE_FILE, expression, "a11y-name");
    gtk_expression_bind (expression, listitem, "accessible-label", listitem);
}

static void
bind_cell (GtkSignalListItemFactory *factory, GtkListItem *listitem, NautilusMillerColumn *self)
{
    g_autoptr (NautilusViewItem) item = gtk_tree_list_row_get_item (GTK_TREE_LIST_ROW (gtk_list_item_get_item (listitem)));
    nautilus_view_item_set_item_ui (item, gtk_list_item_get_child (listitem));
    gtk_widget_set_visible (g_object_get_data (G_OBJECT (listitem), "miller-arrow"),
                            nautilus_file_is_directory (nautilus_view_item_get_file (item)));
}

static void
unbind_cell (GtkSignalListItemFactory *factory, GtkListItem *listitem, NautilusMillerColumn *self)
{
    GtkTreeListRow *row = gtk_list_item_get_item (listitem);
    if (row != NULL)
    {
        g_autoptr (NautilusViewItem) item = gtk_tree_list_row_get_item (row);
        if (item != NULL) { nautilus_view_item_set_item_ui (item, NULL); }
    }
}

static void
model_changed (NautilusMillerColumn *self)
{
    NautilusViewModel *model = nautilus_list_base_get_model (NAUTILUS_LIST_BASE (self));
    gtk_list_view_set_model (self->list, GTK_SELECTION_MODEL (model));
    if (model != NULL)
    {
        g_autoptr (GVariant) state = get_sort_state (NAUTILUS_LIST_BASE (self));
        set_sort_state (NAUTILUS_LIST_BASE (self), state);
    }
}

static gboolean
miller_grab_focus (GtkWidget *widget)
{
    NautilusViewModel *model = nautilus_list_base_get_model (NAUTILUS_LIST_BASE (widget));
    if (model == NULL || g_list_model_get_n_items (G_LIST_MODEL (model)) == 0)
    {
        GtkWidget *scroller = nautilus_list_base_get_scrolled_window (NAUTILUS_LIST_BASE (widget));
        gtk_widget_set_focusable (scroller, TRUE);
        return gtk_widget_grab_focus (scroller);
    }
    return GTK_WIDGET_CLASS (nautilus_miller_column_parent_class)->grab_focus (widget);
}

static void
nautilus_miller_column_class_init (NautilusMillerColumnClass *klass)
{
    GTK_WIDGET_CLASS (klass)->grab_focus = miller_grab_focus;
    NautilusListBaseClass *base = NAUTILUS_LIST_BASE_CLASS (klass);
    base->get_view_info = get_view_info;
    base->get_icon_size = get_icon_size;
    base->get_zoom_level = get_zoom_level;
    base->set_zoom_level = set_zoom_level;
    base->get_sort_state = get_sort_state;
    base->set_sort_state = set_sort_state;
    base->scroll_to = scroll_to;
    base->set_enable_rubberband = set_enable_rubberband;
}

static void
nautilus_miller_column_init (NautilusMillerColumn *self)
{
    self->sort_attribute = g_quark_from_static_string ("name");
    self->zoom = NAUTILUS_LIST_ZOOM_LEVEL_MEDIUM;
    GtkListItemFactory *factory = gtk_signal_list_item_factory_new ();
    g_signal_connect (factory, "setup", G_CALLBACK (setup_cell), self);
    g_signal_connect (factory, "bind", G_CALLBACK (bind_cell), self);
    g_signal_connect (factory, "unbind", G_CALLBACK (unbind_cell), self);
    self->list = GTK_LIST_VIEW (gtk_list_view_new (NULL, factory));
    gtk_list_view_set_single_click_activate (self->list, FALSE);
    gtk_list_view_set_tab_behavior (self->list, GTK_LIST_TAB_ITEM);
    g_signal_connect (self->list, "activate", G_CALLBACK (activate), self);
    gtk_accessible_update_property (GTK_ACCESSIBLE (self->list), GTK_ACCESSIBLE_PROPERTY_LABEL,
                                    _("Folder Column"), -1);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (nautilus_list_base_get_scrolled_window (NAUTILUS_LIST_BASE (self))),
                                   GTK_WIDGET (self->list));
    nautilus_list_base_disable_single_click_activation (NAUTILUS_LIST_BASE (self));
    nautilus_list_base_setup_gestures (NAUTILUS_LIST_BASE (self));
    g_signal_connect_swapped (self, "notify::model", G_CALLBACK (model_changed), self);
    gtk_widget_add_css_class (GTK_WIDGET (self), "nautilus-miller-column");
}

NautilusMillerColumn *nautilus_miller_column_new (void)
{
    return g_object_new (NAUTILUS_TYPE_MILLER_COLUMN, NULL);
}
