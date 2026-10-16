/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <glib.h>

#define MILLER_TYPEAHEAD_MAX_CHARACTERS 256

/* Matching changes no filesystem identity. Buffers retain the entered text;
 * only comparisons apply compatibility normalization and Unicode case fold. */
gboolean nautilus_miller_typeahead_matches (const char *name, const char *prefix);
char *nautilus_miller_typeahead_append (const char *prefix, const char *text);
char *nautilus_miller_typeahead_remove_grapheme (const char *text);
