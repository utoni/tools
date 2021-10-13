#define _DEFAULT_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <time.h>
#include <assert.h>

#define MAX_CMDLINE_LEN 512
#define MAX_TERMINAL_WIDTH 2048


struct filtered_dir_entries {
    struct dirent ** entries;
    size_t entries_num;
};


static int search_dir(const char * const dir,
                      int (*filter_fn)(struct dirent const * const),
                      struct filtered_dir_entries * const entries)
{
    struct dirent ** names;
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

static int dirent_filter_only_numeric(struct dirent const * const ent)
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
                                  const char * const fd, char * const dest, size_t siz)
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

#define XFER_RATES_LENGTH \
    (sizeof( ((struct file_info *)0)->xfer_rate_history.xfer_rates ) \
    / sizeof( ((struct file_info *)0)->xfer_rate_history.xfer_rates[0]) )
struct file_info {
    int proc_fdinfo_fd;
    long int current_position;
    long int last_position;
    long int max_size;
    struct timespec loop_start;
    struct timespec loop_end;
    struct {
        float xfer_rates[32];
        size_t next_index;
        float last_reported_rate;
    } xfer_rate_history;
    struct {
        char buf[MAX_TERMINAL_WIDTH];
        size_t printable_chars;
        size_t unprintable_chars;
    } terminal_output;
};

struct terminal {
    struct winsize dimensions;
    int dimensions_changed;
};
static struct terminal term = {};

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

    if (finfo->max_size == 0) {
        fprintf(stderr, "setup_file_info: fd does not have any max size, probably a special file\n");
        return -1;
    }

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

    finfo->last_position = finfo->current_position;
    finfo->current_position = strtoul(pospos, NULL, 10);

    return 0;
}

static void read_proc_cmdline(char * const dest, size_t size,
                             const char * const proc_pid)
{
    int cmdline_fd;
    char buf[MAX_CMDLINE_LEN];
    ssize_t bytes_read;

    assert(dest && size && proc_pid);

    if (snprintf(buf, sizeof buf, "%s/%s/cmdline",
                 "/proc", proc_pid) <= 0)
    {
        dest[0] = '\0';
    }
    cmdline_fd = open(buf, 0);
    bytes_read = read(cmdline_fd, buf, sizeof buf);
    if (bytes_read <= 0) {
        dest[0] = '\0';
    }
    close(cmdline_fd);

    while (--bytes_read) {
        if (buf[bytes_read - 1] == '\0') {
            buf[bytes_read - 1] = ' ';
        }
    }
    strncpy(dest, buf, size);
}

static void reset_terminal_output_buffer(struct file_info * const finfo)
{
    assert(finfo);

    finfo->terminal_output.buf[0] = '\r';
    finfo->terminal_output.buf[1] = '\0';
    finfo->terminal_output.unprintable_chars = 1;
    finfo->terminal_output.printable_chars = 0;
}

static int get_terminal_dimensions(struct terminal * const term)
{
    return ioctl(0, TIOCGWINSZ, &term->dimensions);
}

static size_t remaining_printable_chars(struct terminal const * const term,
                                        struct file_info const * const finfo)
{
    assert(term && finfo);

    return term->dimensions.ws_col -
           strnlen(finfo->terminal_output.buf, finfo->terminal_output.printable_chars);
}

static int vadd_printable_buf(struct terminal const * const term,
                              struct file_info * const finfo,
                              const char * const format, va_list ap)
{
    char tmp_buf[MAX_TERMINAL_WIDTH];
    int snprintf_retval;
    size_t remaining_len, total_len;

    assert(finfo && format);

    remaining_len = remaining_printable_chars(term, finfo);
    if (!remaining_len) {
        return -1;
    }

    total_len = finfo->terminal_output.printable_chars +
                finfo->terminal_output.unprintable_chars;

    snprintf_retval = vsnprintf(tmp_buf,  sizeof tmp_buf, format, ap);
    if (snprintf_retval > 0) {
        size_t buf_len = snprintf_retval;
        if (buf_len > remaining_len ||
            buf_len + total_len + 1 > sizeof finfo->terminal_output.buf)
        {
            return -1;
        }
        memcpy(finfo->terminal_output.buf + total_len, tmp_buf, buf_len);
        finfo->terminal_output.printable_chars += snprintf_retval;
        finfo->terminal_output.buf[total_len + buf_len] = '\0';
    }
    return snprintf_retval;
}

