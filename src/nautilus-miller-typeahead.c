/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "nautilus-miller-typeahead.h"
#include <pango/pango.h>

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
