#define _DEFAULT_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <time.h>
#include <assert.h>

#define MAX_CMDLINE_LEN 512
#define MAX_TERMINAL_LEN 2048


struct filtered_dir_entries {
    struct dirent ** entries;
    size_t entries_num;
};


static int search_dir(const char * const dir,
                      int (*filter_fn)(const struct dirent *),
                      struct filtered_dir_entries * const entries)
{
    struct dirent **names;
    int n;

    assert(dir && filter_fn && entries);

    n = scandir(dir, &names, filter_fn, alphasort);
    if (n < 0) {
        return 1;
    }

    entries->entries = names;
    entries->entries_num = n;

    return 0;
}

static int dirent_filter_only_numeric(const struct dirent * ent)
{
    char * end_ptr;

    assert(ent);

    errno = 0;
    strtoul(ent->d_name, &end_ptr, 10);
    return (errno != 0 || *end_ptr == '\0');
}

static int search_procfs_fd(const char * const dir, const char * const subdir,
                              struct filtered_dir_entries * const entries)
{
    char buf[BUFSIZ];

    assert(dir && subdir && entries);

    snprintf(buf, sizeof buf, "%s/%s/fd", dir ,subdir);
    return search_dir(buf, dirent_filter_only_numeric, entries);
}

static ssize_t realpath_procfs_fd(const char * const dir, const char * const pid,
                                  const char * const fd, char * dest, size_t siz)
{
    char buf[BUFSIZ];

    assert(dir && pid && fd && dest && siz > 0);

    snprintf(buf, sizeof buf, "%s/%s/fd/%s", dir, pid, fd);
    return readlink(buf, dest, siz);
}

static void free_filtered_dir_entries(struct filtered_dir_entries * const entries)
{
    assert(entries);

    if (!entries->entries) {
        return;
    }
    for (size_t i = 0; i < entries->entries_num; ++i) {
        free(entries->entries[i]);
    }
    free(entries->entries);
    entries->entries = NULL;
    entries->entries_num = 0;
}

enum proc_subdir_type {
    PROC_SUBDIR_FD, PROC_SUBDIR_FDINFO
};

static int open_in_procfs(const char * const pid, const char * const fd, enum proc_subdir_type type)
{
    char proc_path[BUFSIZ];
    const char * subdir_path = NULL;

    assert(pid && fd);

    switch (type) {
        case PROC_SUBDIR_FD:
            subdir_path = "fd";
            break;
        case PROC_SUBDIR_FDINFO:
            subdir_path = "fdinfo";
            break;
        default:
            return -1;
    }

    if (snprintf(proc_path, sizeof proc_path, "%s/%s/%s/%s", "/proc", pid, subdir_path, fd) <= 0) {
        return -1;
    }

    int proc_fd = open(proc_path, 0);
    if (proc_fd < 0) {
        perror("open proc_fd");
        return -1;
    }

    return proc_fd;
}

struct file_info {
    int proc_fdinfo_fd;
    long int current_position;
    long int max_size;
    struct {
        struct winsize dimensions;
        char output[MAX_TERMINAL_LEN];
        size_t printable_chars;
        size_t unprintable_chars;
    } terminal;
};

static int setup_file_info(struct file_info * const finfo, int proc_fd_fd, int proc_fdinfo_fd)
{
    struct stat buf;

    assert(finfo && proc_fd_fd >= 0 && proc_fdinfo_fd >= 0);

    if (fstat(proc_fd_fd, &buf)) {
        perror("setup_file_info: fstat");
        return -1;
    }

    finfo->proc_fdinfo_fd = proc_fdinfo_fd;
    finfo->current_position = 0;
    finfo->max_size = buf.st_size;
    return 0;
}

static int read_and_parse_fd_pos(struct file_info * const finfo)
{
    ssize_t nread;
    char proc_fdinfo[BUFSIZ];
    static const char needle[] = "pos:\t";
    char * pospos;

    assert(finfo);

    if (lseek(finfo->proc_fdinfo_fd, 0, SEEK_SET) < 0) {
        return -1;
    }

    nread = read(finfo->proc_fdinfo_fd, &proc_fdinfo[0], sizeof proc_fdinfo);
    if (nread <= 0) {
        return -1;
    }
    proc_fdinfo[nread - 1] = '\0';

    pospos = strstr(&proc_fdinfo[0], &needle[0]);
    if (!pospos) {
        return 1;
    }
    pospos += (sizeof(needle) - 1) / sizeof(needle[0]);

    finfo->current_position = strtoul(pospos, NULL, 10);

    return 0;
}

