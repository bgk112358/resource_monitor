#!/usr/bin/env python3
"""
plot_gui.py — TBox 应用资源画像本地 GUI 客户端

tkinter Canvas 柱状图, 签名校验, 窗口直接显示。

用法:
  python3 plot_gui.py <profile_dir>
  python3 plot_gui.py /tmp/test_signed_64325_20260704_185631
"""
import sys, os, csv, subprocess
import tkinter as tk
from tkinter import ttk

# ── csv_verify 二进制路径 ─────────────────────────
CSV_VERIFY_BIN = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               '..', 'src', 'build', 'csv_verify')

def verify_csv_signature(path):
    """调用 C 二进制 csv_verify -q 验签。
    返回 (valid, msg):
      True        = 签名有效
      False       = 签名无效 (数据被篡改)
      None        = CSV 无签名行
      'no_binary' = csv_verify 二进制不存在, 无法验签
    """
    try:
        r = subprocess.run([CSV_VERIFY_BIN, '-q', path],
                           capture_output=True, timeout=5)
        # 退出码: 0=有效, 1=无效, 2=无签名, 3=IO错误
        if r.returncode == 0:
            return True, None
        elif r.returncode == 2:
            return None, None
        else:
            return False, r.stderr.decode().strip() or "签名验证失败"
    except FileNotFoundError:
        return 'no_binary', None
    except Exception as e:
        return None, str(e)

def read_csv(path):
    """读取单列 CSV, 返回 [(x, y), ...]; 跳过 # 行"""
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
    return rows  # header line already filtered by float() parse failure

def read_csv_multi(path):
    """读取双列 CSV, 返回 ([y1,...], [y2,...]); 跳过 # 行"""
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
    return a, b  # header line already filtered by float() parse failure


