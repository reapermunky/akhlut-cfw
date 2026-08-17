#!/usr/bin/env python3
"""
Akhlut CFW Installer — FreeWili OG
One-click custom firmware installer with WASM app manager.

Requirements: pip install pyserial freewili
"""

import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import threading
import time
import os
import struct
import ctypes
import string
import sys

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    root = tk.Tk()
    root.withdraw()
    messagebox.showerror("Missing dependency",
                         "pyserial is required.\n\nRun:  pip install pyserial")
    sys.exit(1)

try:
    from freewili import FreeWili
    from freewili.types import FreeWiliProcessorType
    HAS_FREEWILI = True
except ImportError:
    HAS_FREEWILI = False

try:
    from PIL import Image, ImageTk
    HAS_PIL = True
except ImportError:
    HAS_PIL = False

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FW_DIR = os.path.join(SCRIPT_DIR, "firmware")
STOCK_DIR = os.path.join(FW_DIR, "stock")
APPS_DIR = os.path.join(SCRIPT_DIR, "apps")

MAIN_UF2 = os.path.join(FW_DIR, "freewili_main.uf2")
DISPLAY_UF2 = os.path.join(FW_DIR, "freewili_display.uf2")
STOCK_MAIN_UF2 = os.path.join(STOCK_DIR, "ogfw_mainV21.uf2")
STOCK_DISPLAY_UF2 = os.path.join(STOCK_DIR, "FreeWiliDisplayV67.uf2")

VID_RP2040 = 0x2E8A
VID_INTREPID = 0x093C
BAUD = 115200
BAUD_RESET = 1200

BG = "#0d1117"
BG_CARD = "#161b22"
BG_INPUT = "#21262d"
BG_BTN = "#1a1a1a"
BG_BTN_HOVER = "#252525"
BG_BTN_DISABLED = "#1a1a1a"
ACCENT = "#2CB5FF"
ACCENT_BRIGHT = "#5ac8ff"
ACCENT_DIM = "#0a6e7a"
BORDER = "#2CB5FF"
BORDER_PRIMARY = "#5ac8ff"
BORDER_DISABLED = "#333333"
TEXT = "#e6edf3"
TEXT_DIM = "#8b949e"
TEXT_DISABLED = "#666666"
GREEN = "#3fb950"
RED = "#f85149"
YELLOW = "#d29922"

VERSION = "1.0.0"


def find_rpi_rp2_drive():
    kernel32 = ctypes.windll.kernel32
    buf = ctypes.create_unicode_buffer(256)
    for letter in string.ascii_uppercase:
        drive = f"{letter}:\\"
        if not os.path.exists(drive):
            continue
        try:
            ok = kernel32.GetVolumeInformationW(
                drive, buf, 256, None, None, None, None, 0)
            if ok and buf.value == "RPI-RP2":
                return f"{letter}:"
        except Exception:
            pass
    return None


def find_freewili_ports():
    result = []
    for p in serial.tools.list_ports.comports():
        if p.vid in (VID_RP2040, VID_INTREPID):
            result.append(p)
    return result


