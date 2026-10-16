/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "nautilus-list-base.h"
G_BEGIN_DECLS
#define NAUTILUS_TYPE_MILLER_COLUMN (nautilus_miller_column_get_type ())
G_DECLARE_FINAL_TYPE (NautilusMillerColumn, nautilus_miller_column, NAUTILUS, MILLER_COLUMN, NautilusListBase)
NautilusMillerColumn *nautilus_miller_column_new (void);
G_END_DECLS
