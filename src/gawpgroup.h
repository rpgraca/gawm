#ifndef GAWPGROUP_H
#define GAWPGROUP_H

/*
 * gawpgroup.h - Panel Group interface
 *
 * A PanelGroup contains panels that share an X axis, cursors, and scrollbar.
 * Multiple PanelGroups can exist with independent X axes.
 */

typedef struct _PanelGroup PanelGroup;

struct _PanelGroup {
   UserData *ud;               /* back pointer to application data */
   gchar *name;                /* group name (for display) */

   GawLabels *xLabels;         /* X axis for this group */
   AWCursor **cursors;         /* 3 cursors (cursor0, cursor1, delta) */
   int last_dragged_cursor;    /* index (0 or 1) of last dragged cursor */

   GList *panelList;           /* list of WavePanel* in this group */
   WavePanel *selected_panel;  /* currently selected panel */
   WDataSet *curwds;           /* last dataset used for x processing */

   /* Widgets owned by this group */
   GtkWidget *groupBox;        /* top-level VBox for this group */
   GtkWidget *panelTable;      /* GtkGrid for panels */
   GtkWidget *panel_scrolled;  /* scrolled window around panelTable */
   GtkWidget *meas_hbox;       /* cursor measure buttons */
   int meas_hbox_shown;        /* 1 if measure buttons visible */
   GtkWidget *xlabel_ev_box;   /* event box for xlabel area */
   GtkWidget *xlabel_box;      /* box for xlabels */
   GtkWidget *xlabel_table;    /* layout for xlabels */
   GList *xlabel_list;         /* xlabel label list */
   GtkWidget *allline_box;     /* hbox for logXbox, xlabels */
   GtkWidget *logx_box;        /* box for logX indicator */
   GtkWidget *win_xlabel_log;  /* logX label */
   GtkWidget *xscrollbar;      /* horizontal scrollbar */
   GtkAdjustment *xadj;        /* scrollbar adjustment */

   GList *all_measure_buttons; /* measure buttons list */

   int sbSize;                 /* scrollbar size */
   int panelWidth;             /* current panel width */
   int panelHeight;            /* current panel height */
   int panelScrolledHeight;    /* height of panel scrolled widget */
   int maxHeight;              /* maximum height for panel_scrolled */
};

/*
 * prototypes
 */
PanelGroup *pg_group_new(UserData *ud, const gchar *name);
void pg_group_destroy(PanelGroup *pg);
void pg_group_build_widgets(PanelGroup *pg);

#endif /* GAWPGROUP_H */
