/*
 * test_complexvar_harness.c - display-free acceptance node for the dirty
 * complex-derived-WaveVar patch (checks C1..C9).
 *
 * Builds WDataSets entirely in memory (no file I/O, no GTK), registers a
 * scalar independent column and a two-column complex variable "V(out)" holding
 * (real, imag), then derives magnitude / phase WaveVars through the public
 * signature
 *
 *     WaveVar *wavevar_new_derived(WaveVar *src, const char *suffix, int mode)
 *
 * and asserts the FROZEN intended behaviour:
 *
 *   C1  direct linear magnitude:  3+4i -> 5 ; -1+1i -> sqrt(2)
 *   C2  signed phase in degrees:  3+4i -> 53.13010235415598 ; -1+1i -> 135 ;
 *                                  2-2i -> -45
 *   C3  robust huge magnitude:    1e200+1e200i finite ~= hypot(1e200,1e200)
 *                                  (naive sqrt overflows to inf -> RED)
 *   C4  interpolation derives AFTER interpolating real/imag:
 *                                  samples (.5,-1,1) and (.75,2,-2) ->
 *                                  t=.625 re=.5 im=-.5 mag=sqrt(.5) phase=-45
 *   C5  derived min/max reflect the accessor values (exact fixture); finite
 *   C6  derived range over [0,.75] reflects derived values: mag min 0 max 5
 *   C7  a scalar (ncols=1) WaveVar stays raw; derive_mode does not corrupt it
 *   C8  canonical names V(out):mag / V(out):phase and
 *       dataset_get_var_for_name returns those exact pointers (RED today)
 *   C9  repeated wavevar_new_derived for same src+mode returns the same
 *       pointer (get-or-create), and no derived WaveVar is leaked after
 *       dataset_destroy (RED today: pointer identity + LeakSanitizer)
 *
 * Every check is evaluated and reported (no early abort); the process reports
 * a final PASS only when all checks pass.  `--lifecycle-only` runs just the
 * create/destroy lifecycle for LeakSanitizer and returns 0 for a leak-free
 * process (LSan enforces exitcode separately in the runner).
 *
 * In --lifecycle-only under AddressSanitizer, the pre-existing
 * wds->vars GPtrArray backing allocation is explicitly ignored via
 * __lsan_ignore_object() so that LeakSanitizer reports ONLY feature-derived
 * leaks (the WaveVars/names that wavevar_new_derived allocates but that
 * dataset_destroy does not own).  That base container leak (dataset_destroy
 * calls g_ptr_array_free(vars, FALSE), which never frees ->pdata) predates
 * and is unrelated to the derived-WaveVar feature and must not force an
 * implementation change; hence it is carved out here.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <wavevar.h>
#include <dataset.h>

#if defined(__SANITIZE_ADDRESS__)
/* Provided by the AddressSanitizer runtime in the ASan/UBSan lifecycle build;
 * the plain (non-ASan) build never references it.  Used only to isolate the
 * pre-existing wds->vars container allocation from feature leak reporting. */
extern void __lsan_ignore_object(const void *p);
#endif

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

/* Feed one row of three columns (col0 independant, col1 real, col2 imag). */
static void
feed3(WDataSet *wds, double v0, double v1, double v2)
{
   dataset_val_add(wds, v0);
   dataset_val_add(wds, v1);
   dataset_val_add(wds, v2);
}

/*
 * Build a complex dataset:  var0 "t" (TIME, 1 col, colno 0); var1 "V(out)"
 * (VOLTAGE, 2 cols, colno 1=real, colno 2=imag).  If with_huge, appends the
 * 1e200+1e200i row used by C3.
 */
static WDataSet *
build_complex(int with_huge, WaveVar **src_out)
{
   WDataSet *wds = dataset_new(0, NULL, 0);
   WaveVar *src;

   dataset_var_add(wds, "t", TIME, 1);
   dataset_var_add(wds, "V(out)", VOLTAGE, 2);

   feed3(wds, 0.00,  3.0,    4.0);     /* mag 5,     phase 53.130102.. */
   feed3(wds, 0.25,  0.0,    0.0);     /* mag 0                          */
   feed3(wds, 0.50, -1.0,    1.0);     /* mag sqrt2, phase 135           */
   feed3(wds, 0.75,  2.0,   -2.0);     /* mag sqrt8, phase -45           */
   feed3(wds, 1.00,  0.0,    0.0);     /* mag 0                          */
   if (with_huge) {
      feed3(wds, 2.00, 1e200, 1e200);  /* mag hypot -> inf under naive sqrt */
   }

   src = (WaveVar *) dataset_get_wavevar(wds, 1);
   if (src_out) {
      *src_out = src;
   }
   return wds;
}

