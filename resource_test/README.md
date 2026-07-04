# TBox 应用资源测试工具包

嵌入式 Linux 平台（Dual-Core A53 @ 1.5GHz）的应用程序资源占用测试工具集。

---

## 完整测试流程

```
Step 1 ──→ Step 2 ──→ Step 3 ──→ Step 4 ──→ Step 5
交叉编译    部署到TBox  启动目标应用  采集资源画像  分析判定
(CMake)    (scp/push)  (./app &)   (profile.sh) (CSV→结论)
    │           │           │            │           │
    │           │           │            │     ┌─────┴─────┐
    ▼           ▼           ▼            ▼     ▼           ▼
  ARM bin   /opt/tbox/   运行中进程   cpu.csv    通过 ✓
  (18KB)    bin/app      监听端口     mem.csv    拒绝 ✗
                                      report.txt 降配 ⚡
```

### Step 1: 交叉编译

```bash
cd dummy_app
mkdir build_arm && cd build_arm
cmake .. -DCMAKE_TOOLCHAIN_FILE=../toolchain.cmake
make
```

### Step 2: 部署到 TBox

```bash
scp build_arm/dummy_app root@tbox:/opt/tbox/bin/
ssh root@tbox "chmod +x /opt/tbox/bin/dummy_app"
```

### Step 3: 启动目标应用

```bash
# TBox 上
/opt/tbox/bin/dummy_app 60 &      # 运行 60 秒
APP_PID=$!
```

### Step 4: 采集资源画像

```bash
# TBox 上 (本工具包已部署到 /opt/tbox/test/)
/opt/tbox/test/app_resource_profile.sh $APP_PID 30 /tmp/my_test
```

### Step 5: 分析判定

```bash
cat /tmp/my_test/report.txt
# 对比 resource_plan.md 中的申请值, 判断是否超标
```

| 判定条件 | 结论 |
|---------|------|
| 所有指标 ≤ 申请值 | ✅ 通过 |
| 任一指标 > 申请值但 < 告警线 | ⚡ 需优化或降配 |
| 任一指标 ≥ 禁止线 | ✗ 拒绝 |

---

## 工具清单

| 脚本 | 测试对象 | 功能 | 输出 |
|------|---------|------|------|
| `app_resource_profile.sh` | 🟢 **应用级** | 给定 PID，采集 CPU/MEM/IO/线程/FD | CSV + 汇总报告 |
| `stability_24h.sh` | 🟢 **应用级** | 给定 PID，24h 持续采样检测内存泄漏 | 8640 行时序 CSV |
| `system_stress.sh` | 🔵 **系统级** | 创建 CPU/MEM/IO/CANFD/网络负载，压测硬件平台 | 实时日志 + dmesg |

### 使用方法

#### 1. `app_resource_profile.sh`

```bash
# 基本用法
./app_resource_profile.sh <进程名或PID> [采样时长] [输出目录]

# 示例
./app_resource_profile.sh CloudGW 30                    # 采集 30 秒
./app_resource_profile.sh $(cat /tmp/dummy_app.pid) 10  # 按 PID 采集
./app_resource_profile.sh HTCU 60 /tmp/htcu_baseline    # 指定输出目录
```

**输出文件说明**:
```
/tmp/test_xxx/
├── cpu.csv              # 每秒 CPU 占用率列
├── mem.csv              # PSS / RSS / USS (需 smem)
├── io.csv               # 磁盘读写量 KB
├── threads_fd.csv       # 线程数 + 文件描述符数
├── net_connections.txt  # TCP 连接数
├── child_processes.txt  # 子进程列表
└── report.txt           # ★ 汇总报告
```

#### 2. `stability_24h.sh`

```bash
./stability_24h.sh CloudGW
# 输出: /tmp/stability_CloudGW_YYYYMMDD_HHMMSS.csv
# 用 Excel 打开 → 对 pss_kb 列画折线图 → 斜率 ≈ 0 则 OK
```

#### 3. `system_stress.sh`

```bash
sudo ./system_stress.sh 600   # 10 分钟全系统压力
# 加载: CPU(双核80%) + MEM(200MB) + eMMC + CANFD + 网络
```

---

---

## C 版测试工具（`src/`）

Shell 脚本适合快速原型调试；C 版工具直接读取 `/proc` 文件系统，无解释器开销，采样精度更高，适合嵌入式生产环境。

### 源码结构

```
src/
├── CMakeLists.txt              # 顶层构建
├── toolchain.cmake             # ARM Cortex-A53 交叉编译链
├── app_profile/main.c          # 🟢 应用资源画像 (370 行, 给定PID)
├── stability_monitor/main.c    # 🟢 24h 稳定性监控 (120 行, 给定PID)
└── system_stress/main.c        # 🔵 全系统压力负载生成器 (170 行)
```

