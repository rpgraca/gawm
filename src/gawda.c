/*
 * gawda.c - drawing area functions
 * 
 * include LICENSE
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <string.h>
#include <math.h>

#include <gaw.h>
#include <datestrconv.h>

#ifdef TRACE_MEM
#include <tracemem.h>
#endif
        


/* forward declarations */
static void da_draw_cursor_annotations(WavePanel *wp, cairo_t *cr, int w, int h);

/*
 * simply forces widget w to redraw itself
 *    indirect call to expose event
 */
void da_drawing_redraw(GtkWidget *w)
{
   GdkRectangle rect;

   gtk_widget_get_allocation (w, &rect);
   rect.x = 0;
   rect.y = 0;

   if ( ! gtk_widget_get_realized (w) ) {
      return;
   }
   /* ask redraw */
   gdk_window_invalidate_rect (gtk_widget_get_window (w),
                               &rect, FALSE);
}

/* 
 * Set the X pointer cursor for all wavepanels: used to provide a
 * hint that we're expecting the user to drag out a line or region.
 */

void da_set_gdk_cursor(GtkWidget *w, int cursorType)
{
   GdkCursor *cursor;

   if (cursorType == -1) {
      cursor = NULL;
   } else {
      cursor = gdk_cursor_new(cursorType);
   }
   gdk_window_set_cursor(gtk_widget_get_window (w), cursor);

   if (cursor) {
      g_object_unref(cursor);
   }
}


void da_draw_srange(SelRange *sr)
{
   cairo_t *cr = sr->wp->cr;

   gdk_cairo_set_source_rgba (cr, sr->color);
   cairo_set_line_width (cr, 1.0);

   if (sr->type & SR_X) {
      cairo_move_to (cr, sr->x1, sr->y1);
      cairo_line_to (cr, sr->x2, sr->y1);
   }
   if (sr->type & SR_Y) {
      cairo_move_to (cr, sr->x1, sr->y1);
      cairo_line_to (cr, sr->x1, sr->y2);
   }
   if (sr->type == SR_XY) {
      cairo_move_to (cr, sr->x1, sr->y2);
      cairo_line_to (cr, sr->x2, sr->y2);

      cairo_move_to (cr, sr->x2, sr->y1);
      cairo_line_to (cr, sr->x2, sr->y2);
   }
   cairo_stroke (cr);
}

void
da_update_srange(SelRange *sr,  GdkEventMotion *event, int draw)
{
   int newx2, newy2;

   /*
    * the event->y does goofy things if the motion continues
    * outside the window, so we generate our own from the root
    * coordinates.
    */
   newx2 = event->x;
   newy2 = sr->y1 + (event->y_root - sr->y1_root);

   sr->drawn = draw;
   if (sr->type & SR_X) {
      sr->x2 = newx2;
   }
   if (sr->type & SR_Y) {
      sr->y2 = newy2;
   }
   /* will be forced to redrawd */
   msg_dbg( "type=%d newx=%d newy=%d draw=%d",
	   sr->type, sr->x2, sr->y2, draw);
   msg_dbg( "m %d %d %d %d",
	   (int) event->x, (int) event->y, 
	   (int) event->x_root, (int) event->y_root);
}

/*
 * done selecting range; do the callback
 */

void
da_callback_srange(UserData *ud, WavePanel *wp )
{
   SelRange *sr = ud->srange;
   double xstart, ystart;
   double xend, yend;
   GawLabels *lbx = ud->xLabels;
   GawLabels *lby = sr->wp->yLabels;
   
   msg_dbg( "type=%d x1=%d x2=%d  y1=%d y2=%d",
	   sr->type, sr->x1, sr->x2, sr->y1, sr->y2);

   xstart = al_label_x2val(lbx, sr->x1);
   xend   = al_label_x2val(lbx, sr->x2);

   ystart = al_label_y2val(lby, sr->y1 );
   yend   = al_label_y2val(lby, sr->y2 );
 
   if ( ystart < yend ) {
      pa_panel_set_yvals( wp, ystart, yend);
   } else {
      pa_panel_set_yvals( wp, yend, ystart);
   }

   switch (sr->type) {
    case SR_X:
      az_cmd_zoom_absolute(ud, xstart, xend );
      break;
    case SR_Y:
      wp->man_yzoom = 1;
      break;
    case SR_XY:
      wp->man_yzoom = 1;
      az_cmd_zoom_absolute(ud, xstart, xend );
      break;
   }
}


/*
 * drawing area configure handler
 * The "configure_event" signal to take any necessary actions
 *   when the widget changes size.
 */
static gboolean
da_drawing_configure_cb (GtkWidget *widget, GdkEventConfigure *event,
		      gpointer data )
{
   WavePanel *wp =  (WavePanel *) data;
   GtkAllocation walloc;
   
   gtk_widget_get_allocation (widget, &walloc);
   msg_dbg( "w %d h = %d new %d %d 0x%lx",
	    walloc.width,
	    walloc.height,
	    event->width,  event->height,
	    (long unsigned int) widget );

   if ( ! wp->ud->bg_color ) {
      ac_color_initialize(wp);
   }
   if ( wp->grid_color ) {
      pa_panel_color_grid_set(wp, wp->grid_color);
   }
   
   pa_panel_label_size(wp);
   wp->configure_seen = 1;
   return TRUE;
}

/*
 *  draw one wave in a panel
 */
