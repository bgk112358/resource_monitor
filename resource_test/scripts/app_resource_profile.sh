#!/bin/sh
#=============================================================================
# app_resource_profile.sh — TBox 应用程序资源画像采集
# 用法: ./app_resource_profile.sh <process_name> [duration_seconds] [output_dir]
# 示例: ./app_resource_profile.sh CloudGW 30
#       ./app_resource_profile.sh HTCU 60 /tmp/my_test
#=============================================================================
APP_NAME="${1:?用法: $0 <process_name_or_pid> [duration_seconds] [output_dir]}"
DURATION="${2:-30}"
OUTDIR="${3:-/tmp/resource_profile_${APP_NAME}_$(date +%Y%m%d_%H%M%S)}"
mkdir -p "$OUTDIR"

# ── 定位进程 (先按 PID, 再按进程名) ──
PID=""
case "$APP_NAME" in
    ''|*[!0-9]*) 
        # 非纯数字: 按进程名查找
        PID=$(pidof "$APP_NAME" 2>/dev/null | awk '{print $1}')
        [ -z "$PID" ] && PID=$(pgrep -f "$APP_NAME" 2>/dev/null | head -1)
        ;;
    *) 
        # 纯数字: 直接用作 PID
        if [ -d "/proc/$APP_NAME" ]; then
            PID="$APP_NAME"
            # 获取真实进程名
            APP_NAME=$(cat /proc/$PID/comm 2>/dev/null || echo "pid_$PID")
        fi
        ;;
esac

if [ -z "$PID" ] || [ ! -d "/proc/$PID" ]; then
    echo "❌ 进程 '$APP_NAME' 未运行或 PID 无效"
    echo "   用法: $0 <进程名>  (如: $0 CloudGW)"
    echo "         $0 <PID>     (如: $0 12345)"
    echo "   当前运行进程:"
    ps aux | head -1; ps aux | grep -v grep | head -20
    exit 1
fi
echo "📊 采集进程 $APP_NAME (PID=$PID) 的资源数据, 持续 ${DURATION}s..."
echo "   输出目录: $OUTDIR"

# ── 检查依赖工具 ──
check_tool() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "⚠  缺少工具: $1 — 跳过相关采集"
        return 1
    fi
    return 0
}
check_tool smem || echo "   安装: apt-get install smem"

# ▼ CPU 每秒采样 ──────────────────────────────────────────
echo "sec,cpu_percent" > "$OUTDIR/cpu.csv"
for i in $(seq 1 "$DURATION"); do
    cpu=$(ps -p "$PID" -o %cpu --no-headers 2>/dev/null | tr -d ' ' | sed 's/^$/0/')
    echo "$i,${cpu:-0}" >> "$OUTDIR/cpu.csv"
    sleep 1
done

# ▼ 内存 PSS / RSS / USS ──────────────────────────────────
SAMPLES=5
INTERVAL=$((DURATION / SAMPLES))
[ "$INTERVAL" -lt 2 ] && INTERVAL=2
echo "sample,pss_kb,rss_kb,uss_kb" > "$OUTDIR/mem.csv"
for i in $(seq 1 "$SAMPLES"); do
    if command -v smem >/dev/null 2>&1; then
        smem -P "$APP_NAME" -c "pid pss rss uss" --no-headers 2>/dev/null | head -1 | \
            awk -v i="$i" '{printf "%d,%s,%s,%s\n", i, $2, $3, $4}' >> "$OUTDIR/mem.csv"
    else
        # 回退到 /proc/PID/status (仅 RSS)
        rss=$(awk '/VmRSS/ {print $2}' /proc/$PID/status 2>/dev/null || echo 0)
        echo "$i,,${rss}," >> "$OUTDIR/mem.csv"
    fi
    sleep "$INTERVAL"
done

# ▼ 线程数 + FD 数 ─────────────────────────────────────────
echo "sample,threads,fd_count" > "$OUTDIR/threads_fd.csv"
for i in $(seq 1 "$SAMPLES"); do
    t_cnt=$(ls /proc/$PID/task 2>/dev/null | wc -l)
    fd_cnt=$(ls /proc/$PID/fd 2>/dev/null | wc -l)
    echo "$i,${t_cnt},${fd_cnt}" >> "$OUTDIR/threads_fd.csv"
    sleep "$INTERVAL"
done

# ▼ IO 读写量 ──────────────────────────────────────────────
echo "sample,read_kb,write_kb" > "$OUTDIR/io.csv"
for i in $(seq 1 "$SAMPLES"); do
    if [ -r "/proc/$PID/io" ]; then
        rchar=$(awk '/^read_bytes:/ {print $2}' /proc/$PID/io 2>/dev/null)
        wchar=$(awk '/^write_bytes:/ {print $2}' /proc/$PID/io 2>/dev/null)
        rchar=$(( ${rchar:-0} + 0 ))
        wchar=$(( ${wchar:-0} + 0 ))
        echo "$i,$((rchar/1024)),$((wchar/1024))" >> "$OUTDIR/io.csv"
    else
        echo "$i,0,0" >> "$OUTDIR/io.csv"
    fi
    sleep "$INTERVAL"
done
[ ! -r "/proc/$PID/io" ] && echo "⚠  无法读取 /proc/$PID/io (需要 root 权限)"

# ▼ 网络连接 ───────────────────────────────────────────────
ss -tnp 2>/dev/null | grep -c "pid=$PID" > "$OUTDIR/net_connections.txt" || echo "0" > "$OUTDIR/net_connections.txt"