# ══════════════════════════════════════════════════════════
#   BarChart — tkinter Canvas 柱状图组件
# ══════════════════════════════════════════════════════════
class BarChart(tk.Canvas):
    COLORS = {
        'cpu':  '#58a6ff',
        'mem':  '#3fb950',
        'thr':  '#f0883e',
        'fd':   '#d2a8ff',
        'ior':  '#f85149',
        'iow':  '#f59e0b',
    }

    def __init__(self, parent, title, data, color_key, sig_ok, **kw):
        w = kw.pop('width', 460)
        h = kw.pop('height', 220)
        super().__init__(parent, width=w, height=h, bg='#161b22',
                         highlightthickness=1, highlightbackground='#30363d', **kw)

        self.title = title
        self.data = data
        self.color = self.COLORS.get(color_key, '#888')
        self.sig_ok = sig_ok
        self._draw()

    def _draw(self):
        self.delete('all')
        w = int(self['width'])
        h = int(self['height'])
        ml, mr, mb = 48, 16, 32
        pw = w - ml - mr

        if not self.data:
            self.create_text(w//2, h//2, text='No data', fill='#888', font=('', 11))
            return

        ys = self.data
        ymax = max(ys)
        if ymax == 0: ymax = 1
        if ymax <= 10:   ymax = int(ymax) + 1
        elif ymax <= 100: ymax = ((int(ymax)//10)+1)*10
        else:             ymax = ((int(ymax)//100)+1)*100

        # 签名状态 (先画, 确定标题位置)
        ty = 12
        if self.sig_ok is False:
            self.create_rectangle(0, 0, w, 20, fill='#3d1115', outline='')
            self.create_text(w//2, 12, text='⚠ 签名无效 — 数据可能被篡改',
                             fill='#f85149', font=('', 9, 'bold'))
            ty = 30
        elif self.sig_ok is None:
            self.create_rectangle(0, 0, w, 20, fill='#332810', outline='')
            self.create_text(w//2, 12, text='⚠ 无签名 — 无法核实真伪',
                             fill='#d2991d', font=('', 9, 'bold'))
            ty = 30
        elif self.sig_ok == 'no_binary':
            self.create_rectangle(0, 0, w, 20, fill='#332810', outline='')
            self.create_text(w//2, 12, text='⚠ 无法验签 — 验签工具未找到',
                             fill='#d2991d', font=('', 9, 'bold'))
            ty = 30

        # 标题 (画在签名横幅下面)
        self.create_text(w//2, ty, text=self.title, fill='#ccc', font=('', 11, 'bold'))

        mt = ty + 14
        ph = h - mt - mb

        def tx(i):
            return ml + i / max(1, len(ys)) * pw
        def ty_(v):
            return h - mb - (v / ymax) * ph

        # 网格线
        for i in range(5):
            gy = h - mb - ph * i / 4
            self.create_line(ml, gy, w-mr, gy, fill='#333', dash=(3,3))

        # Y 轴标签
        for i in range(5):
            val = ymax * i / 4
            gy = h - mb - ph * i / 4
            self.create_text(ml-6, gy, text=f'{val:.0f}', fill='#888',
                             font=('', 8), anchor='e')

        # X 轴
        self.create_line(ml, h-mb, w-mr, h-mb, fill='#555')
        self.create_line(ml, mt, ml, h-mb, fill='#555')

        # 柱子
        n = len(ys)
        bar_w = max(3, pw / n * 0.6)
        gap = pw / n
        for i, y in enumerate(ys):
            bx = ml + gap * i + (gap - bar_w) / 2
            by = ty_(y)
            bh = max(1, ty_(0) - ty_(y))
            self.create_rectangle(bx, by, bx+bar_w, by+bh,
                                  fill=self.color, outline='', tags='bar')

        # X 轴标签
        step = max(1, n // 8)
        for i in range(0, n, step):
            bx = ml + gap * i + gap/2
            self.create_text(bx, h-mb+12, text=str(i+1), fill='#888',
                             font=('', 8), anchor='n')
        if n > 1 and (n-1) % step != 0:
            bx = ml + gap * (n-1) + gap/2
            self.create_text(bx, h-mb+12, text=str(n), fill='#888',
                             font=('', 8), anchor='n')


# ══════════════════════════════════════════════════════════
#   Application
# ══════════════════════════════════════════════════════════
class App(tk.Tk):
    def __init__(self, profile_dir):
        super().__init__()
        self.title("TBox Resource Profile")
        self.configure(bg='#0d1117')

        # 读取 report.txt 元数据
        proc_name = "unknown"
        rpt_path = os.path.join(profile_dir, "report.txt")
        if os.path.exists(rpt_path):
            with open(rpt_path) as f:
                for line in f:
                    if "进程名称:" in line:
                        proc_name = line.split(":")[-1].strip()

        # 标题栏
        header = tk.Frame(self, bg='#0d1117')
        header.pack(fill='x', padx=16, pady=(12, 4))
        tk.Label(header, text=f"TBox Resource Profile — {proc_name}",
                 fg='#f0f6fc', bg='#0d1117', font=('', 13, 'bold')).pack(anchor='w')
        tk.Label(header, text=f"Data: {profile_dir}",
                 fg='#8b949e', bg='#0d1117', font=('', 8)).pack(anchor='w')

        # 图表网格 2×3
        grid = tk.Frame(self, bg='#0d1117')
        grid.pack(fill='both', expand=True, padx=12, pady=8)

        csv_path = lambda f: os.path.join(profile_dir, f)

        # 加载数据 + 验证签名
        cpu_data = read_csv(csv_path('cpu.csv'))
        mem_data = read_csv(csv_path('mem.csv'))
        thr_data, fd_data = read_csv_multi(csv_path('threads_fd.csv'))
        ior_data, iow_data = read_csv_multi(csv_path('io.csv'))

        def sig(fname):
            r = verify_csv_signature(csv_path(fname))
            return r[0] if r else None

        charts = [
            ('CPU Usage (%)',         cpu_data,  'cpu',  sig('cpu.csv')),
            ('Memory RSS (KB)',       mem_data,  'mem',  sig('mem.csv')),
            ('Thread Count',          thr_data,  'thr',  sig('threads_fd.csv')),
            ('Open File Descriptors', fd_data,   'fd',   sig('threads_fd.csv')),
            ('IO Read (KB/s)',        ior_data,  'ior',  sig('io.csv')),
            ('IO Write (KB/s)',       iow_data,  'iow',  sig('io.csv')),
        ]

        for i, (title, data, ck, sig_ok) in enumerate(charts):
            row, col = i // 2, i % 2
            chart = BarChart(grid, title, data, ck, sig_ok, width=420, height=200)
            chart.grid(row=row, column=col, padx=6, pady=6, sticky='nsew')
            grid.grid_columnconfigure(col, weight=1)
            grid.grid_rowconfigure(row, weight=1)

        # 底部状态栏
        footer = tk.Frame(self, bg='#0d1117')
        footer.pack(fill='x', padx=16, pady=(0, 8))
        tk.Label(footer, text="plot_gui.py — TBox Resource Monitor Toolkit",
                 fg='#8b949e', bg='#0d1117', font=('', 7)).pack(side='left')
        tk.Label(footer, text="Ctrl+Q 退出",
                 fg='#8b949e', bg='#0d1117', font=('', 7)).pack(side='right')

        self.bind('<Control-q>', lambda e: self.destroy())
        self.bind('<Escape>', lambda e: self.destroy())

        # 窗口尺寸
        self.update_idletasks()
        w = max(900, self.winfo_reqwidth())
        h = max(680, self.winfo_reqheight())
        self.geometry(f"{w}x{h}")
        self.minsize(780, 560)


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(f"用法: {sys.argv[0]} <profile_dir>")
        sys.exit(1)
    profile_dir = sys.argv[1]
    if not os.path.isdir(profile_dir):
        print(f"❌ 目录不存在: {profile_dir}")
        sys.exit(1)

    app = App(profile_dir)
    app.mainloop()
