/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <glib.h>
#include <gtk/gtk.h>
#include "nautilus-types.h"

#define MILLER_TYPEAHEAD_MAX_CHARACTERS 256

/* Matching changes no filesystem identity. Buffers retain the entered text;
 * only comparisons apply compatibility normalization and Unicode case fold. */
gboolean nautilus_miller_typeahead_matches (const char *name, const char *prefix);
char *nautilus_miller_typeahead_append (const char *prefix, const char *text);
char *nautilus_miller_typeahead_remove_grapheme (const char *text);

#define NAUTILUS_TYPE_MILLER_TYPEAHEAD (nautilus_miller_typeahead_get_type ())
G_DECLARE_FINAL_TYPE (NautilusMillerTypeahead, nautilus_miller_typeahead, NAUTILUS, MILLER_TYPEAHEAD, GObject)
NautilusMillerTypeahead *nautilus_miller_typeahead_new (NautilusFilesView *view, GtkLabel *hint);
NautilusMillerTypeahead *nautilus_miller_typeahead_new_with_context (NautilusFilesView *view, GtkLabel *hint, GtkIMContext *context);
gboolean nautilus_miller_typeahead_handle_event (NautilusMillerTypeahead *self, GdkEvent *event);
void nautilus_miller_typeahead_reset (NautilusMillerTypeahead *self);
void nautilus_miller_typeahead_set_focused (NautilusMillerTypeahead *self, gboolean focused);
gboolean nautilus_miller_typeahead_has_preedit (NautilusMillerTypeahead *self);
gboolean nautilus_miller_typeahead_get_searching (NautilusMillerTypeahead *self);
const char *nautilus_miller_typeahead_get_prefix (NautilusMillerTypeahead *self);
