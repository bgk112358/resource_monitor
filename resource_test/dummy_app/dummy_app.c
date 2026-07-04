/**
 * dummy_app.c — TBox 模拟测试应用
 *
 * 模拟一个典型 TBox 应用的资源占用模式:
 *   - 2 个 CPU 计算线程 (浮点运算, 模拟 CAN 报文解析)
 *   - 1 个 IO 日志线程 (周期性写文件)
 *   - 1 个 TCP 监听线程 (模拟对外服务端口)
 *   - 8MB 堆内存分配并长期持有
 *   - 可配置的运行时长
 *
 * 构建:
 *   mkdir build && cd build
 *   cmake .. -DCMAKE_TOOLCHAIN_FILE=../toolchain.cmake
 *   make
 *
 * 用法:
 *   ./dummy_app [duration_seconds]
 *   ./dummy_app 120    # 运行 120 秒后自动退出
 *   ./dummy_app 0      # 持续运行直到 Ctrl+C
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

/* ── 配置常量 ─────────────────────────────────────── */
#define MEM_POOL_MB       8              /* 常驻内存池 (MB)        */
#define CPU_WORKERS       2              /* CPU 计算线程数         */
#define IO_WORKERS        1              /* IO 写日志线程数        */
#define NET_WORKERS       1              /* TCP 监听线程数         */
#define TOTAL_THREADS     (CPU_WORKERS + IO_WORKERS + NET_WORKERS)
#define LISTEN_PORT       19999          /* TCP 监听端口           */
#define LOG_FILE          "/tmp/dummy_app.log"
#define PID_FILE          "/tmp/dummy_app.pid"
#define CPU_BATCH_SIZE    100000         /* 每批浮点运算次数       */
#define CPU_SLEEP_US      200000         /* 批次间隔 (微秒)        */
#define IO_SLEEP_SEC      2              /* 日志写入间隔 (秒)      */

/* ── 全局状态 ──────────────────────────────────────── */
static volatile int g_running = 1;
static unsigned char *g_mem_pool = NULL;

/* ── 信号处理 ──────────────────────────────────────── */
static void sig_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* ── CPU 工作线程: 持续浮点运算 ────────────────────── */
static void *cpu_worker(void *arg) {
    int id = *(int *)arg;
    double result = 0.0;
    unsigned long long count = 0;

    printf("  [CPU-%d] started\n", id);
    while (g_running) {
        for (int i = 0; i < CPU_BATCH_SIZE; i++) {
            result += sin(i * 0.001) * cos(i * 0.0001);
        }
        count++;
        usleep(CPU_SLEEP_US);
    }
    printf("  [CPU-%d] stopped (%.2e batches, result=%.2f)\n", id, (double)count, result);
    return NULL;
}

/* ── IO 工作线程: 周期性写日志 ──────────────────────── */
static void *io_worker(void *arg) {
    int id = *(int *)arg;
    FILE *fp = fopen(LOG_FILE, "a");
    if (!fp) {
        fprintf(stderr, "  [IO-%d] failed to open %s: %s\n", id, LOG_FILE, strerror(errno));
        return NULL;
    }

    printf("  [IO-%d] started, log: %s\n", id, LOG_FILE);
    unsigned long long count = 0;
    struct timespec ts;

    while (g_running) {
        clock_gettime(CLOCK_REALTIME, &ts);
        fprintf(fp, "[%lld.%09ld] IO-%d heartbeat #%llu\n",
                (long long)ts.tv_sec, ts.tv_nsec, id, count);
        fflush(fp);
        count++;
        sleep(IO_SLEEP_SEC);
    }

    fclose(fp);
    printf("  [IO-%d] stopped (%llu entries)\n", id, count);
    return NULL;
}

