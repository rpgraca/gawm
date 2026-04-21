/*
 * datafile.c - datafile interface functions
 * 
 * include LICENSE
 */
#include <stdio.h>
#include <string.h>

#include <gtk/gtk.h>

#include <gaw.h>
#include <duprintf.h>
#include <gawpixmaps.h>
 
#ifdef TRACE_MEM
#include <tracemem.h>
#endif
        

/*
 *** \brief Allocates memory for a new DataFile object.
 */

DataFile *datafile_new( void *ud, char *name )
{
   DataFile *wdata;

   wdata =  app_new0(DataFile, 1);
   datafile_construct( wdata, ud, name );
   app_class_overload_destroy( (AppClass *) wdata, datafile_destroy );
   return wdata;
}

/** \brief Constructor for the DataFile object. */

void datafile_construct( DataFile *wdata, void *ud, char *name )
{
   static int next_tag = 0;
   
   app_class_construct( (AppClass *) wdata );
   wdata->ud = ud;
   wdata->wt = wavetable_new( (AppClass *) wdata, name ) ;
   wdata->ftag = next_tag++;
   aw_vl_menu_item_add( wdata);
}

/** \brief Destructor for the DataFile object. */

void datafile_destroy(void *wdata)
{
   DataFile *this = (DataFile *) wdata;

   if (wdata == NULL) {
      return;
   }
   if ( this->vlmenu ){
      g_object_ref_sink (this->vlmenu);
   }
   /* remove per-file GUI stuff */
   datafile_list_win_destroy(this);

   if ( this->drag_icon ) {
      g_object_unref (this->drag_icon);
   }
   if ( this->lbpopmenu ){
      g_object_ref_sink (this->lbpopmenu);
   }
   
   app_free(this->filename);
   app_free(this->format);
   
   wavetable_destroy(this->wt);
   
   app_class_destroy( wdata );
}

void datafile_dup_filename(DataFile *wdata, char *filename)
{
   app_free(wdata->filename);
   wdata->filename = app_strdup(filename);
}

void datafile_dup_format(DataFile *wdata, char *format)
{
   app_free(wdata->format);
   wdata->format = app_strdup(format);
}

void datafile_set_file(DataFile *wdata,  char *filename, char *format)
{
   datafile_dup_filename(wdata, filename);
   datafile_dup_format(wdata, format);
   wdata->method = DATAFILE_FILE;
}

void datafile_set_sound(DataFile *wdata,  SoundParams *sparams)
{
   wdata->sparams = sparams;
   wdata->method = DATAFILE_SOUND;
}

int datafile_load(DataFile *wdata)
{
   int ret;
   
   if ( wdata->method == DATAFILE_SOUND ) {
      sound_new( wdata->sparams, wdata->wt);
   } else  if ( wdata->method == DATAFILE_FILE ) {
      SpiceStream *ss = spicestream_new( wdata->filename, wdata->format, wdata->wt);
      if ( ss->status ) {
	 return  ss->status ;
      }
   }
   if ( ( ret = wavetable_fill_tables( wdata->wt, wdata->filename)) < 0 ){
      return ret;
   }
   if ( ! wdata->wlist_win ) {
      datafile_create_list_win (wdata);
   }
   return 0;
}

int datafile_reload(DataFile *wdata)
{
   int ret;
   UserData *ud = wdata->ud; /* stefan */

   wdata->old_wt = wdata->wt;   /* need this for clean up */
   wdata->wt = wavetable_new( (void *) wdata, wavetable_get_tblname( wdata->wt) ) ;
   if ( ( ret = datafile_load(wdata)) < 0 ){
      return ret;
   }
   if ( ud->gawio ){  /* stefan */
      GawIoData *gawio = (GawIoData *) ud->gawio; /* stefan */
      gawio->wds = wavetable_get_cur_dataset(wdata->wt); /* stefan */
   }
   return 0;
}

