#define _DEFAULT_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

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
        perror("scandir");
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

int main(int argc, char **argv)
{
    struct filtered_dir_entries proc_pid_entries = {};
    struct filtered_dir_entries proc_fd_entries = {};
    ssize_t realpath_used;
    char realpath[BUFSIZ];

    (void) argc;
    (void) argv;

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
                                &realpath[0], sizeof realpath);
            if (realpath_used < 0) {
                continue;
            }
            printf("%s: __%.*s__\n", proc_pid_entries.entries[i]->d_name, (int)realpath_used, realpath);
        }
        free_filtered_dir_entries(&proc_fd_entries);
    }
    free_filtered_dir_entries(&proc_pid_entries);
}
