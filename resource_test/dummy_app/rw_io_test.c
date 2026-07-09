/**
 * rw_io_test.c — 磁盘 IO 压力生成工具
 *
 * 每秒向指定文件追加写入 100KB 数据, 用于验证 IO 监控采集。
 *
 * 用法:
 *   ./rw_io_test <文件路径> [持续时间_秒]
 *   ./rw_io_test -h
 *
 * 示例:
 *   ./rw_io_test /opt/test.log 30     # 写 30 秒
 *   ./rw_io_test /tmp/data.bin        # 默认 60 秒
 *
 * 编译:
 *   gcc -std=c11 -O2 -o rw_io_test rw_io_test.c
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#define BLOCK_KB   100
#define BLOCK_SIZE (BLOCK_KB * 1024)
#define DEFAULT_DURATION 60

static volatile int g_running = 1;

static void sig_handler(int sig) {
    (void)sig;
    g_running = 0;
}

static void usage(const char *prog) {
    printf("用法: %s [-h] <文件路径> [持续时间_秒]\n", prog);
    printf("\n");
    printf("每秒向指定文件追加写入 %dKB 数据, 用于验证 IO 监控。\n", BLOCK_KB);
    printf("\n");
    printf("参数:\n");
    printf("  <文件路径>      写入目标文件 (必须可写, 目录需存在)\n");
    printf("  [持续时间_秒]    运行时长 (默认 %d, 1-3600)\n", DEFAULT_DURATION);
    printf("\n");
    printf("选项:\n");
    printf("  -h              显示此帮助\n");
    printf("\n");
    printf("示例:\n");
    printf("  %s /opt/test.log 30\n", prog);
    printf("  %s /tmp/io_stress.bin\n", prog);
}

int main(int argc, char *argv[]) {
    if (argc < 2 || (argc == 2 && strcmp(argv[1], "-h") == 0)) {
        usage(argv[0]);
        return argc < 2 ? 1 : 0;
    }
    if (argc > 2 && strcmp(argv[1], "-h") == 0) {
        usage(argv[0]);
        return 0;
    }

    const char *filepath = argv[1];
    int duration = (argc > 2) ? atoi(argv[2]) : DEFAULT_DURATION;
    if (duration < 1)   duration = 1;
    if (duration > 3600) duration = 3600;

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    /* 预分配写缓冲区 (全部填 'A') */
    char *buf = (char *)malloc(BLOCK_SIZE);
    if (!buf) {
        fprintf(stderr, "❌ 分配缓冲区失败 (%dKB)\n", BLOCK_KB);
        return 1;
    }
    memset(buf, 'A', BLOCK_SIZE);

    FILE *f = fopen(filepath, "a");
    if (!f) {
        fprintf(stderr, "❌ 无法打开文件: %s (%s)\n", filepath, strerror(errno));
        free(buf);
        return 1;
    }
    /* 关闭缓冲, 确保每次 write 直接发 IO 请求 */
    setvbuf(f, NULL, _IONBF, 0);

    printf("📝 IO 写入测试: %s\n", filepath);
    printf("   每次 %dKB, 每秒 1 次, 持续 %ds\n", BLOCK_KB, duration);
    printf("   Ctrl-C 提前停止\n");

    long total_kb = 0;
    time_t start = time(NULL);
    int sec;
    for (sec = 0; sec < duration && g_running; sec++) {
        size_t written = fwrite(buf, 1, BLOCK_SIZE, f);
        fflush(f);

        if (written < BLOCK_SIZE) {
            fprintf(stderr, "❌ 写入失败 at 第 %ds (%s)\n",
                    sec + 1, strerror(errno));
            break;
        }
        total_kb += BLOCK_KB;

        /* 整秒对齐: 补偿写入耗时 */
        time_t elapsed = time(NULL) - start;
        if (elapsed < sec + 1)
            sleep((unsigned int)(sec + 1 - elapsed));
    }

    fclose(f);
    free(buf);

    printf("\n✅ 完成: %ds 写入 %ldKB (%.1fMB), 平均 %.0fKB/s\n",
           sec, total_kb, total_kb / 1024.0,
           sec > 0 ? (double)total_kb / sec : 0);
    return 0;
}