/* Bounded complex dataset (no huge row) for exact C5 min/max. */
static WDataSet *
build_small(WaveVar **src_out)
{
   WDataSet *wds = dataset_new(0, NULL, 0);

   dataset_var_add(wds, "t", TIME, 1);
   dataset_var_add(wds, "V(out)", VOLTAGE, 2);

   feed3(wds, 0.00,  3.0,  4.0);   /* mag 5   */
   feed3(wds, 0.50, -1.0,  1.0);   /* mag sqrt2 */
   feed3(wds, 1.00,  0.0,  0.0);   /* mag 0   */

   if (src_out) {
      *src_out = (WaveVar *) dataset_get_wavevar(wds, 1);
   }
   return wds;
}

/* Scalar (ncols=1) dataset for C7. */
static WDataSet *
build_scalar(WaveVar **x_out)
{
   WDataSet *wds = dataset_new(0, NULL, 0);

   dataset_var_add(wds, "x", VOLTAGE, 1);
   dataset_val_add(wds, 1.5);
   dataset_val_add(wds, -2.5);
   dataset_val_add(wds, 3.5);

   if (x_out) {
      *x_out = (WaveVar *) dataset_get_wavevar(wds, 0);
   }
   return wds;
}

/* C9 get-or-create semantics: repeated requests must return the same pointer. */
static void
check_c9_get_or_create(WaveVar *src)
{
   WaveVar *m1 = wavevar_new_derived(src, ":mag", WV_DERIVE_MAGNITUDE);
   WaveVar *m2 = wavevar_new_derived(src, ":mag", WV_DERIVE_MAGNITUDE);
   WaveVar *p1 = wavevar_new_derived(src, ":phase", WV_DERIVE_PHASE_DEG);
   WaveVar *p2 = wavevar_new_derived(src, ":phase", WV_DERIVE_PHASE_DEG);

   report("C9 pointer identity (get-or-create mag)",
          (m1 == m2), m1 == m2 ? "same pointer" : "distinct pointers (RED)");
   report("C9 pointer identity (get-or-create phase)",
          (p1 == p2), p1 == p2 ? "same pointer" : "distinct pointers (RED)");
}