/*
 * Tree store columns for hierarchical signal list
 */
#define GAW_PRIVATE_DND_MAGIC 0xf00bbaad
#define TREE_TARGET_DVAR 1

static GtkTargetEntry tree_dnd_targets[] = {
   { "x-gaw/dvar", GTK_TARGET_SAME_APP, TREE_TARGET_DVAR },
};

enum {
   COL_LABEL = 0,    /* display text */
   COL_VAR,          /* WaveVar pointer, NULL for folders */
   COL_FULLNAME,     /* full variable name for tooltip */
   NUM_COLS
};

/*
 * Parse a SPICE variable name into hierarchy path parts and a leaf label.
 * E.g. "v(xcirc1.xcirc2.node1)" -> path=["xcirc1","xcirc2"], leaf="v(node1)"
 *      "i(v1)" -> path=[], leaf="i(v1)"
 *      "xcirc1.foo#branch" -> path=["xcirc1"], leaf="foo#branch"
 *
 * Caller must free the returned path array elements, array, and leaf string with g_free.
 */
static void
datafile_parse_var_name(const char *varName, char ***out_path, int *out_npath, char **out_leaf)
{
   const char *open_paren = strchr(varName, '(');
   const char *close_paren = strrchr(varName, ')');
   char *inner = NULL;
   char *prefix = NULL;
   char *suffix = NULL;

   if (open_paren && close_paren && close_paren > open_paren) {
      /* Has parentheses: e.g. "v(xcirc1.xcirc2.node1)" */
      int prefix_len = (int)(open_paren - varName + 1);
      prefix = g_strndup(varName, prefix_len);
      int inner_len = (int)(close_paren - open_paren - 1);
      inner = g_strndup(open_paren + 1, inner_len);
      suffix = g_strdup(close_paren);
   } else {
      /* No parentheses: split on '.' directly */
      prefix = g_strdup("");
      inner = g_strdup(varName);
      suffix = g_strdup("");
   }

   char **parts = g_strsplit(inner, ".", -1);
   int nparts = (int)g_strv_length(parts);

   if (nparts <= 1) {
      *out_npath = 0;
      *out_path = NULL;
      *out_leaf = g_strdup(varName);
   } else {
      *out_npath = nparts - 1;
      *out_path = g_new(char *, nparts - 1);
      for (int i = 0; i < nparts - 1; i++) {
         (*out_path)[i] = g_strdup(parts[i]);
      }
      *out_leaf = g_strdup_printf("%s%s%s", prefix, parts[nparts - 1], suffix);
   }

   g_strfreev(parts);
   g_free(inner);
   g_free(prefix);
   g_free(suffix);
}

/*
 * Find a child row of parent with the given label text,
 * only among folder rows (COL_VAR == NULL).
 * Returns TRUE and sets result if found.
 */
static gboolean
datafile_tree_find_child(GtkTreeStore *store, GtkTreeIter *parent,
                         const char *label, GtkTreeIter *result)
{
   GtkTreeIter iter;
   gboolean valid;

   valid = gtk_tree_model_iter_children(GTK_TREE_MODEL(store), &iter, parent);
   while (valid) {
      WaveVar *var = NULL;
      char *text = NULL;
      gtk_tree_model_get(GTK_TREE_MODEL(store), &iter,
                         COL_LABEL, &text, COL_VAR, &var, -1);
      if (var == NULL && text && strcmp(text, label) == 0) {
         g_free(text);
         *result = iter;
         return TRUE;
      }
      g_free(text);
      valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
   }
   return FALSE;
}

/*
 * Find the position of the first leaf child (COL_VAR != NULL) under parent.
 * Folders are kept before leaves; this finds the insertion point for new folders.
 */