static int add_printable_buf(struct terminal const * const term,
                             struct file_info * const finfo,
                             const char * const format, ...)
{
    int ret;
    va_list ap;

    assert(finfo && format);

    va_start(ap, format);
    ret = vadd_printable_buf(term, finfo, format, ap);
    va_end(ap);
    return ret;
}

enum unit_suffix {
    NONE, KILO, MEGA, GIGA
};

static enum unit_suffix choose_appropriate_unit(long int bytes, float * const result)
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

static void fillup_remaining(struct terminal const * const term,
                             struct file_info * const finfo)
{
    size_t remaining_len = remaining_printable_chars(term, finfo);

    if (remaining_len) {
        char spaces[remaining_len + 1];

        memset(spaces, ' ', sizeof(spaces));
        spaces[remaining_len] = '\0';
        add_printable_buf(term, finfo, "%s", spaces);
    }
}

static void prettify_with_units(long int bytes, char * const buf, size_t siz)
{
    float unit_bytes = 0.0f;
    enum unit_suffix us = choose_appropriate_unit(bytes, &unit_bytes);

    assert(buf && siz > 0);

    switch (us) {
        case KILO:
            snprintf(buf, siz, "%.2fKb", unit_bytes);
            break;
        case MEGA:
            snprintf(buf, siz, "%.2fMb", unit_bytes);
            break;
        case GIGA:
            snprintf(buf, siz, "%.2fGb", unit_bytes);
            break;

        case NONE:
        default:
            snprintf(buf, siz, "%ldb", bytes);
            break;
    }
}

static void show_positions(struct terminal * const term,
                           struct file_info * const finfo)
{
    char curpos[64];
    char maxpos[64];

    assert(finfo);

    prettify_with_units(finfo->current_position, curpos, sizeof curpos);
    /* if the user is currently writing/appending to a file, show only current offset */
    if (finfo->current_position <= finfo->max_size) {
        prettify_with_units(finfo->max_size, maxpos, sizeof maxpos);
        add_printable_buf(term, finfo, "[%s/%s]", curpos, maxpos);
    } else {
        add_printable_buf(term, finfo, "[%s WRITTEN]", curpos);
    }
}

static void measure_realtime(struct timespec * const tp)
{
    if (clock_gettime(CLOCK_REALTIME, tp)) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
}

static void loop_start(struct file_info * const finfo)
{
    measure_realtime(&finfo->loop_start);
}

static void loop_end(struct file_info * const finfo)
{
    measure_realtime(&finfo->loop_end);
}

#define NANO_CONVERSION_F ((float)(1000.0f * 1000.0f * 1000.0f))
static void show_rate(struct terminal * const term,
                      struct file_info * const finfo)
{
    char out[64];
    size_t xfer_rate_index;
    float new_xfer_rate;
    float diff_pos;
    float result = 0.0f;

    assert(finfo);
    diff_pos = finfo->current_position - finfo->last_position;

    if (diff_pos > 0.0f) {
        /* calculate diff between last loop-end and loop-start for seconds and nano-seconds */
        time_t diff_time_sec = finfo->loop_end.tv_sec - finfo->loop_start.tv_sec;
        long diff_time_nsec = finfo->loop_end.tv_nsec - finfo->loop_start.tv_nsec;
        float diff_time_all = diff_time_sec + (diff_time_nsec / NANO_CONVERSION_F);

        /* prevent division-by-zero and unreliable rates */
        if (diff_time_all >= 0.01f) {
            /* calculate current xfer rate */
            result = diff_pos;
            result /= diff_time_all;
        }
    }

    /* save current rate before calculating the average */
    xfer_rate_index = finfo->xfer_rate_history.next_index++;
    new_xfer_rate = result;

    /* calculate average xfer rate using values from the past */
    for (size_t index = 0; index < XFER_RATES_LENGTH; ++index) {
        result += finfo->xfer_rate_history.xfer_rates[index];
    }
    result /= XFER_RATES_LENGTH + 1;

    /* update history */
    finfo->xfer_rate_history.xfer_rates[xfer_rate_index % XFER_RATES_LENGTH] = new_xfer_rate;

