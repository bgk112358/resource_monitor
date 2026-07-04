#!/usr/bin/env python3
"""
plot_profile.py — 读取 app_profile CSV 输出, 生成 4 合 1 折线图 HTML

用法:
  python3 plot_profile.py <profile_dir>
  python3 plot_profile.py /tmp/test_concurrent_523934_20260704_094051

输出: <profile_dir>/chart.html — 用浏览器打开即可查看
"""
import sys, os, csv, subprocess

# ── csv_verify 二进制路径 ─────────────────────────
CSV_VERIFY_BIN = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               '..', 'src', 'build', 'csv_verify')

def verify_csv_signature(path):
    """调用 C 二进制 csv_verify -q 验签。
    返回: True=有效, False=无效, None=CSV无签名, 'no_binary'=工具未找到
    """
    try:
        r = subprocess.run([CSV_VERIFY_BIN, '-q', path],
                           capture_output=True, timeout=5)
        if r.returncode == 0:   return True, None
        elif r.returncode == 2: return None, None
        else:                   return False, r.stderr.decode().strip() or "签名验证失败"
    except FileNotFoundError:
        return 'no_binary', None
    except Exception as e:
        return None, str(e)

def read_csv(path, skip_header=True):
    """读取 CSV, 返回 [(x, y), ...]; 跳过 # 开头的签名行"""
    rows = []
    with open(path, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            parts = line.split(',')
            try:
                val = float(parts[1]) if len(parts) > 1 else float(parts[0])
                rows.append(val)
            except (ValueError, IndexError):
                pass
    # header line fails float parse, so first row is first data point
    return rows

def read_csv_multi(path):
    """读取多列 CSV, 返回 ([y1,...], [z1,...]); 跳过 # 开头的签名行"""
    a, b = [], []
    with open(path, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            parts = line.split(',')
            try:
                if len(parts) >= 3:
                    a.append(float(parts[1]))
                    b.append(float(parts[2]))
            except (ValueError, IndexError):
                pass
    return a, b

def svg_bar_chart(data, width, height, color, ylabel, sig_ok=None, ymax_override=None):
    """将 [(x,y),...] 渲染为 inline SVG 柱状图。
    sig_ok: None=无签名不提示, True=签名有效不提示, False=签名无效显示警告
    """
    if not data or len(data) < 1:
        return f'<svg viewBox="0 0 {width} {height}" xmlns="http://www.w3.org/2000/svg"><rect x="0" y="0" width="{width}" height="{height}" fill="#161b22" rx="4"/><text x="{width//2}" y="{height//2}" text-anchor="middle" fill="#888">No data</text></svg>'

    xs = [p[0] for p in data]
    ys = [p[1] for p in data]

    xmin, xmax = min(xs), max(xs)
    if xmin == xmax: xmax = xmin + 1
    ymin, ymax = 0, max(ys)       # 柱状图始终从 0 开始
    if ymax == 0: ymax = 1
    if ymax_override is not None:
        ymax = ymax_override
    # 向上舍入到整洁的刻度
    if ymax <= 10: ymax = (int(ymax) + 1) if ymax > 0 else 1
    elif ymax <= 100: ymax = ((int(ymax) // 10) + 1) * 10
    else: ymax = ((int(ymax) // 100) + 1) * 100

    margin_l, margin_r, margin_t, margin_b = 52, 20, 38, 40
    pw = width - margin_l - margin_r
    ph = height - margin_t - margin_b

    def ty(y):
        return height - margin_b - (y - ymin) / (ymax - ymin) * ph

    # 柱子宽度 = 可用宽度 / 柱子数 / 1.5 (留间距)
    bar_w = max(2, pw / len(data) / 1.5)

    # 柱子
    bars = ''
    for x, y in data:
        bx = margin_l + (x - xmin) / (xmax - xmin + 1) * pw + bar_w * 0.25
        bh = max(1, ty(ymin) - ty(y))
        by = ty(y)
        opacity = "0.8"
        bars += f'<rect x="{bx:.1f}" y="{by:.1f}" width="{bar_w:.1f}" height="{bh:.1f}" fill="{color}" opacity="{opacity}" rx="2"/>'

    # 网格线
    grid_lines = ''
    for i in range(5):
        gy = margin_t + ph * i / 4
        grid_lines += f'<line x1="{margin_l}" y1="{gy:.1f}" x2="{width-margin_r}" y2="{gy:.1f}" stroke="#333" stroke-dasharray="4,4"/>'

    # Y 轴标签
    y_labels = ''
    for i in range(5):
        val = ymin + (ymax - ymin) * i / 4
        gy = margin_t + ph * (4 - i) / 4
        y_labels += f'<text x="{margin_l-6}" y="{gy+4:.1f}" text-anchor="end" fill="#888" font-size="10">{val:.1f}</text>'

    # X 轴标签
    x_labels = ''
    step = max(1, len(data) // 8)
    for i, (x, _) in enumerate(data):
        if i % step == 0 or i == len(data) - 1:
            bx = margin_l + (x - xmin) / (xmax - xmin + 1) * pw + bar_w * 0.25 + bar_w / 2
            x_labels += f'<text x="{bx:.1f}" y="{height-margin_b+16}" text-anchor="middle" fill="#888" font-size="9">{x}</text>'

    # 签名状态提示
    warning = ''
    title_y = 16
    if sig_ok is False:
        warning = f'''
    <rect x="0" y="0" width="{width}" height="22" fill="#f8514933" rx="0"/>
    <text x="{width//2}" y="15" text-anchor="middle" fill="#f85149" font-size="11" font-weight="bold">⚠ 签名无效 — 数据可能被篡改</text>
    '''
        title_y = 36
    elif sig_ok is None:
        warning = f'''
    <rect x="0" y="0" width="{width}" height="22" fill="#d2991d33" rx="0"/>
    <text x="{width//2}" y="15" text-anchor="middle" fill="#d2991d" font-size="11" font-weight="bold">⚠ 无签名 — 无法核实真伪</text>
    '''
        title_y = 36
    elif sig_ok == 'no_binary':
        warning = f'''
    <rect x="0" y="0" width="{width}" height="22" fill="#d2991d33" rx="0"/>
    <text x="{width//2}" y="15" text-anchor="middle" fill="#d2991d" font-size="11" font-weight="bold">⚠ 无法验签 — 验签工具未找到</text>
    '''
        title_y = 36

    return f'''<svg viewBox="0 0 {width} {height}" xmlns="http://www.w3.org/2000/svg">
    <rect x="0" y="0" width="{width}" height="{height}" fill="#161b22" rx="4"/>
    <text x="{width//2}" y="{title_y}" text-anchor="middle" fill="#ccc" font-size="13" font-weight="bold">{ylabel}</text>
    {warning}
    {grid_lines}
    {y_labels}
    {x_labels}
    <line x1="{margin_l}" y1="{height-margin_b}" x2="{width-margin_r}" y2="{height-margin_b}" stroke="#555" stroke-width="1"/>
    <line x1="{margin_l}" y1="{margin_t}" x2="{margin_l}" y2="{height-margin_b}" stroke="#555" stroke-width="1"/>
    {bars}
</svg>'''

def build_html(profile_dir):
    """从 CSV 文件构建完整的 HTML 图表"""
    proc_name = "unknown"
    rpt_path = os.path.join(profile_dir, "report.txt")
    if os.path.exists(rpt_path):
        with open(rpt_path) as f:
            for line in f:
                if "进程名称:" in line:
                    proc_name = line.split(":")[-1].strip()
                    break

    # 读取 4 个 CSV + 验证签名
    csv_files = {
        'cpu':          'cpu.csv',
        'mem':          'mem.csv',
        'threads_fd':   'threads_fd.csv',
        'io':           'io.csv',
    }
    sig_results = {}
    for key, fname in csv_files.items():
        fpath = os.path.join(profile_dir, fname)
        sig_results[key] = verify_csv_signature(fpath) if os.path.exists(fpath) else (None, None)

    cpu_data   = [(i+1, v) for i, v in enumerate(read_csv(os.path.join(profile_dir, "cpu.csv")))]
    mem_data   = [(i+1, v) for i, v in enumerate(read_csv(os.path.join(profile_dir, "mem.csv")))]
    thr_raw, fd_raw = read_csv_multi(os.path.join(profile_dir, "threads_fd.csv"))
    thr_data   = [(i+1, v) for i, v in enumerate(thr_raw)]
    fd_data    = [(i+1, v) for i, v in enumerate(fd_raw)]
    ior_raw, iow_raw = read_csv_multi(os.path.join(profile_dir, "io.csv"))
    io_r_data  = [(i+1, v) for i, v in enumerate(ior_raw)]
    io_w_data  = [(i+1, v) for i, v in enumerate(iow_raw)]

    w, h = 500, 260

    def sig_flag(key):
        """None=无签名, True=有效, False=无效"""
        r = sig_results.get(key)
        return r[0] if r else None

    charts = [
        ('CPU (%)',        'cpu',     svg_bar_chart(cpu_data, w, h, '#58a6ff', 'CPU Usage (%)',          sig_ok=sig_flag('cpu'))),
        ('Memory RSS (KB)', 'mem',    svg_bar_chart(mem_data, w, h, '#3fb950', 'Memory RSS (KB)',         sig_ok=sig_flag('mem'))),
        ('Threads',         'thr',    svg_bar_chart(thr_data, w, h, '#f0883e', 'Thread Count',             sig_ok=sig_flag('threads_fd'))),
        ('File Descriptors','fd',     svg_bar_chart(fd_data, w, h, '#d2a8ff', 'Open File Descriptors',     sig_ok=sig_flag('threads_fd'))),
        ('IO Read (KB/s)',  'io_r',   svg_bar_chart(io_r_data, w, h, '#f85149', 'IO Read Throughput (KB/s)',  sig_ok=sig_flag('io'))),
        ('IO Write (KB/s)', 'io_w',   svg_bar_chart(io_w_data, w, h, '#f59e0b', 'IO Write Throughput (KB/s)', sig_ok=sig_flag('io'))),
    ]

    panels = ''
    for title, cid, svg in charts:
        panels += f'''
        <div class="chart-panel">
            {svg}
        </div>
        '''

    return f'''<!DOCTYPE html>
<html lang="zh-CN">
<head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>TBox Resource Profile — {proc_name}</title>
<style>
*{{box-sizing:border-box;margin:0;padding:0}}
body{{background:#0d1117;color:#c9d1d9;font-family:-apple-system,BlinkMacSystemFont,"Microsoft YaHei",sans-serif;padding:24px 32px}}
h1{{color:#f0f6fc;font-size:1.4rem;margin-bottom:4px}}
.sub{{color:#8b949e;font-size:.85rem;margin-bottom:24px}}
.grid{{display:grid;grid-template-columns:repeat(auto-fill,minmax(520px,1fr));gap:18px}}
.chart-panel{{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:12px}}
.chart-panel svg{{width:100%;height:auto;display:block}}
.summary{{display:flex;gap:24px;flex-wrap:wrap;margin-bottom:24px}}
.card{{background:#161b22;border:1px solid #30363d;border-radius:6px;padding:12px 18px;min-width:120px}}
.card .label{{font-size:.75rem;color:#8b949e}}
.card .value{{font-size:1.3rem;color:#f0f6fc;font-weight:600}}
.footer{{text-align:center;color:#8b949e;font-size:.75rem;margin-top:30px;padding-top:16px;border-top:1px solid #30363d}}
</style></head><body>
<h1>TBox Resource Profile — {proc_name}</h1>
<p class="sub">Data source: {profile_dir}</p>
<div class="grid">
    {panels}
</div>
<div class="footer"><p>Generated by plot_profile.py — TBox Resource Monitor Toolkit</p></div>
</body></html>'''

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(f"用法: {sys.argv[0]} <profile_dir>")
        print(f"示例: {sys.argv[0]} /tmp/test_concurrent_523934_20260704_094051")
        sys.exit(1)

    profile_dir = sys.argv[1]
    if not os.path.isdir(profile_dir):
        print(f"❌ 目录不存在: {profile_dir}")
        sys.exit(1)

    html = build_html(profile_dir)
    out_path = os.path.join(profile_dir, "chart.html")
    with open(out_path, 'w', encoding='utf-8') as f:
        f.write(html)
    print(f"✅ 图表已生成: {out_path}")
    print(f"   用浏览器打开即可查看")
