/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <gio/gio.h>
#include "nautilus-miller-path.h"

static void
assert_column (NautilusMillerPath *path, guint index, const char *uri)
{
    g_autoptr (GFile) expected = g_file_new_for_uri (uri);
    g_assert_true (g_file_equal (nautilus_miller_path_get_column (path, index), expected));
}

static void
test_reconcile (void)
{
    g_autoptr (NautilusMillerPath) path = nautilus_miller_path_new ();
    g_autoptr (GFile) first = g_file_new_for_path ("/a/b");
    g_autoptr (GFile) sibling = g_file_new_for_path ("/a/c");
    g_assert_cmpuint (nautilus_miller_path_set_location (path, first), ==, 0);
    g_assert_cmpuint (nautilus_miller_path_get_n_columns (path), ==, 3);
    assert_column (path, 0, "file:///");
    assert_column (path, 1, "file:///a");
    assert_column (path, 2, "file:///a/b");
    guint64 generation = nautilus_miller_path_get_generation (path);
    g_assert_cmpuint (nautilus_miller_path_set_location (path, first), ==, 3);
    g_assert_true (nautilus_miller_path_is_current (path, generation));
    g_assert_cmpuint (nautilus_miller_path_set_location (path, sibling), ==, 2);
    g_assert_false (nautilus_miller_path_is_current (path, generation));
    assert_column (path, 2, "file:///a/c");
    g_autoptr (GFile) root = g_file_new_for_path ("/");
    g_assert_cmpuint (nautilus_miller_path_set_location (path, root), ==, 1);
    g_assert_cmpuint (nautilus_miller_path_get_n_columns (path), ==, 1);
}

static void
test_uri_roots (void)
{
    g_autoptr (NautilusMillerPath) path = nautilus_miller_path_new ();
    g_autoptr (GFile) remote = g_file_new_for_uri ("sftp://example.test/a/b");
    nautilus_miller_path_set_location (path, remote);
    g_assert_cmpuint (nautilus_miller_path_get_n_columns (path), ==, 3);
    assert_column (path, 0, "sftp://example.test/");
    assert_column (path, 1, "sftp://example.test/a");
    assert_column (path, 2, "sftp://example.test/a/b");
    g_autoptr (GFile) other_host = g_file_new_for_uri ("sftp://other.test/a/b");
    g_assert_cmpuint (nautilus_miller_path_set_location (path, other_host), ==, 0);
}

static void
test_names_and_lexical_paths (void)
{
    g_autoptr (NautilusMillerPath) path = nautilus_miller_path_new ();
    g_autoptr (GFile) location = g_file_new_for_uri ("file:///a%20b/%E7%8C%AB/100%25/link");
    nautilus_miller_path_set_location (path, location);
    g_assert_cmpuint (nautilus_miller_path_get_n_columns (path), ==, 5);
    assert_column (path, 1, "file:///a%20b");
    assert_column (path, 2, "file:///a%20b/%E7%8C%AB");
    assert_column (path, 3, "file:///a%20b/%E7%8C%AB/100%25");
    assert_column (path, 4, "file:///a%20b/%E7%8C%AB/100%25/link");
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/miller/path/reconcile", test_reconcile);
    g_test_add_func ("/miller/path/uri-roots", test_uri_roots);
    g_test_add_func ("/miller/path/names", test_names_and_lexical_paths);
    return g_test_run ();
}
