/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <gio/gio.h>
G_BEGIN_DECLS
#define NAUTILUS_TYPE_MILLER_PATH (nautilus_miller_path_get_type ())
G_DECLARE_FINAL_TYPE (NautilusMillerPath, nautilus_miller_path, NAUTILUS, MILLER_PATH, GObject)
NautilusMillerPath *nautilus_miller_path_new (void);
guint nautilus_miller_path_set_location (NautilusMillerPath *self, GFile *location);
guint nautilus_miller_path_get_n_columns (NautilusMillerPath *self);
GFile *nautilus_miller_path_get_column (NautilusMillerPath *self, guint position);
guint64 nautilus_miller_path_get_generation (NautilusMillerPath *self);
gboolean nautilus_miller_path_is_current (NautilusMillerPath *self, guint64 generation);
G_END_DECLS