class InstallerApp:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("Akhlut CFW Installer")
        self.root.geometry("860x700")
        self.root.configure(bg=BG)
        self.root.minsize(800, 650)

        try:
            self.root.iconbitmap(default="")
        except Exception:
            pass

        self.com_port = None
        self.working = False
        self._buttons = []

        self._build_ui()
        self.detect_device()

    # ── UI ────────────────────────────────────────────────────

    def _build_ui(self):
        pad = 18
        main = tk.Frame(self.root, bg=BG, padx=pad, pady=pad)
        main.pack(fill="both", expand=True)

        # Logo / title
        hdr = tk.Frame(main, bg=BG)
        hdr.pack(fill="x", pady=(0, 10))

        logo_loaded = False
        logo_path = os.path.join(SCRIPT_DIR, "akhlut_logo.jpg")
        if not os.path.exists(logo_path):
            logo_path = os.path.join(SCRIPT_DIR, "akhlut_logo.png")
        if os.path.exists(logo_path) and HAS_PIL:
            try:
                img = Image.open(logo_path)
                w, h = img.size
                new_w = 240
                new_h = int(h * new_w / w)
                img = img.resize((new_w, new_h), Image.LANCZOS)
                self._logo_img = ImageTk.PhotoImage(img)
                tk.Label(hdr, image=self._logo_img, bg=BG).pack(
                    pady=(0, 2))
                logo_loaded = True
            except Exception:
                pass

        if not logo_loaded:
            tk.Label(hdr, text="AKHLUT",
                     font=("Segoe UI", 28, "bold"),
                     fg=ACCENT, bg=BG).pack()

        tk.Label(hdr,
                 text=f"Custom Firmware Installer  —  "
                      f"FreeWili OG  v{VERSION}",
                 font=("Segoe UI", 10), fg=TEXT_DIM, bg=BG).pack()

        tk.Frame(main, bg=ACCENT_DIM, height=1).pack(
            fill="x", pady=(4, 10))

        # ── Device status card ────────────────────────────────
        card = tk.Frame(main, bg=BG_CARD,
                        highlightbackground=ACCENT_DIM,
                        highlightthickness=1, padx=14, pady=10)
        card.pack(fill="x", pady=(0, 10))

        tk.Label(card, text="DEVICE",
                 font=("Segoe UI", 8, "bold"),
                 fg=TEXT_DIM, bg=BG_CARD).pack(anchor="w")

        row = tk.Frame(card, bg=BG_CARD)
        row.pack(fill="x", pady=(4, 0))

        self.dot = tk.Label(row, text="●",
                            font=("Segoe UI", 14),
                            fg=TEXT_DIM, bg=BG_CARD)
        self.dot.pack(side="left")

        self.status_lbl = tk.Label(row, text="Scanning…",
                                   font=("Segoe UI", 11),
                                   fg=TEXT, bg=BG_CARD)
        self.status_lbl.pack(side="left", padx=(8, 0))

        self.refresh_btn = self._link_btn(
            row, "Refresh", self.detect_device)
        self.refresh_btn.pack(side="right")

        # ── Firmware buttons ──────────────────────────────────
        brow = tk.Frame(main, bg=BG)
        brow.pack(fill="x", pady=(0, 10))
        brow.columnconfigure(0, weight=1)
        brow.columnconfigure(1, weight=1)
        brow.columnconfigure(2, weight=1)

        self.install_stock_btn = self._themed_btn(
            brow, "Install\nfrom Stock", self.install_from_stock,
            primary=True)
        self.install_stock_btn.grid(row=0, column=0, sticky="nsew",
                                    padx=(0, 4))

        self.update_btn = self._themed_btn(
            brow, "Update\nAkhlut", self.update_akhlut)
        self.update_btn.grid(row=0, column=1, sticky="nsew",
                             padx=4)

        self.restore_btn = self._themed_btn(
            brow, "Restore\nStock FW", self.restore)
        self.restore_btn.grid(row=0, column=2, sticky="nsew",
                              padx=(4, 0))

        self._buttons = [self.install_stock_btn, self.update_btn,
                         self.restore_btn]

        # ── Progress ──────────────────────────────────────────
        style = ttk.Style()
        style.theme_use("clam")
        style.configure("cyan.Horizontal.TProgressbar",
                        troughcolor=BG_INPUT, background=ACCENT,
                        darkcolor=ACCENT, lightcolor=ACCENT,
                        borderwidth=0)

        self.pvar = tk.DoubleVar()
        self.pbar = ttk.Progressbar(
            main, variable=self.pvar, maximum=100,
            style="cyan.Horizontal.TProgressbar")
        self.pbar.pack(fill="x", pady=(0, 2))

        self.step_lbl = tk.Label(main, text="",
                                 font=("Segoe UI", 10),
                                 fg=TEXT_DIM, bg=BG, anchor="w")
        self.step_lbl.pack(fill="x")

        # ── Log + WASM Apps (side by side) ────────────────────
        bottom = tk.Frame(main, bg=BG)
        bottom.pack(fill="both", expand=True, pady=(10, 0))
        bottom.columnconfigure(0, weight=3)
        bottom.columnconfigure(1, weight=2)

        # ── Log panel (left, 60%) ─────────────────────────
        lf = tk.Frame(bottom, bg=BG_CARD,
                      highlightbackground=ACCENT_DIM,
                      highlightthickness=1)
        lf.grid(row=0, column=0, sticky="nsew",
                padx=(0, 5))

        log_hdr = tk.Label(lf, text="LOG",
                           font=("Segoe UI", 8, "bold"),
                           fg=TEXT_DIM, bg=BG_CARD,
                           anchor="w", padx=10)
        log_hdr.pack(fill="x", pady=(6, 0))

        log_inner = tk.Frame(lf, bg=BG_CARD)
        log_inner.pack(fill="both", expand=True,
                       padx=(0, 2))

        self.log_box = tk.Text(
            log_inner, bg=BG_CARD, fg=TEXT_DIM,
            font=("Consolas", 9), bd=0, padx=10, pady=4,
            state="disabled", wrap="word",
            selectbackground=ACCENT_DIM,
            insertbackground=TEXT)
        self.log_box.pack(side="left", fill="both",
                          expand=True)

        log_scroll = tk.Scrollbar(
            log_inner, orient="vertical",
            command=self.log_box.yview)
        log_scroll.pack(side="right", fill="y")
        self.log_box.configure(
            yscrollcommand=log_scroll.set)

        for tag, color in [("info", TEXT_DIM),
                           ("ok", GREEN), ("err", RED),
                           ("warn", YELLOW),
                           ("hl", ACCENT)]:
            self.log_box.tag_configure(
                tag, foreground=color)

        # ── WASM Apps panel (right, 40%) ──────────────────
        app_card = tk.Frame(bottom, bg=BG_CARD,
                            highlightbackground=ACCENT_DIM,
                            highlightthickness=1, padx=10,
                            pady=8)
        app_card.grid(row=0, column=1, sticky="nsew",
                      padx=(5, 0))

        tk.Label(app_card, text="WASM APPS",
                 font=("Segoe UI", 8, "bold"),
                 fg=TEXT_DIM, bg=BG_CARD).pack(
                     anchor="w")

        app_links = tk.Frame(app_card, bg=BG_CARD)
        app_links.pack(fill="x", pady=(2, 0))

        self.app_refresh_btn = self._link_btn(
            app_links, "Refresh", self.refresh_apps)
        self.app_refresh_btn.pack(side="left")

        self.app_add_btn = self._link_btn(
            app_links, "Add .wasm", self.add_app)
        self.app_add_btn.pack(side="left",
                              padx=(10, 0))

        self.app_delete_btn = self._link_btn(
            app_links, "Delete", self.delete_app)
        self.app_delete_btn.pack(side="left",
                                 padx=(10, 0))

        list_frame = tk.Frame(app_card, bg=BG_INPUT)
        list_frame.pack(fill="both", expand=True,
                        pady=(6, 0))

        self.app_listbox = tk.Listbox(
            list_frame, bg=BG_INPUT, fg=TEXT,
            font=("Consolas", 9),
            selectbackground=ACCENT_DIM,
            selectforeground=TEXT,
            bd=0, highlightthickness=0,
            activestyle="none")
        self.app_listbox.pack(fill="both", expand=True,
                              padx=2, pady=2)

        self.app_status = tk.Label(
            app_card, text="Click Refresh to load apps",
            font=("Segoe UI", 9), fg=TEXT_DIM,
            bg=BG_CARD, anchor="w")
        self.app_status.pack(fill="x", pady=(4, 0))

    def _themed_btn(self, parent, text, cmd, primary=False):
        border_color = BORDER_PRIMARY if primary else BORDER
        font = ("Segoe UI", 11, "bold") if primary else \
               ("Segoe UI", 10)

        frame = tk.Frame(parent, bg=border_color,
                         padx=1, pady=1)
        btn = tk.Button(
            frame, text=text, font=font,
            fg=ACCENT_BRIGHT if primary else ACCENT,
            bg=BG_BTN,
            activeforeground=ACCENT_BRIGHT,
            activebackground=BG_BTN_HOVER,
            disabledforeground=TEXT_DISABLED,
            bd=0, padx=16, pady=14, cursor="hand2",
            command=cmd)
        btn.pack(fill="both", expand=True)

        btn._border_frame = frame
        btn._is_primary = primary
        btn._border_default = border_color

        btn.bind("<Enter>",
                 lambda e, b=btn: b.configure(bg=BG_BTN_HOVER)
                 if str(b["state"]) != "disabled" else None)
        btn.bind("<Leave>",
                 lambda e, b=btn: b.configure(bg=BG_BTN)
                 if str(b["state"]) != "disabled" else None)

        return frame

    def _link_btn(self, parent, text, cmd):
        b = tk.Label(parent, text=text,
                     font=("Segoe UI", 9, "underline"),
                     fg=ACCENT, bg=parent["bg"], cursor="hand2")
        b.bind("<Button-1>", lambda e: cmd())
        b.bind("<Enter>",
               lambda e: b.configure(fg=ACCENT_BRIGHT))
        b.bind("<Leave>",
               lambda e: b.configure(fg=ACCENT))
        return b

    # ── Helpers ───────────────────────────────────────────────

    def _log(self, msg, tag="info"):
        self.log_box.configure(state="normal")
        self.log_box.insert("end", msg + "\n", tag)
        self.log_box.see("end")
        self.log_box.configure(state="disabled")

    def _log_clear(self):
        self.log_box.configure(state="normal")
        self.log_box.delete("1.0", "end")
        self.log_box.configure(state="disabled")

    def _step(self, text):
        self.step_lbl.configure(text=text)

    def _prog(self, pct):
        self.pvar.set(pct)
        self.root.update_idletasks()

    def _set_status(self, text, color):
        self.status_lbl.configure(text=text)
        self.dot.configure(fg=color)

    def _set_busy(self, busy):
        self.working = busy
        for frame in self._buttons:
            btn = frame.winfo_children()[0]
            if busy:
                btn.configure(state="disabled", bg=BG_BTN_DISABLED)
                frame.configure(bg=BORDER_DISABLED)
            else:
                btn.configure(state="normal", bg=BG_BTN)
                frame.configure(bg=btn._border_default)

    # ── Detection ─────────────────────────────────────────────

    def detect_device(self):
        self.com_port = None
        ports = find_freewili_ports()
        if not ports:
            self._set_status(
                "No device found — plug in your FreeWili "
                "and click Refresh", YELLOW)
            self._log("No FreeWili detected. Plug it in with the "
                      "battery connected.", "warn")
            return

        best = None
        for p in ports:
            if p.vid == VID_RP2040:
                if best is None or \
                        "USB Serial Device" in (p.description or ""):
                    best = p
        if best is None:
            best = ports[0]

        self.com_port = best.device
        self._set_status(
            f"FreeWili OG detected on {best.device}", GREEN)
        vid = f"{best.vid:04X}" if best.vid else "?"
        pid = f"{best.pid:04X}" if best.pid else "?"
        self._log(f"Found: {best.description}  ({best.device}  "
                  f"VID:PID {vid}:{pid})", "ok")

    # ── Low-level serial ops ──────────────────────────────────

    def _serial_cmd(self, port, cmd_byte, wait_for, timeout=5):
        s = serial.Serial(port, BAUD, timeout=2)
        time.sleep(0.3)
        s.reset_input_buffer()
        s.write(cmd_byte)
        end = time.time() + timeout
        buf = b""
        while time.time() < end:
            if s.in_waiting:
                buf += s.read(s.in_waiting)
                if wait_for in buf:
                    time.sleep(0.1)
                    if s.in_waiting:
                        buf += s.read(s.in_waiting)
                    break
            time.sleep(0.05)
        s.close()
        return buf.decode("utf-8", errors="replace")

    def _baud_touch(self, port):
        try:
            s = serial.Serial(port, BAUD_RESET)
            s.dtr = True
            time.sleep(0.3)
            s.dtr = False
            time.sleep(0.3)
            s.close()
        except (serial.SerialException, OSError):
            pass

    def _wait_drive(self, timeout=45):
        end = time.time() + timeout
        while time.time() < end:
            d = find_rpi_rp2_drive()
            if d:
                return d
            time.sleep(0.5)
        return None

    def _copy_uf2(self, drive, path):
        dest = os.path.join(drive + "\\", os.path.basename(path))
        with open(path, "rb") as f:
            data = f.read()
        with open(dest, "wb") as f:
            f.write(data)

    def _wait_port(self, timeout=30):
        end = time.time() + timeout
        while time.time() < end:
            for p in serial.tools.list_ports.comports():
                if p.vid in (VID_RP2040, VID_INTREPID):
                    self.com_port = p.device
                    return p.device
            time.sleep(0.5)
        return None

    def _send_B(self, port):
        s = serial.Serial(port, BAUD, timeout=2)
        s.dtr = True
        s.rts = True
        time.sleep(1)
        s.reset_input_buffer()
        s.write(b"\r\n\r\n")
        time.sleep(0.5)
        s.reset_input_buffer()
        s.write(b"B\r\n")
        time.sleep(2)
        resp = b""
        if s.in_waiting:
            resp = s.read(s.in_waiting)
        s.close()
        return resp.decode("utf-8", errors="replace")

    def _freewili_display_bootloader(self, log, step):
        step("Entering Display bootloader (freewili package)…")
        log("Using freewili package to reset Display…", "hl")
        r = FreeWili.find_first()
        fw = r.ok()
        if fw is None:
            log("freewili package could not find device", "err")
            return None
        fw.reset_to_uf2_bootloader(FreeWiliProcessorType.Display)
        log("Display reset command sent", "ok")

        step("Waiting for Display bootloader drive…")
        log("Waiting for RPI-RP2 drive (up to 45s)…")
        drive = self._wait_drive()
        if drive:
            log(f"Bootloader drive: {drive}", "ok")
        return drive

    def _upload_file(self, port, filepath):
        name = os.path.basename(filepath)
        dev_path = f"/apps/{name}"

        with open(filepath, "rb") as f:
            data = f.read()

        if len(data) > 32768:
            return False, \
                f"{name} too large ({len(data)} bytes, max 32 KB)"

        s = serial.Serial(port, BAUD, timeout=3)
        time.sleep(0.3)
        s.reset_input_buffer()
        s.write(b"U")

        end = time.time() + 5
        buf = b""
        while time.time() < end:
            if s.in_waiting:
                buf += s.read(s.in_waiting)
                if b"READY" in buf:
                    break
            time.sleep(0.05)
        else:
            s.close()
            return False, "Device did not respond READY"

        time.sleep(0.05)
        pb = dev_path.encode()
        s.write(bytes([len(pb)]))
        s.write(pb)
        s.write(struct.pack("<I", len(data)))

        for i in range(0, len(data), 256):
            s.write(data[i:i + 256])
            time.sleep(0.005)

        end = time.time() + 10
        buf = b""
        while time.time() < end:
            if s.in_waiting:
                buf += s.read(s.in_waiting)
                if b"OK" in buf or b"ERR" in buf:
                    break
            time.sleep(0.05)

        s.close()
        resp = buf.decode("utf-8", errors="replace").strip()
        if "OK" in resp:
            return True, f"{name} ({len(data)} bytes)"
        return False, f"Upload error: {resp}"

    def _list_apps_serial(self, port):
        resp = self._serial_cmd(port, b"L", b"END")
        apps = []
        for line in resp.splitlines():
            line = line.strip()
            if line.startswith("APP "):
                parts = line.split()
                if len(parts) >= 3:
                    name = parts[1]
                    try:
                        size = int(parts[2])
                    except ValueError:
                        size = 0
                    apps.append((name, size))
        return apps

    def _delete_app_serial(self, port, dev_path):
        s = serial.Serial(port, BAUD, timeout=3)
        time.sleep(0.3)
        s.reset_input_buffer()
        s.write(b"X")

        end = time.time() + 3
        buf = b""
        while time.time() < end:
            if s.in_waiting:
                buf += s.read(s.in_waiting)
                if b"DELREADY" in buf:
                    break
            time.sleep(0.05)
        else:
            s.close()
            return False, "Device did not respond"

        time.sleep(0.05)
        pb = dev_path.encode()
        s.write(bytes([len(pb)]))
        s.write(pb)

        end = time.time() + 5
        buf = b""
        while time.time() < end:
            if s.in_waiting:
                buf += s.read(s.in_waiting)
                if b"OK" in buf or b"ERR" in buf:
                    break
            time.sleep(0.05)

        s.close()
        resp = buf.decode("utf-8", errors="replace").strip()
        if "OK" in resp:
            return True, "Deleted"
        return False, f"Delete error: {resp}"

    # ── App Manager ───────────────────────────────────────────

    def refresh_apps(self):
        if self.working:
            return
        if not self.com_port:
            self.detect_device()
        if not self.com_port:
            self.app_status.configure(
                text="No device connected", fg=YELLOW)
            return

        self.app_status.configure(
            text="Reading apps…", fg=TEXT_DIM)
        self.root.update_idletasks()

        def do_refresh():
            try:
                apps = self._list_apps_serial(self.com_port)
                self.root.after(
                    0, lambda: self._populate_app_list(apps))
            except Exception as e:
                self.root.after(
                    0, lambda: self.app_status.configure(
                        text=f"Error: {e}", fg=RED))

        threading.Thread(target=do_refresh, daemon=True).start()

    def _populate_app_list(self, apps):
        self.app_listbox.delete(0, tk.END)
        if not apps:
            self.app_status.configure(
                text="No apps installed", fg=TEXT_DIM)
            return
        for name, size in apps:
            if size >= 1024:
                sz = f"{size / 1024:.1f} KB"
            else:
                sz = f"{size} B"
            self.app_listbox.insert(
                tk.END, f"  {name:<28s} {sz}")
        self.app_status.configure(
            text=f"{len(apps)} app"
                 f"{'s' if len(apps) != 1 else ''} on device",
            fg=GREEN)
        self._app_data = apps

    def add_app(self):
        if self.working:
            return
        if not self.com_port:
            self.detect_device()
        if not self.com_port:
            messagebox.showwarning(
                "No Device", "Plug in your FreeWili first.")
            return

        files = filedialog.askopenfilenames(
            title="Select .wasm files to upload",
            filetypes=[("WASM apps", "*.wasm"),
                       ("All files", "*.*")])
        if not files:
            return

        self._set_busy(True)
        self.app_status.configure(text="Uploading…", fg=ACCENT)

        def do_upload():
            for f in files:
                name = os.path.basename(f)
                self.root.after(0, lambda n=name: self._log(
                    f"Uploading {n}…", "hl"))
                ok, msg = self._upload_file(self.com_port, f)
                tag = "ok" if ok else "err"
                self.root.after(0, lambda m=msg, t=tag: self._log(
                    f"  {m}", t))
                time.sleep(0.3)

            self.root.after(0, lambda: self._set_busy(False))
            self.root.after(0, self.refresh_apps)

        threading.Thread(target=do_upload, daemon=True).start()

    def delete_app(self):
        if self.working:
            return
        sel = self.app_listbox.curselection()
        if not sel:
            messagebox.showinfo(
                "No Selection",
                "Select an app from the list first.")
            return
        if not self.com_port:
            return

        idx = sel[0]
        if not hasattr(self, '_app_data') or \
                idx >= len(self._app_data):
            return

        name = self._app_data[idx][0]
        if not messagebox.askyesno(
                "Delete App", f"Delete {name} from device?"):
            return

        self._set_busy(True)
        self.app_status.configure(
            text=f"Deleting {name}…", fg=ACCENT)

        def do_delete():
            dev_path = f"/apps/{name}"
            ok, msg = self._delete_app_serial(
                self.com_port, dev_path)
            tag = "ok" if ok else "err"
            self.root.after(0, lambda: self._log(
                f"Delete {name}: {msg}", tag))
            self.root.after(0, lambda: self._set_busy(False))
            self.root.after(0, self.refresh_apps)

        threading.Thread(target=do_delete, daemon=True).start()

    # ── Flash sequences ───────────────────────────────────────

    def _rescan_port(self):
        for p in serial.tools.list_ports.comports():
            if p.vid in (VID_RP2040, VID_INTREPID):
                self.com_port = p.device
                return p.device
        return None

    def _enter_main_bootloader(self, port, log, step):
        drive = find_rpi_rp2_drive()
        if drive:
            log("RPI-RP2 already mounted — skipping reset",
                "ok")
            return drive

        step("Entering Main bootloader…")
        log("Resetting Main into bootloader (1200 baud)…",
            "hl")
        self._baud_touch(port)

        step("Waiting for bootloader drive…")
        log("Waiting for RPI-RP2 drive (up to 45s)…")
        drive = self._wait_drive()
        if drive:
            log(f"Bootloader drive: {drive}", "ok")
        return drive

    def _enter_display_bootloader(self, port, log, step):
        step("Entering Display bootloader…")
        log("Sending 'B' to enter Display bootloader…", "hl")
        resp = self._send_B(port)
        if resp.strip():
            log(f"Main response: {resp.strip()}", "info")
        else:
            log("Main sent no response to 'B'", "warn")

        step("Waiting for Display bootloader drive…")
        log("Waiting for RPI-RP2 drive (up to 30s)…")
        drive = self._wait_drive(timeout=30)
        if drive:
            log(f"Bootloader drive: {drive}", "ok")
        return drive

    def _wait_drive_gone(self, drive, timeout=15):
        end = time.time() + timeout
        while time.time() < end:
            if not os.path.exists(drive + "\\"):
                return True
            time.sleep(0.5)
        return False

    def _flash_uf2(self, drive, uf2, label, log, step,
                   size_warning=None):
        step(f"Writing {label} firmware…")
        if size_warning:
            log(size_warning, "warn")
            step(size_warning)
        log(f"Copying {os.path.basename(uf2)}…", "hl")
        self._copy_uf2(drive, uf2)
        step(f"Waiting for {label} to eject…")
        if self._wait_drive_gone(drive):
            log(f"{label} firmware written — device "
                "rebooting…", "ok")
        else:
            log(f"{label} drive still present — may need "
                "a moment…", "warn")

    def _wait_reboot(self, log, step, boot_wait=3):
        step("Waiting for reboot…")
        time.sleep(boot_wait)
        port = self._wait_port()
        if port:
            log(f"Device back on {port}", "ok")
            time.sleep(5)
            log("Post-boot settle complete", "ok")
        return port

    def _verify_alive(self, log, step):
        step("Verifying device is alive…")
        port = self._rescan_port()
        if port:
            try:
                s = serial.Serial(port, BAUD, timeout=3)
                s.dtr = True
                s.rts = True
                time.sleep(1)
                s.reset_input_buffer()
                s.write(b"\r\n")
                time.sleep(1)
                resp = s.read(s.in_waiting or 1)
                s.close()
                if b">" in resp:
                    log("Device responding — verified OK",
                        "ok")
                    return True
            except Exception:
                pass
        log("If the screen is not on, unplug USB and battery, "
            "wait 5 seconds, then reconnect.", "warn")
        return False

    def _show_retry(self, label, callback):
        self.root.after(
            0, lambda: self._make_retry_btn(label, callback))

    def _make_retry_btn(self, label, callback):
        self._retry_btn = tk.Button(
            self.step_lbl.master, text=f"Retry {label}",
            font=("Segoe UI", 10, "bold"), fg=BG, bg=YELLOW,
            activeforeground=BG, activebackground=ACCENT,
            bd=0, padx=16, pady=6, cursor="hand2",
            command=lambda: self._on_retry(callback))
        self._retry_btn.pack(pady=(4, 0))

    def _on_retry(self, callback):
        if hasattr(self, '_retry_btn'):
            self._retry_btn.destroy()
            del self._retry_btn
        self._set_busy(True)
        threading.Thread(target=callback, daemon=True).start()

    def _clear_retry(self):
        def do():
            if hasattr(self, '_retry_btn'):
                self._retry_btn.destroy()
                del self._retry_btn
        self.root.after(0, do)

    # ── Install from Stock ────────────────────────────────────
    # Display first (freewili package), then Main (1200 baud).
    # NO 'B' command — it corrupts stock Display firmware.

    def _install_from_stock_thread(self, resume_at=1):
        log = lambda m, t="info": self.root.after(
            0, lambda: self._log(m, t))
        step = lambda m: self.root.after(
            0, lambda: self._step(m))
        prog = lambda p: self.root.after(
            0, lambda: self._prog(p))

        try:
            # ── Step 1: Flash Display via freewili package ───
            if resume_at <= 1:
                prog(5)
                if not os.path.exists(DISPLAY_UF2):
                    log("Display UF2 not found — skipping",
                        "warn")
                else:
                    drive = self._freewili_display_bootloader(
                        log, step)
                    if not drive:
                        log("Display bootloader drive did not "
                            "appear. Click Retry.", "err")
                        step("Display bootloader not found")
                        self.root.after(0, lambda: self._set_busy(
                            False))
                        self._show_retry(
                            "Display flash",
                            lambda: (
                                self._install_from_stock_thread(1)))
                        return

                    prog(20)
                    self._flash_uf2(
                        drive, DISPLAY_UF2, "Display", log, step)

                    prog(30)
                    port = self._wait_reboot(
                        log, step, boot_wait=4)
                    if not port:
                        log("Device did not come back after "
                            "Display flash. Unplug, reconnect, "
                            "click Retry.", "err")
                        step("Device not found — retry")
                        self.root.after(0, lambda: self._set_busy(
                            False))
                        self._show_retry(
                            "Main flash",
                            lambda: (
                                self._install_from_stock_thread(2)))
                        return

                    step("Waiting for boot…")
                    time.sleep(15)

            # ── Step 2: Flash Main (1200 baud reset) ─────────
            if resume_at <= 2:
                prog(40)
                port = self._rescan_port()
                if not port:
                    port = self.com_port
                if not port:
                    log("No device detected. Plug in and click "
                        "Retry.", "err")
                    step("No device — plug in and retry")
                    self.root.after(0, lambda: self._set_busy(
                        False))
                    self._show_retry(
                        "Main flash",
                        lambda: (
                            self._install_from_stock_thread(2)))
                    return

                drive = self._enter_main_bootloader(
                    port, log, step)
                if not drive:
                    log("Bootloader drive did not appear. "
                        "Unplug, plug back in, click Retry.",
                        "err")
                    step("Bootloader drive not found — retry")
                    self.root.after(0, lambda: self._set_busy(
                        False))
                    self._show_retry(
                        "Main flash",
                        lambda: (
                            self._install_from_stock_thread(2)))
                    return

                prog(55)
                self._flash_uf2(
                    drive, MAIN_UF2, "Main", log, step)

                prog(65)
                port = self._wait_reboot(
                    log, step, boot_wait=3)
                if not port:
                    log("Device did not come back. Unplug, "
                        "reconnect, click Retry.", "err")
                    step("Device not found after flash")
                    self.root.after(0, lambda: self._set_busy(
                        False))
                    self._show_retry(
                        "Main flash",
                        lambda: (
                            self._install_from_stock_thread(2)))
                    return

                step("Waiting for Main to fully boot…")
                log("Waiting 15s for full boot (FPGA, radios, "
                    "IPP)…")
                time.sleep(15)

            # ── Step 3: Upload WASM apps + verify ────────────
            self._finish_install(log, step, prog)

        except Exception as e:
            log(f"Unexpected error: {e}", "err")
            step(f"Error: {e}")
            self.root.after(0, lambda: self._set_status(
                "Flash failed", RED))

        finally:
            self.root.after(0, lambda: self._set_busy(False))

    # ── Update Akhlut ─────────────────────────────────────────
    # Main first (1200 baud), then Display ('B' command).
    # Only for devices already running Akhlut.

    def _update_akhlut_thread(self, resume_at=1):
        log = lambda m, t="info": self.root.after(
            0, lambda: self._log(m, t))
        step = lambda m: self.root.after(
            0, lambda: self._step(m))
        prog = lambda p: self.root.after(
            0, lambda: self._prog(p))

        try:
            # ── Step 1: Flash Main (1200 baud reset) ─────────
            if resume_at <= 1:
                prog(5)
                port = self._rescan_port()
                if not port:
                    port = self.com_port
                if not port:
                    log("No device detected. Plug in and click "
                        "Retry.", "err")
                    step("No device — plug in and retry")
                    self.root.after(0, lambda: self._set_busy(
                        False))
                    self._show_retry(
                        "Main flash",
                        lambda: self._update_akhlut_thread(1))
                    return

                drive = self._enter_main_bootloader(
                    port, log, step)
                if not drive:
                    log("Bootloader drive did not appear. "
                        "Unplug, plug back in, click Retry.",
                        "err")
                    step("Bootloader drive not found — retry")
                    self.root.after(0, lambda: self._set_busy(
                        False))
                    self._show_retry(
                        "Main flash",
                        lambda: self._update_akhlut_thread(1))
                    return

                prog(20)
                self._flash_uf2(
                    drive, MAIN_UF2, "Main", log, step)

                prog(30)
                port = self._wait_reboot(
                    log, step, boot_wait=3)
                if not port:
                    log("Device did not come back. Unplug, "
                        "reconnect, click Retry.", "err")
                    step("Device not found after flash")
                    self.root.after(0, lambda: self._set_busy(
                        False))
                    self._show_retry(
                        "Display flash",
                        lambda: self._update_akhlut_thread(2))
                    return

                step("Waiting for Main to fully boot…")
                log("Waiting 15s for full boot (FPGA, radios, "
                    "IPP)…")
                time.sleep(15)

            # ── Step 2: Flash Display via 'B' command ────────
            if resume_at <= 2:
                prog(40)
                if not os.path.exists(DISPLAY_UF2):
                    log("Display UF2 not found — skipping",
                        "warn")
                else:
                    port = self._rescan_port()
                    if not port:
                        log("Cannot find device for Display "
                            "flash. Unplug, reconnect, click "
                            "Retry.", "err")
                        step("Device not found — retry")
                        self.root.after(
                            0, lambda: self._set_busy(False))
                        self._show_retry(
                            "Display flash",
                            lambda: self._update_akhlut_thread(2))
                        return

                    drive = self._enter_display_bootloader(
                        port, log, step)
                    if not drive:
                        log("Display bootloader did not appear. "
                            "Main is installed. Click Retry.",
                            "err")
                        step("Display flash failed — "
                             "Main is OK")
                        self.root.after(
                            0, lambda: self._set_status(
                                "Main installed — Display "
                                "needs retry", YELLOW))
                        self.root.after(
                            0, lambda: self._set_busy(False))
                        self._show_retry(
                            "Display flash",
                            lambda: self._update_akhlut_thread(2))
                        return

                    prog(55)
                    self._flash_uf2(
                        drive, DISPLAY_UF2, "Display", log, step)

                    prog(65)
                    port = self._wait_reboot(
                        log, step, boot_wait=4)
                    if not port:
                        log("Device did not come back after "
                            "Display flash. Main is installed. "
                            "Unplug, reconnect, click Retry.",
                            "err")
                        step("Display flash failed — "
                             "Main is OK")
                        self.root.after(
                            0, lambda: self._set_busy(False))
                        self._show_retry(
                            "Display flash",
                            lambda: self._update_akhlut_thread(2))
                        return

                    step("Waiting for boot…")
                    time.sleep(15)

            # ── Step 3: Upload WASM apps + verify ────────────
            self._finish_install(log, step, prog)

        except Exception as e:
            log(f"Unexpected error: {e}", "err")
            step(f"Error: {e}")
            self.root.after(0, lambda: self._set_status(
                "Flash failed", RED))

        finally:
            self.root.after(0, lambda: self._set_busy(False))

    # ── Shared install finish (WASM + verify) ─────────────────

    def _finish_install(self, log, step, prog):
        prog(80)
        port = self._rescan_port()
        wasm_files = []
        if os.path.isdir(APPS_DIR):
            wasm_files = sorted(
                os.path.join(APPS_DIR, f)
                for f in os.listdir(APPS_DIR)
                if f.endswith(".wasm"))

        if wasm_files and port:
            step("Uploading WASM apps…")
            for wf in wasm_files:
                n = os.path.basename(wf)
                log(f"Uploading {n}…", "hl")
                ok, msg = self._upload_file(port, wf)
                log(f"  {msg}", "ok" if ok else "warn")
                time.sleep(0.5)
        elif not wasm_files:
            log("No .wasm apps in apps/ folder.", "info")

        prog(95)
        self._verify_alive(log, step)

        prog(100)
        msg = "Akhlut CFW installed — FreeWili OG ready"
        step(msg)
        log("")
        log(msg, "ok")
        self.root.after(0, lambda: self._set_status(msg, GREEN))

    # ── Restore Stock ─────────────────────────────────────────

    def _restore_thread(self, resume_at=1):
        log = lambda m, t="info": self.root.after(
            0, lambda: self._log(m, t))
        step = lambda m: self.root.after(
            0, lambda: self._step(m))
        prog = lambda p: self.root.after(
            0, lambda: self._prog(p))

        try:
            # ── Step 1: Flash Display via 'B' ────────────────
            if resume_at <= 1:
                prog(5)
                port = self._rescan_port()
                if not port:
                    port = self.com_port
                if not port:
                    log("No device detected. Plug in and click "
                        "Retry.", "err")
                    step("No device — plug in and retry")
                    self.root.after(0, lambda: self._set_busy(
                        False))
                    self._show_retry(
                        "Display flash",
                        lambda: self._restore_thread(1))
                    return

                drive = self._enter_display_bootloader(
                    port, log, step)
                if not drive:
                    log("Display bootloader drive did not "
                        "appear. Click Retry to try again.",
                        "err")
                    step("Display bootloader not found — "
                         "retry")
                    self.root.after(0, lambda: self._set_busy(
                        False))
                    self._show_retry(
                        "Display flash",
                        lambda: self._restore_thread(1))
                    return

                prog(15)
                self._flash_uf2(
                    drive, STOCK_DISPLAY_UF2, "Display",
                    log, step,
                    size_warning="Stock Display FW is 16 MB "
                    "— this may take a minute. "
                    "Do not unplug.")

                prog(30)
                port = self._wait_reboot(
                    log, step, boot_wait=4)
                if not port:
                    log("Device did not come back after Display "
                        "flash. Unplug, reconnect, click Retry.",
                        "err")
                    step("Device not found — retry")
                    self.root.after(0, lambda: self._set_busy(
                        False))
                    self._show_retry(
                        "Main flash",
                        lambda: self._restore_thread(2))
                    return

                step("Waiting for boot…")
                time.sleep(15)

            # ── Step 2: Flash Main (1200 baud reset) ─────────
            if resume_at <= 2:
                prog(45)
                port = self._rescan_port()
                if not port:
                    log("Cannot find device for Main flash. "
                        "Unplug, reconnect, click Retry.", "err")
                    step("Device not found — retry")
                    self.root.after(0, lambda: self._set_busy(
                        False))
                    self._show_retry(
                        "Main flash",
                        lambda: self._restore_thread(2))
                    return

                drive = self._enter_main_bootloader(
                    port, log, step)
                if not drive:
                    log("Bootloader drive did not appear. "
                        "Unplug, plug back in, click Retry.",
                        "err")
                    step("Bootloader drive not found — "
                         "retry")
                    self.root.after(0, lambda: self._set_busy(
                        False))
                    self._show_retry(
                        "Main flash",
                        lambda: self._restore_thread(2))
                    return

                prog(55)
                self._flash_uf2(
                    drive, STOCK_MAIN_UF2, "Main", log, step,
                    size_warning="Stock Main FW is 17 MB "
                    "— this will take 5–10 minutes. "
                    "Do not unplug the device.")

                prog(75)
                port = self._wait_reboot(
                    log, step, boot_wait=3)
                if not port:
                    log("Device did not come back after Main "
                        "flash. Unplug and reconnect.", "err")
                    step("Device not found — unplug and "
                         "reconnect")
                    self.root.after(0, lambda: self._set_busy(
                        False))
                    return

            prog(95)
            self._verify_alive(log, step)

            prog(100)
            msg = "Stock firmware restored — both processors"
            step(msg)
            log("")
            log(msg, "ok")
            self.root.after(
                0, lambda: self._set_status(msg, GREEN))

        except Exception as e:
            log(f"Unexpected error: {e}", "err")
            step(f"Error: {e}")
            self.root.after(0, lambda: self._set_status(
                "Flash failed", RED))

        finally:
            self.root.after(0, lambda: self._set_busy(False))

    # ── Button handlers ───────────────────────────────────────

    def _check_device(self):
        if self.working:
            return False
        if not self.com_port:
            self.detect_device()
        if not self.com_port:
            messagebox.showwarning(
                "No Device",
                "No FreeWili detected.\n\n"
                "Plug in your FreeWili OG with the battery "
                "connected and click Refresh.")
            return False
        return True

    def install_from_stock(self):
        if not self._check_device():
            return
        if not HAS_FREEWILI:
            messagebox.showerror(
                "Missing Package",
                "freewili package is required for stock "
                "install.\n\nRun:  pip install freewili")
            return
        if not os.path.exists(MAIN_UF2):
            messagebox.showerror(
                "Missing Firmware",
                f"Main firmware not found:\n{MAIN_UF2}\n\n"
                "Place firmware files in the firmware/ folder.")
            return

        self._clear_retry()
        self._set_busy(True)
        self._log_clear()
        self._prog(0)
        self._log("═══ Installing Akhlut CFW "
                  "(from stock) ═══", "hl")
        self._log("Display first (freewili package), "
                  "then Main (1200 baud)", "info")
        threading.Thread(
            target=self._install_from_stock_thread,
            daemon=True).start()

    def update_akhlut(self):
        if not self._check_device():
            return
        if not os.path.exists(MAIN_UF2):
            messagebox.showerror(
                "Missing Firmware",
                f"Main firmware not found:\n{MAIN_UF2}\n\n"
                "Place firmware files in the firmware/ folder.")
            return

        self._clear_retry()
        self._set_busy(True)
        self._log_clear()
        self._prog(0)
        self._log("═══ Updating Akhlut CFW "
                  "═══", "hl")
        self._log("Main first (1200 baud), "
                  "then Display ('B' command)", "info")
        threading.Thread(
            target=self._update_akhlut_thread,
            daemon=True).start()

    def restore(self):
        if self.working:
            return
        if not self.com_port:
            self.detect_device()
        if not self.com_port:
            messagebox.showwarning(
                "No Device",
                "No FreeWili detected.\n\n"
                "Plug in your FreeWili OG with the battery "
                "connected and click Refresh.")
            return
        missing = []
        if not os.path.exists(STOCK_MAIN_UF2):
            missing.append(f"Main: {STOCK_MAIN_UF2}")
        if not os.path.exists(STOCK_DISPLAY_UF2):
            missing.append(f"Display: {STOCK_DISPLAY_UF2}")
        if missing:
            messagebox.showerror(
                "Missing Stock Firmware",
                "Stock firmware not found:\n\n" +
                "\n".join(missing) +
                "\n\nPlace the stock UF2 files in "
                "firmware/stock/")
            return
        if not messagebox.askyesno(
                "Restore Stock Firmware",
                "This will replace Akhlut CFW with the original "
                "FreeWili stock firmware on both processors.\n\n"
                "Continue?"):
            return

        self._clear_retry()
        self._set_busy(True)
        self._log_clear()
        self._prog(0)
        self._log("═══ Restoring Stock Firmware "
                  "═══", "hl")
        self._log("Stock firmware is 17 MB — this will take "
                  "5–10 minutes. Do not unplug the device.",
                  "warn")
        threading.Thread(
            target=self._restore_thread,
            daemon=True).start()

    def run(self):
        self.root.mainloop()


if __name__ == "__main__":
    InstallerApp().run()
