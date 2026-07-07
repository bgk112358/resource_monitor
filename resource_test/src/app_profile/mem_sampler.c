/**
 * mem_sampler.c — 内存 RSS / PSS / USS 采样模块
 *
 * RSS: 读 /proc/PID/status 的 VmRSS (全部物理内存)
 * PSS: 遍历 /proc/PID/smaps 的 Pss 字段求和 (共享按比例分摊)
 * USS: 遍历 /proc/PID/smaps 的 Private_Clean + Private_Dirty 求和 (独占内存)
 *
 * 接口: mem_sampler_run(pid, count, interval, snap, csv)
 */
#include "sampler.h"

/* 读取 /proc/PID/smaps, 返回 PSS 和 USS (单位 kB) */
static void read_smaps(pid_t pid, long *pss_out, long *uss_out) {
    char path[MAX_PATH], line[MAX_LINE];
    snprintf(path, sizeof(path), "/proc/%d/smaps", pid);
    FILE *f = fopen(path, "r");
    if (!f) { *pss_out = -1; *uss_out = -1; return; }

    long pss = 0, uss = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "Pss:", 4) == 0) {
            pss += strtol(line + 4, NULL, 10);
        } else if (strncmp(line, "Private_Clean:", 14) == 0) {
            uss += strtol(line + 14, NULL, 10);
        } else if (strncmp(line, "Private_Dirty:", 14) == 0) {
            uss += strtol(line + 14, NULL, 10);
        }
    }
    fclose(f);
    *pss_out = pss;
    *uss_out = uss;
}

int mem_sampler_run(pid_t pid, int count, int interval_sec,
                     ResourceSnapshot *snap, FILE *csv) {
    long rss_sum = 0, pss_sum = 0, uss_sum = 0;
    int samples = 0;

    for (int i = 1; i <= count && g_running; i++) {
        /* RSS */
        char path[MAX_PATH], line[MAX_LINE];
        snprintf(path, sizeof(path), "/proc/%d/status", pid);
        long rss = -1;
        FILE *f = fopen(path, "r");
        if (f) {
            while (fgets(line, sizeof(line), f)) {
                if (strncmp(line, "VmRSS:", 6) == 0) {
                    sscanf(line + 6, "%ld", &rss); break;
                }
            }
            fclose(f);
        }

        /* PSS / USS */
        long pss = -1, uss = -1;
        read_smaps(pid, &pss, &uss);

        fprintf(csv, "%d,%ld,%ld,%ld\n", i,
                rss >= 0 ? rss : 0,
                pss >= 0 ? pss : 0,
                uss >= 0 ? uss : 0);

        if (rss >= 0) rss_sum += rss;
        if (pss >= 0) pss_sum += pss;
        if (uss >= 0) uss_sum += uss;
        samples++;

        sleep(interval_sec);
        { char _p[64]; snprintf(_p, sizeof(_p), "/proc/%d", pid);
          struct stat _st; if (stat(_p, &_st) != 0 || !S_ISDIR(_st.st_mode)) break; }
    }

    snap->rss_kb      = samples > 0 ? rss_sum / samples : 0;
    snap->pss_kb      = samples > 0 ? pss_sum / samples : 0;
    snap->uss_kb      = samples > 0 ? uss_sum / samples : 0;
    snap->mem_samples = samples;
    return samples > 0 ? 0 : -1;
}
