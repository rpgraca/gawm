/*
 * test_derived_cache_harness.c - display-free acceptance node for the
 * derived-value min/max/range cache (checks G1..G9).
 *
 * The dirty complex patch rescans the whole dataset every time a derived
 * variable's min/max/range is needed (O(n) per draw on the min/max/range
 * paths).  This unit adds a lazy per-derived-var min/max cache keyed on the
 * dataset row count, with explicit invalidation for replacement writes. It is
 * a behaviour-preserving optimization; because it has NO black-box behavioural
 * change, the frozen harness proves the cache exists and invalidates via a
 * dedicated read-only seam `wavevar_derived_cache_valid`, and proves the
 * results stay correct via independent rescan oracles:
 *
 *   G1  cache population: after a derived `wavevar_val_get_min` the seam
 *       equals the dataset nrows (cache was computed for this row count).
 *   G2  invalidation + correctness: build a 2-row complex var, force the
 *       cache, append 2 rows with a smaller magnitude (nrows 2 -> 4); a fresh
 *       `val_get_min` equals an independent harness scan and the seam equals
 *       4 (no stale cache).
 *   G3  full-column `get_range` equals cache min/max (folded endpoints) over
 *       the data bounds, and it populates the cache (seam == nrows).
 *   G4  replacing an existing complex sample invalidates the cache even
 *       though nrows is unchanged; fresh min/max match a rescan.
 *   G5  a new derived var starts invalid with zero cache scans.
 *   G6  the first derived min access scans exactly once.
 *   G7  a second access at unchanged data is a cache hit (no new scan).
 *   G8  phase-derived full-column min/max are cached correctly.
 *   G9  a partial phase range excludes whole-column extrema.
 *
 * Every check is evaluated and reported (no early abort); the process reports
 * a final PASS only when all checks pass and the seam/cache is present.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <math.h>

#include <wavevar.h>
#include <dataset.h>

/* Frozen seam declaration: absent on the pre-fix tree, so this harness links
 * RED until production exposes the cache miss/rescan count. */
extern int wavevar_derived_cache_scans(WaveVar *wv);

static int g_checks = 0;
static int g_failures = 0;

static void
report(const char *check, int ok, const char *detail)
{
   g_checks++;
   if (ok) {
      printf("c[ok ] %s\n", check);
   } else {
      printf("c[FAIL] %s -- %s\n", check, detail);
      g_failures++;
   }
}

static int
approx(double a, double b, double tol_abs, double tol_rel)
{
   double diff = fabs(a - b);
   if (diff <= tol_abs) {
      return 1;
   }
   return diff <= tol_rel * fmax(1.0, fabs(b));
}

/*
 * Independent rescan oracle: min/max over all rows of the derived value,
 * folded with the interpolated values at x0 and x1 - identical to what the
 * non-cached scan + endpoint fold computes and what the cached path must
 * reproduce.
 */
static void
oracle_range(WaveVar *dv, double x0, double x1, double *omin, double *omax)
{
   int nrows = wavevar_nrows_get(dv);
   int i;
   double v;
   double min_v = G_MAXDOUBLE;
   double max_v = -G_MAXDOUBLE;

   for (i = 0; i < nrows; i++) {
      v = wavevar_val_get(dv, i);
      if (v < min_v) min_v = v;
      if (v > max_v) max_v = v;
   }
   v = wavevar_interp_value(dv, x0);
   if (v < min_v) min_v = v;
   if (v > max_v) max_v = v;
   v = wavevar_interp_value(dv, x1);
   if (v < min_v) min_v = v;
   if (v > max_v) max_v = v;
   *omin = min_v;
   *omax = max_v;
}

/*
 * Build a complex dataset: independent "t" (TIME, 1 col, colno 0) and
 * dependent "V(out)" (VOLTAGE, 2 cols, colno 1).  rows is a (t, re, im)
 * triplets array, nrows triplets.
 */
static WDataSet *
build_complex(const double *rows, int nrows, WaveVar **src_out)
{
   WDataSet *wds = dataset_new(0, NULL, 0);
   WaveVar *src;
   int i;

   dataset_var_add(wds, "t", TIME, 1);
   dataset_var_add(wds, "V(out)", VOLTAGE, 2);

   for (i = 0; i < nrows; i++) {
      dataset_val_add(wds, rows[i * 3 + 0]);   /* t   */
      dataset_val_add(wds, rows[i * 3 + 1]);   /* re  */
      dataset_val_add(wds, rows[i * 3 + 2]);   /* im  */
   }

   src = (WaveVar *) dataset_get_wavevar(wds, 1);
   if (src_out) {
      *src_out = src;
   }
   return wds;
}

