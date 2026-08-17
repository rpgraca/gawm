#ifndef SPICEVAR_H
#define SPICEVAR_H

/*
 * wavevar.h - wavevar protocol interface
 * 
 * include LICENSE
 */

typedef struct _WaveVar WaveVar;

#include <dataset.h>

typedef enum {
        UNKNOWN = 0,
        TIME = 1,
        VOLTAGE = 2,
        CURRENT = 3,
        FREQUENCY = 4,
} VarType;

/* Derived-value modes for complex variables */
#define WV_DERIVE_NORMAL    0  /* use raw column value (real part for complex) */
#define WV_DERIVE_MAGNITUDE 1  /* sqrt(re^2 + im^2) */
#define WV_DERIVE_PHASE_DEG 2  /* atan2(im, re) * 180/pi */

struct _WaveVar {
   AppClass parent;
   int type;                /* type of independant variable */
   char *varName;           /*  name of the variable  (column) */
   WDataSet *wds;           /* data for one or more columns */
   int colno;               /* index (column) in WDataSet   */
   int ncols;               /* num cols used by this variable (complex is 2) */
   int derive_mode;         /* WV_DERIVE_NORMAL, _MAGNITUDE, or _PHASE_DEG */
   AppClass *wvtable;       /* backpointer to the caller Wave var table  */
   int tblno;               /* number of the table containing this var */
   double prev_y2;          /* yval[-2]  temp value              */
   double prev_y1;          /* yval[-1]  temp value              */
   /* Lazy per-derived-var min/max cache (only meaningful when
       derive_mode && ncols == 2).  cache_nrows is the dataset row count for
       which cache_min/cache_max are valid; -1 means invalid.  Appends change
       nrows; explicit-row writes reset cache_nrows to -1. */
   int cache_nrows;         /* dataset nrows at which the cache was computed; -1 = invalid */
   double cache_min;        /* full-column derived min over the sampled rows */
   double cache_max;        /* full-column derived max over the sampled rows */
   int cache_scans;         /* number of cache misses this var's refresh has served; 0 initially */
};

/*
 * prototypes
 */

WaveVar *wavevar_new( WDataSet *wds, char *varName, int type, int colno, int ncols );
void wavevar_construct( WaveVar *wv, WDataSet *wds, char *varName,
			 int type, int colno, int ncols );
void wavevar_destroy(void *wv);

char *wavevar_get_name(WaveVar *wv );
void wavevar_dup_name(WaveVar *wv, char *varName );
double wavevar_interp_value(WaveVar *dv, double ival);
double wavevar_maxof_value(WaveVar *dv, double ival, int npoints);
double wavevar_get_yvalue(WaveVar *dv, double ival, int npoints);
void wavevar_get_range(WaveVar *dv, double x_start, double x_end, double *y_min, double *y_max);

int wavevar_get_type(WaveVar *wv );
void wavevar_set_type(WaveVar *wv, int type );
void wavevar_set_wavetable(WaveVar *wv, AppClass *wt, int tblno );
char *wavevar_get_label(WaveVar *var, int tag);

double wavevar_ivar_get(WaveVar *var, int row );
double wavevar_val_get(WaveVar *var, int row );
int wavevar_nrows_get(WaveVar *var);

double wavevar_val_get_col_min(WaveVar *var, int col );
double wavevar_val_get_col_max(WaveVar *var, int col );
double wavevar_val_get_min(WaveVar *wv );
double wavevar_val_get_max(WaveVar *wv );
double wavevar_ivar_get_max(WaveVar *wv );
double wavevar_ivar_get_min(WaveVar *wv );

char *wavevar_type2str( int type);
int wavevar_str2type( char *s);

WaveVar *wavevar_new_derived(WaveVar *src, const char *suffix, int derive_mode);

/* Read-only seam for the derived min/max cache: returns the dataset row count
   for which a derived var's cache is currently valid, or -1 if invalid (no
   cache computed yet, or the var is not a derived complex var).  Used by the
   frozen test harness to prove the cache exists and invalidates on nrows
   change. */
int wavevar_derived_cache_valid(WaveVar *wv);

/* Read-only seam for the derived min/max cache miss/rescan count: returns the
   number of cache misses that wavevar_derived_cache_refresh has served for
   this var, or 0 when the var is not a derived complex two-column var.  Used by
   the frozen test harness to prove a cache miss scans once and a hit scans
   zero times. */
int wavevar_derived_cache_scans(WaveVar *wv);

#endif /* SPICEVAR_H */
