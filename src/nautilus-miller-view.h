/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <adwaita.h>
#include "nautilus-types.h"
G_BEGIN_DECLS
#define NAUTILUS_TYPE_MILLER_VIEW (nautilus_miller_view_get_type ())
G_DECLARE_FINAL_TYPE (NautilusMillerView, nautilus_miller_view, NAUTILUS, MILLER_VIEW, AdwBin)
NautilusMillerView *nautilus_miller_view_new (NautilusWindowSlot *slot);
void nautilus_miller_view_set_location (NautilusMillerView *self, GFile *location, NautilusFileList *selection);
NautilusFilesView *nautilus_miller_view_get_active_view (NautilusMillerView *self);
GFile *nautilus_miller_view_get_location (NautilusMillerView *self);
gboolean nautilus_miller_view_get_loading (NautilusMillerView *self);
void nautilus_miller_view_set_active (NautilusMillerView *self, gboolean active);
void nautilus_miller_view_stop_loading (NautilusMillerView *self);
void nautilus_miller_view_reload (NautilusMillerView *self);
G_END_DECLS