static gboolean
datafile_tree_find_first_leaf(GtkTreeStore *store, GtkTreeIter *parent,
                              GtkTreeIter *result)
{
   GtkTreeIter iter;
   gboolean valid;

   valid = gtk_tree_model_iter_children(GTK_TREE_MODEL(store), &iter, parent);
   while (valid) {
      WaveVar *var = NULL;
      gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, COL_VAR, &var, -1);
      if (var != NULL) {
         *result = iter;
         return TRUE;
      }
      valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
   }
   return FALSE;
}

/*
 * Add a WaveVar to the tree store, creating intermediate folder nodes as needed.
 * Folders are inserted before leaves at each level so they appear at the top.
 * The file root node (wdata->wlist_root_iter) is the starting parent.
 */
static void
datafile_add_tree_var(gpointer d, gpointer p)
{
   WaveVar *var = (WaveVar *) d;
   DataFile *wdata = (DataFile *) p;
   char **path = NULL;
   int npath = 0;
   char *leaf = NULL;
   GtkTreeIter parent_iter;
   GtkTreeIter *parent = NULL;
   GtkTreeIter iter;
   int i;

   /* start from the file root node */
   parent_iter = wdata->wlist_root_iter;
   parent = &parent_iter;

   datafile_parse_var_name(var->varName, &path, &npath, &leaf);

   /* Navigate/create intermediate folder nodes */
   for (i = 0; i < npath; i++) {
      GtkTreeIter found;
      if (datafile_tree_find_child(wdata->wlist_store, parent, path[i], &found)) {
         parent_iter = found;
      } else {
         /* Insert folder before the first leaf to keep folders at top */
         GtkTreeIter first_leaf;
         if (datafile_tree_find_first_leaf(wdata->wlist_store, parent, &first_leaf)) {
            gtk_tree_store_insert_before(wdata->wlist_store, &iter, parent, &first_leaf);
         } else {
            gtk_tree_store_append(wdata->wlist_store, &iter, parent);
         }
         gtk_tree_store_set(wdata->wlist_store, &iter,
                           COL_LABEL, path[i],
                           COL_VAR, NULL,
                           COL_FULLNAME, NULL,
                           -1);
         parent_iter = iter;
      }
      parent = &parent_iter;
   }

   /* Append leaf node (after any folders) */
   gtk_tree_store_append(wdata->wlist_store, &iter, parent);
   gtk_tree_store_set(wdata->wlist_store, &iter,
                     COL_LABEL, leaf,
                     COL_VAR, var,
                     COL_FULLNAME, var->varName,
                     -1);

   for (i = 0; i < npath; i++) {
      g_free(path[i]);
   }
   g_free(path);
   g_free(leaf);
}

/*
 * Cell data function for the single label renderer.
 * Prepends depth-based indentation spaces, then a triangle arrow for folder rows
 * (▸ collapsed, ▾ expanded).  Leaf rows get the same indent but no arrow,
 * so the arrow on a folder is aligned with the first letter of a leaf at the
 * same depth.
 */
static void
datafile_label_cell_data_func(GtkTreeViewColumn *col, GtkCellRenderer *cell,
                              GtkTreeModel *model, GtkTreeIter *iter,
                              gpointer data)
{
   char *label = NULL;
   GString *display;
   int i;

   gtk_tree_model_get(model, iter, COL_LABEL, &label, -1);

   GtkTreePath *path = gtk_tree_model_get_path(model, iter);
   int depth = gtk_tree_path_get_depth(path) - 1;

   display = g_string_new("");
   for (i = 0; i < depth; i++) {
      g_string_append(display, "    ");
   }

   if (gtk_tree_model_iter_has_child(model, iter)) {
      GtkWidget *tv = gtk_tree_view_column_get_tree_view(col);
      gboolean expanded = gtk_tree_view_row_expanded(GTK_TREE_VIEW(tv), path);
      g_string_append(display, expanded ? "\xe2\x96\xbe " : "\xe2\x96\xb8 ");
   }

   g_string_append(display, label ? label : "");
   g_object_set(cell, "text", display->str, NULL);

   g_string_free(display, TRUE);
   g_free(label);
   gtk_tree_path_free(path);
}

