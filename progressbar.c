#define _DEFAULT_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/stat.h>


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

    errno = 0;
    strtoul(ent->d_name, &end_ptr, 10);
    return (errno != 0 || *end_ptr == '\0');
}

static int search_procfs_fd(const char * const dir, const char * const subdir,
                              struct filtered_dir_entries * const entries)
{
    char buf[BUFSIZ];

    snprintf(buf, sizeof buf, "%s/%s/fd", dir ,subdir);
    return search_dir(buf, dirent_filter_only_numeric, entries);
}

static ssize_t realpath_procfs_fd(const char * const dir, const char * const pid,
                                  const char * const fd, char * dest, size_t siz)
{
    char buf[BUFSIZ];

    snprintf(buf, sizeof buf, "%s/%s/fd/%s", dir, pid, fd);
    return readlink(buf, dest, siz);
}

static void free_filtered_dir_entries(struct filtered_dir_entries * const entries)
{
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
};

static int setup_file_info(struct file_info * const finfo, int proc_fd_fd, int proc_fdinfo_fd)
{
    struct stat buf;

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

int main(int argc, char **argv)
{
    struct filtered_dir_entries proc_pid_entries = {};
    struct filtered_dir_entries proc_fd_entries = {};
    ssize_t realpath_used;
    size_t target_filepath_len;
    char file_realpath[BUFSIZ] = {};
    char pid[32];
    char fd[32];
    bool found_target = false;

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
            realpath_used = realpath_procfs_fd("/proc",
                                proc_pid_entries.entries[i]->d_name,
                                proc_fd_entries.entries[j]->d_name,
                                &file_realpath[0], sizeof file_realpath);
            if (realpath_used <= 0) {
                continue;
            }
            file_realpath[realpath_used] = '\0';
            if (realpath_used == target_filepath_len) {
                if (!strncmp(argv[1], file_realpath, realpath_used)) {
                    found_target = true;
                    if (snprintf(pid, sizeof pid, "%s", proc_pid_entries.entries[i]->d_name) <= 0) {
                        found_target = false;
                    }
                    if (snprintf(fd, sizeof fd, "%s", proc_fd_entries.entries[j]->d_name) <= 0) {
                        found_target = false;
                    }
                    break;
                }
            }
        }
        free_filtered_dir_entries(&proc_fd_entries);

        if (found_target) {
            break;
        }
    }
    free_filtered_dir_entries(&proc_pid_entries);

    if (!found_target) {
        fprintf(stderr, "%s: file '%s' not found in /proc\n", argv[0], argv[1]);
        exit(EXIT_FAILURE);
    }

    printf("PID: %s, FD: %s, FILE: '%.*s'\n", pid, fd, (int)realpath_used, file_realpath);

    int proc_fd_fd = open_in_procfs(pid, fd, PROC_SUBDIR_FD);
    if (proc_fd_fd < 0) {
        perror("open proc_fd");
        exit(EXIT_FAILURE);
    }

    int proc_fdinfo_fd = open_in_procfs(pid, fd, PROC_SUBDIR_FDINFO);
    if (proc_fdinfo_fd < 0) {
        perror("open proc_fdinfo");
        exit(EXIT_FAILURE);
    }

    struct file_info finfo;
    if (setup_file_info(&finfo, proc_fd_fd, proc_fdinfo_fd)) {
        exit(EXIT_FAILURE);
    }
    close(proc_fd_fd);

    while (!read_and_parse_fd_pos(&finfo)) {
        printf("\r[%ld..%ld] ", finfo.current_position, finfo.max_size);
        fflush(stdout);
        sleep(1);
    }
    puts("\n");

    close(finfo.proc_fdinfo_fd);
}