void da_drawing_draw_wave (VisibleWave *vw, WavePanel *wp)
{
   cairo_t *cr = wp->cr;

   if ( ! vw->shown ) {
      return;
   }
   gdk_cairo_set_source_rgba (cr, vw->color);
   cairo_set_line_join (cr, CAIRO_LINE_JOIN_MITER);
   cairo_set_line_cap (cr, CAIRO_LINE_CAP_BUTT);

   double line_width = 1.0;
   if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(vw->button))) {
      line_width = 2.0;
   }
   cairo_set_line_width (cr, line_width);

   wp->drawFunc (vw, wp); /* call the selected drawing function */
   cairo_stroke (cr);
}
/*
 *  draw segments
 */
static void
da_draw_segments(WavePanel *wp, GawSegment *segp, gint nsegments )
{
   int i;
   cairo_t *cr = wp->cr;
   
   for ( i = 0 ; i < nsegments; i++ ){
      cairo_move_to (cr, segp->x1, segp->y1);
      cairo_line_to (cr, segp->x2, segp->y2);
      segp++;
   }
}

/*
 *  draw grid in a panel
 */
static void
da_drawing_draw_grid (WavePanel *wp)
{
   GtkAllocation walloc;
   gtk_widget_get_allocation (wp->drawing, &walloc);
   int w = walloc.width;
   int h = walloc.height;

   ArrayStruct *ary = array_struct_new( sizeof(GawSegment), 128, NULL);
   GawLabels *lbx = wp->ud->xLabels;
   GawLabels *lby = wp->yLabels;

   /* calculate grid segments */
   
   /* vertical lines */
   if ( al_label_do_logAxis( lbx) ) {
      al_label_draw_vlog_grid( lbx, ary, w, h );
   } else {
      al_label_draw_vlin_grid( lbx, ary, w, h );
   }

   /* horizontal lines */
   if ( al_label_do_logAxis( lby) ) {
      al_label_draw_hlog_grid( lby, ary, w, h );
   } else {
      al_label_draw_hlin_grid( lby, ary, w, h );
   }

   /* draw grid */
   da_draw_segments(wp, (GawSegment *) array_struct_table_get(ary),
                    array_struct_get_nelem(ary) );
   array_struct_destroy(ary);
}

void
da_drawing_post_configure (GtkWidget *widget, WavePanel *wp, int w, int h )
{
   UserData  *ud =   wp->ud;
   GawLabels *lby = wp->yLabels;

   wp->configure_seen = 0;
   /*
    * Need this here for getting data when they are valid for :
    *    - init purpose
    *    - set the size of label box :
    *         - after a main window change
    *         - main scroll bar show/hide
    */
   if ( (lby->changed & CV_INIT) == 0){ /* panel creation */
      lby->changed |= CV_INIT;
      pa_panel_update_min_max(wp);
   }
   if ( lby->changed & CV_CHANGED  ){
      lby->changed &= ~CV_CHANGED ;
      pa_panel_draw_ylabels( wp );
   }
   
   GawLabels *lbx = wp->ud->xLabels;
   if ( lbx->changed & (CV_CHANGED | CV_SBCHANGED) ){
      int vis = lbx->changed & CV_SBSHOW ;
      int xlwidth = w;
      
      if ( vis ) {
         xlwidth += ud->sbSize;
      }
      if ( xlwidth != lbx->wh ){ 
         gtk_widget_set_size_request (GTK_WIDGET(ud->xlabel_box),
                                      xlwidth, lbx->lbheight);
         lbx->wh = xlwidth; /* x label box width */
         lbx->changed |= CV_CHANGED ;
      }
      lbx->changed &= ~CV_SBCHANGED ;
      msg_dbg("w %d, h %d, lbx->wh %d, visible %d", lbx->w, lbx->h, lbx->wh, vis);
   }
   if ( (lbx->changed & CV_INIT) == 0){
      /* init data at first run */
      pa_panel_update_all_data(ud); /* this call al_label_draw */
      lbx->changed |= CV_INIT | CV_CHANGED;
      lbx->changed &= ~CV_CHANGED;
   }
   if ( lbx->changed & CV_CHANGED ){
      lbx->changed &= ~CV_CHANGED ;
      al_label_draw( lbx );
   }

   ud->panelHeight = h;
   ud->panelWidth = w;
//   msg_dbg("HQ1 w %d, h %d", ud->panelWidth, ud->panelHeight);
   GtkWidget *scr = gtk_scrolled_window_get_vscrollbar(
                            GTK_SCROLLED_WINDOW(ud->panel_scrolled)) ;
   if ( gtk_widget_get_visible (scr) == FALSE ) {
	 ud->panelWidth = w - ud->sbSize ;
   }
   if ( ud->panelWidth % 2 ) {
      ud->panelWidth += 1;
   }
//  msg_dbg("HQ2 w %d, h %d, sb %d", ud->panelWidth, ud->panelHeight, ud->sbSize);
}

