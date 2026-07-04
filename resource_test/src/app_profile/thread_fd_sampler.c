/**
 * thread_fd_sampler.c — 线程数与文件描述符数采样模块
 *
 * 计数 /proc/PID/task 和 /proc/PID/fd 下的条目数,
 * 每 interval_sec 采样一次, 写入 CSV, 返回均值。
 *
 * 接口: thrfd_sampler_run(pid, count, interval, snap, csv)
 */
#include "sampler.h"

int thrfd_sampler_run(pid_t pid, int count, int interval_sec,
                       ResourceSnapshot *snap, FILE *csv) {
    long thr_sum = 0, fd_sum = 0;
    int samples = 0;

    for (int i = 1; i <= count && g_running; i++) {
        int thr = -1;
        {   /* 计数 /proc/PID/task */
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "/proc/%d/task", pid);
            DIR *d = opendir(path);
            if (d) {
                thr = 0;
                struct dirent *de;
                while ((de = readdir(d)) != NULL)
                    if (de->d_name[0] != '.') thr++;
                closedir(d);
            }
        }

        int fd = -1;
        {   /* 计数 /proc/PID/fd */
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "/proc/%d/fd", pid);
            DIR *d = opendir(path);
            if (d) {
                fd = 0;
                struct dirent *de;
                while ((de = readdir(d)) != NULL)
                    if (de->d_name[0] != '.') fd++;
                closedir(d);
            }
        }

        fprintf(csv, "%d,%d,%d\n", i, thr, fd);
        if (thr >= 0) thr_sum += thr;
        if (fd >= 0)  fd_sum  += fd;
        samples++;

        sleep(interval_sec);
        if (kill(pid, 0) != 0) break;
    }

    snap->threads        = samples > 0 ? (int)(thr_sum / samples) : 0;
    snap->fds            = samples > 0 ? (int)(fd_sum  / samples) : 0;
    snap->thrfd_samples  = samples;
    return samples > 0 ? 0 : -1;
}
