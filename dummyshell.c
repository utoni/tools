/*
 * build with: gcc -Wall -O2 -D_GNU_SOURCE=1 -D_HAS_CMD=1 -D_HAS_MSG=1 -D_HAS_HOSTENT=1 -D_HAS_SIGNAL=1 -D_HAS_UTMP=1 -D_HAS_SYSINFO -ffunction-sections -fdata-sections -ffast-math -fomit-frame-pointer dummyshell.c -o dummyshell
 * strip -s dummyshell
 */


#ifdef _HAVE_CONFIG
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <time.h>
#include <ctype.h>       /* isprint(...) */
#include <sys/ioctl.h>   /* ioctl(...) */
#include <stdint.h>      /* UINT8_MAX */
#include <string.h>      /* memset(...) */
#include <sys/types.h>
#include <pwd.h>         /* getpwuid(...) */
#ifdef _HAS_CMD
#include <sys/wait.h>
#else
#warning "COMMANDS(_HAS_CMD) disabled!"
#endif
#ifdef _HAS_MSG
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#else
#warning "MESSAGE(_HAS_MSG) disabled!"
#endif
#ifdef _HAS_HOSTENT
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#else
#warning "HOSTENT(_HAS_HOSTENT) disabled!"
#endif
#ifdef _HAS_SIGNAL
#include <signal.h>      /* signal(...) */
#else
#warning "SIGNAL(_HAS_SIGNAL) disabled!"
#endif
#ifdef _HAS_UTMP
#include <utmp.h>        /* utmp structure */
#else
#warning "UTMP(_HAS_UTMP) disabled!"
#endif
#ifdef _HAS_SYSINFO
#include "sys/types.h"   /* sysinfo structture */
#include "sys/sysinfo.h" /* sysinfo(...) */
#else
#warning "SYSINFO(_HAS_SYSINFO) disabled!"
#endif

/* for print_memusage() and print cpuusage() see: http://stackoverflow.com/questions/63166/how-to-determine-cpu-and-memory-consumption-from-inside-a-process */


static const char keymsg[] = " ['q'-EXIT | 'm'-MESSAGE | 'i'-INFO | 'c'-CMDS] ";
static const char txtheader[] =
  "**************\n"
  "* dummyshell *\n"
  "**************\n"
  "!(C) by Toni Uhlig\n"
  "@see: https://raw.githubusercontent.com/lnslbrty/tools/master/dummyshell.c\n"
  "@features: "
#ifdef _HAS_CMD
  "COMMAND "
#endif
#ifdef _HAS_MSG
  "MESSAGE "
#endif
#ifdef _HAS_HOSTENT
  "NETHOST "
#endif
#ifdef _HAS_SIGNAL
  "SIGNAL "
#endif
#ifdef _HAS_UTMP
  "UTMP "
#endif
#ifdef _HAS_SYSINFO
  "SYSINFO "
#endif
#ifdef FLOOD_PROTECTION
  "ANTIFLOOD "
#endif
  "\n";

static volatile unsigned char doLoop = 1;


static void printQuitLoop(void) {
  printf("quit in 3 .. ");
  fflush(stdout);
  sleep(1);
  printf("2 .. ");
  fflush(stdout);
  sleep(1);
  printf("1 .. ");
  fflush(stdout);
  sleep(1);
  printf("\n");
  doLoop = 0;
}

#define I_CLEARBUF 0x1
static char readInput(char* buf, size_t* siz, size_t szMax, char key, int flags) {
  if (flags & I_CLEARBUF) {
    memset(&buf[0], '\0', szMax);
    *siz = 0;
  } else switch (key) {
    case '\n':
      break;
    case 127:
      if (*siz > 0)
        buf[--(*siz)] = '\0';
      break;
    case EOF: break;
    default:
      if (isprint(key) && *siz < szMax)
        buf[(*siz)++] = key;
      break;
  }
  return key;
}

#ifdef _HAS_CMD
#define NO_ARGS   0b00000001
#define NEED_ARGS 0b00000010
struct __attribute__((__packed__)) cmd {
  char* name;
  char* path;
  char* defargs;
  unsigned char flags;
  unsigned char argcount;
};

static const struct cmd cmds[] = {
  { "ether-wake", "/usr/bin/suid-ether-wake", "-b -i lan", NEED_ARGS, 4 },
  { "ping", "/usr/bin/suid-ping", "-c 5 -w 2", NEED_ARGS, 5 },
  { "netstat-client", "/bin/netstat", "-pntu", NO_ARGS, 0 },
  { "netstat-server", "/bin/netstat", "-lpn", NO_ARGS, 0 },
  { "echo", "/bin/echo", NULL, NEED_ARGS, 0 },
  { NULL, NULL, NULL, 0, 0 }
};

