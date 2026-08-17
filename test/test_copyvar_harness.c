/*
 * test_copyvar_harness.c - executable regression for malformed commands.
 *
 * Drives the real server command parser (aio_process_line) with truncated
 * `copyvar` and `delvar` lines that lack the required panel argument.  Both
 * handlers previously dereferenced a NULL token (atoi(panel + 1)) and
 * segfaulted; the correct behaviour is to report an error (-1) with a
 * message and stay alive.
 *
 * Compiling this against the unmodified sources must fail (the process dies
 * by SIGSEGV on the first malformed command); against the fixed sources it
 * must exit 0 after every command returns an error.
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

/* Run one malformed command through the real parser; require an error return
 * with a message and no crash.  Returns 0 on pass, non-zero on failure. */
static int
check_malformed(const char *cmd)
{
   UserData *ud = g_malloc0(sizeof(UserData));
   GawIoData gawio;
   int ret;
   int pass = 1;

   memset(&gawio, 0, sizeof(gawio));
   gawio.ud = ud;
   gawio.state = 0; /* GAWIO_CMD */
   /* A non-NULL sentinel so the check_wds fallback path is not taken here;
      the malformed-command error path never dereferences wds. */
   gawio.wds = (WDataSet *) (uintptr_t) 1;

   ret = aio_process_line(&gawio, (gchar *) cmd, strlen(cmd));

   if (ret == 0) {
      fprintf(stderr, "FAIL: malformed %s unexpectedly succeeded (ret=0)\n", cmd);
      pass = 0;
   }
   if (gawio.msg == NULL) {
      fprintf(stderr, "FAIL: malformed %s produced no error message\n", cmd);
      pass = 0;
   }
   g_free(ud);
   return pass ? 0 : 1;
}

int
main(void)
{
   static const char *const cmds[] = { "copyvar", "delvar", "dataset", NULL };
   int i;
   int pass = 1;

   for (i = 0; cmds[i] != NULL; i++) {
      if (check_malformed(cmds[i]) != 0) {
         pass = 0;
      }
   }

   if (pass) {
      fprintf(stderr, "PASS: malformed copyvar/delvar/dataset returned error without crashing\n");
      return 0;
   }
   return 1;
}
