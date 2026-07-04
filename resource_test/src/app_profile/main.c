/**
 * main.c — 应用资源画像采集 (orchestrator)
 *
 * 仅负责: 参数解析 → 进程验证 → 逐个调用采样模块 → 生成报告。
 * 所有具体逻辑在 cpu_sampler / mem_sampler / thrfd_sampler / io_sampler / report 中。
 *
 * 用法:
 *   ./app_profile <PID> [duration_sec] [output_dir]
 */
#include "sampler.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "用法: %s <PID> [duration_sec] [output_dir]\n", argv[0]);
        fprintf(stderr, "示例: %s 12345 30 /tmp/my_profile\n", argv[0]);
        return 1;
    }

    pid_t pid = (pid_t)atoi(argv[1]);
    int duration = (argc > 2) ? atoi(argv[2]) : DEFAULT_DURATION;
    const char *out_prefix = (argc > 3) ? argv[3] : NULL;

    if (proc_validate(pid) != 0) {
        fprintf(stderr, "❌ 进程 PID=%d 不存在\n", pid);
        return 1;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    char *outdir = report_mk_outdir(out_prefix, pid);
    printf("📊 采集进程 PID=%d 的资源数据, 持续 %ds\n", pid, duration);
    printf("   输出目录: %s\n", outdir);

    /* 进程名 */
    char proc_name[PROC_NAME_MAX] = "unknown";
    proc_get_name(pid, proc_name, sizeof(proc_name));

    /* 打开 CSV */
    FILE *fcpu = report_csv_open(outdir, "cpu.csv",  "sec,cpu_percent");
    FILE *fmem = report_csv_open(outdir, "mem.csv",  "sample,rss_kb");
    FILE *fthr = report_csv_open(outdir, "threads_fd.csv", "sample,threads,fd_count");
    FILE *fio  = report_csv_open(outdir, "io.csv",   "sample,read_kb,write_kb");

    /* 初始化快照 */
    ResourceSnapshot snap;
    memset(&snap, 0, sizeof(snap));

    /* ── 调用各采样模块 ─────────────────────── */
    /* CPU: 每秒采样 duration 次 */
    if (fcpu) cpu_sampler_run(pid, duration, &snap, fcpu);
    fclose(fcpu);

    int n_intervals = MEM_SAMPLES;
    int interval = duration / n_intervals;
    if (interval < 1) interval = 1;

    /* 内存 */
    if (fmem) mem_sampler_run(pid, n_intervals, interval, &snap, fmem);
    fclose(fmem);

    /* 线程 + FD */
    if (fthr) thrfd_sampler_run(pid, n_intervals, interval, &snap, fthr);
    fclose(fthr);

    /* IO */
    if (fio) io_sampler_run(pid, n_intervals, interval, &snap, fio);
    fclose(fio);

    /* 子进程 */
    snap.child_count = proc_count_children(pid);

    /* ── 生成报告 ──────────────────────────── */
    report_write(outdir, proc_name, pid, duration, &snap);

    free(outdir);
    return 0;
}
