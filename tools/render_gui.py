#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ADOCAO 视频渲染器 —— Tkinter 前端

用法：把本脚本和 ADOCAO.exe 放在同一目录，然后运行本脚本。
只用标准库（tkinter），不需要装任何东西。
"""

from __future__ import annotations

import os
import queue
import re
import subprocess
import threading
import time
import tkinter as tk
from tkinter import filedialog, messagebox, scrolledtext, ttk

HERE = os.path.dirname(os.path.abspath(__file__))
PROGRESS_RE = re.compile(r"frame\s+(\d+)\s*/\s*(\d+)")

RESOLUTIONS = ("1920x1080", "2560x1440", "3840x2160", "1280x720", "854x480")
FPS_LIST = ("120", "60", "30", "24")
LEVEL_TYPES = (("ADOFAI 关卡", "*.adofai *.level"), ("所有文件", "*.*"))
MUSIC_TYPES = (("音频", "*.wav *.ogg *.mp3 *.flac *.m4a *.aiff"), ("所有文件", "*.*"))


def find_exe():
    """ADOCAO.exe 优先在脚本所在目录找，找不到再看相邻的 build/。"""
    for d in (HERE, os.path.join(HERE, "build"),
              os.path.dirname(HERE), os.path.join(os.path.dirname(HERE), "build")):
        for name in ("ADOCAO.exe", "ADOCAO"):
            p = os.path.join(d, name)
            if os.path.isfile(p):
                return p
    return None


class RenderApp:
    def __init__(self, root, exe):
        self.root = root
        self.exe = exe
        self.exe_dir = os.path.dirname(exe)
        self.log_path = os.path.join(self.exe_dir, "ADOCAO.log")
        self.proc = None
        self.queue = queue.Queue()
        self.log_pos = 0
        self.out_file = ""

        self.v_level = tk.StringVar()
        self.v_music = tk.StringVar()
        self.v_out = tk.StringVar()
        self.v_res = tk.StringVar(value=RESOLUTIONS[0])
        self.v_w = tk.StringVar(value="1920")
        self.v_h = tk.StringVar(value="1080")
        self.v_fps = tk.StringVar(value="120")
        self.v_hit = tk.BooleanVar(value=True)
        self.v_zoom = tk.StringVar()
        self.v_tail = tk.StringVar()

        root.title("ADOCAO 视频渲染器")
        root.minsize(700, 520)
        self._build()
        self.root.after(120, self._drain)

    # ------------------------------------------------------------------ UI

    def _build(self):
        frm = ttk.Frame(self.root, padding=10)
        frm.grid(sticky="nsew")
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(0, weight=1)
        frm.columnconfigure(1, weight=1)
        frm.rowconfigure(9, weight=1)

        def label(r, text):
            ttk.Label(frm, text=text).grid(row=r, column=0, sticky="w", pady=3, padx=(0, 8))

        def browse(entry, kinds, title="选择文件"):
            p = filedialog.askopenfilename(title=title, filetypes=kinds)
            if p:
                entry.set(p)

        # 0 关卡
        label(0, "关卡文件")
        ttk.Entry(frm, textvariable=self.v_level).grid(row=0, column=1, sticky="ew", pady=3)
        ttk.Button(frm, text="浏览…", width=8,
                   command=lambda: browse(self.v_level, LEVEL_TYPES, "选择关卡")).grid(
            row=0, column=2, padx=(6, 0))

        # 1 音乐
        label(1, "音乐文件")
        ttk.Entry(frm, textvariable=self.v_music).grid(row=1, column=1, sticky="ew", pady=3)
        ttk.Button(frm, text="浏览…", width=8,
                   command=lambda: browse(self.v_music, MUSIC_TYPES, "选择音乐")).grid(
            row=1, column=2, padx=(6, 0))

        # 2 输出
        label(2, "输出视频")
        ttk.Entry(frm, textvariable=self.v_out).grid(row=2, column=1, sticky="ew", pady=3)
        ttk.Button(frm, text="另存为…", width=8, command=self._pick_out).grid(
            row=2, column=2, padx=(6, 0))

        # 3 分辨率
        label(3, "分辨率")
        res = ttk.Frame(frm)
        res.grid(row=3, column=1, columnspan=2, sticky="ew", pady=3)
        cb = ttk.Combobox(res, textvariable=self.v_res, values=RESOLUTIONS, width=11,
                          state="readonly")
        cb.grid(row=0, column=0)
        cb.bind("<<ComboboxSelected>>", self._apply_res)
        ttk.Entry(res, textvariable=self.v_w, width=6).grid(row=0, column=1, padx=(8, 2))
        ttk.Label(res, text="x").grid(row=0, column=2)
        ttk.Entry(res, textvariable=self.v_h, width=6).grid(row=0, column=3, padx=(2, 0))
        ttk.Label(res, text="（可直接改宽/高）", foreground="#888").grid(row=0, column=4, padx=(8, 0))

        # 4 帧率
        label(4, "帧率 (fps)")
        ttk.Combobox(frm, textvariable=self.v_fps, values=FPS_LIST, width=11).grid(
            row=4, column=1, sticky="w", pady=3)

        # 5 高级
        label(5, "高级（可留空）")
        adv = ttk.Frame(frm)
        adv.grid(row=5, column=1, columnspan=2, sticky="ew", pady=3)
        ttk.Label(adv, text="缩放 --zoom").grid(row=0, column=0)
        ttk.Entry(adv, textvariable=self.v_zoom, width=8).grid(row=0, column=1, padx=(6, 16))
        ttk.Label(adv, text="结尾秒数 --tail").grid(row=0, column=2)
        ttk.Entry(adv, textvariable=self.v_tail, width=8).grid(row=0, column=3, padx=(6, 16))
        ttk.Label(adv, text="可见宽度 = 2133.3 / zoom", foreground="#888").grid(row=0, column=4)

        # 6 打击音 + 按钮
        bar = ttk.Frame(frm)
        bar.grid(row=6, column=0, columnspan=3, sticky="ew", pady=(10, 4))
        ttk.Checkbutton(bar, text="启用打击音", variable=self.v_hit).grid(row=0, column=0)
        ttk.Button(bar, text="打开输出目录", command=self._open_out_dir).grid(row=0, column=1,
                                                                          padx=(16, 0))
        self.btn_stop = ttk.Button(bar, text="停止", command=self._stop, state="disabled")
        self.btn_stop.grid(row=0, column=3, padx=(6, 0))
        self.btn_start = ttk.Button(bar, text="开始渲染", command=self._start)
        self.btn_start.grid(row=0, column=2, padx=(24, 0))
        bar.columnconfigure(0, weight=1)

        # 7 进度
        self.bar = ttk.Progressbar(frm, mode="determinate", maximum=100)
        self.bar.grid(row=7, column=0, columnspan=3, sticky="ew", pady=(4, 2))
        self.lbl_status = ttk.Label(frm, text="就绪", foreground="#444")
        self.lbl_status.grid(row=8, column=0, columnspan=3, sticky="w")

        # 9 日志
        self.log = scrolledtext.ScrolledText(frm, height=14, wrap="none", state="disabled",
                                            font=("Consolas", 9))
        self.log.grid(row=9, column=0, columnspan=3, sticky="nsew", pady=(6, 0))

    # --------------------------------------------------------- 交互回调

    def _apply_res(self, _=None):
        try:
            w, h = self.v_res.get().lower().split("x")
            self.v_w.set(w)
            self.v_h.set(h)
        except ValueError:
            pass

    def _pick_out(self):
        level = self.v_level.get().strip()
        initial = os.path.join(os.path.dirname(level) if level else HERE,
                               os.path.splitext(os.path.basename(level))[0] + ".mp4"
                               if level else "output.mp4")
        p = filedialog.asksaveasfilename(title="保存为", defaultextension=".mp4",
                                         initialfile=os.path.basename(initial),
                                         initialdir=os.path.dirname(initial),
                                         filetypes=(("MP4 视频", "*.mp4"),))
        if p:
            self.v_out.set(p)

    def _open_out_dir(self):
        d = os.path.dirname(self.out_file) if self.out_file else ""
        if not d or not os.path.isdir(d):
            messagebox.showinfo("提示", "还没有输出文件")
            return
        if os.name == "nt":
            os.startfile(d)  # noqa: S606
        else:
            subprocess.Popen(["xdg-open", d])

    def _stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            self._log(">>> 已请求停止")

    # ------------------------------------------------------------ 渲染

    def _args(self):
        """校验并组装命令行，返回 None 表示校验失败。"""
        level = self.v_level.get().strip()
        if not level or not os.path.isfile(level):
            messagebox.showerror("参数错误", "请选择存在的关卡文件")
            return None
        music = self.v_music.get().strip()
        if music and not os.path.isfile(music):
            messagebox.showerror("参数错误", "音乐文件不存在")
            return None

        out = self.v_out.get().strip()
        if not out:
            base = os.path.splitext(level)[0]
            out = base + ".mp4"
            self.v_out.set(out)
        try:
            w, h = int(self.v_w.get()), int(self.v_h.get())
            fps = int(self.v_fps.get())
            if min(w, h, fps) <= 0:
                raise ValueError
        except ValueError:
            messagebox.showerror("参数错误", "宽 / 高 / 帧率必须是正整数")
            return None

        args = [self.exe, "--level", level, "--render", out,
                "--width", str(w), "--height", str(h), "--fps", str(fps)]
        if music:
            args += ["--music", music]
        if not self.v_hit.get():
            args.append("--no-hitsound")
        if self.v_zoom.get().strip():
            args += ["--zoom", self.v_zoom.get().strip()]
        if self.v_tail.get().strip():
            args += ["--tail", self.v_tail.get().strip()]
        self.out_file = out
        return args

    def _start(self):
        if self.proc and self.proc.poll() is None:
            return
        args = self._args()
        if not args:
            return

        self.log.configure(state="normal")
        self.log.delete("1.0", "end")
        self.log.configure(state="disabled")
        self.bar["value"] = 0
        self._log("> " + " ".join(f'"{a}"' if " " in a else a for a in args))
        self._log("")

        # 只读取本次新增的日志行
        self.log_pos = os.path.getsize(self.log_path) if os.path.exists(self.log_path) else 0

        self.btn_start.configure(state="disabled")
        self.btn_stop.configure(state="normal")
        self._status("渲染中…", "#0a0")

        try:
            self.proc = subprocess.Popen(args, cwd=self.exe_dir,
                                         stdout=subprocess.DEVNULL,
                                         stderr=subprocess.DEVNULL)
        except OSError as e:
            self._finish(-1, f"启动失败：{e}")
            return
        threading.Thread(target=self._watch, daemon=True).start()

    def _watch(self):
        while True:
            rc = self.proc.poll()
            self._tail()
            if rc is not None:
                self._tail()
                self.queue.put(("done", rc))
                return
            time.sleep(0.3)

    def _tail(self):
        try:
            size = os.path.getsize(self.log_path)
        except OSError:
            return
        if size < self.log_pos:      # 日志被重建
            self.log_pos = 0
        if size == self.log_pos:
            return
        try:
            with open(self.log_path, "r", encoding="utf-8", errors="replace") as f:
                f.seek(self.log_pos)
                data = f.read()
                self.log_pos = f.tell()
        except OSError:
            return
        for line in data.splitlines():
            if not line.strip():
                continue
            self.queue.put(("log", line))
            m = PROGRESS_RE.search(line)
            if m:
                self.queue.put(("progress", (int(m.group(1)), int(m.group(2)))))

    # ------------------------------------------------- 主线程事件泵

    def _drain(self):
        try:
            while True:
                kind, payload = self.queue.get_nowait()
                if kind == "log":
                    self._log(payload)
                elif kind == "progress":
                    done, total = payload
                    self.bar["value"] = 100.0 * done / total if total else 0
                    self._status(f"渲染中… {done} / {total} 帧", "#0a0")
                elif kind == "done":
                    self._finish(payload)
        except queue.Empty:
            pass
        self.root.after(120, self._drain)

    def _finish(self, rc, extra=""):
        self.btn_start.configure(state="normal")
        self.btn_stop.configure(state="disabled")
        if rc == 0:
            self.bar["value"] = 100
            size = os.path.getsize(self.out_file) / 1048576 if os.path.isfile(self.out_file) else 0
            self._status(f"完成：{self.out_file}（{size:.1f} MB）", "#0a0")
            self._log(f"\n>>> 完成，用时见上方日志。输出：{self.out_file}（{size:.1f} MB）")
        else:
            self._status(f"失败（退出码 {rc}）{extra}", "#c00")
            self._log(f"\n>>> 失败，退出码 {rc} {extra}。详见上方日志。")

    def _log(self, text):
        self.log.configure(state="normal")
        self.log.insert("end", text + "\n")
        self.log.see("end")
        self.log.configure(state="disabled")

    def _status(self, text, color="#444"):
        self.lbl_status.configure(text=text, foreground=color)


def main():
    exe = find_exe()
    root = tk.Tk()
    try:
        ttk.Style().theme_use("vista" if os.name == "nt" else "clam")
    except tk.TclError:
        pass
    if not exe:
        messagebox.showerror("找不到 ADOCAO.exe",
                             f"请把本脚本和 ADOCAO.exe 放在同一目录：\n{HERE}")
        return 1
    app = RenderApp(root, exe)
    app._log(f"使用：{exe}")
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