/*
 * Redraw the parent row after expand/collapse so the arrow direction updates.
 */
static void
datafile_tree_row_toggled_cb(GtkTreeView *tv, GtkTreeIter *iter,
                             GtkTreePath *path, gpointer data)
{
   GtkTreeModel *model = gtk_tree_view_get_model(tv);
   gtk_tree_model_row_changed(model, path, iter);
}

/*
 * Tree view row activated (double-click): add variable to selected panel.
 */
static void
datafile_tree_row_activated_cb(GtkTreeView *treeview, GtkTreePath *path,
                               GtkTreeViewColumn *column, gpointer data)
{
   GtkTreeModel *model = gtk_tree_view_get_model(treeview);
   GtkTreeIter iter;
   WaveVar *var = NULL;

   if (gtk_tree_model_get_iter(model, &iter, path)) {
      gtk_tree_model_get(model, &iter, COL_VAR, &var, -1);
      if (var) {
         ap_panel_add_var(NULL, var, NULL, NULL);
      }
      /* folders are handled by single-click in button_press_cb */
   }
}

/*
 * Tree view button press:
 *   Left click on folder row: toggle expand/collapse
 *   Right click on leaf: popup menu
 */
static gint
datafile_tree_button_press_cb(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
   DataFile *wdata = (DataFile *) data;
   GtkTreePath *path = NULL;
   GtkTreeView *treeview = GTK_TREE_VIEW(widget);

   if (!gtk_tree_view_get_path_at_pos(treeview, (gint)event->x, (gint)event->y,
                                       &path, NULL, NULL, NULL)) {
      return FALSE;
   }

   GtkTreeModel *model = gtk_tree_view_get_model(treeview);
   GtkTreeIter iter;

   if (!gtk_tree_model_get_iter(model, &iter, path)) {
      gtk_tree_path_free(path);
      return FALSE;
   }

   WaveVar *var = NULL;
   gtk_tree_model_get(model, &iter, COL_VAR, &var, -1);

   if (event->button == 1 && event->type == GDK_BUTTON_PRESS && var == NULL) {
      /* Single left click on folder: toggle expand/collapse */
      if (gtk_tree_view_row_expanded(treeview, path)) {
         gtk_tree_view_collapse_row(treeview, path);
      } else {
         gtk_tree_view_expand_row(treeview, path, FALSE);
      }
      gtk_tree_path_free(path);
      return TRUE;
   }

   if (event->button == 3 && event->type == GDK_BUTTON_PRESS && var != NULL) {
      /* Right click on leaf: popup menu */
      g_object_set_data(G_OBJECT(wdata->lbpopmenu),
                        "ListButtonPopup-action", var);
      gtk_menu_popup(GTK_MENU(wdata->lbpopmenu), NULL, NULL,
                     NULL, var, 3, event->time);
      gtk_tree_path_free(path);
      return TRUE;
   }

   gtk_tree_path_free(path);
   return FALSE;
}

/*
 * Tree view drag-data-get callback: provide DnDSrcData for the dragged row
 */
static void
datafile_tree_drag_data_get_cb(GtkWidget *widget, GdkDragContext *context,
                                GtkSelectionData *selection_data,
                                guint info, guint time, gpointer data)
{
   if (info == TREE_TARGET_DVAR) {
      GtkTreeView *treeview = GTK_TREE_VIEW(widget);
      GtkTreeSelection *sel = gtk_tree_view_get_selection(treeview);
      GtkTreeModel *model;
      GtkTreeIter iter;

      if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
         WaveVar *var = NULL;
         gtk_tree_model_get(model, &iter, COL_VAR, &var, -1);
         if (var) {
            DnDSrcData dd;
            dd.magic = GAW_PRIVATE_DND_MAGIC;
            dd.var = var;
            dd.data = NULL;

            GdkAtom target = gtk_selection_data_get_target(selection_data);
            gtk_selection_data_set(selection_data, target, 8,
                                  (gpointer)&dd, sizeof(DnDSrcData));
         }
      }
   }
}

