/*
 * test_copyvar_harness.c - executable regression for malformed `copyvar`.
 *
 * Drives the real server command parser (aio_process_line -> aio_copyvar)
 * with a truncated `copyvar` line that lacks the required panel argument.
 *
 * The unmodified parser dereferences a NULL token (atoi(panel + 1)) and
 * segfaults; the correct behaviour is to report an error (-1) with a message
 * and stay alive.  Compiling this against the unmodified sources must fail
 * (the process dies by SIGSEGV); against the fixed sources it must exit 0.
 *
 * Built by the runner script `run-copyvar-check.sh` against a disposable
 * out-of-tree build, mirroring how aio_process_line is exercised in gawio.c.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#include <gaw.h>

/* aio_process_line is a non-static global defined in src/gawio.c */
extern int aio_process_line(GawIoData *gawio, gchar *linebuf, gsize length);

int
main(void)
{
   UserData *ud = g_malloc0(sizeof(UserData));
   GawIoData gawio;
   int ret;
   int pass = 1;

   memset(&gawio, 0, sizeof(gawio));
   gawio.ud = ud;
   gawio.state = 0; /* GAWIO_CMD */
   /* A non-NULL sentinel so the check_wds fallback path is not taken here;
      the malformed-copyvar error path never dereferences wds. */
   gawio.wds = (WDataSet *) (uintptr_t) 1;

   ret = aio_process_line(&gawio, "copyvar", 7);

   if (ret == 0) {
      fprintf(stderr, "FAIL: malformed copyvar unexpectedly succeeded (ret=0)\n");
      pass = 0;
   }
   if (gawio.msg == NULL) {
      fprintf(stderr, "FAIL: malformed copyvar produced no error message\n");
      pass = 0;
   }

   if (pass) {
      fprintf(stderr, "PASS: malformed copyvar returned error without crashing\n");
      return 0;
   }
   return 1;
}