static int read_proc_cmdline(char * dest, size_t size,
                             const char * const proc_pid)
{
    int cmdline_fd;
    char buf[MAX_CMDLINE_LEN];

    assert(dest && size && proc_pid);

    if (snprintf(buf, sizeof buf, "%s/%s/cmdline",
                 "/proc", proc_pid) <= 0)
    {
        dest[0] = '\0';
        return -1;
    }
    cmdline_fd = open(buf, 0);
    if (read(cmdline_fd, buf, sizeof buf) <= 0)
    {
        dest[0] = '\0';
        return -1;
    }
    close(cmdline_fd);

    return snprintf(dest, size, "%.*s (%s)", MAX_CMDLINE_LEN - 35, buf, proc_pid);
}

static int reset_terminal_output(struct file_info * const finfo)
{
    assert(finfo);

    finfo->terminal.output[0] = '\r';
    finfo->terminal.output[1] = '\0';
    finfo->terminal.unprintable_chars = 1;
    finfo->terminal.printable_chars = 0;
    return ioctl(0, TIOCGWINSZ, &finfo->terminal.dimensions);
}

static size_t remaining_printable_chars(struct file_info * const finfo)
{
    assert(finfo);

    return finfo->terminal.dimensions.ws_col -
           strnlen(finfo->terminal.output, finfo->terminal.printable_chars);
}

static int vadd_printable_buf(struct file_info * const finfo, const char * format, va_list ap)
{
    char tmp_buf[MAX_TERMINAL_LEN];
    int snprintf_retval;
    size_t remaining_len;

    assert(finfo && format);

    remaining_len = remaining_printable_chars(finfo);
    if (!remaining_len) {
        return -1;
    }

    snprintf_retval = vsnprintf(tmp_buf,  sizeof tmp_buf, format, ap);
    if (snprintf_retval > 0) {
        if ((size_t)snprintf_retval > remaining_len) {
            return -1;
        }
        memcpy(finfo->terminal.output + finfo->terminal.printable_chars +
               finfo->terminal.unprintable_chars,
               tmp_buf, snprintf_retval);
        finfo->terminal.printable_chars += snprintf_retval;
    }
    return snprintf_retval;
}

static int add_printable_buf(struct file_info * const finfo, const char * format, ...)
{
    int ret;
    va_list ap;

    assert(finfo && format);

    va_start(ap, format);
    ret = vadd_printable_buf(finfo, format, ap);
    va_end(ap);
    return ret;
}

enum unit_suffix {
    NONE, KILO, MEGA, GIGA
};

static enum unit_suffix choose_appropriate_unit(long int bytes, float *result)
{
    float pretty_bytes;

    assert(result);

    pretty_bytes = (float)bytes / (1024.0f * 1024.0f * 1024.0f);
    if (pretty_bytes >= 1.0f) {
        *result = pretty_bytes;
        return GIGA;
    }

    pretty_bytes = (float)bytes / (1024.0f * 1024.0f);
    if (pretty_bytes >= 1.0f) {
        *result = pretty_bytes;
        return MEGA;
    }

    pretty_bytes = (float)bytes / 1024.0f;
    if (pretty_bytes >= 1.0f) {
        *result = pretty_bytes;
        return KILO;
    }

    return NONE;
}

static void prettify_with_units(long int bytes, char * buf, size_t siz)
{
    float unit_bytes = 0.0f;
    enum unit_suffix up = choose_appropriate_unit(bytes, &unit_bytes);

    assert(buf && siz > 0);

    switch (up) {
        case KILO:
            snprintf(buf, siz, "%.2fK", unit_bytes);
            break;
        case MEGA:
            snprintf(buf, siz, "%.2fM", unit_bytes);
            break;
        case GIGA:
            snprintf(buf, siz, "%.2fG", unit_bytes);
            break;

        case NONE:
        default:
            snprintf(buf, siz, "%ld", bytes);
            break;
    }
}

static void show_positions(struct file_info * const finfo)
{
    char curpos[66];
    char maxpos[66];

    assert(finfo);

    prettify_with_units(finfo->current_position, curpos, sizeof curpos);
    prettify_with_units(finfo->max_size, maxpos, sizeof maxpos);

    add_printable_buf(finfo, "[%s..%s]", curpos, maxpos);
}

static void show_progressbar(struct file_info * const finfo)
{
    char buf[BUFSIZ];
    size_t remaining_len;

    assert(finfo);

    remaining_len = remaining_printable_chars(finfo);
    if (remaining_len < 8 || remaining_len >= sizeof buf) {
        return;
    }

    float progress = (float)finfo->current_position / finfo->max_size;
    add_printable_buf(finfo, "[%.2f%%]", progress * 100.0f);

    remaining_len = remaining_printable_chars(finfo);
    if (remaining_len < 3 || remaining_len >= sizeof buf) {
        return;
    }

    float printable_progress = progress * remaining_len;
    memset(buf, '-', remaining_len - 2);
    memset(buf, '#', (size_t)printable_progress);
    buf[remaining_len - 2] = '\0';
    add_printable_buf(finfo, "[%s]", buf);
}