void
da_drawing_draw_all (GtkWidget *widget, cairo_t *cr, WavePanel *wp,
                     int w, int h )
{
   UserData  *ud =   wp->ud;
   GawLabels *lby = wp->yLabels;
   int i;
   int y;
//   GdkRGBA color;

   wp->cr = cr;
//   GtkStyleContext *context = gtk_widget_get_style_context (widget);

   /* draw background */
//   gtk_style_context_get_background_color(context, GTK_STATE_FLAG_NORMAL, &color);
   gdk_cairo_set_source_rgba (cr, ud->bg_color);
   cairo_paint(cr);  /* set background */

   cairo_set_line_width (cr, 0.1);
   cairo_rectangle (cr, 0, 0,  w, h);
   cairo_stroke (cr);

   if (wp->selected) {
      gdk_cairo_set_source_rgba (cr, ud->hl_color );
      cairo_set_line_width (cr, 1.0);
      cairo_rectangle (cr, 1, 1,  w - 2, h - 2);
      cairo_stroke (cr);
   }
   /* set color for grid */
   gdk_cairo_set_source_rgba (cr, wp->grid_color);
   cairo_set_line_width (cr, 1.0);
   
   if ( wp->showGrid ) {
      /* graticule */
      da_drawing_draw_grid (wp);
   } else {
      /* draw horizontal line at y=zero. */
      if (lby->start_val < 0 && lby->end_val > 0) {
	 y = VAL2Y(lby, 0, h);
         cairo_move_to( cr, 0, y);
         cairo_line_to( cr, w, y);
      }
   }
   cairo_stroke (cr);
   
   /* draw waves */
   g_list_foreach(wp->vwlist, (GFunc) da_drawing_draw_wave, wp); 

   /* draw the 2 cursors in the panel */
   for (i = 0 ; i < 2 ; i++) {
      AWCursor *csp = ud->cursors[i];
      if (csp->shown) {
         gdk_cairo_set_source_rgba (cr, csp->color);
         cairo_set_line_width (cr, 1.0);
         cairo_move_to(cr, csp->x, 0);
         cairo_line_to(cr, csp->x, h);
         cairo_stroke (cr);
      }
   }

   /* draw cursor annotations: x-values, y-values at intersections, delta */
   da_draw_cursor_annotations(wp, cr, w, h);

   /* draw select-range line, if in this WavePanel */
   if ( ud->srange && ud->srange->drawn && ud->srange->wp == wp) {
      da_draw_srange(ud->srange);
   }

   /* draw text */
   g_list_foreach(wp->textlist, (GFunc) gawtext_draw_text, wp);
}

/*
 * drawing area draw handler gtk3
 *  draw all in the panel
 */

gboolean
da_drawing_draw_cb (GtkWidget *widget, cairo_t *cr, gpointer data )
{
   WavePanel *wp =  (WavePanel *) data;
   UserData  *ud =   wp->ud;
   int w = gtk_widget_get_allocated_width(widget);
   int h = gtk_widget_get_allocated_height(widget);

   msg_dbg( "called width %d height %d 0x%lx", w, h, (long unsigned int) widget );

   if ( wp->configure_seen ) {
         da_drawing_post_configure (widget, wp, w, h);
   }
   if ( ud->suppress_redraw ) {
      return FALSE;
   }

   /* Panel drawing with cairo */
   da_drawing_draw_all (widget, cr, wp, w, h );

   return FALSE;
}

/*
 * Find the closest visible wave to a given (x, y) pixel position.
 * Returns the VisibleWave pointer, or NULL if none found within threshold.
 */
static VisibleWave *
da_find_closest_wave(WavePanel *wp, int px, int py)
{
   UserData *ud = wp->ud;
   GawLabels *lbx = ud->xLabels;
   GawLabels *lby = wp->yLabels;
   int h = gtk_widget_get_allocated_height(wp->drawing);
   double min_dist = 1e30;
   VisibleWave *closest = NULL;
   GList *list;
   int threshold = 10; /* pixels */

   for (list = wp->vwlist; list; list = list->next) {
      VisibleWave *vw = (VisibleWave *) list->data;
      if ( ! vw->shown ) continue;

      /* Horizontal window in values */
      double x_start = al_label_x2val(lbx, px - threshold);
      double x_end   = al_label_x2val(lbx, px + threshold);

      double y_min_val, y_max_val;
      wavevar_get_range(vw->var, x_start, x_end, &y_min_val, &y_max_val);

      int wy_min, wy_max;
      if (al_label_do_logAxis(lby)) {
         wy_min = VAL2LY(lby, y_min_val, h);
         wy_max = VAL2LY(lby, y_max_val, h);
      } else {
         wy_min = VAL2Y(lby, y_min_val, h);
         wy_max = VAL2Y(lby, y_max_val, h);
      }

      /* In pixel space, y_min might be numerically greater than y_max if y-axis is inverted */
      int p_ymin = MIN(wy_min, wy_max);
      int p_ymax = MAX(wy_min, wy_max);

      double dist;
      if (py >= p_ymin && py <= p_ymax) {
         dist = 0; /* Within the vertical range swept by the wave in the horizontal window */
      } else if (py < p_ymin) {
         dist = (double)(p_ymin - py);
      } else {
         dist = (double)(py - p_ymax);
      }

      if (dist < min_dist) {
         min_dist = dist;
         closest = vw;
      }
   }
   if (min_dist > threshold) {
      return NULL;
   }
   return closest;
}

/*
 * Find which cursor (0 or 1) is closest to pixel x position.
 * Returns cursor index (0 or 1) or -1 if none within threshold.
 */
static int
da_find_nearest_cursor(UserData *ud, int px)
{
   int i;
   int threshold = 6; /* pixels */
   int best = -1;
   int best_dist = threshold + 1;

   for (i = 0; i < 2; i++) {
      AWCursor *csp = ud->cursors[i];
      if (csp->shown) {
         int dist = abs(px - csp->x);
         if (dist < best_dist) {
            best_dist = dist;
            best = i;
         }
      }
   }
   return best;
}

/* Index of cursor being dragged (-1 = none, use drag_button mapping) */
static int da_dragged_cursor = -1;

/*
 * Draw cursor annotations: x-value box at top, y-values at wave intersections,
 * and delta between cursors.
 */
