/**
 * proc_reader.h — /proc 文件系统读取函数 (提取自 app_profile + stability_monitor)
 *
 * 所有函数接收 pid_t 参数，返回读取到的值或错误码。
 * 从 main.c 中提取，便于单元测试复用。
 */
#ifndef PROC_READER_H
#define PROC_READER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── CPU: 读取 /proc/PID/stat 的 utime + stime ────────── */
static inline int proc_read_cpu_ticks(pid_t pid, unsigned long long *utime, unsigned long long *stime) {
    char path[512], line[1024];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(line, sizeof(line), f)) { fclose(f); return -1; }
    fclose(f);

    char *p = line;
    /* fields: 1=pid ... 14=utime 15=stime */
    for (int field = 1; field < 14; field++) {
        p = strchr(p, ' ');
        if (!p) return -1;
        p++;
    }
    *utime = strtoull(p, &p, 10);
    *stime = strtoull(p + 1, NULL, 10);
    return 0;
}

/* ── RSS: 读取 /proc/PID/status 的 VmRSS ────────────── */
static inline long proc_read_rss_kb(pid_t pid) {
    char path[512], line[1024];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    long rss = -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line + 6, "%ld", &rss);
            break;
        }
    }
    fclose(f);
    return rss;
}

/* ── Threads: 计数 /proc/PID/task ────────────────────── */
static inline int proc_count_threads(pid_t pid) {
    char path[512];
    snprintf(path, sizeof(path), "/proc/%d/task", pid);
    DIR *d = opendir(path);
    if (!d) return -1;
    int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL)
        if (de->d_name[0] != '.') n++;
    closedir(d);
    return n;
}

/* ── FDs: 计数 /proc/PID/fd ──────────────────────────── */
static inline int proc_count_fds(pid_t pid) {
    char path[512];
    snprintf(path, sizeof(path), "/proc/%d/fd", pid);
    DIR *d = opendir(path);
    if (!d) return -1;
    int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL)
        if (de->d_name[0] != '.') n++;
    closedir(d);
    return n;
}

/* ── IO: 读取 /proc/PID/io ────────────────────────────── */
static inline int proc_read_io(pid_t pid, long *read_bytes, long *write_bytes) {
    char path[512], line[1024];
    snprintf(path, sizeof(path), "/proc/%d/io", pid);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    *read_bytes = *write_bytes = -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "read_bytes:", 11) == 0)  sscanf(line + 11, "%ld", read_bytes);
        if (strncmp(line, "write_bytes:", 12) == 0) sscanf(line + 12, "%ld", write_bytes);
    }
    fclose(f);
    return (*read_bytes >= 0 && *write_bytes >= 0) ? 0 : -1;
}

/* ── Children: 读取 /proc/PID/task/PID/children ───────── */
static inline int proc_count_children(pid_t pid) {
    char path[512], line[4096];
    snprintf(path, sizeof(path), "/proc/%d/task/%d/children", pid, pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    if (!fgets(line, sizeof(line), f)) { fclose(f); return 0; }
    fclose(f);
    if (line[0] == '\0' || line[0] == '\n') return 0;
    int n = 0;
    for (char *p = line; *p; p++) if (*p == ' ') n++;
    return n + 1;
}

/* ── CPU%: 计算两次采样间的 CPU 占用百分比 ───────────── */
static inline double proc_calc_cpu_pct(unsigned long long utime1, unsigned long long stime1,
                                        unsigned long long utime2, unsigned long long stime2,
                                        long ticks_per_sec) {
    if (ticks_per_sec <= 0) ticks_per_sec = 100;
    unsigned long long total1 = utime1 + stime1;
    unsigned long long total2 = utime2 + stime2;
    /* handle clock wrap-around or time going backwards (unsigned underflow) */
    if (total2 < total1) return 0.0;
    unsigned long long delta = total2 - total1;
    double pct = (double)delta / (double)ticks_per_sec * 100.0;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

#ifdef __cplusplus
}
#endif

#endif /* PROC_READER_H */