    /* do not show every updated rate; can be very annoying if values are changing e.g. between 10.0 and 9.0 */
    if (finfo->xfer_rate_history.next_index - 1 < XFER_RATES_LENGTH /* except right after startup */ ||
        (finfo->xfer_rate_history.next_index - 1) % (XFER_RATES_LENGTH / 2) == 0)
    {
        finfo->xfer_rate_history.last_reported_rate = result;
    }

    /* print it to the output buffer after "prettified" and units appended */
    prettify_with_units(finfo->xfer_rate_history.last_reported_rate, out, sizeof out);
    add_printable_buf(term, finfo, "[%s/s]", out);
}

static void show_progressbar(struct terminal const * const term,
                             struct file_info * const finfo)
{
    char buf[BUFSIZ];
    size_t remaining_len;

    assert(finfo);

    remaining_len = remaining_printable_chars(term, finfo);
    if (remaining_len < 16 || remaining_len >= sizeof buf) {
        return;
    }
    /* do not show progressbar if someone writes/appendes to this file */
    if (finfo->max_size == 0 || finfo->current_position > finfo->max_size) {
        return;
    }

    float progress = (float)finfo->current_position / finfo->max_size;
    add_printable_buf(term, finfo, "[%.2f%%]", progress * 100.0f);

    remaining_len = remaining_printable_chars(term, finfo);

    float printable_progress = progress * (remaining_len - 2.);
    memset(buf, '-', remaining_len - 2);
    memset(buf, '#', (size_t)printable_progress);
    buf[remaining_len - 2] = '\0';
    add_printable_buf(term, finfo, "[%s]", buf);
}

static int nsleep(unsigned long long int nanosecs)
{
    struct timespec tim;
    tim.tv_sec = 0;
    tim.tv_nsec = nanosecs;

    return nanosleep(&tim , NULL);
}

struct filepath {
    int wanted;
    int proc_fd_fd;
    int proc_fdinfo_fd;
    char pid[32];
    char fd[32];
    char cmdline[MAX_CMDLINE_LEN];
    struct filepath * next;
};

static void printf_cmd_info(size_t index, struct filepath const * const fp)
{
    if (index) {
        printf("[%zu]['/proc/%s/fd/%s']['%s']\n", index, fp->pid, fp->fd, fp->cmdline);
    } else {
        printf("['/proc/%s/fd/%s']['%s']\n", fp->pid, fp->fd, fp->cmdline);
    }
}

static void open_procfs_paths(struct filepath * const cur)
{
    cur->proc_fd_fd = open_in_procfs(cur->pid, cur->fd, PROC_SUBDIR_FD);
    if (cur->proc_fd_fd < 0) {
        perror("open proc_fd");
        exit(EXIT_FAILURE);
    }

    cur->proc_fdinfo_fd = open_in_procfs(cur->pid, cur->fd, PROC_SUBDIR_FDINFO);
    if (cur->proc_fdinfo_fd < 0) {
        perror("open proc_fdinfo");
        exit(EXIT_FAILURE);
    }

    cur->wanted = 1;
}

static int choose_filepath(struct filepath * const filepath)
{
    struct filepath * next;
    size_t choice = 0, menu_index = 0;

    puts("Choose file to watch ..\n");
    next = filepath;
    while (next && ++menu_index) {
        struct filepath * const cur = next;
        next = next->next;

        read_proc_cmdline(cur->cmdline, sizeof cur->cmdline, cur->pid);
        printf_cmd_info(menu_index, cur);
    }

    printf("\nYour choice: ");
    if (scanf("%zu", &choice) != 1 || choice > menu_index || !choice) {
        return 1;
    }

    next = filepath;
    menu_index = 0;
    while (next && ++menu_index) {
        struct filepath * const cur = next;
        next = next->next;

        if (menu_index == choice) {
            open_procfs_paths(cur);
        }
    }

    return 0;
}

void signal_handler(int signo)
{
    switch (signo) {
        case SIGWINCH:
            get_terminal_dimensions(&term);
            term.dimensions_changed = 1;
            break;
        default:
            break;
    }
}