static void
da_draw_cursor_annotations(WavePanel *wp, cairo_t *cr, int w, int h)
{
   UserData *ud = wp->ud;
   int i;
   PangoLayout *layout;
   PangoFontDescription *font_desc;
   int tw, th;
   int pad = 3;

   layout = pango_cairo_create_layout(cr);
   if (ud->panelfont) {
      font_desc = pango_font_description_from_string(ud->panelfont);
   } else {
      font_desc = pango_font_description_from_string("Sans 8");
   }
   pango_layout_set_font_description(layout, font_desc);

   for (i = 0; i < 2; i++) {
      AWCursor *csp = ud->cursors[i];
      if (!csp->shown) {
         continue;
      }
      int cx = csp->x;

      /* Draw x-value box at top of cursor */
      char *xstr = val2str(csp->xval, ud->up->scientific);
      pango_layout_set_text(layout, xstr, -1);
      pango_layout_get_pixel_size(layout, &tw, &th);

      int bx = cx - tw / 2 - pad;
      int by = 1;
      /* Keep box inside panel */
      if (bx < 0) bx = 0;
      if (bx + tw + 2 * pad > w) bx = w - tw - 2 * pad;

      /* Background box */
      gdk_cairo_set_source_rgba(cr, csp->color);
      cairo_rectangle(cr, bx, by, tw + 2 * pad, th + 2 * pad);
      cairo_fill(cr);
      /* Text */
      gdk_cairo_set_source_rgba(cr, ud->bg_color);
      cairo_move_to(cr, bx + pad, by + pad);
      pango_cairo_show_layout(cr, layout);

      /* Draw y-values at cursor-wave intersections */
      GawLabels *lby = wp->yLabels;
      GList *list;
      for (list = wp->vwlist; list; list = list->next) {
         VisibleWave *vw = (VisibleWave *) list->data;
         double yval = wavevar_interp_value(vw->var, csp->xval);
         int wy;
         if (al_label_do_logAxis(lby)) {
            wy = VAL2LY(lby, yval, h);
         } else {
            wy = VAL2Y(lby, yval, h);
         }
         /* Skip if outside visible area */
         if (wy < 0 || wy > h) continue;

         /* Draw a small dot at the intersection */
         gdk_cairo_set_source_rgba(cr, vw->color);
         cairo_arc(cr, cx, (double)wy, 3.0, 0, 2 * M_PI);
         cairo_fill(cr);

         /* Draw y-value label */
         char *ystr = val2str(yval, ud->up->scientific);
         pango_layout_set_text(layout, ystr, -1);
         pango_layout_get_pixel_size(layout, &tw, &th);

         int lx = cx + 5;
         int ly = wy - th / 2;
         /* Flip to left side if too close to right edge */
         if (lx + tw + 2 * pad > w) lx = cx - tw - 2 * pad - 5;
         if (ly < 0) ly = 0;
         if (ly + th + 2 * pad > h) ly = h - th - 2 * pad;

         /* Background */
         gdk_cairo_set_source_rgba(cr, vw->color);
         cairo_rectangle(cr, lx, ly, tw + 2 * pad, th + 2 * pad);
         cairo_fill(cr);
         /* Text */
         gdk_cairo_set_source_rgba(cr, ud->bg_color);
         cairo_move_to(cr, lx + pad, ly + pad);
         pango_cairo_show_layout(cr, layout);
      }
   }

   /* Draw delta between cursors if both shown */
   AWCursor *c0 = ud->cursors[0];
   AWCursor *c1 = ud->cursors[1];
   if (c0->shown && c1->shown) {
      /* 1. X Delta */
      double dx = c1->xval - c0->xval;
      char dbuf[256];
      snprintf(dbuf, sizeof(dbuf), "\xce\x94X=%s", val2str(dx, ud->up->scientific));
      pango_layout_set_text(layout, dbuf, -1);
      pango_layout_get_pixel_size(layout, &tw, &th);

      int mid_x = (c0->x + c1->x) / 2;
      int bx = mid_x - tw / 2 - pad;
      int by = th + 2 * pad + 3;
      if (bx < 0) bx = 0;
      if (bx + tw + 2 * pad > w) bx = w - tw - 2 * pad;

      gdk_cairo_set_source_rgba(cr, c0->color);
      cairo_rectangle(cr, bx, by, tw + 2 * pad, th + 2 * pad);
      cairo_fill(cr);
      gdk_cairo_set_source_rgba(cr, ud->bg_color);
      cairo_move_to(cr, bx + pad, by + pad);
      pango_cairo_show_layout(cr, layout);

      /* 2. Y Deltas for each wave */
      GList *l1;
      int delta_y_offset = by + th + 2 * pad + 3;
      
      for (l1 = wp->vwlist; l1; l1 = l1->next) {
         VisibleWave *vw1 = (VisibleWave *) l1->data;
         double y1_c0 = wavevar_interp_value(vw1->var, c0->xval);
         double y1_c1 = wavevar_interp_value(vw1->var, c1->xval);
         
         /* Delta Y for THIS wave between C0 and C1 */
         double dy = y1_c1 - y1_c0;
         snprintf(dbuf, sizeof(dbuf), "\xce\x94Y(%s)=%s", vw1->var->varName, val2str(dy, ud->up->scientific));
         pango_layout_set_text(layout, dbuf, -1);
         pango_layout_get_pixel_size(layout, &tw, &th);
         
         if (delta_y_offset + th + 2 * pad < h) {
            bx = mid_x - tw / 2 - pad;
            if (bx < 0) bx = 0;
            if (bx + tw + 2 * pad > w) bx = w - tw - 2 * pad;
            
            gdk_cairo_set_source_rgba(cr, vw1->color);
            cairo_rectangle(cr, bx, delta_y_offset, tw + 2 * pad, th + 2 * pad);
            cairo_fill(cr);
            gdk_cairo_set_source_rgba(cr, ud->bg_color);
            cairo_move_to(cr, bx + pad, delta_y_offset + pad);
            pango_cairo_show_layout(cr, layout);
            delta_y_offset += th + 2 * pad + 2;
         }
      }
   }

   pango_font_description_free(font_desc);
   g_object_unref(layout);
}

