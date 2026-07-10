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
#include <sys/wait.h>
#include <ctype.h>

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
    mem_sampler_run(a->pid, a->duration, 1, a->snap, a->csv);
    return NULL;
}

/* ── 线程+FD 线程入口 ──────────────────────────── */
static void *thrfd_thread(void *arg) {
    SamplerArgs *a = (SamplerArgs *)arg;
    thrfd_sampler_run(a->pid, a->duration, 1, a->snap, a->csv);
    return NULL;
}

/* ── IO 线程入口 ────────────────────────────────── */
static void *io_thread(void *arg) {
    SamplerArgs *a = (SamplerArgs *)arg;
    io_sampler_run(a->pid, a->duration, 1, a->snap, a->csv);
    return NULL;
}

/* ── Core 线程入口 ──────────────────────────────── */
typedef struct { int duration; int *out_count; FILE *csv; } CountArgs;
static void *core_thread(void *arg) {
    CountArgs *a = (CountArgs *)arg;
    core_sampler_run(a->duration, a->out_count, a->csv);
    return NULL;
}
static void *net_thread(void *arg) {
    CountArgs *a = (CountArgs *)arg;
    net_sampler_run(a->duration, a->out_count, a->csv);
    return NULL;
}

/* ── SELinux 探测与降级 ──────────────────────────── */
static void selinux_probe(void) {
    const char *enforce_path = "/sys/fs/selinux/enforce";
    if (access(enforce_path, F_OK) != 0) {
        printf("🔒 SELinux: 未启用\n");
        return;
    }
    FILE *f = fopen(enforce_path, "r");
    if (!f) {
        printf("⚠ SELinux: 无法读取状态\n");
        return;
    }
    char val = (char)fgetc(f);
    fclose(f);

    if (val == '0') {
        printf("🔒 SELinux: 已是 Permissive\n");
        return;
    }
    if (val != '1') {
        printf("⚠ SELinux: 未知状态\n");
        return;
    }

    /* Enforcing → 尝试切 Permissive */
    f = fopen(enforce_path, "w");
    if (f) {
        fputc('0', f);
        fclose(f);
        f = fopen(enforce_path, "r");
        if (f) {
            val = (char)fgetc(f);
            fclose(f);
            if (val == '0')
                printf("🔒 SELinux: Enforcing → 已切换为 Permissive\n");
            else
                printf("⚠ SELinux: 写入成功但状态未变 (%c)\n", val);
        }
    } else {
        printf("⚠ SELinux: Enforcing, 无法切换 (需 root), IO 数据可能为 0\n");
    }
}

/* ── 主函数 ────────────────────────────────────── */
#define APP_VERSION "26.0.2"
#define DEFAULT_WAIT_SEC 3