int
main(int argc, char **argv)
{
   int lifecycle_only = (argc > 1 && strcmp(argv[1], "--lifecycle-only") == 0);

   if (lifecycle_only) {
      int i;
      WDataSet *wds = build_complex(1, NULL);
      WaveVar *src = (WaveVar *) dataset_get_wavevar(wds, 1);

      for (i = 0; i < 5; i++) {
         (void) wavevar_new_derived(src, ":mag", WV_DERIVE_MAGNITUDE);
         (void) wavevar_new_derived(src, ":phase", WV_DERIVE_PHASE_DEG);
      }

#if defined(__SANITIZE_ADDRESS__)
      /* Isolate the pre-existing wds->vars container allocation so the LSan
         pass reports ONLY the feature-derived WaveVar/name leaks (C9).  This
         is the exact allocation that dataset_destroy fails to free
         (g_ptr_array_free(vars, FALSE)); it is out of scope for this feature.
         Derived WaveVar/name allocations are deliberately NOT ignored. */
      __lsan_ignore_object(((GPtrArray *) wds->vars)->pdata);
#endif

      dataset_destroy(wds);
      return 0;
   }

   {
      WaveVar *src, *dmag, *dphase, *x;
      WDataSet *ds = build_complex(1, &src);
      double v, ymin, ymax;

      dmag   = wavevar_new_derived(src, ":mag",   WV_DERIVE_MAGNITUDE);
      dphase = wavevar_new_derived(src, ":phase", WV_DERIVE_PHASE_DEG);

      /* ---- C1 direct linear magnitude ---- */
      v = wavevar_val_get(dmag, 0);
      report("C1 3+4i magnitude == 5",
             approx(v, 5.0, 1e-9, 1e-12), "");
      v = wavevar_val_get(dmag, 2);
      report("C1 -1+1i magnitude == sqrt(2)",
             approx(v, sqrt(2.0), 1e-9, 1e-12), "");

      /* ---- C2 signed phase degrees ---- */
      v = wavevar_val_get(dphase, 0);
      report("C2 3+4i phase == 53.13010235415598",
             approx(v, 53.13010235415598, 1e-9, 1e-12), "");
      v = wavevar_val_get(dphase, 2);
      report("C2 -1+1i phase == 135",
             approx(v, 135.0, 1e-9, 1e-12), "");
      v = wavevar_val_get(dphase, 3);
      report("C2 2-2i phase == -45",
             approx(v, -45.0, 1e-9, 1e-12), "");

      /* ---- C3 robust huge magnitude ---- */
      v = wavevar_val_get(dmag, 5);
      report("C3 huge magnitude finite",
             (isfinite(v)), v == v ? "" : "NaN");
      report("C3 huge magnitude ~= hypot(1e200,1e200)",
             approx(v, hypot(1e200, 1e200), 0.0, 1e-12), "");

      /* ---- C4 interpolate THEN derive ---- */
      v = wavevar_interp_value(dmag, 0.625);
      report("C4 interp magnitude @.625 == sqrt(0.5)",
             approx(v, sqrt(0.5), 1e-9, 1e-9), "");
      v = wavevar_interp_value(dphase, 0.625);
      report("C4 interp phase @.625 == -45",
             approx(v, -45.0, 1e-9, 1e-9), "");

      /* ---- C6 derived range over [0,.75] (excludes huge row) ---- */
      wavevar_get_range(dmag, 0.0, 0.75, &ymin, &ymax);
      report("C6 range[0,.75] mag min == 0",
             approx(ymin, 0.0, 1e-9, 1e-12), "");
      report("C6 range[0,.75] mag max == 5",
             approx(ymax, 5.0, 1e-9, 1e-12), "");

      /* ---- C8 canonical names + lookup ---- */
      {
         const char *nm = wavevar_get_name(dmag);
         const char *np = wavevar_get_name(dphase);
         report("C8 mag name == V(out):mag",
                nm && strcmp(nm, "V(out):mag") == 0,
                nm ? nm : "(null)");
         report("C8 phase name == V(out):phase",
                np && strcmp(np, "V(out):phase") == 0,
                np ? np : "(null)");
         {
            WaveVar *look = (WaveVar *) dataset_get_var_for_name(ds, "V(out):mag");
            report("C8 dataset_get_var_for_name returns dmag",
                   (look == dmag), look ? "wrong pointer" : "(null) (RED)");
         }
         {
            WaveVar *look = (WaveVar *) dataset_get_var_for_name(ds, "V(out):phase");
            report("C8 dataset_get_var_for_name returns dphase",
                   (look == dphase), look ? "wrong pointer" : "(null) (RED)");
         }
      }

      /* ---- C9 get-or-create pointer identity ---- */
      check_c9_get_or_create(src);

      /* ---- C5 derived min/max on bounded fixture ---- */
      {
         WDataSet *sds = build_small(&src);
         WaveVar *smag = wavevar_new_derived(src, ":mag", WV_DERIVE_MAGNITUDE);
         double smin = wavevar_val_get_min(smag);
         double smax = wavevar_val_get_max(smag);
         report("C5 min == 0 (finite)",
                isfinite(smin) && approx(smin, 0.0, 1e-9, 1e-12), "");
         report("C5 max == 5 (finite)",
                isfinite(smax) && approx(smax, 5.0, 1e-12, 1e-12), "");
      }

      /* ---- C7 scalar unaffected by derive_mode ---- */
      {
         WDataSet *xds = build_scalar(&x);
         x->derive_mode = WV_DERIVE_MAGNITUDE;
         (void) xds;
         v = wavevar_val_get(x, 1);
         report("C7 scalar val_get raw == -2.5",
                approx(v, -2.5, 1e-9, 1e-12), "");
         report("C7 scalar min == -2.5",
                approx(wavevar_val_get_min(x), -2.5, 1e-9, 1e-12), "");
         report("C7 scalar max == 3.5",
                approx(wavevar_val_get_max(x), 3.5, 1e-9, 1e-12), "");
      }

      printf("== complexvar acceptance: %d/%d checks passed ==\n",
             g_checks - g_failures, g_checks);
      if (g_failures == 0) {
         printf("PASS: complexvar C1-C9 all satisfied\n");
         return 0;
      }
      printf("FAIL: %d check(s) failed\n", g_failures);
      return 1;
   }
}
