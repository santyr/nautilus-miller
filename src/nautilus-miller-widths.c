/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "nautilus-miller-widths.h"

#define MAX_WIDTHS 128

static gboolean
safe_identity (const char *identity)
{
    if (strlen (identity) > 4096) { return FALSE; }
    g_autoptr (GUri) uri = g_uri_parse (identity, G_URI_FLAGS_NONE, NULL);
    if (uri == NULL || g_uri_get_userinfo (uri) != NULL ||
        g_uri_get_query (uri) != NULL || g_uri_get_fragment (uri) != NULL)
    {
        return FALSE;
    }
    const char *scheme = g_uri_get_scheme (uri);
    const char *host = g_uri_get_host (uri);
    const char *path = g_uri_get_path (uri);
    if (path == NULL || path[0] != '/') { return FALSE; }
    if (g_strcmp0 (scheme, "file") == 0)
    {
        return host == NULL || host[0] == '\0' || g_str_equal (host, "localhost");
    }
    return (g_strcmp0 (scheme, "sftp") == 0 || g_strcmp0 (scheme, "smb") == 0) &&
           host != NULL && host[0] != '\0';
}

/* One bounded settings value is the MRU. Always sanitize before rewriting:
 * an older or manually edited setting must not retain unsafe URI identities. */
static int
update_widths (GSettings *settings, GFile *location, int requested, gboolean reset)
{
    g_autofree char *identity = g_file_get_uri (location);
    g_autoptr (GVariant) current = g_settings_get_value (settings, MILLER_WIDTHS_KEY);
    int width = g_settings_get_uint (settings, MILLER_DEFAULT_WIDTH_KEY);
    gboolean safe = safe_identity (identity);
    gboolean found = FALSE;
    GVariantIter iter;
    const char *key;
    guint stored;
    g_variant_iter_init (&iter, current);
    while (safe && g_variant_iter_next (&iter, "(&su)", &key, &stored))
    {
        if (g_str_equal (key, identity) && stored >= MILLER_MIN_WIDTH && stored <= MILLER_MAX_WIDTH)
        {
            width = stored;
            found = TRUE;
            break;
        }
    }
    GVariantBuilder builder;
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(su)"));
    g_autoptr (GHashTable) seen = g_hash_table_new (g_str_hash, g_str_equal);
    guint count = 0;
    if (safe && !reset && (requested != 0 || found))
    {
        if (requested != 0) { width = CLAMP (requested, MILLER_MIN_WIDTH, MILLER_MAX_WIDTH); }
        g_variant_builder_add (&builder, "(su)", identity, (guint) width);
        g_hash_table_add (seen, identity);
        count++;
    }
    g_variant_iter_init (&iter, current);
    while (count < MAX_WIDTHS && g_variant_iter_next (&iter, "(&su)", &key, &stored))
    {
        if ((safe && reset && g_str_equal (key, identity)) ||
            stored < MILLER_MIN_WIDTH || stored > MILLER_MAX_WIDTH ||
            !safe_identity (key) || !g_hash_table_add (seen, (gpointer) key))
        {
            continue;
        }
        g_variant_builder_add (&builder, "(su)", key, stored);
        count++;
    }
    g_autoptr (GVariant) updated = g_variant_ref_sink (g_variant_builder_end (&builder));
    if (!g_variant_equal (current, updated))
    {
        g_settings_set_value (settings, MILLER_WIDTHS_KEY, updated);
    }
    return reset ? g_settings_get_uint (settings, MILLER_DEFAULT_WIDTH_KEY) : width;
}

int
nautilus_miller_width_get (GSettings *settings, GFile *location)
{
    return update_widths (settings, location, 0, FALSE);
}

void
nautilus_miller_width_set (GSettings *settings, GFile *location, int width)
{
    /* Zero is the internal read sentinel, not a caller's width policy. */
    update_widths (settings, location, CLAMP (width, MILLER_MIN_WIDTH, MILLER_MAX_WIDTH), FALSE);
}

void
nautilus_miller_width_reset (GSettings *settings, GFile *location)
{
    update_widths (settings, location, 0, TRUE);
}
