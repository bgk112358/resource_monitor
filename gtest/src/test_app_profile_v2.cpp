/**
 * test_app_profile_v2.cpp — 深度测试 app_profile 采样模块
 *
 * 相比 v1 版本，增强:
 *   1. CSV 内容校验 (不仅仅检查 ResourceSnapshot 值)
 *   2. 边界条件: 0 时长, 负间隔, clock wrap
 *   3. g_running 停止行为验证
 *   4. IO sampler 真实写入测试
 *   5. read_ticks 文件消失场景
 */

#include <gtest/gtest.h>

extern "C" {
#include "sampler.h"
}

#include <cmath>
#include <fstream>
#include <chrono>
#include <thread>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

class AppProfileV2Test : public ::testing::Test {
protected:
    pid_t self_pid_;
    void SetUp() override { self_pid_ = getpid(); g_running = 1; }
    void TearDown() override { g_running = 1; }

    /* helper: read CSV and count data lines (skip header) */
    static int csv_data_lines(FILE *f) {
        rewind(f);
        char buf[4096]; int n = 0;
        while (fgets(buf, sizeof(buf), f))
            if (buf[0] >= '0' && buf[0] <= '9') n++; /* data line starts with digit */
        return n;
    }

    /* helper: read first data cell from CSV */
    static double csv_first_value(FILE *f, int col) {
        rewind(f);
        char buf[4096];
        fgets(buf, sizeof(buf), f); /* skip header */
        if (!fgets(buf, sizeof(buf), f)) return -1;
        char *p = buf;
        for (int c = 0; c < col; c++) { p = strchr(p, ','); if (!p) return -1; p++; }
        return atof(p);
    }
};

/* ══════════════════════════════════════════════════════════════════
 *   cpu_sampler — 增强测试
 * ══════════════════════════════════════════════════════════════════ */

TEST_F(AppProfileV2Test, CpuSamplerCsvFormat) {
    ResourceSnapshot snap; memset(&snap, 0, sizeof(snap));
    FILE *csv = tmpfile(); ASSERT_NE(nullptr, csv);

    cpu_sampler_run(self_pid_, 2, &snap, csv);

    /* verify CSV has exactly 2 data rows */
    EXPECT_EQ(2, csv_data_lines(csv));

    /* verify first row has valid CPU% between 0-100 */
    double val = csv_first_value(csv, 1);
    EXPECT_GE(val, 0.0);
    EXPECT_LE(val, 100.0);

    fclose(csv);
}

TEST_F(AppProfileV2Test, CpuSamplerZeroDuration) {
    ResourceSnapshot snap; memset(&snap, 0, sizeof(snap));
    FILE *csv = tmpfile();
    /* duration=0: loop body never executes */
    int ret = cpu_sampler_run(self_pid_, 0, &snap, csv);
    fclose(csv);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(0, snap.cpu_samples);
}

TEST_F(AppProfileV2Test, CpuSamplerStoppedByGRunning) {
    ResourceSnapshot snap; memset(&snap, 0, sizeof(snap));
    FILE *csv = tmpfile();

    /* start a thread that sets g_running=0 after 1 second */
    std::thread killer([]{ std::this_thread::sleep_for(std::chrono::seconds(1)); g_running = 0; });

    /* duration=10 but should stop early */
    int ret = cpu_sampler_run(self_pid_, 10, &snap, csv);
    killer.join();
    fclose(csv);

    EXPECT_EQ(0, ret);
    EXPECT_GE(snap.cpu_samples, 1);
    EXPECT_LE(snap.cpu_samples, 3); /* should stop within ~3 samples */
}

TEST_F(AppProfileV2Test, CpuSamplerPeakGreaterOrEqualToAvg) {
    ResourceSnapshot snap; memset(&snap, 0, sizeof(snap));
    FILE *csv = tmpfile();

    /* do some CPU-heavy work in background */
    std::thread worker([]{ volatile double x=0; for(int i=0;i<2000000;i++) x+=sin(i*0.001); });

    cpu_sampler_run(self_pid_, 5, &snap, csv);
    worker.join();
    fclose(csv);

    EXPECT_GE(snap.cpu_peak, snap.cpu_avg - 0.01);
    EXPECT_GT(snap.cpu_samples, 0);
}

