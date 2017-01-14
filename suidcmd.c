/*
 * build with: gcc -std=c99 -D_GNU_SOURCE=1 -Wall -O2 -ffunction-sections -fdata-sections -fomit-frame-pointer ./suidcmd.c -o ./suidcmd
 * strip -s ./suidcmd
 */

#ifdef _HAVE_CONFIG
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>    /* memset(...), strstr(...) */
#include <sys/wait.h>
#include <libgen.h>    /* basename(...) */


static const char* cmds[] =
  { "/usr/sbin/ether-wake" };
static const size_t cmdsiz = sizeof(cmds)/sizeof(cmds[0]);

static int safe_exec(const char* cmdWithArgs)
{
  pid_t child;
  if ( (child = fork()) == 0 ) {
    size_t szCur = 0, szMax = 10;
    char** args = calloc(szMax, sizeof(char**));
    const char* cmd = NULL;

    const char* prv = cmdWithArgs;
    const char* cur = NULL;
    while ( (cur = strchr(prv, ' ')) ) {
      if (cmd == NULL)
        cmd = strndup(prv, cur-prv);

      args[szCur++] = strndup(prv, cur-prv);
      if (szCur >= szMax) {
        szMax *= 2;
        args = realloc(args, sizeof(char**)*szMax);
      }

      cur++;
      prv = cur;
    }
    if (cmd == NULL) {
      cmd = cmdWithArgs;
    } else {
      args[szCur++] = strndup(prv, cur-prv);
    }
    args[szCur] = NULL;
    execv(cmd, args);
    exit(-5);
  } else if (child != -1) {
    int retval = 0;
    waitpid(child, &retval, 0);
    return retval;
  }
  return -6;
}

static const char* getCmd(char* arg0)
{
  char* barg0 = strdup( basename(arg0) );
  size_t szArg0 = strlen(barg0);
  char* bcmd = NULL;
  const char* retval = NULL;

  if (barg0) {
    for (size_t i = 0; i < cmdsiz; ++i) {
      bcmd = strdup( basename((char*)cmds[i]) );
      if (bcmd) {
        if (strlen(bcmd) == szArg0 && strstr(arg0, bcmd)) {
          retval = cmds[i];
          break;
        }
        free(bcmd);
        bcmd = NULL;
      }
    }
  }

  if (bcmd)
    free(bcmd);
  free(barg0);
  return retval;
}

int main(int argc, char** argv)
{
  uid_t ruid, euid, suid;

  const char* runpath = getCmd(argv[0]);
  if (!runpath) {
    fprintf(stderr, "%s not runnable cmd\n", argv[0]);
    return 1;
  }

  if (getresuid(&ruid, &euid, &suid) != 0) {
    perror("getresuid()");
  } else {
    printf("%s: RUID:%u , EUID:%u , SUID:%u\n", argv[0], ruid, euid, suid);
  }

  if (setuid(0) != 0) {
    perror("setuid(0)");
  } else printf("%s: setuid(0)\n", argv[0]);

  char* cmd = NULL;
  if (asprintf(&cmd, "%s", runpath) <= 0) {
    fprintf(stderr, "%s: asprintf(\"%s\") error\n", argv[0], runpath);
    return 1;
  }

  char* prev_cmd = NULL;
  for (int i = 1; i < argc; ++i) {
    prev_cmd = cmd;
    if (asprintf(&cmd, "%s %s", prev_cmd, argv[i]) < 0) {
      fprintf(stderr, "%s: asprintf(\"%s\") error\n", argv[0], argv[i]);
      return 1;
    }
    free(prev_cmd);
  }

  int retval = -1;
  switch ( (retval = safe_exec(cmd)) ) {
    case -1: fprintf(stderr, "%s: could not create child process..\n", argv[0]); return 1;
    case 127: fprintf(stderr, "%s: could not execute shell (child process)..\n", argv[0]); return 1;
    default:
      printf("%s: child process returned with: %d\n", argv[0], retval);
  }
  free(cmd);
  return 0;
}
