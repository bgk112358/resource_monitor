#!/bin/sh
#=============================================================================
# stability_24h.sh — TBox 应用 24 小时稳定性监控
# 用法: ./stability_24h.sh <process_name>
#=============================================================================
set -e

APP_NAME="${1:?用法: $0 <process_name>}"
PID=$(pidof "$APP_NAME" 2>/dev/null | awk '{print $1}')
[ -z "$PID" ] && PID=$(pgrep -f "$APP_NAME" 2>/dev/null | head -1)

if [ -z "$PID" ]; then
    echo "❌ 进程 '$APP_NAME' 未运行"
    exit 1
fi

LOG="/tmp/stability_${APP_NAME}_$(date +%Y%m%d_%H%M%S).csv"
echo "timestamp,cpu%,pss_kb,rss_kb,threads,fd" > "$LOG"
echo "📊 24h 稳定性监控开始: $APP_NAME (PID=$PID) → $LOG"
echo "   每 10 秒采样一次, 共 8640 次"

SAMPLE=0
MAX_SAMPLES=8640

trap 'echo ""; echo "⚠  收到中断信号, 已采样 $SAMPLE 次, 数据保存在 $LOG"; exit 130' INT TERM

while [ "$SAMPLE" -lt "$MAX_SAMPLES" ]; do
    SAMPLE=$((SAMPLE + 1))
    cpu=$(ps -p "$PID" -o %cpu --no-headers 2>/dev/null | tr -d ' ' | sed 's/^$/0/')

    pss=""; rss=""
    if command -v smem >/dev/null 2>&1; then
        line=$(smem -P "$APP_NAME" -c "pss rss" --no-headers 2>/dev/null | head -1)
        pss=$(echo "$line" | awk '{print $1}')
        rss=$(echo "$line" | awk '{print $2}')
    fi
    [ -z "$pss" ] && pss=$(awk '/VmRSS/ {print $2}' /proc/$PID/status 2>/dev/null || echo 0)

    threads=$(ls /proc/$PID/task 2>/dev/null | wc -l)
    fd=$(ls /proc/$PID/fd 2>/dev/null | wc -l)

    echo "$(date '+%H:%M:%S'),${cpu:-0},${pss:-0},${rss:-0},${threads:-0},${fd:-0}" >> "$LOG"

    # 进度提示 (每 600 次 = 100 分钟)
    [ $((SAMPLE % 600)) -eq 0 ] && echo "  [$SAMPLE/$MAX_SAMPLES] $(date '+%H:%M:%S') — CPU:${cpu}% PSS:${pss}KB Threads:${threads}"

    sleep 10
done

echo ""
echo "✅ 24h 监控完成! 数据: $LOG"
echo "   分析: 用 Excel 打开, 对 pss_kb 列画折线图, 斜率≈0 则无泄漏"