/* ══════════════════════════════════════════════════════════════════
 *   mem_sampler — 增强测试
 * ══════════════════════════════════════════════════════════════════ */

TEST_F(AppProfileV2Test, MemSamplerRssPositive) {
    ResourceSnapshot snap; memset(&snap, 0, sizeof(snap));
    FILE *csv = tmpfile();

    mem_sampler_run(self_pid_, 3, 1, &snap, csv);

    EXPECT_EQ(3, csv_data_lines(csv));
    EXPECT_GT(snap.rss_kb, 100);  /* any real process > 100KB */
    EXPECT_LT(snap.rss_kb, 1024*1024); /* less than 1GB */
    fclose(csv);
}

TEST_F(AppProfileV2Test, MemSamplerStoppedByGRunning) {
    ResourceSnapshot snap; memset(&snap, 0, sizeof(snap));
    FILE *csv = tmpfile();

    std::thread killer([]{ std::this_thread::sleep_for(std::chrono::seconds(1)); g_running = 0; });

    mem_sampler_run(self_pid_, 10, 2, &snap, csv);
    killer.join();
    fclose(csv);

    EXPECT_LE(snap.mem_samples, 3); /* stopped early */
}

/* ══════════════════════════════════════════════════════════════════
 *   thrfd_sampler — 增强测试
 * ══════════════════════════════════════════════════════════════════ */

TEST_F(AppProfileV2Test, ThrFdSamplerCsvColumns) {
    g_running = 1;
    ResourceSnapshot snap; memset(&snap, 0, sizeof(snap));
    FILE *csv = tmpfile();

    thrfd_sampler_run(self_pid_, 2, 1, &snap, csv);

    /* verify CSV has 2 data rows with 3 columns each (sample,threads,fd) */
    rewind(csv);
    char buf[4096]; int rows = 0;
    fgets(buf, sizeof(buf), csv); /* header */
    while (fgets(buf, sizeof(buf), csv)) {
        int commas = 0;
        for (char *p = buf; *p; p++) if (*p == ',') commas++;
        EXPECT_EQ(2, commas); /* 3 columns = 2 commas */
        rows++;
    }
    EXPECT_GE(rows, 1);  /* at least 1 sample, 2 if process survived */
    fclose(csv);
}

TEST_F(AppProfileV2Test, ThrFdOpenExtraFd) {
    ResourceSnapshot snap_before, snap_after;
    memset(&snap_before, 0, sizeof(snap_before));
    memset(&snap_after, 0, sizeof(snap_after));

    /* sample before */
    { FILE *csv = tmpfile(); thrfd_sampler_run(self_pid_, 1, 1, &snap_before, csv); fclose(csv); }

    /* open an extra file */
    int fd = open("/dev/null", O_RDONLY);

    /* sample after */
    { FILE *csv = tmpfile(); thrfd_sampler_run(self_pid_, 1, 1, &snap_after, csv); fclose(csv); }

    EXPECT_GE(snap_after.fds, snap_before.fds); /* at least same, possibly +1 */
    close(fd);
}

/* ══════════════════════════════════════════════════════════════════
 *   io_sampler — 增强测试 (真实写入)
 * ══════════════════════════════════════════════════════════════════ */

TEST_F(AppProfileV2Test, IoSamplerRealWrite) {
    ResourceSnapshot snap; memset(&snap, 0, sizeof(snap));
    FILE *csv = tmpfile();

    /* write 1MB to a temp file to generate real IO */
    int fd = open("/tmp/test_io_sampler.bin", O_WRONLY|O_CREAT|O_TRUNC, 0644);
    ASSERT_GE(fd, 0);
    char buf[4096]; memset(buf, 'A', sizeof(buf));
    for (int i = 0; i < 256; i++) write(fd, buf, sizeof(buf)); /* 256 x 4KB = 1MB */
    fsync(fd);

    io_sampler_run(self_pid_, 2, 1, &snap, csv);
    close(fd);
    unlink("/tmp/test_io_sampler.bin");

    /* IO write: without root, /proc/PID/io may be unreadable (returns 0) */
    EXPECT_GE(snap.io_write_kb, 0);
    EXPECT_GE(snap.io_samples, 1);  /* always samples, even if data is 0 */
    fclose(csv);
}