/*
 * DnD support for dragging waves from drawing area to another panel
 */
#define DA_DND_MAGIC 0xf00bbaad
#define DA_TARGET_DVAR 1

static GtkTargetEntry da_dnd_targets[] = {
   { "x-gaw/dvar", GTK_TARGET_SAME_APP, DA_TARGET_DVAR },
};

static VisibleWave *da_armed_wave = NULL;
static int da_armed_x = 0;
static int da_armed_y = 0;

static void
da_drawing_drag_data_get_cb(GtkWidget *widget, GdkDragContext *context,
                            GtkSelectionData *selection_data,
                            guint info, guint time, gpointer data)
{
   if (da_armed_wave && info == DA_TARGET_DVAR) {
      DnDSrcData dd;
      dd.magic = DA_DND_MAGIC;
      dd.var = da_armed_wave->var;
      dd.data = (gpointer) da_armed_wave;
      GdkAtom target = gtk_selection_data_get_target(selection_data);
      gtk_selection_data_set(selection_data, target, 8,
                             (guchar *) &dd, sizeof(DnDSrcData));
   }
}

static void
da_drawing_drag_end_cb(GtkWidget *widget, GdkDragContext *context,
                       gpointer data)
{
   da_armed_wave = NULL;
}

/*
 * Pan the Y axis of a panel by a fraction of its visible range.
 * Positive fraction pans upward (increases values), negative pans down.
 */
static void
da_pan_y(WavePanel *wp, double fraction)
{
   GawLabels *lby = wp->yLabels;
   double yrange = lby->end_val - lby->start_val;
   double shift = yrange * fraction;
   pa_panel_set_yvals(wp, lby->start_val + shift, lby->end_val + shift);
   wp->man_yzoom = 1;
   ap_all_redraw(wp->ud);
}

/*
 * Pan the X axis by a fraction of its visible range.
 * Positive fraction pans right, negative pans left.
 */
static void
da_pan_x(UserData *ud, double fraction)
{
   GawLabels *lbx = ud->xLabels;
   double range = lbx->end_val - lbx->start_val;
   double shift = range * fraction;
   double new_start = lbx->start_val + shift;
   double new_end = lbx->end_val + shift;
   if (new_start < lbx->min_val) {
      new_end += lbx->min_val - new_start;
      new_start = lbx->min_val;
   }
   if (new_end > lbx->max_val) {
      new_start -= new_end - lbx->max_val;
      new_end = lbx->max_val;
   }
   az_cmd_zoom_absolute(ud, new_start, new_end);
}

/*
 * drawing area scroll event handler - zoom and pan
 */
static gboolean
da_drawing_scroll_cb(GtkWidget *widget, GdkEventScroll *event, gpointer data)
{
   WavePanel *wp = (WavePanel *) data;
   UserData *ud = wp->ud;
   GawLabels *lbx = ud->xLabels;
   double start = lbx->start_val;
   double end = lbx->end_val;
   double range = end - start;
   double factor;

   if (wp->grid_color == NULL) {
      return FALSE;
   }

   /* Side wheel (LEFT/RIGHT): pan X; Shift+side wheel: pan Y */
   if (event->direction == GDK_SCROLL_LEFT ||
       event->direction == GDK_SCROLL_RIGHT) {
      double sign = (event->direction == GDK_SCROLL_LEFT) ? -1.0 : 1.0;
      if (event->state & GDK_SHIFT_MASK) {
         da_pan_y(wp, sign * 0.1);
      } else {
         da_pan_x(ud, sign * 0.1);
      }
      return TRUE;
   }

   /* Ctrl+scroll: zoom Y axis of this panel */
   if (event->state & GDK_CONTROL_MASK) {
      GawLabels *lby = wp->yLabels;
      double yval = al_label_y2val(lby, (int) event->y);
      double ystart = lby->start_val;
      double yend = lby->end_val;
      double yrange = yend - ystart;
      double yfrac = (yrange > 0) ? (yval - ystart) / yrange : 0.5;

      if (event->direction == GDK_SCROLL_UP) {
         factor = 0.8;
      } else if (event->direction == GDK_SCROLL_DOWN) {
         factor = 1.25;
      } else {
         return FALSE;
      }
      double new_yrange = yrange * factor;
      double new_ystart = yval - yfrac * new_yrange;
      double new_yend = yval + (1.0 - yfrac) * new_yrange;
      pa_panel_set_yvals(wp, new_ystart, new_yend);
      wp->man_yzoom = 1;
      ap_all_redraw(ud);
      return TRUE;
   }

   /* Alt+scroll: pan Y axis */
   if (event->state & GDK_MOD1_MASK) {
      double sign = (event->direction == GDK_SCROLL_UP) ? 0.1 : -0.1;
      da_pan_y(wp, sign);
      return TRUE;
   }

   /* Plain scroll: zoom X axis centered on mouse */
   double xval = al_label_x2val(lbx, (int) event->x);
   double frac = (range > 0) ? (xval - start) / range : 0.5;

   if (event->direction == GDK_SCROLL_UP) {
      factor = 0.8;  /* zoom in */
   } else if (event->direction == GDK_SCROLL_DOWN) {
      factor = 1.25; /* zoom out */
   } else {
      return FALSE;
   }

   double new_range = range * factor;
   double new_start = xval - frac * new_range;
   double new_end = xval + (1.0 - frac) * new_range;

   az_cmd_zoom_absolute(ud, new_start, new_end);
   return TRUE;
}

