/**
 * test_proc_reader.cpp — GTest unit tests for proc_reader.h functions
 *
 * 测试所有 /proc 读取函数:
 *   - proc_read_cpu_ticks  / proc_calc_cpu_pct
 *   - proc_read_rss_kb
 *   - proc_count_threads
 *   - proc_count_fds
 *   - proc_read_io
 *   - proc_count_children
 *
 * 编译:
 *   cmake -B build -DBUILD_TESTS=ON
 *   cmake --build build
 *   ctest --test-dir build -V
 */

#include <gtest/gtest.h>

/* C header with extern "C" guard */
extern "C" {
#include "proc_reader.h"
}

#include <chrono>
#include <thread>
#include <atomic>
#include <fstream>
#include <unistd.h>
#include <sys/wait.h>

/* ══════════════════════════════════════════════════════════════════
 *   Fixture
 * ══════════════════════════════════════════════════════════════════ */
class ProcReaderTest : public ::testing::Test {
protected:
    pid_t self_pid_;

    void SetUp() override {
        self_pid_ = getpid();
    }
};

/* ══════════════════════════════════════════════════════════════════
 *   CPU ticks
 * ══════════════════════════════════════════════════════════════════ */

TEST_F(ProcReaderTest, ReadCpuTicksSelf) {
    unsigned long long u1, s1, u2, s2;
    ASSERT_EQ(0, proc_read_cpu_ticks(self_pid_, &u1, &s1));
    /* do some work */
    volatile double x = 0;
    for (int i = 0; i < 500000; i++) x += i * 0.001;
    (void)x;
    ASSERT_EQ(0, proc_read_cpu_ticks(self_pid_, &u2, &s2));
    /* CPU time should have increased */
    EXPECT_LE(u1, u2);
    EXPECT_LE(s1, s2);
}

TEST_F(ProcReaderTest, ReadCpuTicksInvalidPid) {
    unsigned long long u, s;
    EXPECT_EQ(-1, proc_read_cpu_ticks(99999999, &u, &s));
}

TEST_F(ProcReaderTest, CalcCpuPctZeroDelta) {
    double pct = proc_calc_cpu_pct(100, 0, 100, 0, 100);
    EXPECT_DOUBLE_EQ(0.0, pct);
}

TEST_F(ProcReaderTest, CalcCpuPctOnePercent) {
    /* 1 tick out of 100 ticks/sec between two reads = 1% */
    double pct = proc_calc_cpu_pct(0, 0, 1, 0, 100);
    EXPECT_NEAR(1.0, pct, 0.01);
}

TEST_F(ProcReaderTest, CalcCpuPctFullCore) {
    /* 100 ticks in 1 sec at 100 ticks/sec = 100% */
    double pct = proc_calc_cpu_pct(0, 0, 100, 0, 100);
    EXPECT_NEAR(100.0, pct, 0.01);
}

TEST_F(ProcReaderTest, CalcCpuPctClampAbove100) {
    double pct = proc_calc_cpu_pct(0, 0, 200, 0, 100);
    EXPECT_DOUBLE_EQ(100.0, pct);
}

TEST_F(ProcReaderTest, CalcCpuPctClampNegative) {
    /* time going backwards (unlikely) → clamp to 0 */
    double pct = proc_calc_cpu_pct(200, 0, 100, 0, 100);
    EXPECT_DOUBLE_EQ(0.0, pct);
}

/* ══════════════════════════════════════════════════════════════════
 *   RSS
 * ══════════════════════════════════════════════════════════════════ */

TEST_F(ProcReaderTest, ReadRssSelf) {
    long rss = proc_read_rss_kb(self_pid_);
    EXPECT_GT(rss, 0);  /* any real process has > 0 RSS */
    EXPECT_LT(rss, 1024 * 1024);  /* less than 1 GB */
}

TEST_F(ProcReaderTest, ReadRssInvalidPid) {
    EXPECT_EQ(-1, proc_read_rss_kb(99999999));
}

/* ══════════════════════════════════════════════════════════════════
 *   Threads
 * ══════════════════════════════════════════════════════════════════ */

