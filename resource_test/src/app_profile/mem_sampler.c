/**
 * mem_sampler.c — 内存 RSS 采样模块
 *
 * 读取 /proc/PID/status 的 VmRSS, 每 interval_sec 采样一次,
 * 写入 CSV, 返回均值。
 *
 * 接口: mem_sampler_run(pid, count, interval, snap, csv)
 */
#include "sampler.h"

int mem_sampler_run(pid_t pid, int count, int interval_sec,
                     ResourceSnapshot *snap, FILE *csv) {
    long sum = 0;
    int samples = 0;

    for (int i = 1; i <= count && g_running; i++) {
        /* 读 /proc/PID/status */
        char path[MAX_PATH], line[MAX_LINE];
        snprintf(path, sizeof(path), "/proc/%d/status", pid);
        long rss = -1;
        FILE *f = fopen(path, "r");
        if (f) {
            while (fgets(line, sizeof(line), f)) {
                if (strncmp(line, "VmRSS:", 6) == 0) {
                    sscanf(line + 6, "%ld", &rss);
                    break;
                }
            }
            fclose(f);
        }
        /* 读取失败时 rss 保持 -1, 不参与均值计算 */
        if (rss >= 0) {
            fprintf(csv, "%d,%ld\n", i, rss);
            sum += rss;
        } else {
            fprintf(csv, "%d,N/A\n", i);
        }
        samples++;

        sleep(interval_sec);
        if (kill(pid, 0) != 0) break;
    }

    snap->rss_kb      = samples > 0 ? sum / samples : 0;
    snap->mem_samples = samples;
    return samples > 0 ? 0 : -1;
}