int main(int argc, char ** argv)
{
    struct filtered_dir_entries proc_pid_entries = {};
    struct filtered_dir_entries proc_fd_entries = {};
    ssize_t target_filepath_len;
    char file_realpath[BUFSIZ] = {};
    size_t found_targets = 0;
    struct filepath * paths = NULL;
    struct filepath ** paths_next = &paths;

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
            ssize_t realpath_used = realpath_procfs_fd("/proc",
                proc_pid_entries.entries[i]->d_name,
                proc_fd_entries.entries[j]->d_name,
                &file_realpath[0], sizeof file_realpath - 1);
            if (realpath_used <= 0) {
                continue;
            }
            file_realpath[realpath_used] = '\0';
            if (realpath_used == target_filepath_len) {
                if (!strncmp(argv[1], file_realpath, realpath_used)) {
                    *paths_next = (struct filepath *) calloc(1, sizeof(**paths_next));
                    if (!*paths_next) {
                        continue;
                    }

                    if (snprintf((*paths_next)->pid, sizeof (*paths_next)->pid, "%s",
                                 proc_pid_entries.entries[i]->d_name) <= 0)
                    {
                        continue;
                    }
                    if (snprintf((*paths_next)->fd, sizeof (*paths_next)->fd,
                                 "%s", proc_fd_entries.entries[j]->d_name) <= 0)
                    {
                        continue;
                    }

                    found_targets++;
                    paths_next = &(*paths_next)->next;
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
        printf_cmd_info(0, paths);
        open_procfs_paths(paths);
    } else if (choose_filepath(paths)) {
        fprintf(stderr, "%s: user did not choose a valid filepath\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    struct file_info finfo = {};
    struct filepath * next = paths;
    while (next) {
        struct filepath * const cur = next;
        next = next->next;

        if (!cur->wanted) {
            continue;
        }

        if (setup_file_info(&finfo, cur->proc_fd_fd, cur->proc_fdinfo_fd)) {
            exit(EXIT_FAILURE);
        }

        close(cur->proc_fd_fd);
        cur->proc_fd_fd = -1;
    }

    get_terminal_dimensions(&term);
    if (signal(SIGWINCH, signal_handler) == SIG_ERR) {
        perror("signal");
        exit(EXIT_FAILURE);
    }

    loop_start(&finfo);
    while (!read_and_parse_fd_pos(&finfo)) {
        reset_terminal_output_buffer(&finfo);
        if (term.dimensions_changed) {
            term.dimensions_changed = 0;
            finfo.terminal_output.buf[0] = '\n';
        }

        loop_end(&finfo);
        show_positions(&term, &finfo);
        show_rate(&term, &finfo);
        show_progressbar(&term, &finfo);
        fillup_remaining(&term, &finfo);
        loop_start(&finfo);

        int print_len = finfo.terminal_output.printable_chars +
                        finfo.terminal_output.unprintable_chars;

        assert(print_len + 1 <= (int)sizeof(finfo.terminal_output.buf));
        assert(finfo.terminal_output.buf[print_len] == '\0');
        assert(print_len == (int)strlen(finfo.terminal_output.buf));
        assert(finfo.terminal_output.buf[0] == '\r' ||
               finfo.terminal_output.buf[0] == '\n');

        printf("%.*s", print_len, finfo.terminal_output.buf);
        fflush(stdout);
        nsleep(150000000L);
    }
    /* Program exited!? We do not know if the file was fully read/written. Tell the user. */
    if (finfo.current_position < finfo.max_size) {
        reset_terminal_output_buffer(&finfo);
        if (term.dimensions_changed) {
            term.dimensions_changed = 0;
            finfo.terminal_output.buf[0] = '\n';
        }

        show_positions(&term, &finfo);
        show_rate(&term, &finfo);
        add_printable_buf(&term, &finfo, "[%s]", "PROGRAM EXITED!");
        show_progressbar(&term, &finfo);
        fillup_remaining(&term, &finfo);
        int print_len = finfo.terminal_output.printable_chars +
                        finfo.terminal_output.unprintable_chars;
        printf("%.*s", print_len, finfo.terminal_output.buf);
        fflush(stdout);
    }
    puts("");

    while (paths) {
        struct filepath * cur = paths;
        paths = paths->next;

        if (cur->wanted) {
            close(cur->proc_fdinfo_fd);
            cur->proc_fdinfo_fd = -1;
        }
        free(cur);
    }
}
