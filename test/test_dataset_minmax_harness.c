/*
 * Regression for raw dataset extrema after explicit-row replacement.
 *
 * M1 replacing the unique maximum shrinks colMax.
 * M2 replacing the unique minimum raises colMin.
 * M3 replacing a non-extremum preserves both bounds.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <stdio.h>

#include <dataset.h>
#include <wavevar.h>

static int checks;
static int failures;

static int
approx(double a, double b)
{
   return fabs(a - b) <= 1e-12;
}

static void
report(const char *name, int ok)
{
   checks++;
   printf("m[%s] %s\n", ok ? "ok " : "FAIL", name);
   if (!ok) failures++;
}

static WDataSet *
build_fixture(void)
{
   static const double values[] = { 5.0, 1.0, 9.0, 3.0, 7.0 };
   WDataSet *wds = dataset_new(0, NULL, 0);
   int i;

   dataset_var_add(wds, "v", VOLTAGE, 1);
   for (i = 0; i < 5; i++) {
      dataset_val_add(wds, values[i]);
   }
   return wds;
}

int
main(void)
{
   WDataSet *wds;

   wds = build_fixture();
   dataset_col_val_add(wds, 2, 0, 2.0);
   report("M1 replacing unique max shrinks bounds to [1,7]",
          approx(dataset_val_get_min(wds, 0), 1.0) &&
          approx(dataset_val_get_max(wds, 0), 7.0));
   dataset_destroy(wds);

   wds = build_fixture();
   dataset_col_val_add(wds, 1, 0, 6.0);
   report("M2 replacing unique min raises bounds to [3,9]",
          approx(dataset_val_get_min(wds, 0), 3.0) &&
          approx(dataset_val_get_max(wds, 0), 9.0));
   dataset_destroy(wds);

   wds = build_fixture();
   dataset_col_val_add(wds, 3, 0, 4.0);
   report("M3 replacing non-extremum preserves bounds [1,9]",
          approx(dataset_val_get_min(wds, 0), 1.0) &&
          approx(dataset_val_get_max(wds, 0), 9.0));
   dataset_destroy(wds);

   printf("== dataset-minmax acceptance: %d/%d checks passed ==\n",
          checks - failures, checks);
   if (failures == 0) {
      printf("PASS: dataset-minmax M1-M3 all satisfied\n");
      return 0;
   }
   printf("FAIL: %d check(s) failed\n", failures);
   return 1;
}
