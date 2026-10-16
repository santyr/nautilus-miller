/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "nautilus-miller-path.h"

struct _NautilusMillerPath
{
    GObject parent_instance;
    GPtrArray *columns;
    guint64 generation;
};
G_DEFINE_TYPE (NautilusMillerPath, nautilus_miller_path, G_TYPE_OBJECT)

static void
nautilus_miller_path_finalize (GObject *object)
{
    NautilusMillerPath *self = NAUTILUS_MILLER_PATH (object);
    g_ptr_array_unref (self->columns);
    G_OBJECT_CLASS (nautilus_miller_path_parent_class)->finalize (object);
}

static void
nautilus_miller_path_class_init (NautilusMillerPathClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = nautilus_miller_path_finalize;
}

static void
nautilus_miller_path_init (NautilusMillerPath *self)
{
    self->columns = g_ptr_array_new_with_free_func (g_object_unref);
}

NautilusMillerPath *
nautilus_miller_path_new (void)
{
    return g_object_new (NAUTILUS_TYPE_MILLER_PATH, NULL);
}

guint
nautilus_miller_path_set_location (NautilusMillerPath *self, GFile *location)
{
    g_autoptr (GPtrArray) reverse = g_ptr_array_new_with_free_func (g_object_unref);
    g_autoptr (GHashTable) seen = g_hash_table_new (g_file_hash, (GEqualFunc) g_file_equal);
    GFile *current = g_object_ref (location);
    guint common = 0;

    /* GFile parents are lexical. Do not resolve symlinks or contact remote hosts. */
    while (current != NULL)
    {
        if (g_hash_table_contains (seen, current) || reverse->len >= 4096)
        {
            g_object_unref (current);
            break;
        }
        g_hash_table_add (seen, current);
        g_ptr_array_add (reverse, current);
        current = g_file_get_parent (current);
    }

    while (common < self->columns->len && common < reverse->len &&
           g_file_equal (self->columns->pdata[common], reverse->pdata[reverse->len - 1 - common]))
    {
        common++;
    }
    if (common == self->columns->len && common == reverse->len)
    {
        return common;
    }
    g_ptr_array_set_size (self->columns, common);
    for (guint i = common; i < reverse->len; i++)
    {
        g_ptr_array_add (self->columns, g_object_ref (reverse->pdata[reverse->len - 1 - i]));
    }
    self->generation++;
    return common;
}

guint
nautilus_miller_path_get_n_columns (NautilusMillerPath *self)
{
    return self->columns->len;
}

GFile *
nautilus_miller_path_get_column (NautilusMillerPath *self, guint position)
{
    g_return_val_if_fail (position < self->columns->len, NULL);
    return self->columns->pdata[position];
}

guint64
nautilus_miller_path_get_generation (NautilusMillerPath *self)
{
    return self->generation;
}

gboolean
nautilus_miller_path_is_current (NautilusMillerPath *self, guint64 generation)
{
    return self->generation == generation;
}
