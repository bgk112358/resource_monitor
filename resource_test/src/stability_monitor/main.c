/**
 * stability_monitor.c — TBox 24h 稳定性监控 (C 版)
 *
 * 替代 stability_24h.sh, 每 10 秒采样一次 PID 的 CPU/RSS/线程/FD,
 * 写入 CSV, 运行 24 小时 (8640 次采样), 用于检测内存泄漏。
 *
 * 用法:
 *   ./stability_monitor <PID> [output_csv]
 *   ./stability_monitor 12345
 *   ./stability_monitor 12345 /tmp/stability.csv
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>

#define INTERVAL_SEC        10
#define MAX_SAMPLES         8640    /* 24h = 86400s / 10s */
#define PROGRESS_EVERY      360     /* 每小时打印一次进度 */

static volatile int g_running = 1;
static void sig_handler(int sig) { (void)sig; g_running = 0; }

static long read_rss(pid_t pid) {
    char path[256], line[512];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    long rss = -1;
    while (fgets(line, sizeof(line), f))
        if (strncmp(line, "VmRSS:", 6) == 0) { sscanf(line+6, "%ld", &rss); break; }
    fclose(f); return rss;
}

static double read_cpu_pct(pid_t pid, unsigned long long *prev_total) {
    char path[256], line[512];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *f = fopen(path, "r"); if (!f) return -1;
    fgets(line, sizeof(line), f); fclose(f);
    char *p = line;
    for (int i=1; i<14; i++) { p=strchr(p,' '); if(!p) return -1; p++; }
    unsigned long long utime = strtoull(p, &p, 10);
    unsigned long long stime = strtoull(p+1, NULL, 10);
    unsigned long long total = utime + stime;
    double cpu = -1;
    if (*prev_total > 0) {
        long ticks = sysconf(_SC_CLK_TCK); if (ticks<=0) ticks=100;
        cpu = (double)(total - *prev_total) / (double)ticks / (double)INTERVAL_SEC * 100.0;
        if (cpu<0) cpu=0; if (cpu>100) cpu=100;
    }
    *prev_total = total;
    return cpu;
}

static int count_threads(pid_t pid) {
    char path[256]; snprintf(path,sizeof(path),"/proc/%d/task",pid);
    DIR *d=opendir(path); if(!d) return -1;
    int n=0; struct dirent *de;
    while((de=readdir(d))!=NULL) if(de->d_name[0]!='.') n++;
    closedir(d); return n;
}

static int count_fds(pid_t pid) {
    char path[256]; snprintf(path,sizeof(path),"/proc/%d/fd",pid);
    DIR *d=opendir(path); if(!d) return -1;
    int n=0; struct dirent *de;
    while((de=readdir(d))!=NULL) if(de->d_name[0]!='.') n++;
    closedir(d); return n;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "用法: %s <PID> [output_csv]\n", argv[0]);
        return 1;
    }
    pid_t pid = (pid_t)atoi(argv[1]);
    const char *outfile = (argc>2) ? argv[2] : NULL;

    if (kill(pid,0)!=0) { fprintf(stderr,"❌ PID=%d 不存在\n",pid); return 1; }

    char csv_path[512];
    if (outfile) snprintf(csv_path,sizeof(csv_path),"%s",outfile);
    else {
        time_t now=time(NULL); struct tm *tm=localtime(&now);
        strftime(csv_path,sizeof(csv_path),"/tmp/stability_%Y%m%d_%H%M%S.csv",tm);
    }

    signal(SIGINT, sig_handler); signal(SIGTERM, sig_handler);

    FILE *f = fopen(csv_path, "w");
    if (!f) { perror("fopen"); return 1; }
    fprintf(f, "sample,timestamp,cpu%%,rss_kb,threads,fd\n");

    printf("📊 24h 稳定性监控: PID=%d → %s\n", pid, csv_path);
    printf("   每 %ds 采样, 共 %d 次, Ctrl+C 提前结束\n", INTERVAL_SEC, MAX_SAMPLES);

    int sample=0;
    unsigned long long prev_total=0;
    /* 第一次采样获取基线 */
    read_cpu_pct(pid, &prev_total);
    sleep(INTERVAL_SEC);

    while (sample < MAX_SAMPLES && g_running) {
        if (kill(pid,0)!=0) { printf("⚠  进程 PID=%d 已退出 (采样 %d 次)\n", pid, sample); break; }

        sample++;
        double cpu = read_cpu_pct(pid, &prev_total);
        long rss = read_rss(pid);
        int thr = count_threads(pid);
        int fd = count_fds(pid);

        time_t now=time(NULL); char ts[16]; strftime(ts,sizeof(ts),"%H:%M:%S",localtime(&now));
        fprintf(f, "%d,%s,%.1f,%ld,%d,%d\n", sample, ts, cpu>0?cpu:0, rss>0?rss:0, thr, fd);
        fflush(f);

        if (sample % PROGRESS_EVERY == 0)
            printf("  [%4d/%4d] %s CPU:%.1f%% RSS:%ldKB Threads:%d FD:%d\n",
                   sample, MAX_SAMPLES, ts, cpu>0?cpu:0, rss>0?rss:0, thr, fd);

        sleep(INTERVAL_SEC);
    }

    fclose(f);
    printf("\n✅ 完成! 采样 %d 次, 数据: %s\n", sample, csv_path);
    printf("   分析: 用 Excel 打开, 对 rss_kb 列画折线图, 斜率≈0 则无泄漏\n");
    return 0;
}