static void print_cmds(void)
{
  size_t idx = 0;
  printf("\33[2K\r[COMMANDS]\n");
  printf("  command [args][argcount]\n");
  while ( cmds[idx++].path != NULL ) {
    printf("  [%lu] %s [%s][%d]\n", (unsigned long int)idx-1,
      ( cmds[idx-1].name != NULL ? cmds[idx-1].name : "unknown" ),
      ( cmds[idx-1].defargs != NULL ? cmds[idx-1].defargs : "" ),
      ( (cmds[idx-1].flags & NEED_ARGS) ? cmds[idx-1].argcount : -1 ));
  }
}

static int safe_exec(const char* cmdWithArgs, size_t maxArgs)
{
  pid_t child;
  if ( (child = fork()) == 0 ) {
    signal(SIGINT, SIG_IGN);
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
      if (maxArgs && szCur > maxArgs) { /* maxArgs + 1 --> arg0 */
        exit(-4);
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
    waitpid(child, &retval, 0); /* TODO: get more specific info about the exec'd proc (e.g. errno) */
    return 0;
  }
  return -6;
}

static int exec_cmd(size_t i, char* args, size_t szArgs)
{
  size_t idx = (size_t)-1;
  while ( cmds[++idx].path != NULL ) {
    if (idx == i) {
      if (cmds[idx].flags & NEED_ARGS) {
        if (!args)
          return -9;
        if (strnlen(args, szArgs) < 1)
          return -9;
      } else if (cmds[idx].flags & NO_ARGS) {
        if (args)
          return -8;
      }
      size_t siz = strlen(cmds[idx].path)+szArgs+1;
      if (cmds[idx].defargs)
        siz += strlen(cmds[idx].defargs)+2;
      char execbuf[siz+1];
      memset(&execbuf[0], '\0', siz+1);
      if (cmds[idx].defargs) {
        snprintf(&execbuf[0], siz+1, "%s %s %s", cmds[idx].path, cmds[idx].defargs, args);
      } else {
        snprintf(&execbuf[0], siz+1, "%s %s", cmds[idx].path, args);
      }
      return safe_exec(&execbuf[0], cmds[idx].argcount);
    }
  }
  return -7;
}
#endif

#ifdef _HAS_MSG
#define MSGFILE "/tmp/dummyshell.msg"
#define STRLEN(str) (sizeof(str)/sizeof(str[0]))
static int msgfd = -1;
static int init_msg(void)
{
  umask(664);
  struct stat buf;
  memset(&buf, '\0', sizeof(buf));
  if (stat(MSGFILE, &buf) == 0) {
    if (!S_ISREG(buf.st_mode) || !(buf.st_mode & S_IRUSR) || !(buf.st_mode & S_IWUSR) ||
        !(buf.st_mode & S_IRGRP) || !(buf.st_mode & S_IWGRP) || !(buf.st_mode & S_IROTH)) {
      fprintf(stderr, "file (`%s`) mode should be 0664\n", MSGFILE);
      return -1;
    }
  } else if (errno != ENOENT) return -1;
  msgfd = open(MSGFILE, O_RDWR | O_CREAT | O_APPEND | O_DSYNC | O_RSYNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  if (msgfd < 0) {
    fprintf(stderr, "fopen(\"%s\") with write access: %s\n", MSGFILE, strerror(errno));
    msgfd = open(MSGFILE, O_RDONLY | O_CREAT | O_APPEND | O_DSYNC | O_RSYNC);
    if (msgfd < 0) {
      fprintf(stderr, "fopen(\"%s\") readonly: %s\n", MSGFILE, strerror(errno));
      return -2;
    }
  }
  chmod(MSGFILE, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
  return 0;
}

struct __attribute__((__packed__)) msgHdr {
  uint8_t szFrom;
  uint8_t szMsg;
  uint64_t timestamp; /* works for x86 and x64 */
};

struct __attribute__((__packed__)) msg {
  char* from;
  char* msg;
};

static int read_msg(struct msgHdr* hdr, struct msg* msg)
{
  if (msgfd < 0) return -1;
  int ok = 1;
  size_t rb = 0;
  if ( (rb = read(msgfd, hdr, sizeof(struct msgHdr)*1)) == sizeof(struct msgHdr)*1 ) {
    msg->from = calloc(hdr->szFrom+1, sizeof(char));
    msg->msg = calloc(hdr->szMsg+1, sizeof(char));
    if ( (rb = read(msgfd, &(msg->from[0]), sizeof(char)*hdr->szFrom)) != sizeof(char)*hdr->szFrom )
      ok = 0;
    if ( (rb = read(msgfd, &(msg->msg[0]), sizeof(char)*hdr->szMsg)) != sizeof(char)*hdr->szMsg )
      ok = 0;
    char newline = 0;
    if ( (rb = read(msgfd, &newline, sizeof(char)*1)) != sizeof(char)*1 )
      ok = 0;
    if (!ok || newline != '\n') {
      free(msg->from);
      free(msg->msg);
      msg->from = NULL;
      msg->msg = NULL;
      return -1;
    }
    return 0;
  }
  return -1;
}

static int print_msg(void) {
  struct msgHdr hdr;
  struct msg msg;
  memset(&hdr, '\0', sizeof(struct msgHdr));
  memset(&msg, '\0', sizeof(struct msg));
  if (read_msg(&hdr, &msg) == 0) {
    struct tm localtime;
    struct passwd* pwd = NULL;
    unsigned long int uid = strtoul(msg.from, NULL, 10);
    if ( (pwd = getpwuid((uid_t)uid)) ) {
      free(msg.from);
      msg.from = strdup(pwd->pw_name);
    }
    if (localtime_r((const time_t*)&hdr.timestamp, &localtime) != NULL) {
      printf("\33[2K\r[%02d-%02d-%04d %02d:%02d:%02d] Message from %s: %s\n", localtime.tm_mday, localtime.tm_mon+1, 1900+localtime.tm_year, localtime.tm_hour, localtime.tm_min, localtime.tm_sec, msg.from, msg.msg);
    } else {
      printf("\33[2K\r\aMessage from %s: %s\n", msg.from, msg.msg);
    }
    free(msg.from);
    free(msg.msg);
    return 0;
  }
  return -1;
}

static int write_msg(char* msg) {
  char from[6];
  memset(&from[0], '\0', STRLEN(from));
  if (snprintf(&from[0], STRLEN(from), "%u", getuid()) > 0) {
    struct msgHdr hdr;
    hdr.szFrom = strnlen(from, STRLEN(from));
    hdr.szMsg = strnlen(msg, UINT8_MAX);
    hdr.timestamp = time(NULL);
    char* buf = calloc(sizeof(hdr) + hdr.szFrom + hdr.szMsg + 2, sizeof(char));
    if (buf) {
      memcpy(buf, &hdr, sizeof(hdr));
      memcpy(buf+sizeof(hdr), from, hdr.szFrom);
      memcpy(buf+sizeof(hdr)+hdr.szFrom, msg, hdr.szMsg);
      *(buf + sizeof(hdr) + hdr.szFrom + hdr.szMsg) = '\n';
      int failed = 1;
      if ( write(msgfd, buf, sizeof(char)*(sizeof(hdr)+hdr.szFrom+hdr.szMsg+1)) == sizeof(char)*(sizeof(hdr)+hdr.szFrom+hdr.szMsg+1) )
        failed = 0;
      free(buf);
      return failed;
    }
  }
  return -1;
}
#endif

#ifdef _HAS_HOSTENT
#define ARP_STRING_LEN 1024
#define ARP_IP_LEN 32
#define XSTR(s) STR(s)
#define STR(s) #s
static void print_nethost(void)
{
  FILE *arpCache = fopen("/proc/net/arp", "r");
  if (arpCache != NULL) {
    char arpline[ARP_STRING_LEN+1];
    memset(&arpline[0], '\0', ARP_STRING_LEN+1);
    if (fgets(arpline, ARP_STRING_LEN, arpCache)) {
      char arpip[ARP_IP_LEN+1];
      memset(&arpip[0], '\0', ARP_IP_LEN);
      const char nonline[] = "\33[2K\rhost online...: ";
      size_t i = 0;
      while (1 == fscanf(arpCache, "%" XSTR(ARP_IP_LEN) "s %*s %*s %*s %*s %*s", &arpip[0])) {
        struct in_addr ip;
        struct hostent *hp = NULL;
        if (inet_aton(&arpip[0], &ip)) {
          hp = gethostbyaddr((const void *)&ip, sizeof ip, AF_INET);
        }
        char *herrmsg = NULL;
        if (hp == NULL) {
          switch (h_errno) {
            case HOST_NOT_FOUND: herrmsg = "HOST UNKNOWN"; break;
            case NO_ADDRESS:     herrmsg = "IP UNKNOWN"; break;
            case NO_RECOVERY:    herrmsg = "SERVER ERROR"; break;
            case TRY_AGAIN:      herrmsg = "TEMPORARY ERROR"; break;
          }
        }
        printf("%s[%lu] %.*s aka %s\n", nonline, (long unsigned int)++i, ARP_IP_LEN, arpip, (hp != NULL ? hp->h_name : herrmsg));
        memset(&arpip[0], '\0', ARP_IP_LEN);
      }
    }
    fclose(arpCache);
  }
}
#endif

#ifdef _HAS_UTMP
#ifndef _GNU_SOURCE
size_t
strnlen(const char *str, size_t maxlen)
{
  const char *cp;
  for (cp = str; maxlen != 0 && *cp != '\0'; cp++, maxlen--);
  return (size_t)(cp - str);
}
#endif

static void print_utmp(void)
{
  int utmpfd = open("/var/run/utmp", O_RDONLY);
  if (utmpfd >= 0) {
    struct utmp ut;
    memset(&ut, '\0', sizeof(struct utmp));
    const char uonline[] = "\33[2K\ruser online...: ";
    size_t i = 0;
    while ( read(utmpfd, &ut, sizeof(struct utmp)) == sizeof(struct utmp) && strnlen(ut.ut_user, UT_NAMESIZE) > 0 ) {
      if (strnlen(ut.ut_host, UT_HOSTSIZE) > 0) {
        printf("%s[%lu] %.*s from %.*s\n", uonline, (long unsigned int)++i, UT_NAMESIZE, ut.ut_user, UT_HOSTSIZE, ut.ut_host);
      } else {
        printf("%s[%lu] %.*s\n", uonline, (long unsigned int)++i, UT_NAMESIZE, ut.ut_user);
      }
    }
  }
}
#endif

#ifdef _HAS_SYSINFO
static unsigned long long lastTotalUser, lastTotalUserLow, lastTotalSys, lastTotalIdle;

static int init_cpuusage(){
  FILE* file = fopen("/proc/stat", "r");
  if (file) {
    unsigned char success = 1;
    if (fscanf(file, "cpu %llu %llu %llu %llu", &lastTotalUser, &lastTotalUserLow, &lastTotalSys, &lastTotalIdle) != 4)
      success = 0;
    fclose(file);
    if (success)
      return 0;
  }
  return 1;
}

static void print_cpuusage(){
  double percent;
  FILE* file;
  unsigned long long totalUser, totalUserLow, totalSys, totalIdle, total;

  file = fopen("/proc/stat", "r");
  fscanf(file, "cpu %llu %llu %llu %llu", &totalUser, &totalUserLow,
    &totalSys, &totalIdle);
  fclose(file);

  if (totalUser < lastTotalUser || totalUserLow < lastTotalUserLow ||
    totalSys < lastTotalSys || totalIdle < lastTotalIdle){
    //Overflow detection. Just skip this value.
    percent = -1.0;
  } else{
    total = (totalUser - lastTotalUser) + (totalUserLow - lastTotalUserLow) +
      (totalSys - lastTotalSys);
    percent = total;
    total += (totalIdle - lastTotalIdle);
    percent /= total;
    percent *= 100;
  }

  lastTotalUser = totalUser;
  lastTotalUserLow = totalUserLow;
  lastTotalSys = totalSys;
  lastTotalIdle = totalIdle;

  printf("CPU...........: %.02f%%\n", percent);
}

static void print_memusage(void)
{
  struct sysinfo meminfo;
  memset(&meminfo, '\0', sizeof(struct sysinfo));
  if (sysinfo(&meminfo) == 0) {
    unsigned long long totalvmem = meminfo.totalram;
    totalvmem += meminfo.totalswap;
    totalvmem *= meminfo.mem_unit;
    unsigned long long usedvmem = meminfo.totalram - meminfo.freeram;
    usedvmem += meminfo.totalswap - meminfo.freeswap;
    usedvmem *= meminfo.mem_unit;
    printf("VMEM(used/max): %llu/%lld (Mb)\n", (usedvmem/(1024*1024)), (totalvmem/(1024*1024)));
  }
}
#endif

#ifdef _HAS_SIGNAL
void SigIntHandler(int signum)
{
  if (signum == SIGINT || signum == SIGTERM) {
    printf("\33[2K\rRecieved Signal: %s\n", (signum == SIGINT ? "SIGINT" : "SIGTERM"));
    printQuitLoop();
  }
}
#endif

enum mainState { MS_DEFAULT, MS_MESSAGE, MS_INFO, MS_COMMAND };

static void print_info(void)
{
#ifdef _HAS_UTMP
  print_utmp();
#endif
#ifdef _HAS_HOSTENT
  print_nethost();
#endif
#ifdef _HAS_SYSINFO
  print_memusage();
  print_cpuusage();
#endif
}

int main(int argc, char** argv)
{
  enum mainState state = MS_DEFAULT;
  struct timeval tv;
  tv.tv_sec = 1;
  tv.tv_usec = 0;

#ifdef _HAS_SIGNAL
  signal(SIGINT, SigIntHandler);
  signal(SIGTERM, SigIntHandler);
#endif
#ifdef FLOOD_PROTECTION
  unsigned char curInput = 0;
  const unsigned char maxInput = FLOOD_PROTECTION;
#endif
  size_t inputsiz = 0, absiz = UINT8_MAX;
  struct winsize wsiz;
  ioctl(0, TIOCGWINSZ, &wsiz);
  if (wsiz.ws_col < absiz)
    absiz = wsiz.ws_col - 3;
  char inputbuf[absiz+1];
  memset(&inputbuf[0], '\0', absiz+1);
#ifdef _HAS_MSG
  if (init_msg() != 0) {
    fprintf(stderr, "init msg failed\n");
    return 1;
  }
  if (argc > 1) {
    const char optRmsg[] = "readmsg";
    const char optWmsg[] = "writemsg";
    const char optNofollow[] = "-n";
    if (strncmp(argv[1], optRmsg, STRLEN(optRmsg)) == 0) {
      if (argc == 2) while (doLoop) {
        if (print_msg() != 0)
          sleep(1);
      } else if (argc == 3) {
        if (strncmp(argv[2], optNofollow, STRLEN(optNofollow)) == 0) {
          while (print_msg() == 0) {}
        } else {
          fprintf(stderr, "%s readmsg [-n]\n", argv[0]);
          return 1;
        }
      }
      return 0;
    } else if (strncmp(argv[1], optWmsg, STRLEN(optWmsg)) == 0) {
      if (argc == 3) {
        return write_msg(argv[2]);
      } else {
        fprintf(stderr, "%s writemsg msg\n", argv[0]);
      }
      return 0;
    } else {
      fprintf(stderr, "usage: %s [readmsg [-n] | writemsg msg]\n", argv[0]);
      return 1;
    }
  }
#endif
#ifdef _HAS_SYSINFO
  init_cpuusage();
#endif
  int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
  fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

  static struct termios oldt, newt;
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);

  printf("%s\n", txtheader);
  fd_set fds;
  time_t start = time(NULL);
  time_t cur;
  unsigned char mins = 0, hrs = 0, doOnce = 1, prevBufEmpty = 0;
  while (doLoop > 0) {
    cur = time(NULL);
    double diff = difftime(cur, start);
#if defined(_HAS_UTMP) || defined(_HAS_SYSINFO)
    if ((unsigned int)diff % 60 == 0) {
      if (doOnce) {
        if (diff != 0 && ++mins == 60) {
          mins = 0;
          hrs++;
        }
        struct tm localtime;
        if (localtime_r(&cur, &localtime) != NULL) {
          printf("\33[2K\r--- %02d:%02d:%02d ---\n", localtime.tm_hour, localtime.tm_min, localtime.tm_sec);
        }
        print_info();
        doOnce = 0;
      }
    } else doOnce = 1;
#endif
#ifdef _HAS_MSG
    while (print_msg() == 0) {}
#endif
    switch (state) {
      case MS_INFO:
        printf("\33[2K\r[INFO]\n");
        print_info();
        state = MS_DEFAULT;
      case MS_DEFAULT:
        printf("\33[2K\r--- %02d:%02d:%02d ---%s", hrs, mins, ((unsigned int)diff % 60), keymsg);
        break;
      case MS_MESSAGE:
      case MS_COMMAND:
#if defined(_HAS_MSG) || defined(_HAS_CMD)
        printf("\33[2K\r> %s", inputbuf);
#endif
        break;
    }
    fflush(stdout);

    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    int ret = select(FD_SETSIZE, &fds, NULL, NULL, &tv);
    if (doLoop == 1 && ret == 0) {
      tv.tv_sec = 1;
      tv.tv_usec = 0;
#ifdef FLOOD_PROTECTION
      curInput = 0;
#endif
    } else if (FD_ISSET(STDIN_FILENO, &fds)) {
      char key = getchar();
      switch (state) {
        case MS_DEFAULT:
#ifdef FLOOD_PROTECTION
          if (curInput++ >= maxInput) {
            curInput = 0;
            printf("\33[2K\r<flood protection> (suspended for %us)\n", FLOOD_TIMEOUT);
            sleep(FLOOD_TIMEOUT);
            while (getchar() != EOF) {}
          }
#endif
          switch ( key ) {
            case 'q':
#ifdef _HAS_SIGNAL
              signal(SIGINT, SIG_IGN);
#endif
              printQuitLoop();
              break;
            case 'm':
#ifdef _HAS_MSG
              if (lseek(msgfd, 0, SEEK_SET) != -1) {
                printf("\33[2K\r[MESSAGES]\n");
                while (print_msg() == 0) {}
              }
              state = MS_MESSAGE;
#else
              printf("<feature disabled>\n");
#endif
              break;
            case 'i':
              state = MS_INFO;
              break;
            case 'c':
#ifdef _HAS_CMD
              print_cmds();
              state = MS_COMMAND;
#else
              printf("<feature disabled>\n");
#endif
              break;
            default: if (doLoop) printf("unknown key: %c\n", key); break;
          }
          break;
        case MS_INFO:
          break;
        case MS_COMMAND:
        case MS_MESSAGE:
          switch (readInput(&inputbuf[0], &inputsiz, absiz, key, 0)) {
            case 127:
              if (inputsiz > 0) {
                prevBufEmpty=0;
                break;
              } else if (!prevBufEmpty) {
                prevBufEmpty=1;
                break;
              }
              prevBufEmpty=0;
            case 27:
              state = MS_DEFAULT;
              readInput(&inputbuf[0], &inputsiz, absiz, 0, I_CLEARBUF);
              break;
            case '\n':
              if (strnlen(inputbuf, inputsiz) > 0) {
                if (state == MS_MESSAGE) {
#ifdef _HAS_MSG
                  printf("\33[2K\rSending message(%lu): %s\n", (long unsigned int)inputsiz, inputbuf);
                  if (write_msg(inputbuf) != 0)
                    printf("Sending failed.\n");
#endif
                } else if (state == MS_COMMAND) {
#ifdef _HAS_CMD
                  int inputFail = 0;
                  if (inputsiz < 1) {
                    inputFail++;
                  } else {
                    char* endptr = NULL;
                    size_t szEndptr = 0;
                    unsigned long int tmpi = strtoul(inputbuf, &endptr, 10);
                    const char exec_str[] = "\33[2K\rExec CMD #";
                    if (*endptr == ' ') {
                      endptr++;
                      szEndptr = strnlen(endptr, inputsiz-1);
                      printf("%s%lu with args: %s\n", exec_str, tmpi, endptr);
                    } else if (*endptr == '\0') {
                      printf("%s%lu\n", exec_str, tmpi);
                      endptr = NULL;
                    } else inputFail++;

                    if (inputFail == 0) {
                      int retval;
                      switch ( (retval = exec_cmd(tmpi, endptr, szEndptr)) ) {
                        case -9: printf("args required for cmd #%lu\n", tmpi); break;
                        case -8: printf("no args allowed for cmd #%lu\n", tmpi); break;
                        case -7: printf("unknown cmd #%lu\n", tmpi); break;
                        case -6: printf("fork error cmd #%lu\n", tmpi); break;
                        case -5: printf("execute cmd #%lu\n", tmpi); break;
                        case -4: printf("too many arguments for cmd #%lu\n", tmpi); break;

                        case 0: break;
                        default: printf("Something went wrong, child returned: %d\n", retval); break;
                      }
                    }
                  }
                  if (inputFail > 0)
                    printf("\33[2K\rFORMAT: [cmd# [params]]\n");
#endif
                }
              }
              state = MS_DEFAULT;
              readInput(&inputbuf[0], &inputsiz, absiz, 0, I_CLEARBUF);
              break;
          }
          fflush(stdout);
          break;
      }
    }
  }
  while (getchar() != EOF) {}

  tcsetattr( STDIN_FILENO, TCSANOW, &oldt);
  fcntl(STDIN_FILENO, F_SETFL, flags);
  return 0;
}
