/**
 * cpu_sampler.c — CPU 使用率采样模块
 *
 * 读取 /proc/PID/stat 的 utime/stime, 每秒采样一次,
 * 计算 CPU 占用百分比, 写入 CSV。
 *
 * 接口: cpu_sampler_run(pid, duration, snap, csv)
 */
#include "sampler.h"

/* 读 /proc/PID/stat 的 utime + stime (字段 14, 15)
 *
 * /proc/PID/stat 格式: pid (comm) state ppid ... utime stime ...
 * 关键: 字段2 (comm) 可能含空格, 必须跳过括号内容后再计空格。
 */
static int read_ticks(pid_t pid, unsigned long long *utime, unsigned long long *stime) {
    char path[MAX_PATH], line[MAX_LINE];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(line, sizeof(line), f)) { fclose(f); return -1; }
    fclose(f);

    /* 跳过 "(comm)" → 找到 ')' 后 +2 到 state 字段 */
    char *p = strrchr(line, ')');
    if (!p) return -1;
    p += 2;  /* 跳过 ") " → 指向 field 3 (state) */

    /* 再跳过 11 个空格到达 field 14 (utime) */
    for (int field = 3; field < 14; field++) {
        p = strchr(p, ' ');
        if (!p) return -1;
        p++;
    }
    *utime = strtoull(p, &p, 10);
    *stime = strtoull(p + 1, NULL, 10);
    return 0;
}

int cpu_sampler_run(pid_t pid, int duration, ResourceSnapshot *snap, FILE *csv) {
    unsigned long long u1, s1, u2, s2;
    long ticks_per_sec = sysconf(_SC_CLK_TCK);
    if (ticks_per_sec <= 0) ticks_per_sec = 100;

    double sum = 0, peak = 0;
    int samples = 0;

    for (int sec = 1; sec <= duration && g_running; sec++) {
        struct timespec t1, t2;
        if (read_ticks(pid, &u1, &s1) != 0) break;
        clock_gettime(CLOCK_MONOTONIC, &t1);
        sleep(1);
        clock_gettime(CLOCK_MONOTONIC, &t2);
        if (read_ticks(pid, &u2, &s2) != 0) break;

        /* 用实际经过时间计算 CPU%, 而非假设 sleep(1)=1s */
        double elapsed = (t2.tv_sec - t1.tv_sec) + (t2.tv_nsec - t1.tv_nsec) / 1e9;
        if (elapsed <= 0.0) continue;

        unsigned long long total1 = u1 + s1;
        unsigned long long total2 = u2 + s2;
        if (total2 < total1) continue;

        double pct = (double)(total2 - total1) / (double)ticks_per_sec / elapsed * 100.0;

        fprintf(csv, "%d,%.1f\n", sec, pct);
        fflush(csv);
        sum += pct;
        if (pct > peak) peak = pct;
        samples++;
    }

    snap->cpu_avg     = samples > 0 ? sum / samples : 0;
    snap->cpu_peak    = peak;
    snap->cpu_samples = samples;
    return samples > 0 ? 0 : -1;
}
