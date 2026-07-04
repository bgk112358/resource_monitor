/**
 * dummy_app.c — TBox 模拟测试应用 (动态版)
 *
 * 60 秒周期内 CPU/内存/IO/线程周期性大幅变化:
 *   0-10s:  预热期 — CPU 5%, 内存 4MB, IO 低
 *   10-25s: 高峰期 — CPU 60-80%, 线程 +4, IO 1MB/s
 *   25-35s: 内存压力 — 额外分配 16MB, CPU 中
 *   35-50s: 降温期 — CPU 20%, 释放额外内存
 *   50-60s: 基线恢复
 *
 * 周期结束后自动循环。
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <math.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

/* ── 配置 ────────────────────────────────────────── */
#define LISTEN_PORT       19999
#define LOG_FILE          "/tmp/dummy_app.log"
#define READ_FILE         "/tmp/dummy_app_read.dat"
#define PID_FILE          "/tmp/dummy_app.pid"
#define CYCLE_SEC         60

/* ── 全局状态 ────────────────────────────────────── */
static volatile int g_running = 1;
static volatile int g_cycle_sec = 0;      /* 当前周期秒数 0-59 */
static volatile int g_cpu_pct = 5;        /* 目标 CPU 占用 % */
static volatile int g_io_rate_kbps = 10;   /* 目标 IO 写入速率 KB/s */
static volatile int g_io_read_kbps = 10;   /* 目标 IO 读取速率 KB/s */
static volatile int g_extra_threads = 0;   /* 动态创建的额外线程数 */
static unsigned char *g_base_mem = NULL;  /* 常驻 8MB */
static unsigned char *g_extra_mem = NULL; /* 动态 0-16MB */
static int g_extra_fds[16];              /* 动态文件描述符 */
static int g_extra_fd_count = 0;

static void sig_handler(int sig) { (void)sig; g_running = 0; }

/* ── 周期调度器 ──────────────────────────────────── */
static void update_cycle_params(int sec) {
    int s = sec % CYCLE_SEC;
    g_cycle_sec = s;

    if (s < 10) {
        /* 0-10s: 预热 — 低负载 */
        g_cpu_pct = 5;
        g_io_rate_kbps = 10;
        g_io_read_kbps = 10;
        g_extra_threads = 0;
        /* 释放额外内存 */
        if (g_extra_mem) { free(g_extra_mem); g_extra_mem = NULL; }
        /* 关闭额外 FD */
        for (int i = 0; i < g_extra_fd_count; i++) close(g_extra_fds[i]);
        g_extra_fd_count = 0;
    } else if (s < 25) {
        /* 10-25s: 高峰 — CPU 飙升 + 多线程 + 高 IO 读写 */
        g_cpu_pct = 60 + (s % 5) * 4;  /* 60-80% 波动 */
        g_io_rate_kbps = 1024;
        g_io_read_kbps = 512;
        g_extra_threads = 4;
        if (g_extra_mem) { free(g_extra_mem); g_extra_mem = NULL; }
    } else if (s < 35) {
        /* 25-35s: 内存压力 — 额外分配 16MB */
        g_cpu_pct = 30;
        g_io_rate_kbps = 100;
        g_io_read_kbps = 200;
        g_extra_threads = 2;
        if (!g_extra_mem)
            g_extra_mem = (unsigned char *)calloc(16, 1024 * 1024);
    } else if (s < 50) {
        /* 35-50s: 降温 */
        g_cpu_pct = 15 + (s % 5) * 2;
        g_io_rate_kbps = 50;
        g_io_read_kbps = 100;
        g_extra_threads = 1;
        if (g_extra_mem) { free(g_extra_mem); g_extra_mem = NULL; }
        /* 打开一些 FD */
        for (int i = 0; i < 3 && g_extra_fd_count < 16; i++) {
            char name[64]; snprintf(name, sizeof(name), "/tmp/dummy_fd_%d.tmp", i);
            int fd = open(name, O_CREAT|O_RDWR, 0644);
            if (fd >= 0) g_extra_fds[g_extra_fd_count++] = fd;
        }
    } else {
        /* 50-60s: 基线恢复 */
        g_cpu_pct = 5;
        g_io_rate_kbps = 10;
        g_io_read_kbps = 10;
        g_extra_threads = 0;
        if (g_extra_mem) { free(g_extra_mem); g_extra_mem = NULL; }
        for (int i = 0; i < g_extra_fd_count; i++) close(g_extra_fds[i]);
        g_extra_fd_count = 0;
    }
}