/*
 * Tree view drag-begin: set the drag icon
 */
static void
datafile_tree_drag_begin_cb(GtkWidget *widget, GdkDragContext *context, gpointer data)
{
   DataFile *wdata = (DataFile *) data;
   if (wdata->drag_icon) {
      gtk_drag_set_icon_pixbuf(context, wdata->drag_icon, -2, -2);
   }
}

void datafile_list_win_empty(DataFile *wdata)
{
   if (wdata->wlist_store) {
      /* Remove all children of the file root, but keep the root itself */
      GtkTreeIter child;
      while (gtk_tree_model_iter_children(GTK_TREE_MODEL(wdata->wlist_store),
                                           &child, &wdata->wlist_root_iter)) {
         gtk_tree_store_remove(wdata->wlist_store, &child);
      }
   }
}

void datafile_list_win_fill(DataFile *wdata)
{
   if (wdata->wlist_win) {
      wavetable_foreach_wavevar(wdata->wt, datafile_add_tree_var,
				(gpointer) wdata);
   }
}

/* kept for backward compatibility */
void
datafile_add_list_button(gpointer d, gpointer p)
{
   datafile_add_tree_var(d, p);
}

void datafile_list_win_destroy(DataFile *wdata)
{
   aw_vl_menu_item_remove(wdata);
   if (wdata->wlist_win) {
      gtk_widget_destroy(wdata->wlist_win);
   }
   wdata->wlist_win = NULL;
   wdata->wlist_store = NULL;
   wdata->wlist_treeview = NULL;
}
/*
 * Show the variable-list for a waveform data file.
 * The list is embedded as a hierarchical tree in the main window's signal list pane.
 * The file name is a foldable root node; subcircuit hierarchy folds underneath.
 * Everything starts collapsed. Folders sort before leaves at each level.
 */