/* ── TCP 监听线程 ──────────────────────────────────── */
static void *net_worker(void *arg) {
    int id = *(int *)arg;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "  [NET-%d] socket failed: %s\n", id, strerror(errno));
        return NULL;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(LISTEN_PORT),
        .sin_addr.s_addr = INADDR_ANY,
    };

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "  [NET-%d] bind :%d failed: %s\n", id, LISTEN_PORT, strerror(errno));
        close(fd);
        return NULL;
    }

    if (listen(fd, 5) < 0) {
        fprintf(stderr, "  [NET-%d] listen failed: %s\n", id, strerror(errno));
        close(fd);
        return NULL;
    }

    printf("  [NET-%d] listening on TCP :%d\n", id, LISTEN_PORT);

    /* 设置非阻塞模式, 以便响应 g_running */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    while (g_running) {
        struct sockaddr_in client;
        socklen_t client_len = sizeof(client);
        int cfd = accept(fd, (struct sockaddr *)&client, &client_len);
        if (cfd >= 0) {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client.sin_addr, ip, sizeof(ip));
            printf("  [NET-%d] connection from %s:%d\n", id, ip, ntohs(client.sin_port));
            close(cfd);
        }
        usleep(100000); /* 100ms 轮询 */
    }

    close(fd);
    printf("  [NET-%d] stopped\n", id);
    return NULL;
}

/* ── 主函数 ───────────────────────────────────────── */
int main(int argc, char *argv[]) {
    int duration = 0; /* 0 = 持续运行 */

    if (argc > 1) {
        duration = atoi(argv[1]);
        if (duration < 0) duration = 0;
    }

    /* 写入 PID 文件 */
    FILE *pf = fopen(PID_FILE, "w");
    if (pf) { fprintf(pf, "%d\n", getpid()); fclose(pf); }

    /* 注册信号 */
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    printf("╔══════════════════════════════════════════╗\n");
    printf("║  DummyApp — TBox 模拟测试应用            ║\n");
    printf("║  PID:    %-6d                           ║\n", getpid());
    printf("║  时长:   %s                            ║\n",
           duration > 0 ? argv[1] : "持续运行");
    printf("╚══════════════════════════════════════════╝\n");

    /* ── 分配内存池 ── */
    g_mem_pool = (unsigned char *)calloc(MEM_POOL_MB, 1024 * 1024);
    if (!g_mem_pool) {
        fprintf(stderr, "FATAL: failed to allocate %d MB memory\n", MEM_POOL_MB);
        return 1;
    }
    /* 写入数据确保物理页分配 */
    memset(g_mem_pool, 0xA5, MEM_POOL_MB * 1024 * 1024);
    printf("[MEMORY] allocated %d MB at %p\n", MEM_POOL_MB, (void *)g_mem_pool);

    /* ── 创建工作线程 ── */
    pthread_t threads[TOTAL_THREADS];
    int thread_ids[TOTAL_THREADS];
    int t = 0;

    for (int i = 0; i < CPU_WORKERS; i++) {
        thread_ids[t] = i + 1;
        pthread_create(&threads[t], NULL, cpu_worker, &thread_ids[t]);
        t++;
    }
    for (int i = 0; i < IO_WORKERS; i++) {
        thread_ids[t] = i + 1;
        pthread_create(&threads[t], NULL, io_worker, &thread_ids[t]);
        t++;
    }
    for (int i = 0; i < NET_WORKERS; i++) {
        thread_ids[t] = i + 1;
        pthread_create(&threads[t], NULL, net_worker, &thread_ids[t]);
        t++;
    }

    printf("[THREADS] created %d worker threads (%d CPU + %d IO + %d NET)\n",
           TOTAL_THREADS, CPU_WORKERS, IO_WORKERS, NET_WORKERS);
    printf("[STATUS]  running... %s\n",
           duration > 0 ? "(will auto-stop)" : "(Ctrl+C to stop)");

    /* ── 等待指定时长或信号 ── */
    if (duration > 0) {
        sleep(duration);
        g_running = 0;
    } else {
        while (g_running) sleep(1);
    }

    /* ── 等待线程退出 ── */
    printf("[STOP] waiting for threads...\n");
    for (int i = 0; i < TOTAL_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    /* ── 清理 ── */
    free(g_mem_pool);
    unlink(PID_FILE);

    printf("[EXIT] DummyApp stopped cleanly\n");
    return 0;
}
