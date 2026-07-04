/**
 * main.c — 应用资源画像采集 (orchestrator, 并发版)
 *
 * 所有采样模块在同一时间窗口内并发执行:
 *   - CPU:  每秒 1 次 (高频)
 *   - 内存: 每 N 秒 1 次 (低频)
 
 *   - 线程: 每 N 秒 1 次 (低频)
 *   - IO:   每 N 秒 1 次 (低频, 计算吞吐量)
 *
 * 用法:
 *   ./app_profile <PID> [duration_sec] [output_dir]
 */
#define _GNU_SOURCE
#include "sampler.h"
#include "sign.h"
#include <pthread.h>

/* ── 并发采样参数 ──────────────────────────────── */
typedef struct {
    pid_t  pid;
    int    duration;       /* 总时间窗口 (秒), 所有采样器共用 */
    ResourceSnapshot *snap;
    FILE   *csv;
} SamplerArgs;

/* ── CPU 线程入口 ──────────────────────────────── */
static void *cpu_thread(void *arg) {
    SamplerArgs *a = (SamplerArgs *)arg;
    cpu_sampler_run(a->pid, a->duration, a->snap, a->csv);
    return NULL;
}

/* ── 内存线程入口 ──────────────────────────────── */
static void *mem_thread(void *arg) {
    SamplerArgs *a = (SamplerArgs *)arg;
    int interval = a->duration / MEM_SAMPLES;
    if (interval < 1) interval = 1;
    mem_sampler_run(a->pid, MEM_SAMPLES, interval, a->snap, a->csv);
    return NULL;
}

/* ── 线程+FD 线程入口 ──────────────────────────── */
static void *thrfd_thread(void *arg) {
    SamplerArgs *a = (SamplerArgs *)arg;
    int interval = a->duration / MEM_SAMPLES;
    if (interval < 1) interval = 1;
    thrfd_sampler_run(a->pid, MEM_SAMPLES, interval, a->snap, a->csv);
    return NULL;
}

/* ── IO 线程入口 ────────────────────────────────── */
static void *io_thread(void *arg) {
    SamplerArgs *a = (SamplerArgs *)arg;
    int interval = a->duration / MEM_SAMPLES;
    if (interval < 1) interval = 1;
    io_sampler_run(a->pid, MEM_SAMPLES, interval, a->snap, a->csv);
    return NULL;
}

/* ── 主函数 ────────────────────────────────────── */
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
    printf("📊 采集进程 PID=%d 的资源数据, 持续 %ds (并发)\n", pid, duration);
    printf("   输出目录: %s\n", outdir);

    char proc_name[PROC_NAME_MAX] = "unknown";
    proc_get_name(pid, proc_name, sizeof(proc_name));

    /* 打开 CSV */
    FILE *fcpu = report_csv_open(outdir, "cpu.csv",  "sec,cpu_percent");
    FILE *fmem = report_csv_open(outdir, "mem.csv",  "sample,rss_kb");
    FILE *fthr = report_csv_open(outdir, "threads_fd.csv", "sample,threads,fd_count");
    FILE *fio  = report_csv_open(outdir, "io.csv",   "sample,read_kb,write_kb");

    /* 准备采样快照和参数 */
    ResourceSnapshot snap_cpu, snap_mem, snap_thr, snap_io;
    memset(&snap_cpu, 0, sizeof(snap_cpu));
    memset(&snap_mem, 0, sizeof(snap_mem));
    memset(&snap_thr, 0, sizeof(snap_thr));
    memset(&snap_io, 0, sizeof(snap_io));

    SamplerArgs cpu_args = { pid, duration, &snap_cpu, fcpu };
    SamplerArgs mem_args = { pid, duration, &snap_mem, fmem };
    SamplerArgs thr_args = { pid, duration, &snap_thr, fthr };
    SamplerArgs io_args  = { pid, duration, &snap_io,  fio  };

    /* ── 启动 4 个采样线程, 并发运行 duration 秒 ── */
    pthread_t t_cpu, t_mem, t_thr, t_io;
    if (fcpu) pthread_create(&t_cpu, NULL, cpu_thread,   &cpu_args);
    if (fmem) pthread_create(&t_mem, NULL, mem_thread,   &mem_args);
    if (fthr) pthread_create(&t_thr, NULL, thrfd_thread, &thr_args);
    if (fio)  pthread_create(&t_io,  NULL, io_thread,    &io_args);

    /* 等待所有线程完成 (在 duration 秒内并发结束) */
    if (fcpu) pthread_join(t_cpu, NULL);
    if (fmem) pthread_join(t_mem, NULL);
    if (fthr) pthread_join(t_thr, NULL);
    if (fio)  pthread_join(t_io,  NULL);

    fclose(fcpu); fclose(fmem); fclose(fthr); fclose(fio);

    /* ── 签名为防伪 ──────────────────────────── */
    char csv_path[MAX_PATH];
    snprintf(csv_path, sizeof(csv_path), "%s/cpu.csv", outdir);        sign_file(csv_path);
    snprintf(csv_path, sizeof(csv_path), "%s/mem.csv", outdir);        sign_file(csv_path);
    snprintf(csv_path, sizeof(csv_path), "%s/threads_fd.csv", outdir); sign_file(csv_path);
    snprintf(csv_path, sizeof(csv_path), "%s/io.csv", outdir);         sign_file(csv_path);

    /* ── 合并快照 ──────────────────────────── */
    ResourceSnapshot snap;
    snap.cpu_avg     = snap_cpu.cpu_avg;
    snap.cpu_peak    = snap_cpu.cpu_peak;
    snap.cpu_samples = snap_cpu.cpu_samples;
    snap.rss_kb      = snap_mem.rss_kb;
    snap.mem_samples = snap_mem.mem_samples;
    snap.threads     = snap_thr.threads;
    snap.fds         = snap_thr.fds;
    snap.thrfd_samples = snap_thr.thrfd_samples;
    snap.io_read_kb  = snap_io.io_read_kb;
    snap.io_write_kb = snap_io.io_write_kb;
    snap.io_samples  = snap_io.io_samples;
    snap.child_count = proc_count_children(pid);

    report_write(outdir, proc_name, pid, duration, &snap);
    free(outdir);
    return 0;
}