void
datafile_create_list_win (DataFile *wdata)
{
   UserData *ud = wdata->ud;
   GtkWidget *box1;
   GtkWidget *treeview;
   GtkTreeStore *store;
   GtkCellRenderer *renderer;
   GtkTreeViewColumn *column;
   const char *filename;

   if ( ! wdata) {
      msg_warning(_("wdata is NULL"));
      return;
   }
   if ( wdata->wlist_win) {
      /* already embedded in the signal list pane */
      return;
   }

   /* a vertical box to hold menu + tree view */
   box1 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   gtk_widget_set_name(box1, "data_list_box");
   wdata->wlist_win = box1;
   wdata->wlist_vbox = box1;

   /* this sets wlist_win = NULL at destroy event */
   g_signal_connect (box1, "destroy",
		     G_CALLBACK (gtk_widget_destroyed),
		     &(wdata->wlist_win) );

   /* create the per-file menu */
   gm_create_vl_menu ( wdata );
   gtk_box_pack_start (GTK_BOX (box1), wdata->vlmenu, FALSE, FALSE, 0);

   /* create tree store and tree view for hierarchical signal list */
   store = gtk_tree_store_new(NUM_COLS, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_STRING);
   wdata->wlist_store = store;

   treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
   g_object_unref(store);  /* tree view owns it now */
   wdata->wlist_treeview = treeview;
   wdata->wlist_box = treeview;  /* backward compat */
   gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeview), FALSE);
   gtk_widget_set_name(treeview, "signalTreeView");

   /*
    * Disable built-in expanders so that the arrow text we draw in the label
    * renderer is aligned with the first character of leaf rows at the same depth.
    * Indentation is handled by a spacer cell renderer instead.
    */
   gtk_tree_view_set_show_expanders(GTK_TREE_VIEW(treeview), FALSE);
   gtk_tree_view_set_level_indentation(GTK_TREE_VIEW(treeview), 0);

   /* Single column with one renderer: indent + arrow + label via cell data func */
   column = gtk_tree_view_column_new();
   gtk_tree_view_column_set_title(column, "Signal");

   renderer = gtk_cell_renderer_text_new();
   g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, "width-chars", 1, NULL);
   gtk_tree_view_column_pack_start(column, renderer, TRUE);

   gtk_tree_view_column_set_cell_data_func(column, renderer,
                                            datafile_label_cell_data_func,
                                            NULL, NULL);

   gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

   /* tooltip from full variable name */
   gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(treeview), COL_FULLNAME);

   /* row activated: double-click to add variable */
   g_signal_connect(treeview, "row-activated",
                    G_CALLBACK(datafile_tree_row_activated_cb), wdata);
   /* single-click on folders and right-click popup */
   g_signal_connect(treeview, "button-press-event",
                    G_CALLBACK(datafile_tree_button_press_cb), wdata);
   /* redraw arrow direction on expand/collapse */
   g_signal_connect(treeview, "row-expanded",
                    G_CALLBACK(datafile_tree_row_toggled_cb), wdata);
   g_signal_connect(treeview, "row-collapsed",
                    G_CALLBACK(datafile_tree_row_toggled_cb), wdata);

   /* DnD source: drag signals to wave panels */
   gtk_tree_view_enable_model_drag_source(GTK_TREE_VIEW(treeview),
                                           GDK_BUTTON1_MASK,
                                           tree_dnd_targets,
                                           G_N_ELEMENTS(tree_dnd_targets),
                                           GDK_ACTION_COPY | GDK_ACTION_MOVE);
   g_signal_connect(treeview, "drag-data-get",
                    G_CALLBACK(datafile_tree_drag_data_get_cb), wdata);
   g_signal_connect(treeview, "drag-begin",
                    G_CALLBACK(datafile_tree_drag_begin_cb), wdata);

   wdata->drag_icon = gdk_pixbuf_new_from_xpm_data (wave_drag_ok_xpm);

   gtk_box_pack_start (GTK_BOX (box1), treeview, TRUE, TRUE, 0);
   gtk_widget_show(treeview);

   /* Create the file root node (foldable) */
   filename = wdata->filename ? wdata->filename : wdata->wt->tblname;
   gtk_tree_store_append(wdata->wlist_store, &wdata->wlist_root_iter, NULL);
   gtk_tree_store_set(wdata->wlist_store, &wdata->wlist_root_iter,
                     COL_LABEL, filename,
                     COL_VAR, NULL,
                     COL_FULLNAME, filename,
                     -1);

   /* Populate signals under the file root */
   datafile_list_win_fill(wdata);

   /* All nodes start collapsed */
   gtk_tree_view_collapse_all(GTK_TREE_VIEW(treeview));

   /* embed in the main window's signal list pane */
   gtk_box_pack_start (GTK_BOX (ud->siglist_box), box1, FALSE, FALSE, 0);
   gtk_widget_show(box1);
}

void
datafile_recreate_list_win (DataFile *wdata)
{
   datafile_list_win_empty(wdata);
   datafile_list_win_fill(wdata);
}


static void
datafile_similar_var_add (gpointer d, gpointer p)
{
   WaveVar *curvar = (WaveVar *) d;
   WaveVar *var = (WaveVar *) p;
   
   if ( app_strcmp(curvar->varName, var->varName) == 0 ) { 
      ap_panel_add_var( NULL, curvar, NULL, NULL);
   }
}


void
datafile_similar_vars_add (DataFile *wdata, WaveVar *var)
{
   UserData *ud = ( UserData *) wdata->ud;
   
   if ( ud->ag->selected_panel  == NULL) {
      msg_info (aw_panel_not_selected_msg);
      return ;
   }
   wavetable_foreach_wavevar(wdata->wt, datafile_similar_var_add, (gpointer) var);
}

