/*
 * test_dataset_leak_harness.c - display-free acceptance node for the
 * 16-byte container leak in dataset_destroy().
 *
 * The bug: dataset_destroy() runs dataset_remove_all_vars() (which already
 * destroys every element) and then calls g_ptr_array_free(this->vars, FALSE).
 * With FALSE the GPtrArray element data segment (->pdata) is returned to the
 * caller instead of being freed, and no one takes it -- a definite leak
 * (16 bytes for a small/empty dataset) on every dataset destroy.
 *
 * This harness builds a small WDataSet entirely in memory (no GTK, no file
 * I/O), registers three columns via a variable, adds a few rows, destroys it
 * via dataset_destroy() and exits 0.  Under LeakSanitizer (see
 * run-dataset-leak-check.sh) a leaking dataset_destroy makes the process exit
 * nonzero; a fixed dataset_destroy must be leak-free and exit 0.
 *
 * Deliberately NO __lsan_ignore_object anywhere in this harness: the leak we
 * are hunting is precisely the wds->vars backing segment, and it must be
 * reported, not suppressed.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include <wavevar.h>
#include <dataset.h>

int
main(void)
{
   WDataSet *wds = dataset_new(0, NULL, 0);
   int i;

   dataset_var_add(wds, "t", TIME, 1);
   dataset_var_add(wds, "v1", VOLTAGE, 1);
   dataset_var_add(wds, "v2", VOLTAGE, 1);

   for (i = 0; i < 4; i++) {
      dataset_val_add(wds, (double) i);
      dataset_val_add(wds, (double) i * 2.0);
      dataset_val_add(wds, (double) i * 3.0);
   }

   dataset_destroy(wds);
   printf("dataset leak harness: create/destroy OK\n");
   return 0;
}
