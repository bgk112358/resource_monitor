/**
 * test_app_profile.cpp — GTest unit tests for app_profile sampler modules
 *
 * 测试所有采样模块的 API:
 *   - cpu_sampler_run     (CPU 采样)
 *   - mem_sampler_run     (内存 RSS 采样)
 *   - thrfd_sampler_run   (线程 + FD 采样)
 *   - io_sampler_run      (磁盘 IO 采样)
 *   - proc_validate / get_name / count_children
 *   - report_mk_outdir / csv_open / report_write
 */

#include <gtest/gtest.h>

extern "C" {
#include "sampler.h"
}

#include <fstream>
#include <chrono>
#include <thread>
#include <sys/wait.h>

/* ══════════════════════════════════════════════════════════════════
 *   Fixture
 * ══════════════════════════════════════════════════════════════════ */
class AppProfileTest : public ::testing::Test {
protected:
    pid_t self_pid_;

    void SetUp() override {
        self_pid_ = getpid();
    }
};

/* ══════════════════════════════════════════════════════════════════
 *   proc_reader
 * ══════════════════════════════════════════════════════════════════ */

TEST_F(AppProfileTest, ValidateSelf) {
    EXPECT_EQ(0, proc_validate(self_pid_));
}

TEST_F(AppProfileTest, ValidateInvalidPid) {
    EXPECT_EQ(-1, proc_validate(99999999));
}

TEST_F(AppProfileTest, GetName) {
    char name[256];
    ASSERT_EQ(0, proc_get_name(self_pid_, name, sizeof(name)));
    EXPECT_GT(strlen(name), 0u);
}

TEST_F(AppProfileTest, CountChildrenNoFork) {
    int n = proc_count_children(self_pid_);
    EXPECT_GE(n, 0);
}

TEST_F(AppProfileTest, CountChildrenWithFork) {
    pid_t child = fork();
    if (child == 0) { usleep(50000); _exit(0); }
    usleep(10000);
    int n = proc_count_children(self_pid_);
    EXPECT_GE(n, 1);
    waitpid(child, NULL, 0);
}

/* ══════════════════════════════════════════════════════════════════
 *   cpu_sampler
 * ══════════════════════════════════════════════════════════════════ */

TEST_F(AppProfileTest, CpuSamplerSmoke) {
    ResourceSnapshot snap;
    memset(&snap, 0, sizeof(snap));

    FILE *csv = tmpfile();
    ASSERT_NE(nullptr, csv);

    /* 采集自己 3 秒, 含一些 CPU 工作 */
    std::thread worker([]{
        volatile double x = 0;
        for (int i = 0; i < 500000; i++) x += i * 0.001;
    });

    int ret = cpu_sampler_run(self_pid_, 3, &snap, csv);
    worker.join();
    fclose(csv);

    EXPECT_EQ(0, ret);
    EXPECT_GE(snap.cpu_samples, 1);
    EXPECT_GE(snap.cpu_avg, 0.0);
    EXPECT_LE(snap.cpu_avg, 100.0);
}

TEST_F(AppProfileTest, CpuSamplerInvalidPid) {
    ResourceSnapshot snap;
    memset(&snap, 0, sizeof(snap));
    FILE *csv = tmpfile();
    int ret = cpu_sampler_run(99999999, 1, &snap, csv);
    fclose(csv);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(0, snap.cpu_samples);
}

/* ══════════════════════════════════════════════════════════════════
 *   mem_sampler
 * ══════════════════════════════════════════════════════════════════ */

TEST_F(AppProfileTest, MemSamplerSmoke) {
    ResourceSnapshot snap;
    memset(&snap, 0, sizeof(snap));
    FILE *csv = tmpfile();
    ASSERT_NE(nullptr, csv);

    int ret = mem_sampler_run(self_pid_, 2, 1, &snap, csv);
    fclose(csv);

    EXPECT_EQ(0, ret);
    EXPECT_GT(snap.rss_kb, 0);
    EXPECT_EQ(2, snap.mem_samples);
}

TEST_F(AppProfileTest, MemSamplerInvalidPid) {
    ResourceSnapshot snap;
    memset(&snap, 0, sizeof(snap));
    FILE *csv = tmpfile();
    /* 无效 PID: 采样器运行一次后检测进程不存在, 返回 0 (采样到退出) */
    int ret = mem_sampler_run(99999999, 1, 1, &snap, csv);
    fclose(csv);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(0, snap.rss_kb);  /* 进程不存在, RSS 为 0 */
}

/* ══════════════════════════════════════════════════════════════════
 *   thread_fd_sampler
 * ══════════════════════════════════════════════════════════════════ */

TEST_F(AppProfileTest, ThrFdSamplerSmoke) {
    ResourceSnapshot snap;
    memset(&snap, 0, sizeof(snap));
    FILE *csv = tmpfile();
    ASSERT_NE(nullptr, csv);

    int ret = thrfd_sampler_run(self_pid_, 2, 1, &snap, csv);
    fclose(csv);

    EXPECT_EQ(0, ret);
    EXPECT_GE(snap.threads, 1);   /* main thread */
    EXPECT_GE(snap.fds, 3);       /* stdin/stdout/stderr */
}

TEST_F(AppProfileTest, ThrFdSamplerInvalidPid) {
    ResourceSnapshot snap;
    memset(&snap, 0, sizeof(snap));
    FILE *csv = tmpfile();
    /* 无效 PID: 采样器运行一次后检测进程不存在, 返回 0 */
    int ret = thrfd_sampler_run(99999999, 1, 1, &snap, csv);
    fclose(csv);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(0, snap.threads);
    EXPECT_EQ(0, snap.fds);
}