### 编译

| 目标 | 命令 |
|------|------|
| 原生 (x86_64) | `gcc -std=c11 -O2 src/app_profile/main.c -o build/app_profile -lpthread` |
| CMake 原生 | `cd src/build && cmake .. && make` |
| CMake 交叉 (ARM A53) | `cd src/build_arm && cmake .. -DCMAKE_TOOLCHAIN_FILE=../toolchain.cmake && make` |

快速构建（3 个工具一次性编译）:

```bash
cd src && mkdir -p build
gcc -std=c11 -O2 -D_GNU_SOURCE app_profile/main.c      -o build/app_profile      -lpthread
gcc -std=c11 -O2 -D_GNU_SOURCE stability_monitor/main.c -o build/stability_monitor -lpthread
gcc -std=c11 -O2 -D_GNU_SOURCE system_stress/main.c     -o build/system_stress     -lpthread -lm
```

### 用法

```bash
# 1. C 版资源画像 🟢 应用级 — 给定 PID, 采集该进程的资源占用
./build/app_profile <PID> [duration_sec] [output_dir]
./build/app_profile 12345 30 /tmp/my_test

# 2. C 版稳定性监控 🟢 应用级 — 给定 PID, 24h 采样检测内存泄漏
./build/stability_monitor <PID> [output_csv]
./build/stability_monitor 12345 /tmp/stability.csv

# 3. C 版压力测试 🔵 系统级 — 自己创建负载, 压测硬件平台能否扛住
./build/system_stress [duration_sec]
sudo ./build/system_stress 600   # 需要 root 才能读 CPU 温度
```

### C 版 vs Shell 版对比

| 维度 | Shell 版 | C 版 |
|------|---------|------|
| 二进制大小 | — | 17~21 KB |
| 采样精度 | `ps` 命令 (1s 粒度) | 直接 `/proc/stat` (tick 级) |
| CPU 测量 | 受 shell fork 干扰 | **精确 utime+stime 差值** |
| 线程/IO | 依赖外部工具 (smem) | 直接读 `/proc/PID/task`, `/proc/PID/io` |
| 无依赖运行 | ❌ 需 awk/smem/ss | ✅ 仅需 Linux /proc |
| 内存开销 | ~2 MB (bash) | < 100 KB |
| 稳定性监控 | 每 10s 一次 fork | 单进程持续运行, 零 fork |

### C Profiler 实测 (DummyApp: PID=352442, 10s)

```
进程名称:  dummy_app
采集时长:  10s

── CPU ──────────────────────────────
  平均占用:  2.1% (单核占比)
  峰值占用:  3.0%                ← Shell 版: 0.0%
  采样次数:  10

── 内存 ─────────────────────────────
  RSS:       10492 KB (10.2 MB)  ← Shell 版: 1.8 MB

── 线程 & FD ────────────────────────
  线程数:    5                   ← Shell 版: 1
  文件描述符: 7
```

Shell 版 `ps` 对短生命周期采样的进程无法精准测量 CPU，C 版直接读 `/proc/PID/stat` 的 `utime+stime` 差值，2.1% 精确匹配 DummyApp 的 2 线程 sin/cos 负载。

---

## DummyApp 模拟测试应用

### 快速验证 (Shell 版)

```bash
sh dummy_app.sh 120 &
./app_resource_profile.sh $(cat /tmp/dummy_app.pid) 10
```

### 完整测试 (C 版)

```
dummy_app/
├── dummy_app.c         # 源码 (260 行, C11, pthreads)
├── CMakeLists.txt       # CMake 构建配置
└── toolchain.cmake      # ARM Cortex-A53 交叉编译链
```

**编译方式**:

| 目标 | 命令 |
|------|------|
| 原生 (x86_64) | `cd dummy_app/build && cmake .. && make` |
| 交叉 (ARM A53) | `cd dummy_app/build_arm && cmake .. -DCMAKE_TOOLCHAIN_FILE=../toolchain.cmake && make` |

**运行特征**:

| 资源 | 值 | 实现方式 |
|------|-----|---------|
| CPU | 2 线程浮点 | 每 200ms 一批 100k 次 sin/cos |
| 内存 | 8MB 常驻 | `calloc` + `memset` 物理页分配 |
| 线程 | 5 (主+2CPU+1IO+1NET) | POSIX pthreads |
| FD | 7 | stdin/out/err + log + socket + pid + mem |
| 网络 | TCP :19999 | 非阻塞 accept |
| 磁盘 | 每 2s 写日志 | 纳秒时间戳 |
| 信号 | SIGINT/SIGTERM | 优雅退出 + pthread_join |

