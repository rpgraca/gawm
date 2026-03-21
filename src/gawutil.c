/*
 * gawimg.c - Save image to file
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

#include <sockcon.h>

/*
 * create a button with a label and an imamge from theme
 */

GtkWidget *gawutil_button_new_with_label( gchar *label, gchar *themed_name,
                                     GtkIconSize size)
{
   GtkWidget *button = gtk_button_new_with_label (label);

   GIcon *icon = g_themed_icon_new (themed_name);
   GtkWidget *image = gtk_image_new_from_gicon (icon, size);
   gtk_button_set_image (GTK_BUTTON(button), image );

   g_object_unref (icon);
   return button;
}

/*
 * Send a command to xschem to highlight a net
 */
void aw_xschem_highlight_wave(VisibleWave *vw)
{
   UserData *ud = vw->wp->ud;
   UserPrefs *up = ud->up;
   SockCon *cnx;
   char *varName = vw->var->varName;
   char *netName;
   char *p;

   if (!up->xschemHost || up->xschemPort <= 0) return;

   /* Extract net name from v(net) or i(net) */
   if ( (p = strchr(varName, '(')) != NULL ) {
      netName = app_strdup(p + 1);
      if ( (p = strrchr(netName, ')')) != NULL ) {
         *p = '\0';
      }
   } else {
      netName = app_strdup(varName);
   }

   /* Connect to xschem. We use a short-lived connection. */
   cnx = con_new(up->xschemHost, PF_INET, SOCK_STREAM, IPPROTO_IP, up->xschemPort, CON_CONNECT);
   if (cnx->status >= 0) {
      char cmd[1024];
      snprintf(cmd, sizeof(cmd), "xschem highlight %s\n", netName);
      con_send(cnx, cmd, strlen(cmd), 0);
      con_destroy(cnx);
   } else {
      con_destroy(cnx);
   }
   app_free(netName);
}
 
