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

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>

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
 * Extract net name from variable name like v(net) or i(net).
 * Returns allocated string that must be freed with app_free.
 */
static char *extract_net_name(const char *varName)
{
   char *netName;
   const char *p;

   if ( (p = strchr(varName, '(')) != NULL ) {
      netName = app_strdup(p + 1);
      char *end;
      if ( (end = strrchr(netName, ')')) != NULL ) {
         *end = '\0';
      }
   } else {
      netName = app_strdup(varName);
   }
   return netName;
}

/*
 * Sync xschem highlights with gawm: unhighlight all in xschem,
 * then re-highlight all currently highlighted waves.
 * xschem_server reads until EOF, then executes all received text as Tcl.
 */
/*
 * Silently check if a TCP port is reachable (non-blocking, ~50ms timeout).
 * Returns 1 if connectable, 0 otherwise. No error messages.
 */
static int xschem_port_reachable(const char *host, int port)
{
   struct addrinfo hints, *res, *rp;
   char portstr[16];
   int fd, ret = 0;

   memset(&hints, 0, sizeof(hints));
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_STREAM;
   snprintf(portstr, sizeof(portstr), "%d", port);

   if (getaddrinfo(host, portstr, &hints, &res) != 0) return 0;

   for (rp = res; rp; rp = rp->ai_next) {
      fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
      if (fd < 0) continue;

      /* Set non-blocking */
      int flags = fcntl(fd, F_GETFL, 0);
      fcntl(fd, F_SETFL, flags | O_NONBLOCK);

      int rc = connect(fd, rp->ai_addr, rp->ai_addrlen);
      if (rc == 0) {
         ret = 1;
      } else if (errno == EINPROGRESS) {
         struct timeval tv = { .tv_sec = 0, .tv_usec = 50000 }; /* 50ms */
         fd_set wfds;
         FD_ZERO(&wfds);
         FD_SET(fd, &wfds);
         if (select(fd + 1, NULL, &wfds, NULL, &tv) > 0) {
            int err = 0;
            socklen_t errlen = sizeof(err);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen);
            if (err == 0) ret = 1;
         }
      }
      close(fd);
      break;
   }
   freeaddrinfo(res);
   return ret;
}