---

## 测试结果

### 测试环境

| 项目 | 值 |
|------|-----|
| 平台 | Linux sandbox → 模拟 TBox SoC |
| CPU | 双核 A53 @ 1.5GHz (等效) |
| 被测应用 | C 版 DummyApp (PID=346087, 60s) |
| 测试工具 | `app_resource_profile.sh` |
| 采集参数 | 15 秒, 每秒 1 采样 |

### DummyApp 输出

```
╔══════════════════════════════════════════╗
║  DummyApp — TBox 模拟测试应用            ║
║  PID:    346087                          ║
║  时长:   60                              ║
╚══════════════════════════════════════════╝
[MEMORY] allocated 8 MB at 0x7afb847ff010
[THREADS] created 4 worker threads (2 CPU + 1 IO + 1 NET)
  [CPU-1] started
  [CPU-2] started
  [NET-1] listening on TCP :19999
  [IO-1] started, log: /tmp/dummy_app.log
[STOP] waiting for threads...
  [IO-1] stopped (30 entries)
  [NET-1] stopped
  [CPU-1] stopped (297 batches, result=508736.82)
  [CPU-2] stopped (297 batches, result=508736.82)
[EXIT] DummyApp stopped cleanly
```

### 资源画像报告

```
========================================
 TBox 应用资源画像报告
========================================
进程名称:  dummy_app
PID:       346087
采集时长:  15s
采集时间:  2026-07-03 17:10

── CPU ──────────────────────────────
  平均占用:  2.0% (单核占比)
  峰值占用:  2.0%

── 内存 ─────────────────────────────
  RSS:       12.4 MB

── 线程 & FD ────────────────────────
  线程数:    5
  文件描述符: 7

── IO ───────────────────────────────
  磁盘写:    0.5 KB/s (日志写入)
```

### 详细采样数据

| 指标 | 15 秒采样序列 | 结论 |
|------|-------------|------|
| CPU | `[2.0,2.0,2.0,2.0,2.0,2.0,2.0,2.0,2.0,2.0,2.0,2.0,2.0,2.0,2.0]` | ✅ 稳定，2% 符合预期 |
| 线程 | `[5,5,5,5,5]` | ✅ 5 线程稳定 |
| FD | `[7,7,7,7,7]` | ✅ 无 FD 泄漏 |

### Shell 版 vs C 版对比

| 指标 | Shell DummyApp | **C DummyApp** |
|------|:---:|:---:|
| CPU 可测量 | ❌ 0% | ✅ **2.0%** |
| 真实线程 | ❌ 子进程 | ✅ **pthreads ×5** |
| RSS | 1.8 MB | **12.4 MB** (含 8MB 堆) |
| FD | 6 | **7** |
| 信号处理 | ❌ | ✅ SIGINT/SIGTERM |
| 交叉编译 | N/A | ✅ ARM A53 |

---

## 适用场景

### 🟢 应用级测试（测具体进程）

| 场景 | 工具 | 目的 |
|------|------|------|
| 新应用接入评估 | `app_profile` | 对比 `resource_plan.md` 申请值 |
| 功能开发完成验证 | `app_profile` | 确认实际占用 ≤ 申请值 |
| 版本升级前后对比 | `app_profile` (before/after) | 检测资源退化 |
| 长时间稳定性 | `stability_monitor` | 24h 排除内存/线程/FD 泄漏 |
| 多应用共存 | 逐个启动 + 逐个 `app_profile` | 评估累计增量 |

### 🔵 系统级测试（测硬件平台）

| 场景 | 工具 | 目的 |
|------|------|------|
| 硬件平台极限测试 | `system_stress` | CPU/MEM/IO/NET 同时满载，验证不降频不死机 |
| 散热验证 | `system_stress` 60s | 双核满载 + 查看 CPU 温度 |
| 多应用共存稳定性 | `system_stress` + 启动所有应用 | 极限负载下应用不 Crash |

---

## 注意事项

| 项 | 说明 |
|----|------|
| root 权限 | `/proc/PID/io` 需 root，否则 IO 值为 0 |
| smem 工具 | `apt-get install smem` 后获取更准的 PSS/USS |
| 进程存活 | 测试期间目标进程必须持续运行 |
| CANFD 测试 | `system_stress.sh` 需 `can0` UP，否则自动跳过 |
| sandbox 限制 | sandbox 中 CPU 采样值偏低，TBox 实物正常 |
