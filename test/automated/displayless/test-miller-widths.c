/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "nautilus-miller-widths.h"
#include "nautilus-global-preferences.h"

static void
reset_settings (void)
{
    g_settings_reset (nautilus_preferences, MILLER_DEFAULT_WIDTH_KEY);
    g_settings_reset (nautilus_preferences, MILLER_WIDTHS_KEY);
}

static void
test_default_clamp_reset (void)
{
    reset_settings ();
    g_autoptr (GFile) location = g_file_new_for_path ("/width-fixture/猫 %");
    g_assert_cmpint (nautilus_miller_width_get (nautilus_preferences, location), ==, 280);
    nautilus_miller_width_set (nautilus_preferences, location, 5000);
    g_assert_cmpint (nautilus_miller_width_get (nautilus_preferences, location), ==, 1200);
    nautilus_miller_width_set (nautilus_preferences, location, -12);
    g_assert_cmpint (nautilus_miller_width_get (nautilus_preferences, location), ==, 180);
    g_settings_set_uint (nautilus_preferences, MILLER_DEFAULT_WIDTH_KEY, 350);
    nautilus_miller_width_reset (nautilus_preferences, location);
    g_assert_cmpint (nautilus_miller_width_get (nautilus_preferences, location), ==, 350);
}

static void
test_settings_instance_rehydrates (void)
{
    reset_settings ();
    g_autoptr (GFile) location = g_file_new_for_path ("/width-fixture/persist");
    nautilus_miller_width_set (nautilus_preferences, location, 460);
    g_autofree char *schema = NULL;
    g_object_get (nautilus_preferences, "schema-id", &schema, NULL);
    g_autoptr (GSettings) reopened = g_settings_new (schema);
    g_assert_cmpint (nautilus_miller_width_get (reopened, location), ==, 460);
}

static void
test_bounded_mru (void)
{
    reset_settings ();
    for (guint i = 0; i < 128; i++)
    {
        g_autofree char *path = g_strdup_printf ("/width-fixture/%u", i);
        g_autoptr (GFile) location = g_file_new_for_path (path);
        nautilus_miller_width_set (nautilus_preferences, location, 400);
    }
    g_autoptr (GFile) oldest = g_file_new_for_path ("/width-fixture/0");
    g_autoptr (GFile) evicted = g_file_new_for_path ("/width-fixture/1");
    g_autoptr (GFile) newest = g_file_new_for_path ("/width-fixture/128");
    g_assert_cmpint (nautilus_miller_width_get (nautilus_preferences, oldest), ==, 400);
    nautilus_miller_width_set (nautilus_preferences, newest, 600);
    g_assert_cmpint (nautilus_miller_width_get (nautilus_preferences, oldest), ==, 400);
    g_assert_cmpint (nautilus_miller_width_get (nautilus_preferences, evicted), ==, 280);
    g_autoptr (GVariant) entries = g_settings_get_value (nautilus_preferences, MILLER_WIDTHS_KEY);
    g_assert_cmpuint (g_variant_n_children (entries), ==, 128);
}

static void
test_remote_privacy (void)
{
    reset_settings ();
    const char *unsafe[] = {"sftp://alice:secret@example.test/a", "smb://alice@example.test/a",
                            "sftp://example.test/a?token=secret", "sftp://example.test/a#secret",
                            "https://example.test/a", NULL};
    for (guint i = 0; unsafe[i] != NULL; i++)
    {
        g_autoptr (GFile) location = g_file_new_for_uri (unsafe[i]);
        nautilus_miller_width_set (nautilus_preferences, location, 700);
        g_assert_cmpint (nautilus_miller_width_get (nautilus_preferences, location), ==, 280);
    }
    g_autoptr (GVariant) entries = g_settings_get_value (nautilus_preferences, MILLER_WIDTHS_KEY);
    g_assert_cmpuint (g_variant_n_children (entries), ==, 0);
    g_autoptr (GFile) safe = g_file_new_for_uri ("sftp://example.test:2222/a%20b");
    nautilus_miller_width_set (nautilus_preferences, safe, 500);
    g_assert_cmpint (nautilus_miller_width_get (nautilus_preferences, safe), ==, 500);
}

static void
test_invalid_stored_entries (void)
{
    reset_settings ();
    g_autoptr (GVariant) bad = g_variant_parse (G_VARIANT_TYPE ("a(su)"),
        "[('sftp://alice:secret@example.test/a', 400), ('file:///good', 500),"
        "('file:///good', 600), ('file:///small', 1), ('garbage', 450)]", NULL, NULL, NULL);
    g_settings_set_value (nautilus_preferences, MILLER_WIDTHS_KEY, bad);
    g_autoptr (GFile) good = g_file_new_for_path ("/good");
    g_assert_cmpint (nautilus_miller_width_get (nautilus_preferences, good), ==, 500);
    g_autoptr (GVariant) entries = g_settings_get_value (nautilus_preferences, MILLER_WIDTHS_KEY);
    g_assert_cmpuint (g_variant_n_children (entries), ==, 1);
    g_autofree char *printed = g_variant_print (entries, TRUE);
    g_assert_null (strstr (printed, "secret"));
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    nautilus_global_preferences_init ();
    g_test_add_func ("/miller/widths/default-clamp-reset", test_default_clamp_reset);
    g_test_add_func ("/miller/widths/rehydrate", test_settings_instance_rehydrates);
    g_test_add_func ("/miller/widths/bounded-mru", test_bounded_mru);
    g_test_add_func ("/miller/widths/remote-privacy", test_remote_privacy);
    g_test_add_func ("/miller/widths/sanitize", test_invalid_stored_entries);
    return g_test_run ();
}