int
main(int argc, char **argv)
{
   (void) argc;
   (void) argv;

   /* --- G1: cache population ------------------------------------------- */
   {
      /* rows: t, re, im ; mags 5, 10, sqrt(2) */
      static const double rows[] = {
         0.0, 3.0,  4.0,
         1.0, 6.0,  8.0,
         2.0, -1.0, 1.0,
      };
      WDataSet *wds;
      WaveVar *src, *dmag;
      int nrows = (int)(sizeof(rows) / (3 * sizeof(rows[0])));
      int valid;

      wds = build_complex(rows, nrows, &src);
      dmag = wavevar_new_derived(src, ":mag", WV_DERIVE_MAGNITUDE);

      (void) wavevar_val_get_min(dmag);

      valid = wavevar_derived_cache_valid(dmag);
      report("G1 cache populated after val_get_min (seam == nrows)",
             valid == nrows,
             valid == nrows ? "populated" : "cache absent/invalid (RED)");
   }

   /* --- G2: invalidation + correctness ---------------------------------- */
   {
      /* start with 2 rows: mags 5, 10 (min 5) */
      static const double rows0[] = {
         0.0, 3.0, 4.0,
         1.0, 6.0, 8.0,
      };
      /* append 2 rows with a smaller magnitude: mags sqrt(2), sqrt(8) */
      static const double rows1[] = {
         2.0, 1.0, 1.0,   /* mag sqrt(2) ~ 1.414  (new global min) */
         3.0, 2.0, 2.0,   /* mag sqrt(8) ~ 2.828  */
      };
      WDataSet *wds;
      WaveVar *src, *dmag;
      int i;
      double fresh_min;
      double scan_min = G_MAXDOUBLE;
      double v;

      wds = build_complex(rows0, 2, &src);
      dmag = wavevar_new_derived(src, ":mag", WV_DERIVE_MAGNITUDE);

      /* force cache at nrows == 2 */
      (void) wavevar_val_get_min(dmag);
      report("G2 cache present at nrows=2",
             wavevar_derived_cache_valid(dmag) == 2,
             "");

      /* append the two smaller-magnitude rows -> nrows 4 */
      for (i = 0; i < 2; i++) {
         dataset_val_add(wds, rows1[i * 3 + 0]);
         dataset_val_add(wds, rows1[i * 3 + 1]);
         dataset_val_add(wds, rows1[i * 3 + 2]);
      }

      fresh_min = wavevar_val_get_min(dmag);

      for (i = 0; i < wavevar_nrows_get(dmag); i++) {
         v = wavevar_val_get(dmag, i);
         if (v < scan_min) scan_min = v;
      }
      report("G2 fresh val_get_min == independent scan (new smaller min)",
             approx(fresh_min, scan_min, 1e-9, 1e-12) && approx(fresh_min, sqrt(2.0), 1e-9, 1e-12),
             "stale cache would return the old min 5 (RED)");

      report("G2 cache invalidated on nrows change (seam == 4)",
             wavevar_derived_cache_valid(dmag) == 4,
             "cache not invalidated / seam != 4 (RED)");
   }

   /* --- G3: full-column get_range equals cache min/max ------------------ */
   {
      static const double rows[] = {
         0.0, 3.0,  4.0,
         1.0, 6.0,  8.0,
         2.0, -1.0, 1.0,
      };
      WDataSet *wds;
      WaveVar *src, *dmag;
      int nrows = (int)(sizeof(rows) / (3 * sizeof(rows[0])));
      double x0, x1, ymin, ymax, omin, omax;

      wds = build_complex(rows, nrows, &src);
      dmag = wavevar_new_derived(src, ":mag", WV_DERIVE_MAGNITUDE);

      x0 = dataset_val_get(wds, 0, 0);
      x1 = dataset_val_get(wds, nrows - 1, 0);

      wavevar_get_range(dmag, x0, x1, &ymin, &ymax);
      oracle_range(dmag, x0, x1, &omin, &omax);

      report("G3 full-column get_range equals cache min/max (folded endpoints)",
             approx(ymin, omin, 1e-9, 1e-12) && approx(ymax, omax, 1e-9, 1e-12),
             "range mismatch vs independent oracle (RED)");

      report("G3 get_range populated the cache (seam == nrows)",
             wavevar_derived_cache_valid(dmag) == nrows,
             "full-column range did not populate cache (RED)");
   }

   /* --- G4: replacement writes invalidate at unchanged nrows ------------ */
   {
      static const double rows[] = {
         0.0, 3.0, 4.0,   /* mag 5  */
         1.0, 6.0, 8.0,   /* mag 10 */
      };
      WDataSet *wds;
      WaveVar *src, *dmag;
      double fresh_min, fresh_max;

      wds = build_complex(rows, 2, &src);
      dmag = wavevar_new_derived(src, ":mag", WV_DERIVE_MAGNITUDE);
      (void) wavevar_val_get_min(dmag);
      (void) wavevar_val_get_max(dmag);

      dataset_col_val_add(wds, 0, src->colno, 0.0);
      dataset_col_val_add(wds, 0, src->colno + 1, 0.0);
      report("G4 replacement invalidates cache at unchanged nrows",
             wavevar_derived_cache_valid(dmag) == -1,
             "cache seam remained valid after in-place write (RED)");

      fresh_min = wavevar_val_get_min(dmag);
      fresh_max = wavevar_val_get_max(dmag);
      report("G4 fresh min/max reflect replacement",
             approx(fresh_min, 0.0, 1e-9, 1e-12) &&
             approx(fresh_max, 10.0, 1e-9, 1e-12),
             "stale cache returned pre-replacement extrema (RED)");
   }

   /* --- G5..G7: scan counter proves miss then hit ----------------------- */
   {
      static const double rows[] = {
         0.0, 3.0,  4.0,
         1.0, 6.0,  8.0,
         2.0, -1.0, 1.0,
      };
      WDataSet *wds;
      WaveVar *src, *dmag;
      double value;

      wds = build_complex(rows, 3, &src);
      dmag = wavevar_new_derived(src, ":mag", WV_DERIVE_MAGNITUDE);
      report("G5 new cache is invalid with zero scans",
             wavevar_derived_cache_valid(dmag) == -1 &&
             wavevar_derived_cache_scans(dmag) == 0,
             "cache pre-state/seam mismatch (RED)");

      value = wavevar_val_get_min(dmag);
      report("G6 first min access scans exactly once",
             approx(value, sqrt(2.0), 1e-9, 1e-12) &&
             wavevar_derived_cache_valid(dmag) == 3 &&
             wavevar_derived_cache_scans(dmag) == 1,
             "first cache miss did not perform exactly one scan (RED)");

      value = wavevar_val_get_max(dmag);
      report("G7 second access is a cache hit",
             approx(value, 10.0, 1e-9, 1e-12) &&
             wavevar_derived_cache_scans(dmag) == 1,
             "cache hit rescanned the dataset (RED)");
   }

   /* --- G8..G9: phase cache and partial-range fallthrough --------------- */
   {
      static const double rows[] = {
         0.0,  1.0,  0.0,   /* phase 0    */
         1.0,  0.0,  1.0,   /* phase 90   */
         2.0,  0.0, -1.0,   /* phase -90  */
         3.0, -1.0,  0.0,   /* phase 180  */
      };
      WDataSet *wds;
      WaveVar *src, *dphase;
      double ymin, ymax;

      wds = build_complex(rows, 4, &src);
      dphase = wavevar_new_derived(src, ":phase", WV_DERIVE_PHASE_DEG);
      ymin = wavevar_val_get_min(dphase);
      ymax = wavevar_val_get_max(dphase);
      report("G8 phase cache min/max and scan count",
             approx(ymin, -90.0, 1e-9, 1e-12) &&
             approx(ymax, 180.0, 1e-9, 1e-12) &&
             wavevar_derived_cache_valid(dphase) == 4 &&
             wavevar_derived_cache_scans(dphase) == 1,
             "phase cache values/seam mismatch (RED)");

      wavevar_get_range(dphase, 1.0, 2.0, &ymin, &ymax);
      report("G9 partial phase range excludes whole-column max",
             approx(ymin, -90.0, 1e-9, 1e-12) &&
             approx(ymax, 90.0, 1e-9, 1e-12),
             "partial range used whole-column cache extrema (RED)");
   }

   printf("== derived-cache acceptance: %d/%d checks passed ==\n",
          g_checks - g_failures, g_checks);
   if (g_failures == 0) {
      printf("PASS: derived-cache G1-G9 all satisfied\n");
      return 0;
   }
   printf("FAIL: %d check(s) failed\n", g_failures);
   return 1;
}