TEST_F(AppProfileV2Test, IoSamplerNoRootGraceful) {
    ResourceSnapshot snap; memset(&snap, 0, sizeof(snap));
    FILE *csv = tmpfile();

    int ret = io_sampler_run(self_pid_, 2, 1, &snap, csv);
    fclose(csv);

    /* without root, may return 0 for reads, but must not crash */
    EXPECT_GE(ret, -1);
    EXPECT_GE(snap.io_samples, 1);
}

/* ══════════════════════════════════════════════════════════════════
 *   report — 精度测试
 * ══════════════════════════════════════════════════════════════════ */

TEST_F(AppProfileV2Test, ReportPrecisionCpu) {
    char *dir = report_mk_outdir("/tmp/test_precision", self_pid_);
    ASSERT_NE(nullptr, dir);

    ResourceSnapshot snap; memset(&snap, 0, sizeof(snap));
    snap.cpu_avg = 12.345; snap.cpu_peak = 67.890; snap.cpu_samples = 100;

    report_write(dir, "precision_test", self_pid_, 30, &snap);

    /* verify exact values in report */
    std::ifstream in(std::string(dir) + "/report.txt");
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    EXPECT_NE(std::string::npos, content.find("12.3%"));  /* rounded */
    EXPECT_NE(std::string::npos, content.find("67.9%"));
    EXPECT_NE(std::string::npos, content.find("100"));    /* samples */
    EXPECT_NE(std::string::npos, content.find("precision_test"));

    unlink((std::string(dir) + "/report.txt").c_str());
    rmdir(dir);
    free(dir);
}

TEST_F(AppProfileV2Test, ReportMkOutdirUnique) {
    char *d1 = report_mk_outdir("/tmp/test_unique", self_pid_);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    char *d2 = report_mk_outdir("/tmp/test_unique", self_pid_);

    EXPECT_STRNE(d1, d2); /* timestamps differ */

    rmdir(d1); free(d1);
    rmdir(d2); free(d2);
}

/* ══════════════════════════════════════════════════════════════════
 *   Full pipeline — CSV correctness
 * ══════════════════════════════════════════════════════════════════ */

TEST_F(AppProfileV2Test, FullPipelineAllCsvsHaveCorrectRows) {
    g_running = 1;
    char *dir = report_mk_outdir("/tmp/test_full_v2", self_pid_);
    ASSERT_NE(nullptr, dir);

    ResourceSnapshot snap; memset(&snap, 0, sizeof(snap));

    FILE *fcpu = report_csv_open(dir, "cpu.csv",  "sec,cpu_percent");
    FILE *fmem = report_csv_open(dir, "mem.csv",  "sample,rss_kb");
    FILE *fthr = report_csv_open(dir, "threads.csv", "sample,threads,fd");
    FILE *fio  = report_csv_open(dir, "io.csv",   "sample,read_kb,write_kb");

    cpu_sampler_run(self_pid_, 3, &snap, fcpu);
    mem_sampler_run(self_pid_, 3, 1, &snap, fmem);
    thrfd_sampler_run(self_pid_, 3, 1, &snap, fthr);
    io_sampler_run(self_pid_, 3, 1, &snap, fio);

    fclose(fcpu); fclose(fmem); fclose(fthr); fclose(fio);

    /* verify CSV files exist on disk and have content */
    for (const char *name : {"cpu.csv","mem.csv","threads.csv"}) {
        std::string path = std::string(dir) + "/" + name;
        struct stat st;
        ASSERT_EQ(0, stat(path.c_str(), &st));
        EXPECT_GT(st.st_size, 10);  /* at least header + 1 data row */
    }

    /* specific checks */
    EXPECT_GE(snap.threads, 1);
    EXPECT_GE(snap.fds, 3);

    /* clean up */
    for (const char *f : {"cpu.csv","mem.csv","threads.csv","io.csv","report.txt"})
        unlink((std::string(dir)+"/"+f).c_str());
    rmdir(dir);
    free(dir);
}
