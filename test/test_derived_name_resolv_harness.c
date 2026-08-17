/*
 * test_derived_name_resolv_harness.c - executable regression for gawio
 * derived-variable NAME resolution (Unit B).
 *
 * Builds a WDataSet in memory with a two-column complex variable "V(out)"
 * (real, imag), derives magnitude/phase WaveVars through the public
 *
 *     WaveVar *wavevar_new_derived(WaveVar *src, const char *suffix, int mode)
 *
 * and then drives the REAL server command parser, aio_process_line, with
 * `coldatas` lines whose argument is the derived name.  aio_coldatas_add
 * resolves its argument through dataset_get_var_for_name; the derived vars
 * live in wds->dvars, which the resolver scans after wds->vars.
 *
 * Frozen intended behaviour (regression guard, not a new feature):
 *
 *   B1  `coldatas V(out):mag` resolves to the exact derived mag WaveVar and
 *       returns 0, gawio.curcol == mag->colno, state == GAWIO_COLDATA.
 *   B2  `coldatas V(out):phase` resolves to the exact derived phase WaveVar.
 *   B3  `coldatas V(out)` still resolves to the TOP-LEVEL source WaveVar
 *       (the derived vars must not shadow the original name).
 *   B4  `coldatas NOSUCH:mag` returns an error (-1) with a message (the
 *       resolver must scan the right containers; a bad name must fail, not
 *       silently pick a wrong var).
 *   B5  `coldatas V(out):mag` repeated is stable: it keeps resolving to the
 *       same (deduped) derived WaveVar.
 *
 * Compiling this against unmodified sources must pass (derived-name
 * resolution was completed by the magnitude/phase commit); it is a frozen
 * acceptance node guarding against regressions in the gawio -> dataset
 * -> dvars name-resolution chain.  No production code changes are required.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#include <gaw.h>
#include <wavevar.h>
#include <dataset.h>

/* aio_process_line is a non-static global defined in src/gawio.c */
extern int aio_process_line(GawIoData *gawio, gchar *linebuf, gsize length);

/* The _gawIoStateInfo enum lives in gawio.c, not in any header; mirror the
 * two states this harness relies on. */
#define GAWIO_CMD     0
#define GAWIO_COLDATA 2

static int g_checks = 0;
static int g_failures = 0;

static void
report(const char *check, int ok, const char *detail)
{
   g_checks++;
   if (ok) {
      printf("b[ok ] %s\n", check);
   } else {
      printf("b[FAIL] %s -- %s\n", check, detail);
      g_failures++;
   }
}

/* Build a WDataSet with col0 "t" (independant) and a two-column complex
 * variable "V(out)" holding (real, imag). */
static WDataSet *
make_dataset(WaveVar **mag_out, WaveVar **phase_out)
{
   WDataSet *wds = dataset_new(0, NULL, 0);
   WaveVar *src;

   dataset_var_add(wds, "t", TIME, 1);
   dataset_var_add(wds, "V(out)", VOLTAGE, 2);

   /* Find the two-column source. */
   src = (WaveVar *) dataset_get_var_for_name(wds, "V(out)");
   if (src == NULL) {
      fprintf(stderr, "setup failure: could not create V(out)\n");
      return NULL;
   }

   *mag_out   = wavevar_new_derived(src, ":mag",   WV_DERIVE_MAGNITUDE);
   *phase_out = wavevar_new_derived(src, ":phase", WV_DERIVE_PHASE_DEG);
   return wds;
}

/* Drive `coldatas <name>` through the real parser; return ret and record
 * the resulting curcol/state. */
static int
drive_coldatas(WDataSet *wds, const char *name,
               int *out_curcol, int *out_state, char **out_msg)
{
   UserData *ud = g_malloc0(sizeof(UserData));
   GawIoData gawio;
   char *line;
   int ret;

   memset(&gawio, 0, sizeof(gawio));
   gawio.ud = ud;
   gawio.wds = wds;
   gawio.state = GAWIO_CMD;

   line = g_strdup_printf("coldatas %s 0", name);
   ret = aio_process_line(&gawio, (gchar *) line, strlen(line));

   *out_curcol = gawio.curcol;
   *out_state  = gawio.state;
   *out_msg    = gawio.msg;   /* ownership stays with gawio */
   g_free(line);
   g_free(ud);
   return ret;
}

int
main(void)
{
   WDataSet *wds;
   WaveVar *mag, *phase;

   wds = make_dataset(&mag, &phase);
   if (wds == NULL || mag == NULL || phase == NULL) {
      fprintf(stderr, "FAIL: setup\n");
      return 1;
   }

   {
      int curcol, state;
      char *msg = NULL;

      /* B1: V(out):mag resolves to the exact derived mag WaveVar. */
      if (drive_coldatas(wds, "V(out):mag", &curcol, &state, &msg) != 0) {
         report("B1 coldatas V(out):mag returns 0", 0,
                msg ? msg : "no message");
      } else {
         int ok = (curcol == mag->colno) && (state == GAWIO_COLDATA);
         report("B1 coldatas V(out):mag -> mag->colno/GAWIO_COLDATA", ok,
                "curcol/state mismatch");
      }

      /* B2: V(out):phase resolves to the exact derived phase WaveVar. */
      if (drive_coldatas(wds, "V(out):phase", &curcol, &state, &msg) != 0) {
         report("B2 coldatas V(out):phase returns 0", 0,
                msg ? msg : "no message");
      } else {
         int ok = (curcol == phase->colno) && (state == GAWIO_COLDATA);
         report("B2 coldatas V(out):phase -> phase->colno/GAWIO_COLDATA", ok,
                "curcol/state mismatch");
      }

      /* B3: V(out) still resolves to the TOP-LEVEL source (curcol=1). */
      if (drive_coldatas(wds, "V(out)", &curcol, &state, &msg) != 0) {
         report("B3 coldatas V(out) returns 0", 0,
                msg ? msg : "no message");
      } else {
         int ok = (curcol == 1) && (state == GAWIO_COLDATA);
         report("B3 coldatas V(out) -> source colno 1", ok,
                "curcol/state mismatch (derived var shadowed source?)");
      }

      /* B4: a bad derived name fails with an error message. */
      if (drive_coldatas(wds, "NOSUCH:mag", &curcol, &state, &msg) == 0) {
         report("B4 coldatas NOSUCH:mag errors", 0,
                "bad name unexpectedly succeeded");
      } else if (msg == NULL || msg[0] == '\0') {
         report("B4 coldatas NOSUCH:mag has message", 0, "no error message");
      } else {
         report("B4 coldatas NOSUCH:mag errors with message", 1, msg);
      }

      /* B5: repeated V(out):mag is stable (same deduped derived var). */
      if (drive_coldatas(wds, "V(out):mag", &curcol, &state, &msg) != 0) {
         report("B5 coldatas V(out):mag repeat returns 0", 0,
                msg ? msg : "no message");
      } else {
         int ok = curcol == mag->colno;
         report("B5 coldatas V(out):mag repeat stable", ok,
                "curcol changed between resolves");
      }
   }

   dataset_destroy(wds);

   if (g_failures == 0) {
      fprintf(stderr, "PASS: gawio derived-name resolution B1..B5\n");
      return 0;
   }
   fprintf(stderr, "FAIL: %d of %d derived-name checks\n", g_failures, g_checks);
   return 1;
}