char *da_statusFormat = N_("Panel W %d H %d X %d Y %d");

/*
 * drawing area mouse button press handler
 */
static gboolean
da_drawing_button_press_cb (GtkWidget *widget, GdkEventButton *event,
			 gpointer data )
{
   WavePanel *wp =  (WavePanel *) data;
   UserData  *ud =   wp->ud;
   GawText *gtext;
   
   msg_dbg( "button %d state %d mouseState %d",
	   event->button, event->state, ud->mouseState  );

   if ( wp->grid_color == NULL){
      return FALSE; /* we haven't gotten a configure event */
   }
   /* Middle button starts panning */
   if ( event->button == 2 ){
      pa_panel_set_selected( wp, ud );
      gtk_grab_add(widget);
      ud->button_down = event->button;
      ud->mouseState = M_PAN_DRAG;
      ud->drag_button = event->button;
      /* Store starting x position in srange for convenience */
      if (ud->srange) {
         ud->srange->x1 = (int) event->x;
         ud->srange->y1 = (int) event->y;
         ud->srange->wp = wp;
      }
      da_set_gdk_cursor(widget, GDK_FLEUR);
      return TRUE;
   }
   if ( event->button == 3 ){
      GtkWidget *menu;
      VisibleWave *vw_hit;

      gtext = (GawText *) pa_panel_inside_text( wp, event->x, event->y);
      if ( gtext ){
         menu = wp->textpopmenu;
         g_object_set_data (G_OBJECT(menu), "PanelTextPopup-action", (gpointer) gtext);
      } else {
         vw_hit = da_find_closest_wave(wp, (int) event->x, (int) event->y);
         if ( vw_hit ) {
            /* right-click on a wave: select it, then show wave popup */
            pa_panel_set_selected( wp, ud );
            if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(vw_hit->button))) {
               gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(vw_hit->button), TRUE);
            }
            ud->last_clicked_wave = vw_hit;
            menu = vw_hit->buttonpopup;
         } else if ( wp->selected ) {
            pa_panel_set_selected( NULL, ud );
            ud->last_clicked_wave = NULL;
            da_drawing_redraw(wp->drawing);
            menu = wp->popmenu;
         } else {
            ud->last_clicked_wave = NULL;
            menu = wp->popmenu;
         }
      }

      gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
		      NULL, NULL,  3, event->time);

      return TRUE;
   }
   char text[256];

   GtkAllocation walloc;
   gtk_widget_get_allocation (widget, &walloc);
   if ( event->button == 1 ) {
      sprintf (text, gettext(da_statusFormat), walloc.width,
               walloc.height, (int) event->x, (int) event->y );
      gtk_statusbar_push (GTK_STATUSBAR(ud->statusbar), 2, text);
   }
   switch (ud->mouseState) {
    case M_NONE:
      pa_panel_set_selected( wp, ud );
      gtk_grab_add(widget);
      ud->button_down = event->button;

      /* Shift+left-click: start zoom rectangle */
      if ( event->button == 1 && (event->state & GDK_SHIFT_MASK) ) {
         ud->mouseState = M_SELRANGE_ACTIVE;
         ud->srange->type = SR_XY;
         ud->srange->y1 = ud->srange->y2 = event->y;
         ud->srange->x1 = ud->srange->x2 = event->x;
         ud->srange->x1_root = event->x_root;
         ud->srange->y1_root = event->y_root;
         ud->srange->wp = wp;
         da_set_gdk_cursor(widget, GDK_CROSSHAIR);
         break;
      }

      gtext = (GawText *) pa_panel_inside_text( wp, event->x, event->y);
      if ( gtext ){
         ud->mouseState = M_TEXT_DRAG;
         gtext->cx = (int) event->x;
         gtext->cy = (int) event->y;
         gtext->maxwidth = walloc.width;
         gtext->maxheight = walloc.height;
      } else {
         /* Check if click is on a wave trace */
         VisibleWave *closest = da_find_closest_wave(wp, (int) event->x, (int) event->y);
         if (closest) {
            if ( event->type == GDK_2BUTTON_PRESS && event->button == 1 ) {
               /* Fast double left click on drawing: hide it */
               closest->shown = 0;
               wave_label_update(closest);
               da_drawing_redraw(wp->drawing);
               ud->mouseState = M_NONE;
               gtk_grab_remove(widget);
            } else {
               /* Arm for potential drag or toggle on release */
               da_armed_wave = closest;
               da_armed_x = (int) event->x;
               da_armed_y = (int) event->y;
               ud->mouseState = M_WAVE_ARMED;
               ud->last_clicked_wave = closest;
            }
         } else {
            /* Check if clicking near an existing cursor to drag it */
            ud->last_clicked_wave = NULL;
            int near_cursor = da_find_nearest_cursor(ud, (int) event->x);
            if (near_cursor >= 0) {
               da_dragged_cursor = near_cursor;
               ud->drag_button = near_cursor + 1; /* cursor 0→button 1, cursor 1→button 2 */
            } else {
               da_dragged_cursor = ud->last_dragged_cursor;
               ud->drag_button = ud->last_dragged_cursor + 1;
            }
            ud->mouseState = M_CURSOR_DRAG;
            da_set_gdk_cursor(widget, GDK_SB_H_DOUBLE_ARROW);
            cu_display_xcursor(wp, ud->drag_button, event->x, 0);
         }
      }
      break;

    case M_SELRANGE_ARMED:
      gtk_grab_add(widget);
      ud->button_down = event->button;
      ud->mouseState = M_SELRANGE_ACTIVE;

      ud->srange->y1 = ud->srange->y2 = event->y;
      ud->srange->x1 = ud->srange->x2 = event->x;
      ud->srange->x1_root = event->x_root;
      ud->srange->y1_root = event->y_root;
      ud->srange->wp = wp;

      break;
      /* can't start another drag until first one done */

    case M_DRAW_TEXT:
      ud->button_down = event->button;
      break;

    case M_CURSOR_DRAG:
    case M_SELRANGE_ACTIVE:
    default:
      break;
   }
   ap_all_panel_redraw(ud);

   return TRUE;
}
/*
 * drawing area mouse button release handler
 */
