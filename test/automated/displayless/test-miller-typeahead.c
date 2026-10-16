/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "nautilus-miller-typeahead.h"
#include <glib.h>

static void
test_unicode_prefixes (void)
{
    g_assert_true (nautilus_miller_typeahead_matches ("Éclair.txt", "e\314\201c"));
    g_assert_true (nautilus_miller_typeahead_matches ("Straße.txt", "STRASS"));
    g_assert_true (nautilus_miller_typeahead_matches ("Ａlpha.txt", "al"));
    g_assert_true (nautilus_miller_typeahead_matches ("Σ.txt", "ς"));
    g_assert_true (nautilus_miller_typeahead_matches ("猫.txt", "猫"));
    g_assert_true (nautilus_miller_typeahead_matches ("한글.txt", "한"));
    g_assert_false (nautilus_miller_typeahead_matches ("alpha.txt", "beta"));
    g_assert_false (nautilus_miller_typeahead_matches ("alpha.txt", ""));
}

static void
test_grapheme_backspace (void)
{
    const char *before[] = {"ae\314\201", "a👩‍💻", "a🇺🇸", "a👍🏽", "ab", "a", "", NULL};
    const char *after[] = {"a", "a", "a", "a", "a", "", ""};
    for (guint i = 0; before[i] != NULL; i++)
    {
        g_autofree char *result = nautilus_miller_typeahead_remove_grapheme (before[i]);
        g_assert_cmpstr (result, ==, after[i]);
    }
}

static void
test_character_limit (void)
{
    g_autofree char *prefix = g_strnfill (255, 'a');
    g_autofree char *at_limit = nautilus_miller_typeahead_append (prefix, "猫");
    g_assert_nonnull (at_limit);
    g_assert_cmpint (g_utf8_strlen (at_limit, -1), ==, 256);
    g_autofree char *too_long = nautilus_miller_typeahead_append (at_limit, "z");
    g_assert_null (too_long);
    g_autofree char *start = nautilus_miller_typeahead_append ("", "ねこ");
    g_assert_cmpstr (start, ==, "ねこ");
}

static void
test_invalid_input (void)
{
    const char bad[] = {(char) 0xff, 0};
    g_assert_false (nautilus_miller_typeahead_matches (bad, "a"));
    g_assert_false (nautilus_miller_typeahead_matches ("a", bad));
    g_assert_false (nautilus_miller_typeahead_matches (NULL, "a"));
    g_autofree char *append = nautilus_miller_typeahead_append ("a", bad);
    g_autofree char *remove = nautilus_miller_typeahead_remove_grapheme (bad);
    g_assert_null (append);
    g_assert_null (remove);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/miller/typeahead/unicode-prefix", test_unicode_prefixes);
    g_test_add_func ("/miller/typeahead/grapheme-backspace", test_grapheme_backspace);
    g_test_add_func ("/miller/typeahead/character-limit", test_character_limit);
    g_test_add_func ("/miller/typeahead/invalid-input", test_invalid_input);
    return g_test_run ();
}