TEST_F(ProcReaderTest, CountThreadsSelf) {
    int n = proc_count_threads(self_pid_);
    EXPECT_GE(n, 1);  /* at least the main thread */
}

TEST_F(ProcReaderTest, CountThreadsInvalidPid) {
    EXPECT_EQ(-1, proc_count_threads(99999999));
}

/* ══════════════════════════════════════════════════════════════════
 *   FDs
 * ══════════════════════════════════════════════════════════════════ */

TEST_F(ProcReaderTest, CountFdsSelf) {
    int n = proc_count_fds(self_pid_);
    EXPECT_GE(n, 3);  /* stdin, stdout, stderr at minimum */
}

TEST_F(ProcReaderTest, CountFdsInvalidPid) {
    EXPECT_EQ(-1, proc_count_fds(99999999));
}

/* ══════════════════════════════════════════════════════════════════
 *   IO
 * ══════════════════════════════════════════════════════════════════ */

TEST_F(ProcReaderTest, ReadIoSelf) {
    long r, w;
    int ret = proc_read_io(self_pid_, &r, &w);
    /* may fail without root, which is OK — check for crash-free */
    if (ret == 0) {
        EXPECT_GE(r, 0);
        EXPECT_GE(w, 0);
    }
}

TEST_F(ProcReaderTest, ReadIoInvalidPid) {
    long r, w;
    EXPECT_EQ(-1, proc_read_io(99999999, &r, &w));
}

/* ══════════════════════════════════════════════════════════════════
 *   Children
 * ══════════════════════════════════════════════════════════════════ */

TEST_F(ProcReaderTest, CountChildrenSelf) {
    int n = proc_count_children(self_pid_);
    EXPECT_GE(n, 0);  /* may have 0 children */
}

TEST_F(ProcReaderTest, CountChildrenWithFork) {
    pid_t child = fork();
    if (child == 0) {
        /* child: sleep briefly then exit */
        usleep(50000);
        _exit(0);
    }
    /* parent: wait briefly then count children */
    usleep(10000);
    int n = proc_count_children(self_pid_);
    EXPECT_GE(n, 1);  /* should see at least the fork child */
    waitpid(child, NULL, 0);
}

/* ══════════════════════════════════════════════════════════════════
 *   Consistency checks
 * ══════════════════════════════════════════════════════════════════ */

TEST_F(ProcReaderTest, RssStableAcrossReads) {
    /* Multiple RSS reads within a short time should be identical */
    long r1 = proc_read_rss_kb(self_pid_);
    long r2 = proc_read_rss_kb(self_pid_);
    EXPECT_EQ(r1, r2);
}

TEST_F(ProcReaderTest, ThreadCountStable) {
    int n1 = proc_count_threads(self_pid_);
    int n2 = proc_count_threads(self_pid_);
    EXPECT_EQ(n1, n2);
}

TEST_F(ProcReaderTest, FdCountStable) {
    int f1 = proc_count_fds(self_pid_);
    int f2 = proc_count_fds(self_pid_);
    EXPECT_EQ(f1, f2);
}

TEST_F(ProcReaderTest, AllFunctionsNonNullReturn) {
    /* Smoke test: call every function and verify no crash */
    unsigned long long u, s;
    proc_read_cpu_ticks(self_pid_, &u, &s);
    proc_read_rss_kb(self_pid_);
    proc_count_threads(self_pid_);
    proc_count_fds(self_pid_);
    long r, w;
    proc_read_io(self_pid_, &r, &w);
    proc_count_children(self_pid_);
    SUCCEED();  /* no crash = pass */
}

/* ══════════════════════════════════════════════════════════════════
 *   Edge: PID 1 (init/systemd) — may have restricted /proc access
 * ══════════════════════════════════════════════════════════════════ */

TEST_F(ProcReaderTest, ReadInitProcess) {
    /* PID 1 should exist on any Linux system */
    long rss = proc_read_rss_kb(1);
    /* may succeed or fail (permission), but must not crash */
    (void)rss;

    int th = proc_count_threads(1);
    (void)th;

    SUCCEED();
}