/* ══════════════════════════════════════════════════════════════════
 *   io_sampler
 * ══════════════════════════════════════════════════════════════════ */

TEST_F(AppProfileTest, IoSamplerSmoke) {
    ResourceSnapshot snap;
    memset(&snap, 0, sizeof(snap));
    FILE *csv = tmpfile();
    ASSERT_NE(nullptr, csv);

    int ret = io_sampler_run(self_pid_, 2, 1, &snap, csv);
    fclose(csv);

    /* IO may fail without root (returns 0 for reads), but must not crash */
    EXPECT_GE(ret, -1);
    EXPECT_GE(snap.io_read_kb, 0);
    EXPECT_GE(snap.io_write_kb, 0);
}

/* ══════════════════════════════════════════════════════════════════
 *   report
 * ══════════════════════════════════════════════════════════════════ */

TEST_F(AppProfileTest, ReportMkOutdir) {
    char *dir = report_mk_outdir("/tmp/test_report", self_pid_);
    ASSERT_NE(nullptr, dir);
    /* verify directory exists */
    EXPECT_EQ(0, access(dir, F_OK));
    /* clean up */
    rmdir(dir);
    free(dir);
}

TEST_F(AppProfileTest, ReportCsvOpen) {
    char dir[] = "/tmp/test_csv_XXXXXX";
    char *d = mkdtemp(dir);
    ASSERT_NE(nullptr, d);

    FILE *f = report_csv_open(d, "test.csv", "col1,col2");
    ASSERT_NE(nullptr, f);
    fprintf(f, "1,hello\n");
    fclose(f);

    /* verify content */
    std::ifstream in(std::string(d) + "/test.csv");
    std::string line;
    std::getline(in, line);
    EXPECT_EQ("col1,col2", line);

    unlink((std::string(d) + "/test.csv").c_str());
    rmdir(d);
}

TEST_F(AppProfileTest, ReportWrite) {
    char *dir = report_mk_outdir("/tmp/test_full", self_pid_);
    ASSERT_NE(nullptr, dir);

    ResourceSnapshot snap;
    snap.cpu_avg = 5.2; snap.cpu_peak = 8.0; snap.cpu_samples = 10;
    snap.rss_kb = 12345; snap.mem_samples = 5;
    snap.threads = 4; snap.fds = 9; snap.thrfd_samples = 5;
    snap.io_read_kb = 10; snap.io_write_kb = 20; snap.io_samples = 5;
    snap.child_count = 2;

    int ret = report_write(dir, "test_app", self_pid_, 10, &snap);
    EXPECT_EQ(0, ret);

    /* verify report.txt exists and has expected content */
    std::string rpt = std::string(dir) + "/report.txt";
    std::ifstream in(rpt);
    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    EXPECT_NE(std::string::npos, content.find("test_app"));
    EXPECT_NE(std::string::npos, content.find("5.2%"));
    EXPECT_NE(std::string::npos, content.find("12345 KB"));
    EXPECT_NE(std::string::npos, content.find("线程数:    4"));
    EXPECT_NE(std::string::npos, content.find("文件描述符: 9"));

    /* clean up */
    unlink(rpt.c_str());
    rmdir(dir);
    free(dir);
}

/* ══════════════════════════════════════════════════════════════════
 *   Full pipeline integration test
 * ══════════════════════════════════════════════════════════════════ */

TEST_F(AppProfileTest, FullPipeline) {
    char *dir = report_mk_outdir("/tmp/test_pipeline", self_pid_);
    ASSERT_NE(nullptr, dir);

    ResourceSnapshot snap;
    memset(&snap, 0, sizeof(snap));

    FILE *fcpu = report_csv_open(dir, "cpu.csv",  "sec,cpu_percent");
    FILE *fmem = report_csv_open(dir, "mem.csv",  "sample,rss_kb");
    FILE *fthr = report_csv_open(dir, "threads.csv", "sample,threads,fd");
    FILE *fio  = report_csv_open(dir, "io.csv",   "sample,read_kb,write_kb");

    /* tiny duration + tiny interval: fastest possible full pipeline */
    g_running = 1;
    cpu_sampler_run(self_pid_, 2, &snap, fcpu);   fclose(fcpu);
    mem_sampler_run(self_pid_, 2, 1, &snap, fmem); fclose(fmem);
    thrfd_sampler_run(self_pid_, 2, 1, &snap, fthr); fclose(fthr);
    io_sampler_run(self_pid_, 2, 1, &snap, fio);     fclose(fio);
    snap.child_count = proc_count_children(self_pid_);

    EXPECT_GE(snap.rss_kb, 0);
    EXPECT_GE(snap.threads, 1);

    /* report */
    char proc_name[256] = "pipeline_test";
    report_write(dir, proc_name, self_pid_, 2, &snap);

    /* verify all files exist */
    EXPECT_EQ(0, access((std::string(dir) + "/cpu.csv").c_str(), F_OK));
    EXPECT_EQ(0, access((std::string(dir) + "/mem.csv").c_str(), F_OK));
    EXPECT_EQ(0, access((std::string(dir) + "/threads.csv").c_str(), F_OK));
    EXPECT_EQ(0, access((std::string(dir) + "/io.csv").c_str(), F_OK));
    EXPECT_EQ(0, access((std::string(dir) + "/report.txt").c_str(), F_OK));

    /* clean up */
    unlink((std::string(dir) + "/cpu.csv").c_str());
    unlink((std::string(dir) + "/mem.csv").c_str());
    unlink((std::string(dir) + "/threads.csv").c_str());
    unlink((std::string(dir) + "/io.csv").c_str());
    unlink((std::string(dir) + "/report.txt").c_str());
    rmdir(dir);
    free(dir);
}
