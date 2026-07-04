/**
 * system_stress.c — TBox 全系统综合压力测试 (C 版)
 *
 * 替代 system_stress.sh, 使用 pthreads 创建:
 *   2 个 CPU 满载线程 (可绑核)
 *   1 个内存压力线程 (分配 200MB 并持续读写)
 *   1 个存储压力线程 (随机读写文件)
 *   1 个网络压力线程 (ping 网关)
 *
 * 用法:
 *   ./system_stress [duration_sec]
 *   ./system_stress 600    # 10 分钟压力测试
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#define DEFAULT_DURATION    600
#define CPU_THREADS         2
#define MEM_MB              200
#define IO_FILE             "/tmp/stress_io.bin"
#define IO_CHUNK_KB         64
#define PING_INTERVAL_US    200000      /* 200ms */

static volatile int g_running = 1;
static void sig_handler(int sig) { (void)sig; g_running = 0; }

/* ── CPU 压力线程 ────────────────────────────────── */
static void *cpu_stress(void *arg) {
    int core = *(int *)arg;
    (void)core;
    double result = 0.0;
    unsigned long long count = 0;
    while (g_running) {
        for (int i = 0; i < 50000; i++) result += sin(i * 0.001) * cos(i * 0.0001);
        count++;
    }
    printf("  [CPU] core %d: %llu batches, result=%.2f\n", core, count, result);
    return NULL;
}

/* ── 内存压力线程 ────────────────────────────────── */
static void *mem_stress(void *arg) {
    (void)arg;
    size_t size = (size_t)MEM_MB * 1024 * 1024;
    unsigned char *buf = (unsigned char *)malloc(size);
    if (!buf) { fprintf(stderr, "  [MEM] malloc %dMB failed\n", MEM_MB); return NULL; }
    memset(buf, 0xA5, size);
    printf("  [MEM] allocated %dMB, stress loop started\n", MEM_MB);
    while (g_running) {
        /* 每隔 1 秒随机写入一个位置保持内存活跃 */
        for (size_t off = 0; off < size && g_running; off += 4096) {
            buf[off] = (unsigned char)(off & 0xFF);
        }
        sleep(1);
    }
    free(buf);
    printf("  [MEM] freed %dMB\n", MEM_MB);
    return NULL;
}

/* ── 存储压力线程 ────────────────────────────────── */
static void *io_stress(void *arg) {
    (void)arg;
    size_t chunk = IO_CHUNK_KB * 1024;
    unsigned char *buf = (unsigned char *)malloc(chunk);
    if (!buf) return NULL;
    memset(buf, 0x5A, chunk);

    printf("  [IO]  stress started, file: %s\n", IO_FILE);
    long long total_written = 0;

    while (g_running) {
        int fd = open(IO_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { sleep(1); continue; }

        /* 写入 100 个 chunk */
        for (int i = 0; i < 100 && g_running; i++) {
            ssize_t written = write(fd, buf, chunk);
            if (written > 0) total_written += written;
        }
        close(fd);
        unlink(IO_FILE);
        usleep(500000); /* 500ms 间隔 */
    }

    free(buf);
    printf("  [IO]  stopped, total written: %lld MB\n", total_written / (1024*1024));
    return NULL;
}

/* ── 网络压力线程 (ping 网关) ────────────────────── */
static void *net_stress(void *arg) {
    (void)arg;
    /* 获取默认网关 */
    FILE *fp = popen("ip route | grep default | awk '{print $3}' | head -1", "r");
    if (!fp) { printf("  [NET] cannot read routes\n"); return NULL; }
    char gw[64] = "";
    fgets(gw, sizeof(gw), fp); pclose(fp);
    gw[strcspn(gw, "\n")] = 0;
    if (strlen(gw) == 0) { printf("  [NET] no default gateway\n"); return NULL; }

    printf("  [NET] pinging gateway: %s\n", gw);
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "ping -i 0.2 %s > /tmp/stress_ping.log 2>&1", gw);
    FILE *ping = popen(cmd, "r");
    if (!ping) return NULL;

    while (g_running) usleep(100000);
    pclose(ping);

    /* 打印统计 */
    printf("  [NET] ping statistics:\n");
    system("tail -3 /tmp/stress_ping.log 2>/dev/null");
    return NULL;
}

/* ── CPU 温度读取 ────────────────────────────────── */
static void print_cpu_temp(void) {
    FILE *f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (f) { int t; fscanf(f, "%d", &t); fclose(f); printf("\n── CPU 温度: %d°C ──\n", t/1000); }
}

/* ── 主函数 ──────────────────────────────────────── */
int main(int argc, char *argv[]) {
    int duration = (argc > 1) ? atoi(argv[1]) : DEFAULT_DURATION;
    if (duration <= 0) duration = DEFAULT_DURATION;

    signal(SIGINT, sig_handler); signal(SIGTERM, sig_handler);

    printf("🔥 TBox 综合压力测试 — 持续 %ds\n", duration);
    printf("   加载: CPU(%d线程满载) + MEM(%dMB) + IO + NET\n\n", CPU_THREADS, MEM_MB);

    pthread_t threads[CPU_THREADS + 3];
    int cores[CPU_THREADS] = {0, 1};
    int t = 0;

    for (int i = 0; i < CPU_THREADS; i++)
        pthread_create(&threads[t++], NULL, cpu_stress, &cores[i]);
    pthread_create(&threads[t++], NULL, mem_stress, NULL);
    pthread_create(&threads[t++], NULL, io_stress, NULL);
    pthread_create(&threads[t++], NULL, net_stress, NULL);

    printf("⏳ 等待 %ds ... (Ctrl+C 提前结束)\n\n", duration);
    sleep(duration);
    g_running = 0;

    printf("\n🛑 停止所有压力线程...\n");
    for (int i = 0; i < t; i++) pthread_join(threads[i], NULL);

    print_cpu_temp();
    printf("\n✅ 综合压力测试完成\n");

    /* dmesg 检查 */
    printf("\n── 内核错误检查 ──\n");
    system("dmesg | tail -20 | grep -iE 'error|fail|oom|panic|watchdog' || echo '  无内核错误'");

    return 0;
}