/* ── 周期计时器线程 ──────────────────────────────── */
static void *cycle_timer(void *arg) {
    (void)arg;
    int sec = 0;
    while (g_running) {
        update_cycle_params(sec);
        sec++;
        sleep(1);
    }
    return NULL;
}

/* ── CPU 工作线程: 根据 g_cpu_pct 动态调节负载 ────── */
static void *cpu_worker(void *arg) {
    int id = *(int *)arg;
    double result = 0.0;
    printf("  [CPU-%d] started (dynamic)\n", id);
    while (g_running) {
        int target = g_cpu_pct;
        if (target <= 0) target = 1;
        /* 忙碌 μs = target * 100, 空闲 μs = (100-target) * 100 */
        int busy_us  = target * 80;
        int idle_us  = (100 - target) * 80;
        if (idle_us < 1000) idle_us = 1000;

        struct timespec t1, t2;
        clock_gettime(CLOCK_MONOTONIC, &t1);
        do {
            for (int i = 0; i < 5000; i++)
                result += sin(i * 0.001) * cos(i * 0.0001);
            clock_gettime(CLOCK_MONOTONIC, &t2);
        } while ((t2.tv_sec - t1.tv_sec) * 1000000 + (t2.tv_nsec - t1.tv_nsec) / 1000 < busy_us);

        usleep(idle_us);
    }
    printf("  [CPU-%d] stopped (result=%.2f)\n", id, result);
    return NULL;
}

/* ── 动态 CPU 线程 (周期内创建/销毁) ──────────────── */
static void *extra_cpu_worker(void *arg) {
    int id = *(int *)arg;
    double result = 0.0;
    printf("  [CPU-extra-%d] started\n", id);
    while (g_running && g_extra_threads > 0) {
        for (int i = 0; i < 30000; i++)
            result += sin(i * 0.01) * cos(i * 0.001);
        usleep(50000);
    }
    printf("  [CPU-extra-%d] stopped\n", id);
    free(arg);
    return NULL;
}

/* ── IO 写工作线程 ──────────────────────────────── */
static void *io_worker(void *arg) {
    int id = *(int *)arg;
    printf("  [IO-W-%d] started (dynamic, log: %s)\n", id, LOG_FILE);
    while (g_running) {
        int kbps = g_io_rate_kbps;
        if (kbps <= 0) kbps = 1;
        int bytes = kbps * 1024;
        char buf[4096]; memset(buf, 'X', sizeof(buf));
        FILE *fp = fopen(LOG_FILE, "a");
        if (fp) {
            for (int written = 0; written < bytes && g_running; written += sizeof(buf))
                fwrite(buf, 1, sizeof(buf), fp);
            fclose(fp);
        }
        sleep(1);
    }
    printf("  [IO-W-%d] stopped\n", id);
    return NULL;
}

/* ── IO 读工作线程 ──────────────────────────────── */
static void *io_read_worker(void *arg) {
    int id = *(int *)arg;
    /* 创建可读取的数据文件 (4MB)，并确保落地到磁盘 */
    int fd = open(READ_FILE, O_CREAT|O_RDWR|O_TRUNC, 0644);
    if (fd >= 0) {
        char buf[4096]; memset(buf, 'R', sizeof(buf));
        for (int i = 0; i < 1024; i++) write(fd, buf, sizeof(buf)); /* 4MB */
        fsync(fd);
        close(fd);
    }
    printf("  [IO-R-%d] started (O_DIRECT read: %s)\n", id, READ_FILE);
    while (g_running) {
        int kbps = g_io_read_kbps;
        if (kbps <= 0) kbps = 1;
        int bytes = kbps * 1024;
        /* 使用 O_DIRECT 绕过页缓存, 产生真实磁盘读 */
        fd = open(READ_FILE, O_RDONLY | O_DIRECT);
        if (fd >= 0) {
            /* O_DIRECT 需要对齐的缓冲区 */
            char *buf = NULL;
            posix_memalign((void **)&buf, 4096, 4096);
            if (buf) {
                for (int total = 0; total < bytes && g_running; total += 4096)
                    read(fd, buf, 4096);
                free(buf);
            }
            close(fd);
        }
        sleep(1);
    }
    printf("  [IO-R-%d] stopped\n", id);
    return NULL;
}

