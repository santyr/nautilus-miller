/* SPDX-License-Identifier: GPL-3.0-or-later
 * Explicit GTK/libadwaita characterization; not a passing regression gate.
 * Run only in the documented private session. */
#include <adwaita.h>

static void
pump (guint milliseconds)
{
    gint64 end = g_get_monotonic_time () + milliseconds * 1000;
    while (g_get_monotonic_time () < end)
    {
        while (g_main_context_iteration (NULL, FALSE)) {}
        g_usleep (1000);
    }
}

int
main (int argc, char **argv)
{
    adw_init ();
    GtkWindow *window = GTK_WINDOW (adw_window_new ());
    AdwTabView *tabs = adw_tab_view_new ();
    GtkWidget *first = gtk_entry_new ();
    GtkWidget *second = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *weak = first;
    g_object_add_weak_pointer (G_OBJECT (first), (gpointer *) &weak);
    AdwTabPage *a = adw_tab_view_append (tabs, first);
    AdwTabPage *b = adw_tab_view_append (tabs, second);
    adw_window_set_content (ADW_WINDOW (window), GTK_WIDGET (tabs));
    gtk_window_present (window); pump (200);
    gtk_widget_grab_focus (first); pump (100);
    adw_tab_view_set_selected_page (tabs, b);
    if (argc > 1) { pump (100); }
    adw_tab_view_close_page (tabs, a); pump (1000);
    g_print ("GTK=%u.%u.%u ADW=%u.%u.%u wait-between=%d closed-child-retained=%d\n",
        gtk_get_major_version (), gtk_get_minor_version (), gtk_get_micro_version (),
        adw_get_major_version (), adw_get_minor_version (), adw_get_micro_version (),
        argc > 1, weak != NULL);
    gtk_window_destroy (window); pump (100);
    return weak != NULL ? 23 : 0;
}