static gboolean
da_drawing_button_release_cb (GtkWidget *widget, GdkEventButton *event,
			   gpointer data )
{
   WavePanel *wp =  (WavePanel *) data;
   UserData  *ud =  wp->ud;

   app_memcheck();
   msg_dbg( "button %d state %d", event->button, ud->mouseState );

   if ( wp->grid_color == NULL || ud->button_down != event->button ){
      return FALSE; /* we haven't gotten a configure event */
   }
   if ( event->button == 1 ) {
      gtk_statusbar_pop (GTK_STATUSBAR(ud->statusbar), 2);
   }
   GdkWindow *window = gtk_widget_get_window (widget);
   switch(ud->mouseState) {
    case M_WAVE_ARMED:
      /* Click on wave without dragging: toggle selection */
      if (da_armed_wave) {
         gboolean active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(da_armed_wave->button));
         gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(da_armed_wave->button), !active);
      }
      gtk_grab_remove(widget);
      da_armed_wave = NULL;
      break;

    case M_PAN_DRAG:
      gtk_grab_remove(widget);
      gdk_window_set_cursor(window, NULL);
      ud->drag_button = -1;
      break;

    case M_TEXT_DRAG:
    case M_CURSOR_DRAG:
      gtk_grab_remove(widget);
      gdk_window_set_cursor(window, NULL);
      if ( ud->mouseState == M_CURSOR_DRAG) {
         cu_display_xcursor(wp, ud->drag_button, event->x, 0);
      }
      ud->drag_button = -1;
      da_dragged_cursor = -1;
      break;

    case M_SELRANGE_ACTIVE:
      gtk_grab_remove(widget);
      g_list_foreach(ud->panelList, (GFunc) pa_panel_drawing_set_gdk_cursor,
		     GINT_TO_POINTER (-1) ); /* clear gdk cursor */
      da_update_srange(ud->srange, (GdkEventMotion *) event, 0);
      da_callback_srange(ud, wp);
      break;

    case M_DRAW_TEXT:
      ud->gtexttmp = NULL;      
      ud->mouseState = M_NONE;
      break;

    default:
      break;
   }
   ud->button_down = -1;
   ud->mouseState = M_NONE;
   pa_panel_full_redraw(wp);
   return TRUE;
}

/*
 * drawing area mouse motion handler
 */
static gboolean
da_drawing_motion_cb (GtkWidget *widget, GdkEventMotion *event, gpointer data )
{
   WavePanel *wp =  (WavePanel *) data;
   UserData  *ud =  wp->ud;
   char text[256];
   GawText *gtext;
   int x = (int) event->x;
   int y = (int) event->y;
   
//   msg_dbg( "state %d, x %d, y %d", ud->mouseState, x, y );

   if ( wp->grid_color == NULL){
      return FALSE; /* we haven't gotten a configure event */
   }
   GtkAllocation walloc;
   gtk_widget_get_allocation (widget, &walloc);
   if (ud->up->xconvert == 1 ) {
      time_t mytime = (time_t) al_label_x2val(ud->xLabels,  x );
      convert_time_t_to_date( mytime, ud->up->date_fmt, text, sizeof(text) );
   } else {
      sprintf (text, gettext(da_statusFormat), walloc.width,
               walloc.height, x, y );
   }
   gtk_label_set_text ( GTK_LABEL(ud->statusLabel), text); 

   switch(ud->mouseState) {
    case M_NONE:
      gtext = (GawText *) pa_panel_inside_text( wp, x, y);
      if ( gtext) {
         da_set_gdk_cursor(widget, GDK_HAND2);
      } else {
         da_set_gdk_cursor(widget, -1);
      }
      break;

    case M_WAVE_ARMED:
      /* Check if mouse has moved enough to start a drag */
      if (gtk_drag_check_threshold(widget, da_armed_x, da_armed_y, x, y)) {
         GtkTargetList *tlist = gtk_target_list_new(da_dnd_targets, 1);
         gtk_drag_begin_with_coordinates(widget, tlist,
                                         GDK_ACTION_COPY | GDK_ACTION_MOVE,
                                         1, (GdkEvent *) event,
                                         x, y);
         gtk_target_list_unref(tlist);
         gtk_grab_remove(widget);
         ud->mouseState = M_NONE;
         ud->button_down = -1;
      }
      break;

    case M_PAN_DRAG:
      {
         GawLabels *lbx = ud->xLabels;
         int dx = x - ud->srange->x1;
         double start = lbx->start_val;
         double end = lbx->end_val;
         int w = gtk_widget_get_allocated_width(widget);
         double range = end - start;
         double shift = -(double)dx * range / (double)w;
         double new_start = start + shift;
         double new_end = end + shift;
         ud->srange->x1 = x;
         ud->srange->y1 = y;
         az_cmd_zoom_absolute(ud, new_start, new_end);
      }
      break;

    case M_CURSOR_DRAG:
      cu_display_xcursor(wp, ud->drag_button, x, 1);
      break;

    case M_SELRANGE_ACTIVE:
      da_update_srange(ud->srange, event, 1);
      da_drawing_redraw(widget);
      break;
      
    case M_TEXT_DRAG:
      gtext = (GawText *) pa_panel_inside_text( wp, x, y);
      if ( ! gtext) {
         break;
      }
      gawtext_update_pos( gtext, x, y);
      da_drawing_redraw(widget);
      break;
      
    case M_DRAW_TEXT:
      gawtext_update_pos( ud->gtexttmp, event->x, event->y);
      da_drawing_redraw(widget);
      break;
      
    default:
      break;
   }

   return TRUE;
}

