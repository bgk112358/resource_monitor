# Resource Monitor — TBox 应用资源监测工具包

> 嵌入式 Linux（Dual-Core A53 @ 1.5GHz）应用程序资源占用监测、可视化与测试框架。

---

## 目录结构

```
resource_monitor/
├── README.md                    ← 本文件
│
├── resource_test/               ← 资源监测工具包 (C + Shell + Python)
│   ├── src/app_profile/         # C 版资源画像采集器 (并发, 7 模块)
│   ├── src/stability_monitor/   # C 版 24h 稳定性监控
│   ├── src/system_stress/       # C 版系统压力负载生成器
│   ├── scripts/                 # Shell 版测试脚本 (4 个)
│   ├── python/
│   │   ├── plot_profile.py      # CSV → 柱状图 HTML (零依赖)
│   │   └── plot_gui.py          # tkinter GUI 本地客户端
│   ├── dummy_app/               # C 版模拟测试应用 (CMake + ARM 交叉编译)
│   └── README.md                # 详细使用手册
│
├── gtest/                       ← GTest 单元测试
│   ├── src/                     # 51 个测试用例 (3 个测试文件)
│   ├── CMakeLists.txt           # 覆盖率目标: make coverage
│   ├── report/html/             # 覆盖率报告 (99.4% 行覆盖)
│   └── install/                 # GTest 预编译库
│
└── tbox_docs/                   ← TBox 架构文档
    ├── platform.md ~ TTCU.md    # 9 个架构设计文档
    ├── *.doc                    # 8 个 Word 软件设计说明书
    ├── resource_plan.md/csv     # 资源规划表
    └── TBox_Architecture.html   # 架构 HTML 说明书
```

---

## 快速开始

### 1. 构建 C 版工具

```bash
cd resource_test/src

# 原生编译 (x86_64)
mkdir build && cd build
cmake ..
make

# ARM 交叉编译 (TBox 目标)
mkdir build_arm && cd build_arm
cmake .. -DCMAKE_TOOLCHAIN_FILE=../toolchain.cmake
make
```

产出 3 个二进制：`app_profile`、`stability_monitor`、`system_stress`。

### 2. 运行资源画像采集

```bash
# 启动被测应用
./dummy_app/build/dummy_app 60 &
PID=$(cat /tmp/dummy_app.pid)

# 采集 15 秒 (4 个采样器并发运行)
./src/build/app_profile $PID 15 /tmp/my_test
```

### 3. 可视化 (二选一)

```bash
# HTML 版: 浏览器查看
python3 python/plot_profile.py /tmp/my_test_${PID}_*

# GUI 版: tkinter 窗口直接显示 (需 apt install python3-tk)
python3 python/plot_gui.py /tmp/my_test_${PID}_*
```

### 4. 运行单元测试

```bash
cd ../gtest
cmake -B build -S .
cmake --build build
./build/test_proc_reader      # 22 tests
./build/test_app_profile      # 16 tests
./build/test_app_profile_v2   # 13 tests
```

### 5. 生成覆盖率报告

```bash
cmake -B build -S . \
    -DCMAKE_C_FLAGS="-fprofile-arcs -ftest-coverage -O0 -g" \
    -DCMAKE_CXX_FLAGS="-fprofile-arcs -ftest-coverage -O0 -g" \
    -DCMAKE_EXE_LINKER_FLAGS="--coverage"
cmake --build build
./build/test_proc_reader && ./build/test_app_profile && ./build/test_app_profile_v2
cmake --build build --target coverage
# → 浏览器打开 report/html/index.html
```

---

## 核心工具

### app_profile — 应用资源画像采集器

4 个采样线程**并发**运行在同一时间窗口内：

| 采样器 | 数据源 | 频率 | 输出列 |
|--------|--------|------|--------|
| `cpu_sampler` | `/proc/PID/stat` utime+stime | 每秒 1 次 | `cpu.csv` |
| `mem_sampler` | `/proc/PID/status` VmRSS | 每 N 秒 1 次 | `mem.csv` |
| `thread_fd_sampler` | `/proc/PID/task` + `/proc/PID/fd` | 每 N 秒 1 次 | `threads_fd.csv` |
| `io_sampler` | `/proc/PID/io` read_bytes/write_bytes | 每 N 秒 1 次 (差值) | `io.csv` |

**关键特性**：CPU 解析正确处理进程名含空格的情况（`strrchr(line, ')')`）；IO 吞吐量使用相邻采样差值而非累计值；`clock_gettime()` 精确计时替代 `sleep(1)` 近似。

### 单元测试覆盖 (51 tests, 99.4% line coverage)

```
cpu_sampler.c           100% (37/37)
io_sampler.c            100% (24/24)
mem_sampler.c           100% (22/22)
proc_reader.c          95.2% (20/21)  ← sig_handler 仅由 main.c 调用
report.c                100% (33/33)
thread_fd_sampler.c     100% (30/30)
```

---

## 依赖

| 组件 | 依赖 |
|------|------|
| C 工具 | Linux /proc 文件系统, pthread, gcc/clang |
| Shell 脚本 | bash, awk, smem (可选) |
| Python 可视化 | **零依赖** — 纯 `csv` + 内联 SVG |
| GTest 测试 | GTest 预编译库 (已包含在 `gtest/install/`) |
| 交叉编译 | `arm-linux-gnueabihf-gcc` 工具链 |
