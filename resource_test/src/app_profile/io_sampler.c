/**
 * io_sampler.c — 磁盘 IO 吞吐量采样模块
 *
 * 读取 /proc/PID/io 的 read_bytes/write_bytes (累计值),
 * 计算相邻两次采样的差值作为吞吐量 (KB/s), 写入 CSV。
 *
 * 接口: io_sampler_run(pid, count, interval, snap, csv)
 */
#include "sampler.h"

int io_sampler_run(pid_t pid, int count, int interval_sec,
                    ResourceSnapshot *snap, FILE *csv) {
    long prev_r = -1, prev_w = -1;
    long r_sum = 0, w_sum = 0;
    int samples = 0;

    for (int i = 1; i <= count && g_running; i++) {
        char path[MAX_PATH], line[MAX_LINE];
        snprintf(path, sizeof(path), "/proc/%d/io", pid);
        long rbytes = -1, wbytes = -1;

        FILE *f = fopen(path, "r");
        if (f) {
            while (fgets(line, sizeof(line), f)) {
                if (strncmp(line, "read_bytes:", 11) == 0)  sscanf(line + 11, "%ld", &rbytes);
                if (strncmp(line, "write_bytes:", 12) == 0) sscanf(line + 12, "%ld", &wbytes);
            }
            fclose(f);
        }

        long r_kb = 0, w_kb = 0;
        if (prev_r >= 0 && rbytes >= 0 && rbytes >= prev_r)
            r_kb = (rbytes - prev_r) / 1024 / interval_sec;  /* delta / 1024 / s → KB/s */
        if (prev_w >= 0 && wbytes >= 0 && wbytes >= prev_w)
            w_kb = (wbytes - prev_w) / 1024 / interval_sec;

        fprintf(csv, "%d,%ld,%ld\n", i, r_kb, w_kb);
        r_sum += r_kb;
        w_sum += w_kb;
        samples++;

        prev_r = rbytes;
        prev_w = wbytes;
        sleep(interval_sec);
        { char _p[64]; snprintf(_p, sizeof(_p), "/proc/%d", pid);
          struct stat _st; if (stat(_p, &_st) != 0 || !S_ISDIR(_st.st_mode)) break; }
    }

    snap->io_read_kb  = samples > 1 ? (r_sum / (samples - 1)) : 0;
    snap->io_write_kb = samples > 1 ? (w_sum / (samples - 1)) : 0;
    snap->io_samples  = samples;
    return samples > 0 ? 0 : -1;
}