/* ── TCP 监听线程 ────────────────────────────────── */
static void *net_worker(void *arg) {
    int id = *(int *)arg;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { fprintf(stderr, "  [NET-%d] socket failed\n", id); return NULL; }
    int opt = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(LISTEN_PORT), .sin_addr.s_addr = INADDR_ANY };
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(fd); return NULL; }
    if (listen(fd, 5) < 0) { close(fd); return NULL; }
    printf("  [NET-%d] listening on TCP :%d\n", id, LISTEN_PORT);
    int flags = fcntl(fd, F_GETFL, 0); fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    while (g_running) {
        struct sockaddr_in client; socklen_t cl = sizeof(client);
        int cfd = accept(fd, (struct sockaddr *)&client, &cl);
        if (cfd >= 0) close(cfd);
        usleep(100000);
    }
    close(fd);
    printf("  [NET-%d] stopped\n", id);
    return NULL;
}

/* ── 动态线程管理器 ──────────────────────────────── */
static void *thread_manager(void *arg) {
    (void)arg;
    int prev_extra = 0;
    pthread_t extras[8];
    while (g_running) {
        int want = g_extra_threads;
        if (want > 8) want = 8;
        /* 创建新线程 */
        for (int i = prev_extra; i < want; i++) {
            int *pid = malloc(sizeof(int)); *pid = i + 1;
            pthread_create(&extras[i], NULL, extra_cpu_worker, pid);
        }
        prev_extra = want;
        sleep(2);
    }
    /* 等待所有动态线程退出 */
    for (int i = 0; i < prev_extra; i++) pthread_join(extras[i], NULL);
    return NULL;
}

/* ── 主函数 ──────────────────────────────────────── */
int main(int argc, char *argv[]) {
    int duration = 0;
    if (argc > 1) { duration = atoi(argv[1]); if (duration < 0) duration = 0; }

    FILE *pf = fopen(PID_FILE, "w");
    if (pf) { fprintf(pf, "%d\n", getpid()); fclose(pf); }

    signal(SIGINT, sig_handler); signal(SIGTERM, sig_handler);

    printf("╔══════════════════════════════════════════╗\n");
    printf("║  DummyApp v2 — 动态负载模拟              ║\n");
    printf("║  PID: %-6d  周期: %ds                    ║\n", getpid(), CYCLE_SEC);
    printf("║  时长: %s                            ║\n", duration > 0 ? argv[1] : "持续运行");
    printf("╚══════════════════════════════════════════╝\n");

    /* 常驻内存 */
    g_base_mem = (unsigned char *)calloc(8, 1024 * 1024);
    if (!g_base_mem) { fprintf(stderr, "FATAL: malloc failed\n"); return 1; }
    memset(g_base_mem, 0xA5, 8 * 1024 * 1024);
    printf("[MEMORY] base 8MB allocated at %p\n", (void *)g_base_mem);

    /* 启动周期计时器 */
    pthread_t timer_tid;
    pthread_create(&timer_tid, NULL, cycle_timer, NULL);

    /* 常驻线程: 2 CPU + 1 IO写 + 1 IO读 + 1 NET + 1 线程管理 */
    pthread_t threads[6];
    int ids[] = {1, 2, 1, 1, 1, 0};
    pthread_create(&threads[0], NULL, cpu_worker,      &ids[0]);
    pthread_create(&threads[1], NULL, cpu_worker,      &ids[1]);
    pthread_create(&threads[2], NULL, io_worker,       &ids[2]);
    pthread_create(&threads[3], NULL, io_read_worker,  &ids[3]);
    pthread_create(&threads[4], NULL, net_worker,      &ids[4]);
    pthread_create(&threads[5], NULL, thread_manager,  &ids[5]);
    printf("[THREADS] 6 base threads + dynamic extras\n");

    printf("[STATUS] running... %s\n", duration > 0 ? "(will auto-stop)" : "(Ctrl+C to stop)");

    if (duration > 0) { sleep(duration); g_running = 0; }
    else { while (g_running) sleep(1); }

    printf("[STOP] waiting for threads...\n");
    pthread_join(timer_tid, NULL);
    for (int i = 0; i < 6; i++) pthread_join(threads[i], NULL);

    free(g_base_mem);
    if (g_extra_mem) free(g_extra_mem);
    for (int i = 0; i < g_extra_fd_count; i++) close(g_extra_fds[i]);
    unlink(PID_FILE);

    printf("[EXIT] DummyApp v2 stopped cleanly\n");
    return 0;
}