/*
 * drawing area mouse motion handler
 */
static gboolean
da_drawing_crossing_cb (GtkWidget *widget, GdkEventCrossing *event, gpointer data )
{
   WavePanel *wp =  (WavePanel *) data;
   UserData  *ud =  wp->ud;

   if ( wp->grid_color == NULL){
      return FALSE; /* we haven't gotten a configure event */
   }

//   msg_dbg( "state %d, type %s", ud->mouseState,
//            (event->type == GDK_ENTER_NOTIFY ? "Enter" : "Leave") );
   switch(ud->mouseState) {
    case M_DRAW_TEXT:
      if ( event->type == GDK_ENTER_NOTIFY) {
         GtkAllocation walloc;
         gtk_widget_get_allocation (widget, &walloc);
         wp->textlist = g_list_prepend(wp->textlist, ud->gtexttmp);
         ud->gtexttmp->maxwidth = walloc.width;
         ud->gtexttmp->maxheight = walloc.height;
      } else if ( event->type == GDK_LEAVE_NOTIFY) {
         wp->textlist = g_list_remove(wp->textlist, ud->gtexttmp);
      }
      da_drawing_redraw(widget);
      break;

    default:
      break;
   }

   return TRUE;
}

/*
 * da are created to their minimal size
 * They automtically expand to the main window allocated size.
 */

void da_drawing_set_size_request(GtkWidget *drawing, int w, int h)
{
   gtk_widget_set_size_request (GTK_WIDGET(drawing), w, h );
   msg_dbg( "w %d, h %d", w, h );
}


/*
 * Construct drawing area.
 */ 
 GtkWidget *da_drawing_create( WavePanel *wp )
{
   UserData *ud = wp->ud;
   UserPrefs *up = ud->up;
   GtkWidget *drawing;
   
   msg_dbg("width = %d, height = %d, showXlabels = %d",
	   up->panelWidth, up->panelHeight, up->showXLabels);

   /* drawing area for waveform */
   drawing = gtk_drawing_area_new();
   gtk_widget_set_name( drawing, "wavepanel");
   gtk_widget_set_hexpand(drawing, TRUE );
   g_object_ref (drawing); /* increment ref to avoid destruction */

   g_signal_connect( drawing, "draw", 
		     G_CALLBACK (da_drawing_draw_cb), (gpointer) wp);
   g_signal_connect (drawing,"configure_event",
		     G_CALLBACK (da_drawing_configure_cb), (gpointer) wp);

   g_signal_connect( drawing, "button_press_event", 
		     G_CALLBACK (da_drawing_button_press_cb), (gpointer) wp);
   g_signal_connect( drawing, "button_release_event", 
		     G_CALLBACK (da_drawing_button_release_cb), (gpointer) wp);
   g_signal_connect( drawing, "motion_notify_event", 
		     G_CALLBACK ( da_drawing_motion_cb), (gpointer) wp);
   g_signal_connect( drawing, "enter_notify_event", 
		     G_CALLBACK ( da_drawing_crossing_cb), (gpointer) wp);
   g_signal_connect( drawing, "leave_notify_event",
		     G_CALLBACK ( da_drawing_crossing_cb), (gpointer) wp);
   g_signal_connect( drawing, "scroll_event",
		     G_CALLBACK ( da_drawing_scroll_cb), (gpointer) wp);
   g_signal_connect( drawing, "drag_data_get",
		     G_CALLBACK ( da_drawing_drag_data_get_cb), (gpointer) wp);
   g_signal_connect( drawing, "drag_end",
		     G_CALLBACK ( da_drawing_drag_end_cb), (gpointer) wp);

   da_drawing_set_size_request(drawing, up->minPanelWidth, up->minPanelHeight );
   
   gtk_widget_show(drawing);
   
   /* Set up a drawing as a drop target */
   ad_set_drag_dest(drawing, ud, wp, DND_PANEL);

   gtk_widget_set_events(drawing, GDK_EXPOSURE_MASK |
			 GDK_BUTTON_RELEASE_MASK |
			 GDK_BUTTON_PRESS_MASK |
			 GDK_POINTER_MOTION_MASK |
			 GDK_BUTTON1_MOTION_MASK |
			 GDK_BUTTON2_MOTION_MASK |
                         GDK_LEAVE_NOTIFY_MASK |
                         GDK_ENTER_NOTIFY_MASK |
                         GDK_SCROLL_MASK );
   return drawing;
}