static int nsleep(unsigned long long int nanosecs)
{
    struct timespec tim;
    tim.tv_sec = 0;
    tim.tv_nsec = nanosecs;

    return nanosleep(&tim , NULL);
}

struct filepath {
    char pid[32];
    char fd[32];
    char cmdline[MAX_CMDLINE_LEN];
    struct filepath * next;
};

static int choose_filepath(struct filepath * fp)
{
    size_t choice = 0, current = 0;

    puts("Choose file to watch ..\n");
    while (fp && ++current) {
        struct filepath * cur = fp;
        fp = fp->next;

        read_proc_cmdline(cur->cmdline, sizeof cur->cmdline, cur->pid);
        printf("[%zu] %s (%s) fd %s\n", current, cur->cmdline, cur->pid, cur->fd);
    }

    printf("\nYour choice: ");
    if (scanf("%zu", &choice) != 1 || choice > current || !choice) {
        return 1;
    }

    printf("_%zu_\n", choice);
    return 0;
}

int main(int argc, char **argv)
{
    struct filtered_dir_entries proc_pid_entries = {};
    struct filtered_dir_entries proc_fd_entries = {};
    size_t target_filepath_len;
    char file_realpath[BUFSIZ] = {};
    size_t found_targets = 0;
    struct filepath * paths = NULL;
    struct filepath ** next = &paths;

    if (argc != 2) {
        fprintf(stderr, "usage: %s [FILE]\n", (argc > 0 ? argv[0] : "progressbar"));
        exit(EXIT_FAILURE);
    }
    target_filepath_len = strlen(argv[1]);

    if (search_dir("/proc", dirent_filter_only_numeric, &proc_pid_entries)) {
        exit(EXIT_FAILURE);
    }
    for (size_t i = 0; i < proc_pid_entries.entries_num; ++i) {
        if (search_procfs_fd("/proc", proc_pid_entries.entries[i]->d_name,
                             &proc_fd_entries)) {
            continue;
        }

        for (size_t j = 0; j < proc_fd_entries.entries_num; ++j) {
            size_t realpath_used = realpath_procfs_fd("/proc",
                proc_pid_entries.entries[i]->d_name,
                proc_fd_entries.entries[j]->d_name,
                &file_realpath[0], sizeof file_realpath - 1);
            if (realpath_used <= 0) {
                continue;
            }
            file_realpath[realpath_used] = '\0';
            if (realpath_used == target_filepath_len) {
                if (!strncmp(argv[1], file_realpath, realpath_used)) {
                    *next = (struct filepath *) calloc(1, sizeof(**next));
                    if (!*next) {
                        continue;
                    }

                    if (snprintf((*next)->pid, sizeof (*next)->pid, "%s",
                                 proc_pid_entries.entries[i]->d_name) <= 0)
                    {
                        continue;
                    }
                    if (snprintf((*next)->fd, sizeof (*next)->fd,
                                 "%s", proc_fd_entries.entries[j]->d_name) <= 0)
                    {
                        continue;
                    }

                    found_targets++;
                    next = &(*next)->next;
                }
            }
        }
        free_filtered_dir_entries(&proc_fd_entries);
    }
    free_filtered_dir_entries(&proc_pid_entries);

    if (!found_targets) {
        fprintf(stderr, "%s: file '%s' not found in /proc\n", argv[0], argv[1]);
        exit(EXIT_FAILURE);
    }

    if (found_targets == 1) {
        read_proc_cmdline(paths->cmdline, sizeof paths->cmdline, paths->pid);
        printf("FD..: '/proc/%s/fd/%s' | CMD.: %s\n",
               paths->pid, paths->fd, paths->cmdline);
    } else if (choose_filepath(paths)) {
        fprintf(stderr, "%s: user did not choose a valid filepath\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int proc_fd_fd = open_in_procfs(paths->pid, paths->fd, PROC_SUBDIR_FD);
    if (proc_fd_fd < 0) {
        perror("open proc_fd");
        exit(EXIT_FAILURE);
    }

    int proc_fdinfo_fd = open_in_procfs(paths->pid, paths->fd, PROC_SUBDIR_FDINFO);
    if (proc_fdinfo_fd < 0) {
        perror("open proc_fdinfo");
        exit(EXIT_FAILURE);
    }

    struct file_info finfo = {};
    if (setup_file_info(&finfo, proc_fd_fd, proc_fdinfo_fd)) {
        exit(EXIT_FAILURE);
    }
    close(proc_fd_fd);

    while (!read_and_parse_fd_pos(&finfo)) {
        if (reset_terminal_output(&finfo) < 0) {
            break;
        }
        show_positions(&finfo);
        show_progressbar(&finfo);

        printf("%s", finfo.terminal.output);
        fflush(stdout);
        nsleep(150000000L);
    }
    puts("");

    close(finfo.proc_fdinfo_fd);

    while (paths) {
        struct filepath * cur = paths;
        paths = paths->next;

        free(cur);
    }
}