int main(int argc, char *argv[]) {
    if (argc == 2 && (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0)) {
        printf("app_profile v%s\n", APP_VERSION);
        return 0;
    }
    if (argc < 2) {
        fprintf(stderr, "用法: %s [-w wait_sec] <PID|程序路径> [duration_sec] [output_dir]\n", argv[0]);
        fprintf(stderr, "  -w N  启动后等待 N 秒再采样 (默认 %d, 路径模式有效)\n", DEFAULT_WAIT_SEC);
        fprintf(stderr, "示例: %s 12345 30 /tmp/my_profile\n", argv[0]);
        fprintf(stderr, "      %s /opt/tbox/bin/CloudGW 60 /tmp/test\n", argv[0]);
        fprintf(stderr, "      %s -w 10 /opt/tbox/bin/CloudGW 60 /tmp/test\n", argv[0]);
        return 1;
    }

    /* 解析 -w 参数 */
    int wait_sec = DEFAULT_WAIT_SEC;
    int arg_offset = 1;
    if (strcmp(argv[1], "-w") == 0) {
        if (argc < 4) { fprintf(stderr, "❌ -w 需要参数\n"); return 1; }
        wait_sec = atoi(argv[2]);
        if (wait_sec < 0) wait_sec = 0;
        arg_offset = 3;
    }

    pid_t pid = 0;
    int launched = 0;
    int duration = (argc > arg_offset + 1) ? atoi(argv[arg_offset + 1]) : DEFAULT_DURATION;
    const char *out_prefix = (argc > arg_offset + 2) ? argv[arg_offset + 2] : NULL;
    char *target = argv[arg_offset];

    /* 判断参数是 PID 还是程序路径 */
    int is_pid = 1;
    for (char *p = target; *p; p++) {
        if (!isdigit((unsigned char)*p)) { is_pid = 0; break; }
    }

    if (is_pid) {
        pid = (pid_t)atoi(target);
    } else {
        if (access(target, X_OK) != 0) {
            fprintf(stderr, "❌ 程序不可执行: %s\n", target);
            return 1;
        }
        pid = fork();
        if (pid == 0) {
            execvp(target, &argv[arg_offset]);
            perror("execvp");
            _exit(1);
        }
        if (pid < 0) { perror("fork"); return 1; }
        launched = 1;
        printf("🚀 已启动: %s (PID=%d), 等待 %ds 初始化...\n", target, pid, wait_sec);
        sleep(wait_sec);
        /* 检查子进程是否还活着 */
        if (kill(pid, 0) != 0) {
            fprintf(stderr, "❌ 进程 %s 启动后立即退出\n", argv[1]);
            return 1;
        }
    }

    if (proc_validate(pid) != 0) {
        fprintf(stderr, "❌ 进程 PID=%d 不存在\n", pid);
        return 1;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    selinux_probe();

    char *outdir = report_mk_outdir(out_prefix, pid);
    printf("📊 采集进程 PID=%d 的资源数据, 持续 %ds (并发)\n", pid, duration);
    printf("   输出目录: %s\n", outdir);

    char proc_name[PROC_NAME_MAX] = "unknown";
    proc_get_name(pid, proc_name, sizeof(proc_name));

    /* 打开 CSV */
    FILE *fcpu = report_csv_open(outdir, "cpu.csv",  "sec,cpu_percent");
    FILE *fmem = report_csv_open(outdir, "mem.csv",  "sample,rss_kb,pss_kb,uss_kb");
    FILE *fthr = report_csv_open(outdir, "threads_fd.csv", "sample,threads,fd_count");
    FILE *fio  = report_csv_open(outdir, "io.csv",   "sample,read_kB,write_kB");
    /* core.csv header written by core_sampler itself */
    char core_path[MAX_PATH];
    snprintf(core_path, sizeof(core_path), "%s/core.csv", outdir);
    FILE *fcore = fopen(core_path, "w");
    char net_path[MAX_PATH];
    snprintf(net_path, sizeof(net_path), "%s/net.csv", outdir);
    FILE *fnet = fopen(net_path, "w");

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

    /* ── 启动 6 个采样线程, 并发运行 duration 秒 ── */
    pthread_t t_cpu, t_mem, t_thr, t_io, t_core, t_net;
    int core_count = 0, net_count = 0;
    CountArgs core_args = { duration, &core_count, fcore };
    CountArgs net_args  = { duration, &net_count,  fnet  };
    if (fcpu)  pthread_create(&t_cpu,  NULL, cpu_thread,  &cpu_args);
    if (fmem)  pthread_create(&t_mem,  NULL, mem_thread,  &mem_args);
    if (fthr)  pthread_create(&t_thr,  NULL, thrfd_thread,&thr_args);
    if (fio)   pthread_create(&t_io,   NULL, io_thread,   &io_args);
    if (fcore) pthread_create(&t_core, NULL, core_thread, &core_args);
    if (fnet)  pthread_create(&t_net,  NULL, net_thread,  &net_args);

    if (fcpu)  pthread_join(t_cpu,  NULL);
    if (fmem)  pthread_join(t_mem,  NULL);
    if (fthr)  pthread_join(t_thr,  NULL);
    if (fio)   pthread_join(t_io,   NULL);
    if (fcore) pthread_join(t_core, NULL);
    if (fnet)  pthread_join(t_net,  NULL);

    fclose(fcpu); fclose(fmem); fclose(fthr); fclose(fio); fclose(fcore); fclose(fnet);

    /* ── 签名为防伪 ──────────────────────────── */
    char csv_path[MAX_PATH];
    /* ── 追加版本号 (签名之前, 版本号受 HMAC 保护) ── */
    for (int i = 0; i < 6; i++) {
        const char *names[] = {"cpu.csv","mem.csv","threads_fd.csv","io.csv","core.csv","net.csv"};
        snprintf(csv_path, sizeof(csv_path), "%s/%s", outdir, names[i]);
        FILE *vf = fopen(csv_path, "a");
        if (vf) { fprintf(vf, "# VERSION:%s\n", APP_VERSION); fclose(vf); }
    }

    /* ── 签名 (覆盖数据 + 版本号) ── */
    snprintf(csv_path, sizeof(csv_path), "%s/cpu.csv", outdir);        sign_file(csv_path);
    snprintf(csv_path, sizeof(csv_path), "%s/mem.csv", outdir);        sign_file(csv_path);
    snprintf(csv_path, sizeof(csv_path), "%s/threads_fd.csv", outdir); sign_file(csv_path);
    snprintf(csv_path, sizeof(csv_path), "%s/io.csv", outdir);         sign_file(csv_path);
    snprintf(csv_path, sizeof(csv_path), "%s/core.csv", outdir);       sign_file(csv_path);
    snprintf(csv_path, sizeof(csv_path), "%s/net.csv", outdir);        sign_file(csv_path);
    ResourceSnapshot snap;
    snap.cpu_avg     = snap_cpu.cpu_avg;
    snap.cpu_peak    = snap_cpu.cpu_peak;
    snap.cpu_samples = snap_cpu.cpu_samples;
    snap.rss_kb      = snap_mem.rss_kb;
    snap.pss_kb      = snap_mem.pss_kb;
    snap.uss_kb      = snap_mem.uss_kb;
    snap.mem_samples = snap_mem.mem_samples;
    snap.threads     = snap_thr.threads;
    snap.fds         = snap_thr.fds;
    snap.thrfd_samples = snap_thr.thrfd_samples;
    snap.io_read_kb  = snap_io.io_read_kb;
    snap.io_write_kb = snap_io.io_write_kb;
    snap.io_samples  = snap_io.io_samples;
    snap.child_count     = proc_count_children(pid);
    snap.core_count      = core_count;
    snap.net_iface_count = net_count;

    report_write(outdir, proc_name, pid, duration, &snap);
    free(outdir);
    return 0;
}
