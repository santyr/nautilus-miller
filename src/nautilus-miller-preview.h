/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <adwaita.h>
#include "nautilus-types.h"

#define NAUTILUS_TYPE_MILLER_PREVIEW (nautilus_miller_preview_get_type ())
G_DECLARE_FINAL_TYPE (NautilusMillerPreview, nautilus_miller_preview, NAUTILUS, MILLER_PREVIEW, AdwBin)

GtkWidget *nautilus_miller_preview_new (void);
void nautilus_miller_preview_set_selection (NautilusMillerPreview *self, NautilusFileList *files);
void nautilus_miller_preview_set_active (NautilusMillerPreview *self, gboolean active);
