/*
 * gawpgroup.c - Panel Group implementation
 *
 * include LICENSE
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <gtk/gtk.h>

#include <gaw.h>

#ifdef TRACE_MEM
#include <tracemem.h>
#endif

PanelGroup *pg_group_new(UserData *ud, const gchar *name)
{
   PanelGroup *pg = g_new0(PanelGroup, 1);
   pg->ud = ud;
   pg->name = g_strdup(name);
   return pg;
}

void pg_group_destroy(PanelGroup *pg)
{
   if (!pg) return;

   /* destroy panels */
   int n = g_list_length(pg->panelList);
   for (int i = n; i > 0; i--) {
      WavePanel *wp = (WavePanel *) g_list_nth_data(pg->panelList, i - 1);
      pa_panel_destroy(wp);
   }

   if (pg->xLabels) {
      al_label_destroy(pg->xLabels);
   }

   /* free cursors */
   if (pg->cursors) {
      for (int i = 0; i < 2; i++) {
         g_free(pg->cursors[i]->color);
      }
      for (int i = 0; i < AW_NX_MBTN; i++) {
         g_free(pg->cursors[i]);
      }
      g_free(pg->cursors);
   }

   g_free(pg->name);
   g_free(pg);
}

/*
 * Build all widgets for a PanelGroup:
 *   groupBox (VBox)
 *   +-- meas_hbox       (cursor measure buttons)
 *   +-- panel_scrolled   (GtkScrolledWindow around panelTable)
 *   +-- xlabel_ev_box    (X axis labels area)
 *   +-- xscrollbar       (horizontal scrollbar)
 */
void pg_group_build_widgets(PanelGroup *pg)
{
   UserData *ud = pg->ud;
   GtkAdjustment *adj;

   /* top-level VBox for this group */
   pg->groupBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   gtk_widget_set_hexpand(pg->groupBox, TRUE);
   gtk_widget_set_vexpand(pg->groupBox, TRUE);
   gtk_widget_show(pg->groupBox);

   /* --- measure buttons hbox --- */
   pg->meas_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
   gtk_box_pack_start(GTK_BOX(pg->groupBox), pg->meas_hbox, FALSE, FALSE, 0);

   /* --- xlabel event box (created here, filled by ap_xlabel_box_create) --- */
   pg->xlabel_ev_box = gtk_event_box_new();
   gtk_widget_set_hexpand(pg->xlabel_ev_box, TRUE);

   /* --- X labels object --- */
   pg->xLabels = al_label_new(ud->up, ud->up->setLogX, 1);

   /* --- cursors and measure buttons (uses pg->meas_hbox and pg->cursors) --- */
   ap_create_xmeasure_unit(ud);

   /* --- xlabel box (uses pg->xlabel_ev_box, creates allline_box etc.) --- */
   ap_xlabel_box_create(ud);

   /* --- horizontal scrollbar (creates pg->xadj, pg->xscrollbar) --- */
   ap_create_win_bottom(ud);

   /* --- panel table --- */
   pg->panelTable = gtk_grid_new();
   gtk_grid_set_row_spacing(GTK_GRID(pg->panelTable), 1);
   gtk_grid_set_row_homogeneous(GTK_GRID(pg->panelTable), TRUE);
   gtk_widget_set_hexpand(pg->panelTable, TRUE);
   gtk_widget_show(pg->panelTable);

   /* --- scrolled window for panels --- */
   pg->panel_scrolled = gtk_scrolled_window_new(NULL, NULL);
   gtk_widget_set_size_request(GTK_WIDGET(pg->panel_scrolled), -1, 30);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(pg->panel_scrolled),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
   GtkWidget *vscrollbar = gtk_scrolled_window_get_vscrollbar(
                                  GTK_SCROLLED_WINDOW(pg->panel_scrolled));
   gtk_widget_set_can_focus(vscrollbar, FALSE);
   gtk_widget_show(pg->panel_scrolled);
   gtk_container_add(GTK_CONTAINER(pg->panel_scrolled), pg->panelTable);

   adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(pg->panel_scrolled));
   gtk_container_set_focus_vadjustment(GTK_CONTAINER(pg->panelTable), adj);
   gtk_adjustment_set_page_size(adj, gtk_adjustment_get_upper(adj));

   /* scrollbar show/hide callbacks */
   GtkWidget *scr = gtk_scrolled_window_get_vscrollbar(
                        GTK_SCROLLED_WINDOW(pg->panel_scrolled));
   g_signal_connect(scr, "show",
                    G_CALLBACK(aw_scrollbar_show_cb), (gpointer) ud);
   g_signal_connect(scr, "hide",
                    G_CALLBACK(aw_scrollbar_hide_cb), (gpointer) ud);

   /* pack into groupBox: panel_scrolled (expands), xlabel, scrollbar */
   gtk_box_pack_start(GTK_BOX(pg->groupBox), pg->panel_scrolled, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(pg->groupBox), pg->xlabel_ev_box, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(pg->groupBox), pg->xscrollbar, FALSE, FALSE, 0);
}

/*
 * Tab management
 */

GawTab *aw_tab_new(UserData *ud, const gchar *name)
{
   GawTab *tab = g_new0(GawTab, 1);
   tab->ud = ud;
   tab->name = g_strdup(name);

   /* Create the tab content container - a vertical box that holds all panel groups */
   tab->tab_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   gtk_widget_show(tab->tab_content);

   return tab;
}

void aw_tab_destroy(GawTab *tab)
{
   if (!tab) return;

   /* Destroy all groups */
   GList *list = tab->groupList;
   while (list) {
      PanelGroup *pg = (PanelGroup *) list->data;
      pg_group_destroy(pg);
      list = list->next;
   }
   g_list_free(tab->groupList);

   g_free(tab->name);
   g_free(tab);
}

PanelGroup *aw_tab_add_group(GawTab *tab, const gchar *name)
{
   PanelGroup *pg = pg_group_new(tab->ud, name);
   tab->groupList = g_list_append(tab->groupList, pg);
   if (!tab->active_group) {
      tab->active_group = pg;
   }
   return pg;
}

void aw_set_active_group(UserData *ud, PanelGroup *pg)
{
   if (ud->active_tab) {
      ud->active_tab->active_group = pg;
   }
   ud->ag = pg;
}

/*
 * Notebook tab-switch callback: update active tab and group pointers.
 */
void aw_notebook_switch_page_cb(GtkNotebook *notebook, GtkWidget *page,
                                guint page_num, gpointer user_data)
{
   UserData *ud = (UserData *) user_data;
   GawTab *tab = (GawTab *) g_list_nth_data(ud->tabList, page_num);
   if (tab) {
      ud->active_tab = tab;
      ud->ag = tab->active_group;
   }
}
