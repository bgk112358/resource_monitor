/**
 * sampler.h — 资源画像采集器公共类型与常量
 *
 * 每个采样模块独立编译，通过统一的 ResourceSnapshot 汇总结果。
 * 模块接口: xxx_sample(pid, snapshot) — 只采样一次，不持有状态。
 */
#ifndef SAMPLER_H
#define SAMPLER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 配置常量 ──────────────────────────────────── */
#define DEFAULT_DURATION    30
#define DEFAULT_OUTDIR      "/tmp/app_profile"
#define MAX_PATH            512
#define MAX_LINE            1024
#define PROC_NAME_MAX       256

/* ── 全局运行标志 ──────────────────────────────── */
extern volatile int g_running;
void sig_handler(int sig);

/* ── 资源快照 (汇总用) ──────────────────────────── */
typedef struct {
    double cpu_avg;
    double cpu_peak;
    int    cpu_samples;

    long   rss_kb;
    int    mem_samples;

    int    threads;
    int    fds;
    int    thrfd_samples;

    long   io_read_kb;
    long   io_write_kb;
    int    io_samples;

    int    child_count;
} ResourceSnapshot;

/* ── CSV 文件句柄 ──────────────────────────────── */
typedef struct {
    FILE *cpu_csv;
    FILE *mem_csv;
    FILE *thr_csv;
    FILE *io_csv;
} CsvFiles;

/* ── 模块函数声明 ──────────────────────────────── */

/* proc_reader: 进程基本信息 */
int   proc_validate(pid_t pid);
int   proc_get_name(pid_t pid, char *name, size_t size);
int   proc_count_children(pid_t pid);

/* cpu_sampler: CPU 使用率采样 */
int   cpu_sampler_run(pid_t pid, int duration, ResourceSnapshot *snap, FILE *csv);

/* mem_sampler: 内存 RSS 采样 */
int   mem_sampler_run(pid_t pid, int count, int interval_sec, ResourceSnapshot *snap, FILE *csv);

/* thread_fd_sampler: 线程数 + FD 数采样 */
int   thrfd_sampler_run(pid_t pid, int count, int interval_sec, ResourceSnapshot *snap, FILE *csv);

/* io_sampler: 磁盘 IO 读写量采样 */
int   io_sampler_run(pid_t pid, int count, int interval_sec, ResourceSnapshot *snap, FILE *csv);

/* report: 输出目录 / CSV / 报告生成 */
char *report_mk_outdir(const char *prefix, pid_t pid);
FILE *report_csv_open(const char *dir, const char *name, const char *header);
int   report_write(const char *dir, const char *proc_name, pid_t pid,
                   int duration, const ResourceSnapshot *snap);

#ifdef __cplusplus
}
#endif

#endif /* SAMPLER_H */
