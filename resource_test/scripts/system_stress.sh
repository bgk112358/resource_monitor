#!/bin/sh
#=============================================================================
# system_stress.sh — TBox 全系统综合压力测试
# 用法: ./system_stress.sh [duration_seconds]
#=============================================================================
DURATION="${1:-600}"
echo "🔥 TBox 综合压力测试 — 持续 ${DURATION}s"
echo "   同时加载: CPU + 内存 + 存储 + CANFD + 网络"
echo ""

cleanup() {
    echo ""
    echo "🛑 停止所有压力进程..."
    kill $(jobs -p) 2>/dev/null
    wait 2>/dev/null
    echo ""
    echo "=== 内核错误检查 ==="
    dmesg | tail -30 | grep -iE "error|fail|oom|panic|watchdog" || echo "  无内核错误"
    echo ""
    echo "✅ 综合压力测试完成"
}
trap cleanup EXIT INT TERM

# 1. CPU: 双核 80% 占用
stress-ng --cpu 2 --cpu-load 80 --timeout "${DURATION}s" &
echo "  [1/6] CPU stress started"

# 2. 内存: 分配 200MB
stress-ng --vm 1 --vm-bytes 200M --timeout "${DURATION}s" &
echo "  [2/6] Memory stress started (200MB)"

# 3. 存储: 持续随机读写 (如果 /tmp 可写)
if [ -w /tmp ]; then
    dd if=/dev/zero of=/tmp/stress_test.bin bs=1M count=50 2>/dev/null
    stress-ng --hdd 1 --hdd-bytes 10M --timeout "${DURATION}s" &
    echo "  [3/6] Storage stress started"
else
    echo "  [3/6] Storage stress SKIP (/tmp 不可写)"
fi

# 4. CANFD: 满载发送 (如果 can0 存在且 UP)
if ip link show can0 >/dev/null 2>&1; then
    if ip link show can0 | grep -q "UP"; then
        cangen can0 -g 0 -L 64 -f &
        echo "  [4/6] CANFD stress started (can0)"
    else
        echo "  [4/6] CANFD stress SKIP (can0 is DOWN)"
        echo "       提示: ip link set can0 type can bitrate 500000 dbitrate 2000000 fd on"
        echo "             ip link set can0 up"
    fi
else
    echo "  [4/6] CANFD stress SKIP (can0 not found — 无 MCU SPI-CAN GW)"
fi

# 5. 网络: 持续 ping (需要网关可达)
GATEWAY=$(ip route | grep default | awk '{print $3}' | head -1)
if [ -n "$GATEWAY" ]; then
    ping -i 0.2 "$GATEWAY" > /tmp/stress_ping.log 2>&1 &
    echo "  [5/6] Network stress started (ping $GATEWAY)"
else
    echo "  [5/6] Network stress SKIP (无默认网关)"
fi

# 6. GPS: 持续读取 (如果 GPS 设备存在)
GPS_DEV=""
[ -c /dev/ttyGPS0 ] && GPS_DEV=/dev/ttyGPS0
[ -c /dev/ttyACM0 ] && GPS_DEV=/dev/ttyACM0
if [ -n "$GPS_DEV" ]; then
    timeout "$DURATION" cat "$GPS_DEV" > /tmp/stress_gps.log 2>&1 &
    echo "  [6/6] GPS stress started ($GPS_DEV)"
else
    echo "  [6/6] GPS stress SKIP (无 GPS 设备)"
fi

echo ""
echo "⏳ 等待 ${DURATION}s ... (Ctrl+C 提前结束)"
sleep "$DURATION"

# 打印网络统计
if [ -n "$GATEWAY" ]; then
    echo ""
    echo "── 网络丢包统计 ──"
    tail -5 /tmp/stress_ping.log 2>/dev/null || echo "  无数据"
fi

# 打印 CPU 温度
if [ -r /sys/class/thermal/thermal_zone0/temp ]; then
    TEMP=$(cat /sys/class/thermal/thermal_zone0/temp)
    echo ""
    echo "── CPU 温度 ──"
    echo "  $((TEMP/1000))°C"
fi