# ▼ 子进程树 ───────────────────────────────────────────────
if [ -d "/proc/$PID" ]; then
    ps --ppid "$PID" -o pid,comm --no-headers 2>/dev/null > "$OUTDIR/child_processes.txt"
    echo "$(cat "$OUTDIR/child_processes.txt" | wc -l) child processes" >> "$OUTDIR/child_processes.txt"
fi

# ▼ 汇总报告 ───────────────────────────────────────────────
generate_report() {
    local cpu_avg cpu_peak mem_pss mem_rss mem_uss threads fd net_conn
    local io_read io_write

    cpu_avg=$(awk -F',' 'NR>1 {sum+=$2; n++} END {printf "%.1f", (n>0 ? sum/n : 0)}' "$OUTDIR/cpu.csv" 2>/dev/null || echo "0")
    cpu_peak=$(awk -F',' 'NR>1 {if($2+0>max) max=$2+0} END {printf "%.1f", (max>0?max:0)}' "$OUTDIR/cpu.csv" 2>/dev/null || echo "0")

    if command -v smem >/dev/null 2>&1; then
        mem_pss=$(awk -F',' 'NR>1 && $2!="" {sum+=$2; n++} END {printf "%.0f", (n>0 ? sum/n : 0)}' "$OUTDIR/mem.csv" 2>/dev/null || echo "0")
        mem_rss=$(awk -F',' 'NR>1 && $3!="" {sum+=$3; n++} END {printf "%.0f", (n>0 ? sum/n : 0)}' "$OUTDIR/mem.csv" 2>/dev/null || echo "0")
        mem_uss=$(awk -F',' 'NR>1 && $4!="" {sum+=$4; n++} END {printf "%.0f", (n>0 ? sum/n : 0)}' "$OUTDIR/mem.csv" 2>/dev/null || echo "0")
    else
        mem_rss=$(awk -F',' 'NR>1 && $3!="" {sum+=$3; n++} END {printf "%.0f", (n>0 ? sum/n : 0)}' "$OUTDIR/mem.csv" 2>/dev/null || echo "0")
        mem_pss="N/A"
        mem_uss="N/A"
    fi

    threads=$(awk -F',' 'NR>1 {sum+=$2; n++} END {printf "%.0f", (n>0 ? sum/n : 0)}' "$OUTDIR/threads_fd.csv" 2>/dev/null || echo "0")
    fd=$(awk -F',' 'NR>1 {sum+=$3; n++} END {printf "%.0f", (n>0 ? sum/n : 0)}' "$OUTDIR/threads_fd.csv" 2>/dev/null || echo "0")
    net_conn=$(cat "$OUTDIR/net_connections.txt" 2>/dev/null || echo "0")

    # IO 吞吐量 (最后采样 - 首次采样) / 间隔时间
    io_read=$(awk -F',' 'NR==2 {r1=$2} END {if(r1>0 && $2>r1) printf "%.0f", ($2-r1)/((NR-2)*'"$INTERVAL"'); else print "0"}' "$OUTDIR/io.csv" 2>/dev/null || echo "0")
    io_write=$(awk -F',' 'NR==2 {w1=$3} END {if(w1>0 && $3>w1) printf "%.0f", ($3-w1)/((NR-2)*'"$INTERVAL"'); else print "0"}' "$OUTDIR/io.csv" 2>/dev/null || echo "0")

    local mem_pss_val="${mem_pss:-0}"
    local mem_rss_val="${mem_rss:-0}"
    local mem_pss_mb=$(awk "BEGIN {printf \"%.1f\", ${mem_pss_val}/1024}" 2>/dev/null || echo "0.0")
    local mem_rss_mb=$(awk "BEGIN {printf \"%.1f\", ${mem_rss_val}/1024}" 2>/dev/null || echo "0.0")
    local mem_rss_mb=$(awk "BEGIN {printf \"%.1f\", $mem_rss/1024}")

    cat > "$OUTDIR/report.txt" << EOF
========================================
 TBox 应用资源画像报告
========================================
进程名称:  $APP_NAME
PID:       $PID
采集时长:  ${DURATION}s
采集时间:  $(date '+%Y-%m-%d %H:%M:%S')

── CPU ──────────────────────────────
  平均占用:  ${cpu_avg}% (单核占比)
  峰值占用:  ${cpu_peak}%

── 内存 ─────────────────────────────
  PSS:       ${mem_pss} KB (${mem_pss_mb} MB)
  RSS:       ${mem_rss} KB (${mem_rss_mb} MB)
  USS:       ${mem_uss} KB

── 线程 & FD ────────────────────────
  线程数:    ${threads}
  文件描述符: ${fd}

── IO ───────────────────────────────
  磁盘读:    ${io_read} KB/s (均值)
  磁盘写:    ${io_write} KB/s (均值)

── 网络 ─────────────────────────────
  TCP 连接:  ${net_conn}

── 详细数据文件 ─────────────────────
  CPU 采样:    $OUTDIR/cpu.csv
  内存采样:    $OUTDIR/mem.csv
  IO 采样:     $OUTDIR/io.csv
  线程/FD:     $OUTDIR/threads_fd.csv
  网络连接:    $OUTDIR/net_connections.txt
  子进程:      $OUTDIR/child_processes.txt
========================================
EOF
}

generate_report
cat "$OUTDIR/report.txt"
echo ""
echo "✅ 完成! 所有数据保存在: $OUTDIR/"
echo "   汇总报告: $OUTDIR/report.txt"
