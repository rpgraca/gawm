/*
 * test_ncols_guard_harness.c - display-free acceptance node for the
 * ncols == 2 guard hardening (checks N1..N3).
 *
 * The complex-derived-WaveVar feature is valid only for complex variables,
 * which are exactly 2 columns (real at colno, imag at colno+1).  No loader
 * produces ncols > 2, so the guards use `>= 2` today, which would mis-derive
 * a hypothetical 3+-column variable (reading colno+1 as the imaginary part).
 * This node locks in the tightened contract:
 *
 *   N1  wavevar_new_derived(src3, ":mag", WV_DERIVE_MAGNITUDE) returns EXACTLY
 *       src3 (pointer identity) for an ncols=3 source - no derived object.
 *   N2  same for WV_DERIVE_PHASE_DEG on that ncols=3 source.
 *   N3  an ncols=2 source still derives normally (pointer != src, canonical
 *       name <var>:mag, val_get row0 == hypot of the two columns) - a
 *       regression lock that must pass before AND after the fix.
 *
 * Every check is evaluated and reported (no early abort); the process reports
 * a final PASS only when all checks pass.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <math.h>

#include <wavevar.h>
#include <dataset.h>

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
 * Build a synthetic dataset with an independent column "t" (TIME, 1 col,
 * colno 0) and a dependent variable "V3" (VOLTAGE, 3 cols, colno 1).
 * Row0 holds re=3.0, col+1=4.0, col+2=99.0 (a distinct third column value),
 * so a wrong ncols>=2 derivation would read 4.0 as the imaginary part.
 */
static WDataSet *
build_ncols3(WaveVar **src_out)
{
   WDataSet *wds = dataset_new(0, NULL, 0);
   WaveVar *src;

   dataset_var_add(wds, "t", TIME, 1);
   dataset_var_add(wds, "V3", VOLTAGE, 3);

   dataset_val_add(wds, 0.00);
   dataset_val_add(wds, 3.0);
   dataset_val_add(wds, 4.0);
   dataset_val_add(wds, 99.0);

   src = (WaveVar *) dataset_get_wavevar(wds, 1);
   if (src_out) {
      *src_out = src;
   }
   return wds;
}

/* Bounded ncols=2 complex dataset for the N3 regression lock. */
static WDataSet *
build_ncols2(WaveVar **src_out)
{
   WDataSet *wds = dataset_new(0, NULL, 0);
   WaveVar *src;

   dataset_var_add(wds, "t", TIME, 1);
   dataset_var_add(wds, "V(out)", VOLTAGE, 2);

   dataset_val_add(wds, 0.00);
   dataset_val_add(wds, 3.0);
   dataset_val_add(wds, 4.0);

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

   {
      WaveVar *src3, *ret;
      WDataSet *ds3 = build_ncols3(&src3);

      ret = wavevar_new_derived(src3, ":mag", WV_DERIVE_MAGNITUDE);
      report("N1 mag ncols=3 returns EXACTLY src3",
             (ret == src3),
             (ret == src3) ? "same pointer" : "fresh derived pointer (RED)");

      ret = wavevar_new_derived(src3, ":phase", WV_DERIVE_PHASE_DEG);
      report("N2 phase ncols=3 returns EXACTLY src3",
             (ret == src3),
             (ret == src3) ? "same pointer" : "fresh derived pointer (RED)");
   }

   {
      WaveVar *src, *dmag;
      WDataSet *ds = build_ncols2(&src);
      double v;

      dmag = wavevar_new_derived(src, ":mag", WV_DERIVE_MAGNITUDE);

      report("N3 ncols=2 derives new pointer",
             (dmag != src),
             (dmag != src) ? "distinct pointer" : "same pointer (RED)");
      {
         const char *nm = wavevar_get_name(dmag);
         report("N3 name == V(out):mag",
                nm && strcmp(nm, "V(out):mag") == 0,
                nm ? nm : "(null)");
      }
      v = wavevar_val_get(dmag, 0);
      report("N3 ncols=2 val_get row0 == hypot(3,4)",
             approx(v, hypot(3.0, 4.0), 1e-9, 1e-12),
             "");
   }

   printf("== ncols-guard acceptance: %d/%d checks passed ==\n",
          g_checks - g_failures, g_checks);
   if (g_failures == 0) {
      printf("PASS: ncols-guard N1-N3 all satisfied\n");
      return 0;
   }
   printf("FAIL: %d check(s) failed\n", g_failures);
   return 1;
}
