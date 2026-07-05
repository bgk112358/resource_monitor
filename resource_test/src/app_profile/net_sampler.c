/**
 * net_sampler.c — 网络接口吞吐量采样 (上行/下行)
 *
 * 读取 /proc/net/dev 的接口级 RX/TX 字节数,
 * 每秒计算各接口的上行/下行吞吐量 (KB/s), 写入 CSV (列名 _kB 表示千字节/秒)。
 *
 * 接口: net_sampler_run(duration, snap, csv)
 */
#include "sampler.h"
#include <string.h>

#define MAX_IFACES 8

typedef struct {
    char name[32];
    unsigned long long rx_bytes;
    unsigned long long tx_bytes;
} IfaceStats;

/* 解析 /proc/net/dev, 返回接口数和当前统计 */
static int read_net_dev(IfaceStats *ifaces, int max_ifaces) {
    FILE *f = fopen("/proc/net/dev", "r");
    if (!f) return 0;

    char line[MAX_LINE];
    int count = 0;
    /* 跳过前两行 (header) */
    fgets(line, sizeof(line), f);
    fgets(line, sizeof(line), f);

    while (fgets(line, sizeof(line), f) && count < max_ifaces) {
        char *p = line;
        while (*p == ' ') p++;
        char *colon = strchr(p, ':');
        if (!colon) continue;
        *colon = '\0';
        strncpy(ifaces[count].name, p, sizeof(ifaces[count].name) - 1);

        p = colon + 1;
        unsigned long long rx, tx;
        /* 字段: rx_bytes rx_packets rx_errs rx_drop rx_fifo rx_frame rx_compressed rx_multicast
                  tx_bytes tx_packets tx_errs tx_drop tx_fifo tx_colls tx_carrier tx_compressed */
        if (sscanf(p, "%llu %*u %*u %*u %*u %*u %*u %*u %llu", &rx, &tx) == 2) {
            ifaces[count].rx_bytes = rx;
            ifaces[count].tx_bytes = tx;
            count++;
        }
    }
    fclose(f);
    return count;
}

int net_sampler_run(int duration, int *out_count, FILE *csv) {
    IfaceStats prev[MAX_IFACES];
    IfaceStats cur[MAX_IFACES];

    /* 首次读取: 获取接口数 + 基线 */
    int n_if = read_net_dev(prev, MAX_IFACES);
    if (n_if <= 0) return -1;

    /* 写入 CSV 头 */
    fprintf(csv, "sec");
    for (int i = 0; i < n_if; i++) {
        fprintf(csv, ",%s_rx_kB,%s_tx_kB", prev[i].name, prev[i].name);
    }
    fprintf(csv, "\n");

    /* 每秒采样 */
    for (int sec = 1; sec <= duration && g_running; sec++) {
        sleep(1);

        int nc = read_net_dev(cur, MAX_IFACES);
        if (nc <= 0) break;
        if (nc > n_if) nc = n_if;

        fprintf(csv, "%d", sec);
        for (int i = 0; i < n_if; i++) {
            unsigned long long drx = cur[i].rx_bytes - prev[i].rx_bytes;
            unsigned long long dtx = cur[i].tx_bytes - prev[i].tx_bytes;
            int rx_kbps = (drx > 0) ? (int)(drx / 1024) : 0;
            int tx_kbps = (dtx > 0) ? (int)(dtx / 1024) : 0;
            fprintf(csv, ",%d,%d", rx_kbps, tx_kbps);
            prev[i] = cur[i];
        }
        fprintf(csv, "\n");
        fflush(csv);
    }

    *out_count = n_if;
    return 0;
}
