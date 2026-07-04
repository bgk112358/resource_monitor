#!/bin/sh
# dummy_app.sh — TBox 测试用模拟应用
# 模拟一个持续运行的应用: 占用少量 CPU、内存、定期 IO 写入
# 用法: ./dummy_app.sh [duration_seconds]
#       ./dummy_app.sh 60   # 运行 60 秒后自动退出
set -e

DURATION="${1:-0}"
PID_FILE="/tmp/dummy_app.pid"
echo $$ > "$PID_FILE"
trap 'rm -f "$PID_FILE"' EXIT

echo "🚀 DummyApp 启动 (PID=$$, 持续${DURATION}s)"
echo "   功能: 模拟 CPU 计算 + 内存分配 + 磁盘写入 + TCP 监听"

# ── 分配 8MB 内存并保持 ──
MEM_FILE="/tmp/dummy_app_mem.bin"
dd if=/dev/zero of="$MEM_FILE" bs=1M count=8 2>/dev/null
echo "   内存: 已分配 8MB 在 $MEM_FILE"

# ── 打开 TCP 端口模拟网络服务 ──
PORT=19999
nc -l -p "$PORT" -k >/dev/null 2>&1 &
NC_PID=$!
echo "   网络: TCP 监听端口 $PORT (PID=$NC_PID)"
trap 'rm -f "$PID_FILE" "$MEM_FILE"; kill $NC_PID 2>/dev/null' EXIT

# ── 创建 5 个工作线程 ──
WORK_DIR="/tmp/dummy_app_work"
mkdir -p "$WORK_DIR"

# 工作线程: CPU 计算 (每 200ms 一次浮点运算)
worker_cpu() {
    local id="$1"
    while true; do
        awk "BEGIN { for(i=0;i<50000;i++) sin(i*0.001) }" >/dev/null 2>&1
        sleep 0.2
    done
}

# 工作线程: IO 写入 (每 2 秒写一次日志)
worker_io() {
    local id="$1"
    local logfile="$WORK_DIR/worker_${id}.log"
    while true; do
        echo "$(date '+%H:%M:%S.%3N') Worker-$id heartbeat" >> "$logfile"
        sleep 2
    done
}

# 工作线程: 网络任务
worker_net() {
    while true; do
        sleep 5
    done
}

worker_cpu 1 &
worker_cpu 2 &
worker_io 1 &
worker_io 2 &
worker_net 1 &

echo "   线程: 已启动 5 个工作线程 (2 CPU + 2 IO + 1 NET)"

if [ "$DURATION" -gt 0 ]; then
    echo ""
    echo "⏳ 运行 ${DURATION}s 后自动退出..."
    sleep "$DURATION"
    echo "🛑 DummyApp 正常退出"
else
    echo "   运行中... (Ctrl+C 停止)"
    while true; do sleep 60; done
fi