void aw_xschem_sync_highlights(UserData *ud)
{
   UserPrefs *up = ud->up;
   SockCon *cnx;
   GList *plist, *wlist;
   char cmd[8192];
   int len;

   if (!up->xschemHost || up->xschemPort <= 0) return;

   /* Silent check — don't spam errors if xschem isn't running */
   if (!xschem_port_reachable(up->xschemHost, up->xschemPort)) return;

   cnx = con_new(up->xschemHost, PF_INET, SOCK_STREAM, IPPROTO_IP, up->xschemPort, CON_CONNECT);
   if (cnx->status < 0) {
      con_destroy(cnx);
      return;
   }

   /* Tcl script for case-insensitive net probing.
    * Matches gawm wave colors to closest xschem palette color.
    * All drawing is suppressed until a single redraw at the end.
    */
   len = snprintf(cmd, sizeof(cmd),
      /* Helper: descend hierarchy with case-insensitive instance matching */
      "proc gawm_descend {path} {\n"
      "  while {[xschem get currsch]} {xschem go_back}\n"
      "  while {[regexp {\\.\\.?} $path]} {\n"
      "    xschem unselect_all\n"
      "    set inst $path\n"
      "    regsub {\\..*} $inst {} inst\n"
      "    regsub {[^.]+\\.} $path {} path\n"
      "    xschem search exact 1 name $inst 1 no_match_case\n"
      "    set inst_list [split [lindex [xschem expandlabel"
      " [lindex [xschem selected_set] 0]] 0] {,}]\n"
      "    set idx 1\n"
      "    foreach el $inst_list {\n"
      "      if {[string equal -nocase $el $inst]} break\n"
      "      incr idx\n"
      "    }\n"
      "    xschem descend $idx\n"
      "  }\n"
      "  return $path\n"
      "}\n"
      /* Helper: resolve a lowercase net name to xschem's actual case */
      "proc gawm_resolve {net} {\n"
      "  set lnet [string tolower $net]\n"
      "  foreach entry [xschem list_nets] {\n"
      "    set n [lindex $entry 0]\n"
      "    if {[string tolower $n] eq $lnet} {return $n}\n"
      "  }\n"
      "  if {[regexp {^net[0-9]+$} $net]} {return \\#$net}\n"
      "  return $net\n"
      "}\n"
      /* Helper: find closest xschem palette color index to an RGB hex value.
       * Compares against $tctx::colors (the active palette), skipping
       * indices 0-3 which are background/grid/special colors. */
      /* Return hilight_color value (active_layer index) for closest
       * palette color to the given RGB.  hilight_color is mapped through
       * get_color() which indexes active_layer[], so we must return the
       * position in active_layer that holds our target palette index. */
      "proc gawm_closest_color {r g b} {\n"
      "  set best_pal 4\n"
      "  set best_dist 999999\n"
      "  set idx 0\n"
      "  foreach c $tctx::colors {\n"
      "    if {$idx >= 4 && [scan $c {#%%2x%%2x%%2x} cr cg cb] == 3} {\n"
      "      set d [expr {($r-$cr)*($r-$cr)+($g-$cg)*($g-$cg)+($b-$cb)*($b-$cb)}]\n"
      "      if {$d < $best_dist} {set best_dist $d; set best_pal $idx}\n"
      "    }\n"
      "    incr idx\n"
      "  }\n"
      "  return [expr {$best_pal - 4}]\n"
      "}\n"
      /* Helper: restore hierarchy to a saved sch_path */
      "proc gawm_restore_path {path} {\n"
      "  while {[xschem get currsch]} {xschem go_back}\n"
      "  if {$path eq {.} || $path eq {}} return\n"
      "  set path [string trim $path .]\n"
      "  foreach inst [split $path .] {\n"
      "    xschem unselect_all\n"
      "    xschem search exact 1 name $inst 1 no_match_case\n"
      "    if {[llength [xschem selected_set]] > 0} {\n"
      "      set inst_list [split [lindex [xschem expandlabel"
      " [lindex [xschem selected_set] 0]] 0] {,}]\n"
      "      set idx 1\n"
      "      foreach el $inst_list {\n"
      "        if {[string equal -nocase $el $inst]} break\n"
      "        incr idx\n"
      "      }\n"
      "      xschem descend $idx\n"
      "    }\n"
      "  }\n"
      "  xschem unselect_all\n"
      "}\n"
      /* Main script */
      "xschem set no_draw 1\n"
      "set _gawm_orig_path [xschem get sch_path]\n"
      "set _gawm_orig_x [xschem get xorigin]\n"
      "set _gawm_orig_y [xschem get yorigin]\n"
      "set _gawm_orig_zoom [xschem get zoom]\n"
      "if {![info exists gawm_net_colors]} {set gawm_net_colors {}}\n"
      /* Save all current highlights: {token -> color} */
      "set all_hi {}\n"
      "foreach line [split [xschem list_hilights all_nets] \\n] {\n"
      "  set tok [lindex $line 1]\n"
      "  set col [lindex $line 2]\n"
      "  if {$tok ne {}} {dict set all_hi $tok $col}\n"
      "}\n"
      "xschem unhilight_all fast\n"
      /* Restore non-gawm (manual) highlights with original colors */
      "dict for {net col} $all_hi {\n"
      "  if {![dict exists $gawm_net_colors $net]} {\n"
      "    xschem set hilight_color $col\n"
      "    xschem hilight_netname $net\n"
      "  }\n"
      "}\n"
      "set gawm_net_colors {}\n");

   /* For each highlighted wave: set color to match gawm, then highlight */
   for (plist = ud->panelList; plist; plist = plist->next) {
      WavePanel *wp = (WavePanel *) plist->data;
      for (wlist = wp->vwlist; wlist; wlist = wlist->next) {
         VisibleWave *vw = (VisibleWave *) wlist->data;
         if (vw->button && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(vw->button))) {
            char *netName = extract_net_name(vw->var->varName);
            int r = (int)(vw->color->red * 255.0 + 0.5);
            int g = (int)(vw->color->green * 255.0 + 0.5);
            int b = (int)(vw->color->blue * 255.0 + 0.5);
            len += snprintf(cmd + len, sizeof(cmd) - len,
               "set _net [gawm_descend %s]\n"
               "set _net [gawm_resolve $_net]\n"
               "xschem set hilight_color [gawm_closest_color %d %d %d]\n"
               "xschem hilight_netname $_net\n"
               "dict set gawm_net_colors $_net [gawm_closest_color %d %d %d]\n",
               netName, r, g, b, r, g, b);
            app_free(netName);
            if (len >= (int)sizeof(cmd) - 1) break;
         }
      }
   }

   /* Restore original hierarchy position and redraw */
   len += snprintf(cmd + len, sizeof(cmd) - len,
      "gawm_restore_path $_gawm_orig_path\n"
      "xschem origin $_gawm_orig_x $_gawm_orig_y $_gawm_orig_zoom\n"
      "xschem set no_draw 0\n"
      "xschem redraw\n");

   con_send(cnx, cmd, len, 0);
   con_destroy(cnx);
}

/*
 * Send signal values at a cursor position to xschem for backannotation.
 * Writes a temporary ASCII raw OP file with interpolated values,
 * then uses xschem's annotate_op to load it through the proper pipeline
 * (extra_rawfile -> update_op -> draw), which populates cursor_b_val[].
 * cursor_idx: 0 or 1 (which cursor to read X position from)
 */
