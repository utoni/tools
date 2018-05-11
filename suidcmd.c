/*
 * build with: gcc -std=c99 -D_HAVE_CONFIG=1 -D_GNU_SOURCE=1 -Wall -O2 -ffunction-sections -fdata-sections -fomit-frame-pointer ./suidcmd.c -o ./suidcmd
 * strip -s ./suidcmd
 */

#ifdef _HAVE_CONFIG
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>    /* memset(...), strstr(...) */
#include <sys/wait.h>
#include <libgen.h>    /* basename(...) */


static const char* const cmds[] =
  { SUIDCMD_CMDS };
static const size_t cmdsiz = sizeof(cmds)/sizeof(cmds[0]);

static int safe_exec(const char* cmdWithArgs)
{
  if (!cmdWithArgs)
    return -2;
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
    exit(-3);
  } else if (child != -1) {
    int retval = 0;
    waitpid(child, &retval, 0);
    return retval;
  }
  return -4;
}

static const char* getCmd(char* arg0)
{
  char* tmp = basename(arg0);
  if (!tmp)
    return NULL;

  char* barg0 = strdup( tmp );
  char* bcmd = NULL;
  const char* retval = NULL;

  if (barg0) {
    for (size_t i = 0; i < cmdsiz; ++i) {
      tmp = strdup(cmds[i]); /* workaround for uclibc */
      if (tmp) {
        bcmd = strdup(basename(tmp));
        free(tmp);
        tmp = NULL;
      }
      if (bcmd) {
        char* found = strstr(arg0, bcmd);
        if (found && strlen(found) == strlen(bcmd)) {
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

static void printCmds(void)
{
  printf("%s", "Available Commands: ");
  for (size_t i = 0; i < cmdsiz; ++i) {
    char* tmp = strdup(cmds[i]);
    if (!tmp)
      continue;
    printf("%s%s", basename(tmp), (i < cmdsiz-1 ? ", " : ""));
    free(tmp);
  }
  printf("\n");
}

int main(int argc, char** argv)
{
  uid_t ruid, euid, suid;

  if (argc < 1) {
    fprintf(stderr, "argcount = %d < 1\n", argc);
    return 1;
  }

  struct stat buf;
  if (lstat(argv[0], &buf) != 0) {
    perror("lstat");
    return 1;
  }
  if (!S_ISLNK(buf.st_mode)) {
    printCmds();
    return 0;
  }

  static char *real_arg0 = NULL;
  real_arg0 = realpath(argv[0], NULL);
  if (!real_arg0) {
    perror("realpath");
    return 1;
  }

  if (stat(argv[0], &buf) != 0) {
    perror("stat");
    return 1;
  }
  if ((buf.st_mode & S_ISUID) == 0) {
    fprintf(stderr, "%s: not suid\n", real_arg0);
    return 1;
  }

  const char* runpath = getCmd(argv[0]);
  if (!runpath) {
    fprintf(stderr, "%s: %s not runnable cmd\n", real_arg0, argv[0]);
    printCmds();
    return 1;
  }

  if (stat(runpath, &buf) != 0) {
    fprintf(stderr, "%s: %s error: %s\n", real_arg0, runpath, strerror(errno));
    return 1;
  }

  if (getresuid(&ruid, &euid, &suid) != 0) {
    perror("getresuid");
  } else {
    printf("%s: RUID:%u , EUID:%u , SUID:%u\n", argv[0], ruid, euid, suid);
  }

  if (setresuid(0,0,0) != 0) {
    perror("setresuid");
  }

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
    case -2: fprintf(stderr, "%s: invalid command..\n", argv[0]); return 1;
    case -3: fprintf(stderr, "%s: exec failure..\n", argv[0]); return 1;
    case -4: fprintf(stderr, "%s: fork error..\n", argv[0]); return 1;
    case 127: fprintf(stderr, "%s: could not execute shell (child process)..\n", argv[0]); return 1;
    default:
      printf("%s: child process returned with: %d\n", argv[0], retval);
  }
  free(cmd);
  return 0;
}
