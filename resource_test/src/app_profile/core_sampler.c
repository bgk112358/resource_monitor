/**
 * core_sampler.c — 系统 per-core CPU 使用率采样
 *
 * 读取 /proc/stat 的 cpu0/cpu1/... 行, 自动检测核心数,
 * 每秒计算各核心的 busy% (100 - idle%), 写入 CSV。
 *
 * CSV 格式: sec,core0%,core1%,...
 *
 * 接口: core_sampler_run(duration, snap, csv)
 */
#include "sampler.h"

#define MAX_CORES 16

/* 解析 /proc/stat 第一行及各 cpuN 行, 返回核心数 */
static int read_core_stats(unsigned long long *prev_idle, unsigned long long *prev_total,
                            int max_cores) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return 0;

    char line[MAX_LINE];
    int cores = 0;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "cpu", 3) != 0) break;  /* 非 cpu 行则停止 */
        if (line[3] == ' ') continue;              /* 跳过总计行 "cpu " */

        /* cpuN 行: 解析 user nice system idle iowait irq softirq steal */
        unsigned long long vals[8] = {0};
        char *p = line + 3;
        while (*p && *p != ' ') p++;  /* 跳过 "cpuN" */
        p++;
        for (int i = 0; i < 8 && *p; i++) {
            vals[i] = strtoull(p, &p, 10);
        }

        unsigned long long idle  = vals[3] + vals[4];  /* idle + iowait */
        unsigned long long total = vals[0] + vals[1] + vals[2] + vals[3] + vals[4] + vals[5] + vals[6] + vals[7];

        if (cores < max_cores) {
            /* 首次读取: 只存基线 */
            prev_idle[cores]  = idle;
            prev_total[cores] = total;
        }
        cores++;
    }
    fclose(f);
    return cores;
}

int core_sampler_run(int duration, int *out_cores, FILE *csv) {
    unsigned long long prev_idle[MAX_CORES]  = {0};
    unsigned long long prev_total[MAX_CORES] = {0};
    unsigned long long cur_idle[MAX_CORES];
    unsigned long long cur_total[MAX_CORES];

    /* 首次读取: 获取核心数 + 基线 */
    int n_cores = read_core_stats(prev_idle, prev_total, MAX_CORES);
    if (n_cores <= 0) return -1;

    /* 写入 CSV 头 */
    fprintf(csv, "sec");
    for (int c = 0; c < n_cores; c++) fprintf(csv, ",core%d%%", c);
    fprintf(csv, "\n");

    /* 每秒采样 */
    for (int sec = 1; sec <= duration && g_running; sec++) {
        sleep(1);

        /* 读取当前值 */
        int nc = read_core_stats(cur_idle, cur_total, MAX_CORES);
        if (nc <= 0) break;
        if (nc < n_cores) nc = n_cores;

        fprintf(csv, "%d", sec);
        for (int c = 0; c < n_cores; c++) {
            unsigned long long delta_total = cur_total[c] - prev_total[c];
            unsigned long long delta_idle  = cur_idle[c]  - prev_idle[c];
            int pct = 0;
            if (delta_total > 0) {
                pct = (int)((delta_total - delta_idle) * 100 / delta_total);
                if (pct < 0) pct = 0; if (pct > 100) pct = 100;
            }
            fprintf(csv, ",%d", pct);
            prev_idle[c]  = cur_idle[c];
            prev_total[c] = cur_total[c];
        }
        fprintf(csv, "\n");
        fflush(csv);
    }

    *out_cores = n_cores;
    return 0;
}