void aw_xschem_annotate_at_cursor(UserData *ud, int cursor_idx)
{
   UserPrefs *up = ud->up;
   SockCon *cnx;
   GList *dlist;
   char cmd[512];
   int len;
   AWCursor *csp;
   FILE *fp;
   const char *tmpfile = "/tmp/gawm_annotate.raw";
   int t, j;

   if (!up->xschemHost || up->xschemPort <= 0) return;
   if (cursor_idx < 0 || cursor_idx > 1) return;

   csp = ud->cursors[cursor_idx];
   if (!csp->shown) return;

   /* Count all variables across all loaded data files (skip independent var at idx 0) */
   int nvars = 0;
   for (dlist = ud->wdata_list; dlist; dlist = dlist->next) {
      DataFile *wdata = (DataFile *) dlist->data;
      WaveTable *wt = wdata->wt;
      for (t = 0; t < wt->ntables; t++) {
         WDataSet *wds = wavetable_get_dataset(wt, t);
         if (wds && wds->nrows > 0)
            nvars += wds->numVars - 1; /* skip independent variable */
      }
   }
   if (nvars == 0) return;

   /* Write temporary ASCII raw file in ngspice OP format */
   fp = fopen(tmpfile, "w");
   if (!fp) return;

   fprintf(fp, "Title: gawm cursor annotation\n");
   fprintf(fp, "Date: -\n");
   fprintf(fp, "Plotname: Operating Point\n");
   fprintf(fp, "Flags: real\n");
   fprintf(fp, "No. Variables: %d\n", nvars);
   fprintf(fp, "No. Points: 1\n");
   fprintf(fp, "Variables:\n");

   int idx = 0;
   for (dlist = ud->wdata_list; dlist; dlist = dlist->next) {
      DataFile *wdata = (DataFile *) dlist->data;
      WaveTable *wt = wdata->wt;
      for (t = 0; t < wt->ntables; t++) {
         WDataSet *wds = wavetable_get_dataset(wt, t);
         if (!wds || wds->nrows <= 0) continue;
         for (j = 1; j < wds->numVars; j++) {
            WaveVar *var = (WaveVar *) dataset_get_wavevar(wds, j);
            char *varName = var->varName;
            const char *type = "voltage";
            if (varName[0] == 'i' || varName[0] == 'I') type = "current";
            fprintf(fp, "\t%d\t%s\t%s\n", idx, varName, type);
            idx++;
         }
      }
   }

   fprintf(fp, "Values:\n");
   idx = 0;
   for (dlist = ud->wdata_list; dlist; dlist = dlist->next) {
      DataFile *wdata = (DataFile *) dlist->data;
      WaveTable *wt = wdata->wt;
      for (t = 0; t < wt->ntables; t++) {
         WDataSet *wds = wavetable_get_dataset(wt, t);
         if (!wds || wds->nrows <= 0) continue;
         for (j = 1; j < wds->numVars; j++) {
            WaveVar *var = (WaveVar *) dataset_get_wavevar(wds, j);
            double yval = wavevar_interp_value(var, csp->xval);
            if (idx == 0)
               fprintf(fp, " %d\t%.15g\n", 0, yval);
            else
               fprintf(fp, "\t%.15g\n", yval);
            idx++;
         }
      }
   }
   fprintf(fp, "\n"); /* empty line terminates data block */
   fclose(fp);

   /* Send annotate_op command to xschem */
   if (!xschem_port_reachable(up->xschemHost, up->xschemPort)) return;

   cnx = con_new(up->xschemHost, PF_INET, SOCK_STREAM, IPPROTO_IP, up->xschemPort, CON_CONNECT);
   if (cnx->status < 0) {
      con_destroy(cnx);
      return;
   }

   len = snprintf(cmd, sizeof(cmd),
      "xschem annotate_op %s 0\n", tmpfile);

   con_send(cnx, cmd, len, 0);
   con_destroy(cnx);
}

/*
 * Clear ngspice backannotation data in xschem.
 */
void aw_xschem_clear_annotations(UserData *ud)
{
   UserPrefs *up = ud->up;
   SockCon *cnx;

   if (!up->xschemHost || up->xschemPort <= 0) return;
   if (!xschem_port_reachable(up->xschemHost, up->xschemPort)) return;

   cnx = con_new(up->xschemHost, PF_INET, SOCK_STREAM, IPPROTO_IP, up->xschemPort, CON_CONNECT);
   if (cnx->status < 0) {
      con_destroy(cnx);
      return;
   }
   char cmd[] =
      "xschem set live_cursor2_backannotate 0\n"
      "array unset ngspice::ngspice_data\n"
      "xschem redraw\n";
   con_send(cnx, cmd, strlen(cmd), 0);
   con_destroy(cnx);
}
