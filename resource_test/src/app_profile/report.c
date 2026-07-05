/**
 * report.c — CSV 文件创建 + 汇总报告生成模块
 *
 * 接口: report_mk_outdir / report_csv_open / report_write
 */
#include "sampler.h"

char *report_mk_outdir(const char *prefix, pid_t pid) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", tm);
    char *dir = malloc(MAX_PATH);
    snprintf(dir, MAX_PATH, "%s_%d_%s",
             prefix ? prefix : DEFAULT_OUTDIR, pid, ts);
    mkdir(dir, 0755);
    return dir;
}

FILE *report_csv_open(const char *dir, const char *name, const char *header) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    if (f) fprintf(f, "%s\n", header);
    return f;
}

int report_write(const char *dir, const char *proc_name, pid_t pid,
                  int duration, const ResourceSnapshot *snap) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/report.txt", dir);
    FILE *f = fopen(path, "w");
    if (!f) return -1;

    fprintf(f,
        "========================================\n"
        " TBox 应用资源画像报告 (C Profiler)\n"
        "========================================\n"
        "进程名称:  %s\n"
        "PID:       %d\n"
        "采集时长:  %ds\n"
        "采集时间:  %s"
        "\n"
        "── CPU ──────────────────────────────\n"
        "  平均占用:  %.1f%% (单核)\n"
        "  峰值占用:  %.1f%%\n"
        "  采样次数:  %d\n"
        "\n"
        "── 内存 ─────────────────────────────\n"
        "  RSS:       %ld KB (%.1f MB)\n"
        "  PSS:       %ld KB (%.1f MB)\n"
        "  USS:       %ld KB (%.1f MB)\n"
        "  采样次数:  %d\n"
        "\n"
        "── 线程 & FD ────────────────────────\n"
        "  线程数:    %d\n"
        "  文件描述符: %d\n"
        "  采样次数:  %d\n"
        "\n"
        "── IO 吞吐 ──────────────────────────\n"
        "  读吞吐:    %ld KB/s\n"
        "  写吞吐:    %ld KB/s\n"
        "  采样次数:  %d\n"
        "\n"
        "── 系统 ─────────────────────────────\n"
        "  CPU 核心数:  %d\n"
        "  网络接口数:  %d\n"
        "  子进程数:    %d\n"
        "========================================\n",
        proc_name, pid, duration, ctime(&(time_t){time(NULL)}),
        snap->cpu_avg, snap->cpu_peak, snap->cpu_samples,
        snap->rss_kb, snap->rss_kb / 1024.0,
        snap->pss_kb, snap->pss_kb / 1024.0,
        snap->uss_kb, snap->uss_kb / 1024.0,
        snap->mem_samples,
        snap->threads, snap->fds, snap->thrfd_samples,
        snap->io_read_kb, snap->io_write_kb, snap->io_samples,
        snap->core_count, snap->net_iface_count, snap->child_count);
    fclose(f);

    /* 打印到 stdout */
    printf("\n");
    FILE *r = fopen(path, "r");
    if (r) {
        char buf[4096];
        while (fgets(buf, sizeof(buf), r)) fputs(buf, stdout);
        fclose(r);
    }
    printf("\n✅ 完成! 所有数据保存在: %s/\n", dir);
    printf("   汇总报告: %s\n", path);
    return 0;
}
