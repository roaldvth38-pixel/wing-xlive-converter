#!/usr/bin/env python3
"""Desktop UI for the WING X-LIVE multitrack converter."""

from __future__ import annotations

import json
import logging
import math
import os
import queue
import sys
import threading
import traceback
import contextlib
import ctypes
from dataclasses import dataclass
from pathlib import Path
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from PIL import Image, ImageTk

from wing_xlive_multitrack_converter import (
    ConversionCancelled,
    ConversionControl,
    ConversionSummary,
    OutputSettings,
    convert_xlive_folder,
)

APP_NAME = "WING X-LIVE Converter"
SETTINGS_PATH = Path(os.getenv("APPDATA", str(Path.home()))) / "WingXLIVEConverter" / "settings.json"
TRACK_NAME_PROFILES_PATH = SETTINGS_PATH.parent / "track_name_profiles.json"
BANNER_IMAGE_PATH = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent)) / "assets" / "wing_armrest.jpg"
AUTO_BLOCK_FRAMES = 524288
SUPPORTED_OUTPUT_TYPES = ("wav", "flac", "mp3")
TRACK_NAME_SLOT_COUNT = 32
MP3_MODE_LABEL_TO_VALUE = {
    "Constant bitrate (CBR)": "cbr",
    "Variable bitrate (VBR)": "vbr",
}
MP3_MODE_VALUE_TO_LABEL = {value: label for label, value in MP3_MODE_LABEL_TO_VALUE.items()}
MP3_BITRATE_CHOICES = (96, 128, 160, 192, 224, 256, 320)
WAV_BIT_DEPTH_CHOICES = (8, 16, 24, 32)
FLAC_ENCODING_DEPTH_CHOICES = (
    "24",
    "23/24",
    "22/24",
    "21/24",
    "20/24",
    "19/24",
    "18/24",
    "17/24",
    "16",
)
FLAC_COMPRESSION_CHOICES = (
    "0-fastest",
    "1",
    "2",
    "3",
    "4",
    "5-default",
    "6",
    "7",
    "8-slowest",
)

WING_BG = "#23262B"
WING_BG_ALT = "#1D2024"
WING_PANEL = "#66696F"
WING_PANEL_ELEVATED = "#72757B"
WING_CARD = "#5E6168"
WING_CARD_SOFT = "#7A7D84"
WING_BORDER = "#565A60"
WING_BORDER_SOFT = "#878B92"
WING_TEXT = "#E8EEF5"
WING_MUTED = "#9DAAB9"
WING_ACCENT = "#F3C537"
WING_ACCENT_ACTIVE = "#FFD966"
WING_SUCCESS = "#46D29A"
WING_DANGER = "#E35B6A"
WING_WARNING = "#F7B955"
WING_INFO = "#8EB8FF"
WING_BUTTON_TEXT_ON_ACCENT = "#17120A"
WING_LOG_DEBUG = "#7A8898"
WING_GLOW = "#2B4B75"
WING_GLOW_SOFT = "#1A2B43"
WINDOW_START_WIDTH = 1850
WINDOW_START_HEIGHT = 1310
WINDOW_MIN_SIZE = (1240, 760)
UI_SCALE_OPTIONS = ("Auto", "100%", "75%", "50%")
UI_SCALE_TO_MULTIPLIER = {
    "100%": 1.0,
    "75%": 0.75,
    "50%": 0.5,
}


@dataclass
class RunConfig:
    input_root: Path
    output_root: Path
    output_type: str
    wav_keep_original_bit_depth: bool
    wav_bit_depth: int
    flac_encoding_depth: str
    flac_compression_level: int
    mp3_mode: str
    mp3_bitrate_kbps: int
    track_name_overrides: dict[int, str]


class QueueLoggingHandler(logging.Handler):
    def __init__(self, out_queue: "queue.Queue[tuple[str, object]]") -> None:
        super().__init__()
        self._out_queue = out_queue

    def emit(self, record: logging.LogRecord) -> None:
        try:
            self._out_queue.put(("log", self.format(record)))
        except Exception:
            pass


def _enable_windows_dpi_awareness() -> None:
    if sys.platform != "win32":
        return

    with contextlib.suppress(Exception):
        ctypes.windll.shcore.SetProcessDpiAwareness(2)
        return

    with contextlib.suppress(Exception):
        ctypes.windll.user32.SetProcessDPIAware()


def _get_startup_geometry(
    root: tk.Tk,
    target_width: int = WINDOW_START_WIDTH,
    target_height: int = WINDOW_START_HEIGHT,
    min_width: int = WINDOW_MIN_SIZE[0],
    min_height: int = WINDOW_MIN_SIZE[1],
) -> str:
    if sys.platform == "win32":
        with contextlib.suppress(Exception):
            class RECT(ctypes.Structure):
                _fields_ = [
                    ("left", ctypes.c_long),
                    ("top", ctypes.c_long),
                    ("right", ctypes.c_long),
                    ("bottom", ctypes.c_long),
                ]

            rect = RECT()
            ctypes.windll.user32.SystemParametersInfoW(0x0030, 0, ctypes.byref(rect), 0)
            work_width = rect.right - rect.left
            work_height = rect.bottom - rect.top
            width = min(work_width - 80, max(min_width, target_width))
            height = min(work_height - 80, max(min_height, target_height))
            x = rect.left + max(20, (work_width - width) // 2)
            y = rect.top + max(20, (work_height - height) // 2)
            return f"{width}x{height}+{x}+{y}"

    screen_width = root.winfo_screenwidth()
    screen_height = root.winfo_screenheight()
    width = min(screen_width - 80, max(min_width, target_width))
    height = min(screen_height - 80, max(min_height, target_height))
    x = max(20, (screen_width - width) // 2)
    y = max(20, (screen_height - height) // 2)
    return f"{width}x{height}+{x}+{y}"


def _window_targets_for_scale(multiplier: float) -> tuple[int, int, int, int]:
    start_width = max(900, int(WINDOW_START_WIDTH * multiplier))
    start_height = max(620, int(WINDOW_START_HEIGHT * multiplier))
    min_width = max(900, int(WINDOW_MIN_SIZE[0] * multiplier))
    min_height = max(560, int(WINDOW_MIN_SIZE[1] * multiplier))
    return start_width, start_height, min_width, min_height


class WingXLiveConverterApp:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title(APP_NAME)
        self._base_tk_scaling = float(self.root.tk.call("tk", "scaling"))
        self.ui_scale_var = tk.StringVar(value="Auto")
        self._active_scale_mode = "Auto"
        self._current_scale_multiplier = 1.0
        self._ui_ready = False
        self._apply_ui_scale(persist=False)
        self.root.option_add("*Font", f"{{Segoe UI}} {self._scaled_points(10)}")
        self.root.configure(bg=WING_BG)

        self.input_var = tk.StringVar()
        self.output_var = tk.StringVar()
        self.output_type_var = tk.StringVar(value="wav")
        self.wav_bit_depth_var = tk.StringVar(value="24")
        self.wav_keep_original_bit_depth_var = tk.BooleanVar(value=True)
        self.flac_encoding_depth_var = tk.StringVar(value="24")
        self.flac_compression_var = tk.StringVar(value="5-default")
        self.mp3_mode_var = tk.StringVar(value=MP3_MODE_VALUE_TO_LABEL["cbr"])
        self.mp3_bitrate_var = tk.StringVar(value="320")
        self.status_var = tk.StringVar(value="Ready")
        self.track_name_overrides: dict[int, str] = {}
        self.track_name_profile_name = ""

        self._messages: "queue.Queue[tuple[str, object]]" = queue.Queue()
        self._worker_thread: threading.Thread | None = None
        self._is_running = False
        self._is_paused = False
        self._stop_event: threading.Event | None = None
        self._pause_event: threading.Event | None = None
        self._bottom_banner_source: Image.Image | None = None
        self._bottom_banner_photo: ImageTk.PhotoImage | None = None
        self._bottom_banner_width = 0

        self._configure_theme()
        self._build_ui()
        self._ui_ready = True
        self._set_status("Ready", tone="neutral")
        self._load_settings()

        self.root.protocol("WM_DELETE_WINDOW", self._on_close)
        self.root.after(100, self._process_messages)

    def _scaled_points(self, points: int) -> int:
        return max(7, int(round(points * self._current_scale_multiplier)))

    def _ui_font(self, points: int, weight: str | None = None) -> tuple[str, int] | tuple[str, int, str]:
        size = self._scaled_points(points)
        if weight:
            return ("Segoe UI", size, weight)
        return ("Segoe UI", size)

    def _mono_font(self, points: int, weight: str | None = None) -> tuple[str, int] | tuple[str, int, str]:
        size = self._scaled_points(points)
        if weight:
            return ("Consolas", size, weight)
        return ("Consolas", size)

    def _rebuild_ui_for_scale(self) -> None:
        status_text = self.status_var.get()
        log_dump = ""
        if hasattr(self, "log_text"):
            with contextlib.suppress(Exception):
                log_dump = self.log_text.get("1.0", tk.END)

        for child in list(self.root.winfo_children()):
            child.destroy()

        self._configure_theme()
        self._build_ui()
        self._set_status(status_text, tone="neutral")

        if log_dump.strip() and hasattr(self, "log_text"):
            with contextlib.suppress(Exception):
                self.log_text.insert("1.0", log_dump)

    def _auto_scale_multiplier(self) -> float:
        screen_width = self.root.winfo_screenwidth()
        screen_height = self.root.winfo_screenheight()

        if screen_width <= 1280 or screen_height <= 720:
            return 0.5
        if screen_width <= 1600 or screen_height <= 900:
            return 0.75
        return 1.0

    def _scale_mode_to_multiplier(self, mode: str) -> float:
        if mode == "Auto":
            return self._auto_scale_multiplier()
        return UI_SCALE_TO_MULTIPLIER.get(mode, 1.0)

    def _apply_ui_scale(self, persist: bool = True) -> None:
        mode = self.ui_scale_var.get().strip() or "Auto"
        if mode not in UI_SCALE_OPTIONS:
            mode = "Auto"
            self.ui_scale_var.set(mode)

        multiplier = self._scale_mode_to_multiplier(mode)
        self._active_scale_mode = mode
        self._current_scale_multiplier = multiplier
        self.root.tk.call("tk", "scaling", self._base_tk_scaling * multiplier)
        self.root.option_add("*Font", f"{{Segoe UI}} {self._scaled_points(10)}")

        start_width, start_height, min_width, min_height = _window_targets_for_scale(multiplier)
        self.root.minsize(min_width, min_height)

        current_state = str(self.root.state())
        if current_state != "zoomed":
            self.root.geometry(_get_startup_geometry(self.root, start_width, start_height, min_width, min_height))

        if hasattr(self, "bottom_banner"):
            self._bottom_banner_width = 0
            self.root.after_idle(self._refresh_bottom_banner)

        if self._ui_ready:
            self._rebuild_ui_for_scale()

        if persist:
            self._save_settings()

    def _on_scale_changed(self, _event: tk.Event | None = None) -> None:
        if self._is_running:
            self.ui_scale_var.set(self._active_scale_mode)
            messagebox.showinfo(APP_NAME, "Scale cannot be changed during conversion.")
            return
        self._apply_ui_scale(persist=True)

    def _configure_theme(self) -> None:
        style = ttk.Style(self.root)
        with contextlib.suppress(tk.TclError):
            style.theme_use("clam")

        style.configure("TFrame", background=WING_PANEL)

        style.configure(
            "TLabelframe",
            background=WING_PANEL,
            bordercolor=WING_BORDER,
            borderwidth=1,
            relief="solid",
        )
        style.configure(
            "TLabelframe.Label",
            background=WING_PANEL,
            foreground=WING_ACCENT,
            font=self._ui_font(10, "bold"),
        )

        style.configure(
            "Wing.Step.TLabelframe",
            background=WING_PANEL,
            bordercolor=WING_BORDER,
            borderwidth=1,
            relief="solid",
        )
        style.configure(
            "Wing.Step.TLabelframe.Label",
            background=WING_PANEL,
            foreground=WING_ACCENT,
            font=self._ui_font(10, "bold"),
        )

        style.configure("TLabel", background=WING_PANEL, foreground=WING_TEXT)
        style.configure("Wing.Title.TLabel", background=WING_PANEL, foreground=WING_ACCENT, font=self._ui_font(18, "bold"))
        style.configure("Wing.Subtitle.TLabel", background=WING_PANEL, foreground=WING_MUTED)
        style.configure("Wing.Kicker.TLabel", background=WING_PANEL, foreground=WING_INFO, font=self._ui_font(9, "bold"))
        style.configure("Wing.SectionCaption.TLabel", background=WING_PANEL, foreground=WING_MUTED, font=self._ui_font(9))
        style.configure("Wing.Muted.TLabel", background=WING_PANEL, foreground=WING_MUTED)
        style.configure("Wing.Page.SectionCaption.TLabel", background=WING_BG, foreground=WING_MUTED, font=self._ui_font(9))
        style.configure("Wing.Page.StatusNeutral.TLabel", background=WING_BG, foreground=WING_MUTED, font=self._ui_font(10, "bold"))
        style.configure("Wing.Page.StatusInfo.TLabel", background=WING_BG, foreground=WING_INFO, font=self._ui_font(10, "bold"))
        style.configure("Wing.Page.StatusSuccess.TLabel", background=WING_BG, foreground=WING_SUCCESS, font=self._ui_font(10, "bold"))
        style.configure("Wing.Page.StatusWarning.TLabel", background=WING_BG, foreground=WING_WARNING, font=self._ui_font(10, "bold"))
        style.configure("Wing.Page.StatusError.TLabel", background=WING_BG, foreground=WING_DANGER, font=self._ui_font(10, "bold"))

        style.configure(
            "TButton",
            background=WING_PANEL_ELEVATED,
            foreground=WING_TEXT,
            bordercolor=WING_BORDER,
            focuscolor=WING_PANEL_ELEVATED,
            focusthickness=0,
            padding=(10, 5),
        )
        style.map(
            "TButton",
            background=[("pressed", "#273241"), ("active", "#243041"), ("disabled", WING_PANEL)],
            foreground=[("disabled", "#6E7B89")],
        )

        style.configure(
            "Wing.Primary.TButton",
            background=WING_ACCENT,
            foreground=WING_BUTTON_TEXT_ON_ACCENT,
            bordercolor="#B58E16",
            font=self._ui_font(10, "bold"),
        )
        style.map(
            "Wing.Primary.TButton",
            background=[("pressed", "#DEB72D"), ("active", WING_ACCENT_ACTIVE), ("disabled", "#6E6239")],
            foreground=[("disabled", "#30260E")],
        )

        style.configure(
            "Wing.Secondary.TButton",
            background=WING_CARD,
            foreground=WING_TEXT,
            bordercolor=WING_BORDER_SOFT,
            font=self._ui_font(10, "bold"),
        )
        style.map(
            "Wing.Secondary.TButton",
            background=[("pressed", "#26354D"), ("active", "#2A3A54"), ("disabled", WING_PANEL)],
            foreground=[("disabled", "#6E7B89")],
        )

        style.configure("Wing.Danger.TButton", background=WING_DANGER, foreground=WING_TEXT, bordercolor="#9C3642")
        style.configure("Wing.Danger.TButton", font=self._ui_font(10, "bold"))
        style.map(
            "Wing.Danger.TButton",
            background=[("pressed", "#C44A57"), ("active", "#F06C7A"), ("disabled", "#5E3A41")],
            foreground=[("disabled", "#B78A91")],
        )

        style.configure(
            "TEntry",
            fieldbackground=WING_PANEL_ELEVATED,
            foreground=WING_TEXT,
            bordercolor=WING_BORDER,
            lightcolor=WING_BORDER,
            darkcolor=WING_BORDER,
            insertcolor=WING_TEXT,
            padding=4,
        )
        style.map("TEntry", fieldbackground=[("disabled", WING_PANEL)])

        style.configure(
            "TCombobox",
            fieldbackground=WING_PANEL_ELEVATED,
            background=WING_PANEL_ELEVATED,
            foreground=WING_TEXT,
            bordercolor=WING_BORDER,
            arrowcolor=WING_ACCENT,
            lightcolor=WING_BORDER,
            darkcolor=WING_BORDER,
            padding=3,
        )
        style.map(
            "TCombobox",
            fieldbackground=[("readonly", WING_PANEL_ELEVATED), ("disabled", WING_PANEL)],
            foreground=[("disabled", "#6E7B89")],
            arrowcolor=[("disabled", "#6E7B89"), ("!disabled", WING_ACCENT)],
        )

        style.configure("TCheckbutton", background=WING_PANEL, foreground=WING_TEXT)
        style.map("TCheckbutton", foreground=[("disabled", "#6E7B89")])

        style.configure(
            "Wing.Horizontal.TProgressbar",
            troughcolor=WING_PANEL_ELEVATED,
            background=WING_ACCENT,
            bordercolor=WING_BORDER,
            lightcolor=WING_ACCENT,
            darkcolor=WING_ACCENT,
        )

        style.configure(
            "Vertical.TScrollbar",
            background=WING_PANEL_ELEVATED,
            troughcolor=WING_PANEL,
            bordercolor=WING_BORDER,
            arrowcolor=WING_ACCENT,
            lightcolor=WING_BORDER,
            darkcolor=WING_BORDER,
        )

        self.root.option_add("*TCombobox*Listbox*Background", WING_PANEL_ELEVATED)
        self.root.option_add("*TCombobox*Listbox*Foreground", WING_TEXT)
        self.root.option_add("*TCombobox*Listbox*selectBackground", "#2E3C4F")
        self.root.option_add("*TCombobox*Listbox*selectForeground", WING_TEXT)

    def _set_status(self, message: str, tone: str = "neutral") -> None:
        tone_to_style = {
            "neutral": "Wing.Page.StatusNeutral.TLabel",
            "info": "Wing.Page.StatusInfo.TLabel",
            "success": "Wing.Page.StatusSuccess.TLabel",
            "warning": "Wing.Page.StatusWarning.TLabel",
            "error": "Wing.Page.StatusError.TLabel",
        }
        tone_to_color = {
            "neutral": WING_MUTED,
            "info": WING_INFO,
            "success": WING_SUCCESS,
            "warning": WING_WARNING,
            "error": WING_DANGER,
        }
        self.status_var.set(message)
        if hasattr(self, "status_label"):
            self.status_label.configure(style=tone_to_style.get(tone, "Wing.Page.StatusNeutral.TLabel"))
        if hasattr(self, "status_dot_canvas") and hasattr(self, "_status_dot_item"):
            color = tone_to_color.get(tone, WING_MUTED)
            self.status_dot_canvas.itemconfigure(self._status_dot_item, fill=color, outline=color)

    def _create_ambient_strip(self, parent: tk.Misc) -> tk.Canvas:
        strip = tk.Canvas(
            parent,
            height=28,
            bg=WING_BG,
            bd=0,
            highlightthickness=0,
        )
        strip.create_oval(-40, -30, 80, 50, fill=WING_GLOW_SOFT, outline=WING_GLOW_SOFT)
        strip.create_oval(180, -35, 320, 45, fill="#17293E", outline="#17293E")
        strip.create_oval(520, -28, 680, 48, fill="#16273A", outline="#16273A")
        strip.create_oval(930, -30, 1080, 50, fill=WING_GLOW_SOFT, outline=WING_GLOW_SOFT)
        return strip

    def _load_bottom_banner_source(self) -> None:
        if self._bottom_banner_source is not None or not BANNER_IMAGE_PATH.exists():
            return

        with Image.open(BANNER_IMAGE_PATH) as source_image:
            self._bottom_banner_source = source_image.convert("RGB")

    def _refresh_bottom_banner(self, _event: tk.Event | None = None) -> None:
        if not hasattr(self, "bottom_banner"):
            return

        self._load_bottom_banner_source()
        if self._bottom_banner_source is None:
            return

        width = max(1, self.bottom_banner.winfo_width())
        if width <= 1 or width == self._bottom_banner_width:
            return

        source = self._bottom_banner_source
        target_height = max(24, int(width * (source.height / source.width)))
        resized = source.resize((width, target_height), Image.Resampling.LANCZOS)

        photo = ImageTk.PhotoImage(resized)
        self._bottom_banner_photo = photo
        self._bottom_banner_width = width
        self.bottom_banner.configure(image=photo)

    def _paint_knob(self, canvas: tk.Canvas, ratio: float, color: str) -> None:
        ratio = max(0.0, min(1.0, ratio))
        canvas.delete("all")

        cx, cy = 38, 38
        radius = 27
        start = 220
        sweep = 280

        canvas.create_oval(
            cx - radius,
            cy - radius,
            cx + radius,
            cy + radius,
            fill=WING_CARD_SOFT,
            outline=WING_BORDER,
            width=2,
        )
        canvas.create_arc(
            cx - radius,
            cy - radius,
            cx + radius,
            cy + radius,
            start=start,
            extent=sweep,
            style="arc",
            outline="#2B3A4E",
            width=5,
        )
        canvas.create_arc(
            cx - radius,
            cy - radius,
            cx + radius,
            cy + radius,
            start=start,
            extent=max(8, int(sweep * ratio)),
            style="arc",
            outline=color,
            width=5,
        )

        pointer_angle = math.radians(start + (sweep * ratio))
        pointer_length = 19
        pointer_x = cx + (pointer_length * math.cos(pointer_angle))
        pointer_y = cy - (pointer_length * math.sin(pointer_angle))
        canvas.create_line(cx, cy, pointer_x, pointer_y, fill=color, width=3, capstyle=tk.ROUND)
        canvas.create_oval(cx - 4, cy - 4, cx + 4, cy + 4, fill=WING_BG_ALT, outline=color, width=1)

    def _create_knob_widget(self, parent: tk.Misc, title: str) -> dict[str, object]:
        card = tk.Frame(
            parent,
            bg=WING_PANEL,
            highlightthickness=1,
            highlightbackground=WING_BORDER,
            padx=8,
            pady=6,
        )

        title_label = tk.Label(
            card,
            text=title,
            bg=WING_PANEL,
            fg=WING_MUTED,
            font=self._ui_font(8, "bold"),
        )
        title_label.pack(anchor=tk.CENTER)

        knob_canvas = tk.Canvas(
            card,
            width=76,
            height=76,
            bg=WING_PANEL,
            bd=0,
            highlightthickness=0,
        )
        knob_canvas.pack(pady=(2, 2))

        value_label = tk.Label(
            card,
            text="",
            bg=WING_PANEL,
            fg=WING_TEXT,
            font=self._ui_font(9, "bold"),
            width=9,
        )
        value_label.pack(anchor=tk.CENTER)

        return {
            "card": card,
            "title": title_label,
            "canvas": knob_canvas,
            "value": value_label,
        }

    def _set_knob_value(self, knob_key: str, title: str, value: str, ratio: float, color: str) -> None:
        knob = self._knob_widgets.get(knob_key)
        if not isinstance(knob, dict):
            return

        title_label = knob.get("title")
        value_label = knob.get("value")
        knob_canvas = knob.get("canvas")
        if isinstance(title_label, tk.Label):
            title_label.configure(text=title)
        if isinstance(value_label, tk.Label):
            value_label.configure(text=value)
        if isinstance(knob_canvas, tk.Canvas):
            self._paint_knob(knob_canvas, ratio=ratio, color=color)

    def _refresh_decorative_knobs(self) -> None:
        if not hasattr(self, "_knob_widgets"):
            return

        output_type = self.output_type_var.get().strip().lower()

        if output_type == "wav":
            depth_value = "ORIG" if self.wav_keep_original_bit_depth_var.get() else f"{self.wav_bit_depth_var.get()}b"
            depth_ratio = 0.75 if self.wav_keep_original_bit_depth_var.get() else (int(self.wav_bit_depth_var.get()) / 32.0)
            self._set_knob_value("left", "Depth", depth_value, depth_ratio, WING_INFO)
            self._set_knob_value("mid", "Format", "WAV", 0.18, WING_SUCCESS)
            self._set_knob_value("right", "Flow", "Clean", 0.42, WING_ACCENT)
            return

        if output_type == "flac":
            encoding_depth = self.flac_encoding_depth_var.get().split("/", 1)[0]
            try:
                effective_depth = int(encoding_depth)
            except ValueError:
                effective_depth = 24

            comp_level_text = self.flac_compression_var.get().split("-", 1)[0]
            try:
                comp_level = int(comp_level_text)
            except ValueError:
                comp_level = 5

            self._set_knob_value("left", "Depth", f"{effective_depth}b", effective_depth / 24.0, WING_INFO)
            self._set_knob_value("mid", "Comp", str(comp_level), comp_level / 8.0, WING_ACCENT)
            self._set_knob_value("right", "Format", "FLAC", 0.55, WING_SUCCESS)
            return

        try:
            bitrate = int(self.mp3_bitrate_var.get())
        except ValueError:
            bitrate = 320

        bitrate_ratio = (max(96, min(320, bitrate)) - 96) / (320 - 96)
        mode = "CBR" if MP3_MODE_LABEL_TO_VALUE.get(self.mp3_mode_var.get(), "cbr") == "cbr" else "VBR"
        mode_ratio = 0.35 if mode == "CBR" else 0.65

        self._set_knob_value("left", "Rate", f"{bitrate}k", bitrate_ratio, WING_ACCENT)
        self._set_knob_value("mid", "Mode", mode, mode_ratio, WING_INFO)
        self._set_knob_value("right", "Format", "MP3", 0.86, WING_SUCCESS)

    def _build_ui(self) -> None:
        main = tk.Frame(self.root, bg=WING_BG, padx=10, pady=9)
        main.pack(fill=tk.BOTH, expand=True)

        hero = tk.Frame(
            main,
            bg=WING_CARD,
            bd=0,
            highlightthickness=1,
            highlightbackground=WING_BORDER_SOFT,
            padx=10,
            pady=6,
        )
        hero.pack(fill=tk.X)

        hero_left = tk.Frame(hero, bg=WING_CARD)
        hero_left.pack(side=tk.LEFT, fill=tk.X, expand=True)

        hero_right = tk.Frame(hero, bg=WING_CARD)
        hero_right.pack(side=tk.RIGHT, anchor=tk.NE, padx=(10, 0))

        tk.Label(
            hero_right,
            text="Scale",
            bg=WING_CARD,
            fg=WING_MUTED,
            font=self._ui_font(8, "bold"),
        ).pack(anchor=tk.E)

        self.scale_combo = ttk.Combobox(
            hero_right,
            textvariable=self.ui_scale_var,
            values=UI_SCALE_OPTIONS,
            state="readonly",
            width=8,
        )
        self.scale_combo.pack(anchor=tk.E, pady=(2, 0))
        self.scale_combo.bind("<<ComboboxSelected>>", self._on_scale_changed)

        tk.Label(
            hero_left,
            text="WING SESSION TOOLKIT",
            bg=WING_CARD,
            fg=WING_INFO,
            font=self._ui_font(9, "bold"),
        ).pack(anchor=tk.W)

        tk.Label(
            hero_left,
            text=APP_NAME,
            bg=WING_CARD,
            fg=WING_ACCENT,
            font=self._ui_font(15, "bold"),
        ).pack(anchor=tk.W, pady=(1, 0))

        tk.Label(
            hero_left,
            text="Fast conversion with profile-based naming and clean delivery-ready outputs.",
            bg=WING_CARD,
            fg=WING_MUTED,
            font=self._ui_font(9),
        ).pack(anchor=tk.W, pady=(2, 0))

        pill_row = tk.Frame(hero_left, bg=WING_CARD)
        pill_row.pack(anchor=tk.W, pady=(5, 0))
        for badge_text in (
            "64 Track Support",
            "WAV FLAC MP3",
            "Naming Profiles",
        ):
            tk.Label(
                pill_row,
                text=badge_text,
                bg=WING_CARD_SOFT,
                fg=WING_TEXT,
                font=self._ui_font(8, "bold"),
                padx=7,
                pady=2,
            ).pack(side=tk.LEFT, padx=(0, 8))

        workflow_hint = ttk.Label(
            main,
            text="Workflow: 1) Choose folders   2) Pick format   3) Optional names profile   4) Convert and deliver",
            style="Wing.Page.SectionCaption.TLabel",
        )
        workflow_hint.pack(anchor=tk.W, pady=(4, 5))

        paths = ttk.LabelFrame(main, text="Step 1 - Choose Folders", padding=8, style="Wing.Step.TLabelframe")
        paths.pack(fill=tk.X)
        paths.columnconfigure(1, weight=1)

        ttk.Label(paths, text="Input Folder").grid(row=0, column=0, sticky="w", padx=(0, 8), pady=5)
        self.input_entry = ttk.Entry(paths, textvariable=self.input_var)
        self.input_entry.grid(row=0, column=1, sticky="ew", pady=5)
        self.input_browse_button = ttk.Button(paths, text="Browse...", command=self._choose_input_folder, style="Wing.Secondary.TButton")
        self.input_browse_button.grid(row=0, column=2, padx=(8, 0), pady=5)

        ttk.Label(paths, text="Output Folder").grid(row=1, column=0, sticky="w", padx=(0, 8), pady=5)
        self.output_entry = ttk.Entry(paths, textvariable=self.output_var)
        self.output_entry.grid(row=1, column=1, sticky="ew", pady=5)
        self.output_browse_button = ttk.Button(paths, text="Browse...", command=self._choose_output_folder, style="Wing.Secondary.TButton")
        self.output_browse_button.grid(row=1, column=2, padx=(8, 0), pady=5)

        output_type = ttk.LabelFrame(main, text="Step 2 - Output Format", padding=8, style="Wing.Step.TLabelframe")
        output_type.pack(fill=tk.X, pady=(8, 0))
        output_type.columnconfigure(1, weight=1)
        output_type.columnconfigure(3, weight=1)

        ttk.Label(output_type, text="Format").grid(row=0, column=0, sticky="w", pady=(0, 5))
        self.output_type_combo = ttk.Combobox(
            output_type,
            textvariable=self.output_type_var,
            values=SUPPORTED_OUTPUT_TYPES,
            state="readonly",
            width=11,
        )
        self.output_type_combo.grid(row=0, column=1, sticky="w", padx=(8, 0), pady=(0, 5))
        self.output_type_combo.bind("<<ComboboxSelected>>", self._on_output_type_changed)

        self.wav_options_frame = ttk.Frame(output_type)
        self.wav_options_frame.grid(row=1, column=0, columnspan=4, sticky="ew", pady=(3, 5))
        self.wav_options_frame.columnconfigure(1, weight=1)

        ttk.Label(self.wav_options_frame, text="WAV Bit Depth").grid(row=0, column=0, sticky="w", pady=(0, 3))
        self.wav_bit_depth_combo = ttk.Combobox(
            self.wav_options_frame,
            textvariable=self.wav_bit_depth_var,
            values=tuple(str(value) for value in WAV_BIT_DEPTH_CHOICES),
            state="readonly",
            width=11,
        )
        self.wav_bit_depth_combo.grid(row=0, column=1, sticky="w", padx=(8, 0), pady=(0, 3))
        self.wav_bit_depth_combo.bind("<<ComboboxSelected>>", lambda _event: self._refresh_decorative_knobs())

        self.wav_keep_original_frame = tk.Frame(
            self.wav_options_frame,
            bg=WING_PANEL_ELEVATED,
            highlightthickness=1,
            highlightbackground=WING_BORDER,
            bd=0,
            padx=6,
            pady=3,
        )
        self.wav_keep_original_frame.grid(row=1, column=0, columnspan=2, sticky="w", pady=(2, 0))

        self.wav_keep_original_check = ttk.Checkbutton(
            self.wav_keep_original_frame,
            text="Keep original bit depth",
            variable=self.wav_keep_original_bit_depth_var,
            command=self._on_wav_keep_original_changed,
        )
        self.wav_keep_original_check.pack(anchor=tk.W)

        self.flac_options_frame = ttk.Frame(output_type)
        self.flac_options_frame.grid(row=1, column=0, columnspan=4, sticky="ew", pady=(3, 5))
        self.flac_options_frame.columnconfigure(1, weight=1)
        self.flac_options_frame.columnconfigure(3, weight=1)

        ttk.Label(self.flac_options_frame, text="Encoding depth").grid(row=0, column=0, sticky="w", pady=(0, 3))
        self.flac_encoding_depth_combo = ttk.Combobox(
            self.flac_options_frame,
            textvariable=self.flac_encoding_depth_var,
            values=FLAC_ENCODING_DEPTH_CHOICES,
            state="readonly",
            width=11,
        )
        self.flac_encoding_depth_combo.grid(row=0, column=1, sticky="w", padx=(8, 14), pady=(0, 3))
        self.flac_encoding_depth_combo.bind("<<ComboboxSelected>>", lambda _event: self._refresh_decorative_knobs())

        ttk.Label(self.flac_options_frame, text="Data compression").grid(row=0, column=2, sticky="w", pady=(0, 3))
        self.flac_compression_combo = ttk.Combobox(
            self.flac_options_frame,
            textvariable=self.flac_compression_var,
            values=FLAC_COMPRESSION_CHOICES,
            state="readonly",
            width=13,
        )
        self.flac_compression_combo.grid(row=0, column=3, sticky="w", padx=(8, 0), pady=(0, 3))
        self.flac_compression_combo.bind("<<ComboboxSelected>>", lambda _event: self._refresh_decorative_knobs())

        self.mp3_options_frame = ttk.Frame(output_type)
        self.mp3_options_frame.grid(row=1, column=0, columnspan=4, sticky="ew", pady=(3, 5))
        self.mp3_options_frame.columnconfigure(1, weight=1)
        self.mp3_options_frame.columnconfigure(3, weight=1)

        ttk.Label(self.mp3_options_frame, text="Mode").grid(row=0, column=0, sticky="w", pady=(0, 3))
        self.mp3_mode_combo = ttk.Combobox(
            self.mp3_options_frame,
            textvariable=self.mp3_mode_var,
            values=tuple(MP3_MODE_LABEL_TO_VALUE.keys()),
            state="readonly",
            width=25,
        )
        self.mp3_mode_combo.grid(row=0, column=1, sticky="w", padx=(8, 14), pady=(0, 3))
        self.mp3_mode_combo.bind("<<ComboboxSelected>>", lambda _event: self._refresh_decorative_knobs())

        ttk.Label(self.mp3_options_frame, text="Bitrate").grid(row=0, column=2, sticky="w", pady=(0, 3))
        self.mp3_bitrate_combo = ttk.Combobox(
            self.mp3_options_frame,
            textvariable=self.mp3_bitrate_var,
            values=tuple(str(value) for value in MP3_BITRATE_CHOICES),
            state="readonly",
            width=9,
        )
        self.mp3_bitrate_combo.grid(row=0, column=3, sticky="w", padx=(8, 0), pady=(0, 3))
        self.mp3_bitrate_combo.bind("<<ComboboxSelected>>", lambda _event: self._refresh_decorative_knobs())

        ttk.Label(
            output_type,
            text=(
                "Direct export writes tracks immediately to the selected format. "
                "FLAC/MP3 prefer ffmpeg when available, with in-process fallback."
            ),
            style="Wing.Muted.TLabel",
        ).grid(row=2, column=0, columnspan=4, sticky="w")

        self._update_output_controls()
        self._refresh_decorative_knobs()

        actions = ttk.LabelFrame(main, text="Step 3 - Convert", padding=8, style="Wing.Step.TLabelframe")
        actions.pack(fill=tk.X, pady=(8, 0))

        button_row = ttk.Frame(actions)
        button_row.pack(anchor=tk.W)

        self.run_button = ttk.Button(
            button_row,
            text="Start Conversion",
            command=self._start_conversion,
            style="Wing.Primary.TButton",
            width=16,
        )
        self.run_button.grid(row=0, column=0, padx=4)

        self.pause_button = ttk.Button(
            button_row,
            text="Pause Conversion",
            command=self._toggle_pause_conversion,
            state=tk.DISABLED,
            style="Wing.Secondary.TButton",
            width=14,
        )
        self.pause_button.grid(row=0, column=1, padx=4)

        self.stop_button = ttk.Button(
            button_row,
            text="Stop Conversion",
            command=self._stop_conversion,
            state=tk.DISABLED,
            style="Wing.Danger.TButton",
            width=16,
        )
        self.stop_button.grid(row=0, column=2, padx=4)

        self.track_names_button = ttk.Button(
            button_row,
            text="Track Names & Profiles...",
            command=self._edit_track_names,
            style="Wing.Secondary.TButton",
            width=20,
        )
        self.track_names_button.grid(row=0, column=3, padx=4)

        self.open_output_button = ttk.Button(
            button_row,
            text="Open Output Folder",
            command=self._open_output_folder,
            style="Wing.Secondary.TButton",
            width=18,
        )
        self.open_output_button.grid(row=0, column=4, padx=4)

        self.clear_log_button = ttk.Button(
            button_row,
            text="Clear Log",
            command=self._clear_log,
            style="Wing.Secondary.TButton",
            width=16,
        )
        self.clear_log_button.grid(row=0, column=5, padx=4)

        helper_caption = ttk.Label(
            actions,
            text="Tip: Save track-name profiles for recurring channel layouts and speed up delivery.",
            style="Wing.SectionCaption.TLabel",
        )
        helper_caption.pack(anchor=tk.W, pady=(5, 0))

        self.progress = ttk.Progressbar(main, mode="indeterminate", style="Wing.Horizontal.TProgressbar")
        self.progress.pack(fill=tk.X, pady=(7, 0))

        status_row = tk.Frame(main, bg=WING_BG)
        status_row.pack(fill=tk.X, pady=(5, 5))

        self.status_dot_canvas = tk.Canvas(
            status_row,
            width=14,
            height=14,
            bg=WING_BG,
            bd=0,
            highlightthickness=0,
        )
        self.status_dot_canvas.pack(side=tk.LEFT, padx=(0, 7))
        self._status_dot_item = self.status_dot_canvas.create_oval(2, 2, 12, 12, fill=WING_MUTED, outline=WING_MUTED)

        self.status_label = ttk.Label(status_row, textvariable=self.status_var, style="Wing.Page.StatusNeutral.TLabel")
        self.status_label.pack(side=tk.LEFT)

        log_frame = ttk.LabelFrame(main, text="Live Conversion Log", padding=6, style="Wing.Step.TLabelframe")
        log_frame.pack(fill=tk.BOTH, expand=True, pady=(4, 0))

        self.log_text = tk.Text(
            log_frame,
            height=7,
            wrap=tk.WORD,
            bg=WING_PANEL_ELEVATED,
            fg=WING_TEXT,
            insertbackground=WING_ACCENT,
            selectbackground="#324256",
            selectforeground=WING_TEXT,
            relief=tk.FLAT,
            borderwidth=0,
            highlightthickness=1,
            highlightbackground=WING_BORDER,
            highlightcolor=WING_ACCENT,
            font=self._mono_font(9),
            padx=8,
            pady=6,
        )
        self.log_text.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        self.log_text.tag_configure("log_default", foreground=WING_TEXT)
        self.log_text.tag_configure("log_info", foreground=WING_INFO)
        self.log_text.tag_configure("log_warning", foreground=WING_WARNING)
        self.log_text.tag_configure("log_error", foreground=WING_DANGER)
        self.log_text.tag_configure("log_success", foreground=WING_SUCCESS)
        self.log_text.tag_configure("log_debug", foreground=WING_LOG_DEBUG)
        self.log_text.tag_configure("log_header", foreground=WING_ACCENT, font=self._mono_font(9, "bold"))

        scroll = ttk.Scrollbar(log_frame, orient=tk.VERTICAL, command=self.log_text.yview)
        scroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.log_text.configure(yscrollcommand=scroll.set)

        self.bottom_banner = tk.Label(main, bg=WING_BG, bd=0, highlightthickness=0)
        self.bottom_banner.pack(fill=tk.X, pady=(6, 0))
        self.bottom_banner.bind("<Configure>", self._refresh_bottom_banner)
        self.root.after_idle(self._refresh_bottom_banner)

    def _choose_input_folder(self) -> None:
        selected = filedialog.askdirectory(title="Select X-LIVE input folder")
        if selected:
            self.input_var.set(selected)

    def _choose_output_folder(self) -> None:
        selected = filedialog.askdirectory(title="Select output folder")
        if selected:
            self.output_var.set(selected)

    def _append_log(self, line: str) -> None:
        tag = "log_default"
        upper_line = line.upper()
        if line.startswith("="):
            tag = "log_header"
        elif upper_line.startswith("ERROR:"):
            tag = "log_error"
        elif upper_line.startswith("WARNING:"):
            tag = "log_warning"
        elif upper_line.startswith("INFO:"):
            tag = "log_info"
        elif upper_line.startswith("DEBUG:"):
            tag = "log_debug"
        elif upper_line.strip() in {"DONE.", "CONVERSION COMPLETED SUCCESSFULLY."}:
            tag = "log_success"

        self.log_text.insert(tk.END, f"{line}\n", (tag,))
        self.log_text.see(tk.END)

    def _clear_log(self) -> None:
        self.log_text.delete("1.0", tk.END)

    @staticmethod
    def _normalize_track_name_map(raw_map: object) -> dict[int, str]:
        if not isinstance(raw_map, dict):
            return {}

        parsed: dict[int, str] = {}
        for raw_key, raw_value in raw_map.items():
            try:
                track_number = int(raw_key)
            except (TypeError, ValueError):
                continue

            if not 1 <= track_number <= TRACK_NAME_SLOT_COUNT:
                continue

            name = str(raw_value).strip()
            if not name:
                continue

            parsed[track_number] = name

        return parsed

    def _read_track_name_profiles(self) -> dict[str, dict[int, str]]:
        if not TRACK_NAME_PROFILES_PATH.exists():
            return {}

        try:
            payload = json.loads(TRACK_NAME_PROFILES_PATH.read_text(encoding="utf-8"))
        except Exception:
            return {}

        if not isinstance(payload, dict):
            return {}

        raw_profiles = payload.get("profiles", payload)
        if not isinstance(raw_profiles, dict):
            return {}

        profiles: dict[str, dict[int, str]] = {}
        for raw_name, raw_map in raw_profiles.items():
            profile_name = str(raw_name).strip()
            if not profile_name:
                continue

            profiles[profile_name] = self._normalize_track_name_map(raw_map)

        return dict(sorted(profiles.items(), key=lambda item: item[0].lower()))

    def _write_track_name_profiles(self, profiles: dict[str, dict[int, str]]) -> None:
        serializable_profiles = {
            profile_name: {
                str(track_number): name
                for track_number, name in sorted(track_map.items())
            }
            for profile_name, track_map in sorted(profiles.items(), key=lambda item: item[0].lower())
        }

        TRACK_NAME_PROFILES_PATH.parent.mkdir(parents=True, exist_ok=True)
        TRACK_NAME_PROFILES_PATH.write_text(
            json.dumps({"profiles": serializable_profiles}, indent=2),
            encoding="utf-8",
        )

    def _edit_track_names(self) -> None:
        dialog = tk.Toplevel(self.root)
        dialog.title(f"{APP_NAME} - Track Names")
        dialog.geometry("820x680")
        dialog.minsize(760, 620)
        dialog.configure(bg=WING_PANEL)
        dialog.transient(self.root)
        dialog.grab_set()

        container = ttk.Frame(dialog, padding=12)
        container.pack(fill=tk.BOTH, expand=True)

        accent = tk.Frame(container, bg=WING_ACCENT, height=3, bd=0, highlightthickness=0)
        accent.pack(fill=tk.X, pady=(0, 10))

        ttk.Label(container, text="Track Names And Profiles", style="Wing.Title.TLabel").pack(anchor=tk.W)

        ttk.Label(
            container,
            text=(
                "Set custom track names used for output filenames before conversion. "
                f"Use the numbered fields for tracks 1-{TRACK_NAME_SLOT_COUNT}, and save/load name profiles."
            ),
            style="Wing.Subtitle.TLabel",
            wraplength=760,
            justify=tk.LEFT,
        ).pack(anchor=tk.W, pady=(2, 10))

        profiles = self._read_track_name_profiles()

        profile_frame = ttk.Frame(container)
        profile_frame.pack(fill=tk.X, pady=(10, 10))
        profile_frame.columnconfigure(1, weight=1)

        ttk.Label(profile_frame, text="Profile").grid(row=0, column=0, sticky="w")

        profile_var = tk.StringVar(value=self.track_name_profile_name)
        profile_combo = ttk.Combobox(profile_frame, textvariable=profile_var, width=34)
        profile_combo.grid(row=0, column=1, sticky="ew", padx=(8, 8))

        def refresh_profile_combo_values() -> None:
            profile_combo["values"] = tuple(sorted(profiles.keys(), key=str.lower))

        refresh_profile_combo_values()

        current_overrides = self._normalize_track_name_map(self.track_name_overrides)

        numbered_scroll_container = ttk.Frame(container)
        numbered_scroll_container.pack(fill=tk.BOTH, expand=True)

        numbered_canvas = tk.Canvas(
            numbered_scroll_container,
            highlightthickness=1,
            highlightbackground=WING_BORDER,
            highlightcolor=WING_ACCENT,
            bg=WING_PANEL_ELEVATED,
            bd=0,
        )
        numbered_canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        numbered_scrollbar = ttk.Scrollbar(
            numbered_scroll_container,
            orient=tk.VERTICAL,
            command=numbered_canvas.yview,
        )
        numbered_scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        numbered_canvas.configure(yscrollcommand=numbered_scrollbar.set)

        numbered_inner = ttk.Frame(numbered_canvas)
        numbered_window = numbered_canvas.create_window((0, 0), window=numbered_inner, anchor="nw")

        numbered_entry_vars: dict[int, tk.StringVar] = {}
        for track_number in range(1, TRACK_NAME_SLOT_COUNT + 1):
            row_bg = WING_PANEL_ELEVATED if track_number % 2 == 0 else WING_CARD
            row = tk.Frame(
                numbered_inner,
                bg=row_bg,
                highlightthickness=1,
                highlightbackground=WING_BORDER,
                padx=8,
                pady=6,
            )
            row.pack(fill=tk.X, padx=(0, 8), pady=2)
            row.grid_columnconfigure(1, weight=1)

            tk.Label(
                row,
                text=f"{track_number:02d}",
                width=4,
                bg=row_bg,
                fg=WING_ACCENT,
                font=self._ui_font(10, "bold"),
            ).grid(row=0, column=0, sticky="w", padx=(0, 8))
            value_var = tk.StringVar(value=current_overrides.get(track_number, ""))
            ttk.Entry(row, textvariable=value_var).grid(row=0, column=1, sticky="ew")
            numbered_entry_vars[track_number] = value_var

        def update_numbered_scroll_region(_event: object) -> None:
            numbered_canvas.configure(scrollregion=numbered_canvas.bbox("all"))

        def update_numbered_frame_width(event: object) -> None:
            width = getattr(event, "width", 0)
            if isinstance(width, int) and width > 0:
                numbered_canvas.itemconfigure(numbered_window, width=width)

        numbered_inner.bind("<Configure>", update_numbered_scroll_region)
        numbered_canvas.bind("<Configure>", update_numbered_frame_width)

        def collect_numbered_entries() -> dict[int, str]:
            parsed: dict[int, str] = {}
            for track_number in range(1, TRACK_NAME_SLOT_COUNT + 1):
                value = numbered_entry_vars[track_number].get().strip()
                if value:
                    parsed[track_number] = value
            return parsed

        def apply_numbered_entries(track_map: dict[int, str]) -> None:
            for track_number in range(1, TRACK_NAME_SLOT_COUNT + 1):
                numbered_entry_vars[track_number].set(track_map.get(track_number, ""))

        def load_profile() -> None:
            profile_name = profile_var.get().strip()
            if not profile_name:
                messagebox.showwarning(APP_NAME, "Enter or select a profile name first.", parent=dialog)
                return

            profile_map = profiles.get(profile_name)
            if profile_map is None:
                messagebox.showwarning(APP_NAME, f"Profile not found: {profile_name}", parent=dialog)
                return

            apply_numbered_entries(profile_map)
            self.track_name_profile_name = profile_name
            self._append_log(f"Track names profile loaded: {profile_name} ({len(profile_map)} name(s)).")

        def save_profile() -> None:
            profile_name = profile_var.get().strip()
            if not profile_name:
                messagebox.showwarning(APP_NAME, "Enter a profile name before saving.", parent=dialog)
                return

            profile_map = collect_numbered_entries()
            profiles[profile_name] = profile_map
            try:
                self._write_track_name_profiles(profiles)
            except Exception as exc:  # pylint: disable=broad-except
                messagebox.showerror(APP_NAME, f"Could not save profile:\n\n{exc}", parent=dialog)
                return

            self.track_name_profile_name = profile_name
            refresh_profile_combo_values()
            profile_var.set(profile_name)
            self._append_log(f"Track names profile saved: {profile_name} ({len(profile_map)} name(s)).")

        ttk.Button(profile_frame, text="Load Profile", command=load_profile, style="Wing.Secondary.TButton").grid(row=0, column=2, padx=(0, 8))
        ttk.Button(profile_frame, text="Save to Profile", command=save_profile, style="Wing.Primary.TButton").grid(row=0, column=3)

        buttons = ttk.Frame(container)
        buttons.pack(fill=tk.X, pady=(10, 0))

        def clear_all() -> None:
            for value_var in numbered_entry_vars.values():
                value_var.set("")

        def save_and_close() -> None:
            self.track_name_overrides = collect_numbered_entries()
            selected_profile_name = profile_var.get().strip()
            self.track_name_profile_name = selected_profile_name if selected_profile_name in profiles else ""
            if self.track_name_overrides:
                self._append_log(f"Track names: {len(self.track_name_overrides)} custom mapping(s) configured.")
            else:
                self._append_log("Track names: custom mappings cleared.")
            self._save_settings()
            dialog.destroy()

        ttk.Button(buttons, text="Clear", command=clear_all, style="Wing.Secondary.TButton").pack(side=tk.LEFT)
        ttk.Button(buttons, text="Cancel", command=dialog.destroy).pack(side=tk.RIGHT)
        ttk.Button(buttons, text="Save", command=save_and_close, style="Wing.Primary.TButton").pack(side=tk.RIGHT, padx=(0, 8))

        self.root.wait_window(dialog)

    def _build_config(self) -> RunConfig | None:
        input_raw = self.input_var.get().strip()
        output_raw = self.output_var.get().strip()

        if not input_raw:
            messagebox.showerror(APP_NAME, "Choose an input folder first.")
            return None
        if not output_raw:
            messagebox.showerror(APP_NAME, "Choose an output folder first.")
            return None

        input_root = Path(input_raw).resolve()
        output_root = Path(output_raw).resolve()

        if not input_root.exists() or not input_root.is_dir():
            messagebox.showerror(APP_NAME, "Input folder does not exist or is not a directory.")
            return None

        output_type = self.output_type_var.get().strip().lower() or "wav"
        if output_type not in SUPPORTED_OUTPUT_TYPES:
            messagebox.showerror(APP_NAME, f"Output type must be one of: {', '.join(SUPPORTED_OUTPUT_TYPES)}")
            return None

        mp3_mode_label = self.mp3_mode_var.get().strip()
        mp3_mode = MP3_MODE_LABEL_TO_VALUE.get(mp3_mode_label, "cbr")

        try:
            mp3_bitrate = int(self.mp3_bitrate_var.get())
        except Exception:
            mp3_bitrate = 320
        mp3_bitrate = max(64, min(320, mp3_bitrate))

        try:
            wav_bit_depth = int(self.wav_bit_depth_var.get())
        except Exception:
            wav_bit_depth = 24
        if wav_bit_depth not in WAV_BIT_DEPTH_CHOICES:
            wav_bit_depth = 24

        flac_encoding_depth = self.flac_encoding_depth_var.get().strip()
        if flac_encoding_depth not in FLAC_ENCODING_DEPTH_CHOICES:
            flac_encoding_depth = "24"

        flac_compression_label = self.flac_compression_var.get().strip() or "5-default"
        try:
            flac_compression_level = int(flac_compression_label.split("-", 1)[0])
        except Exception:
            flac_compression_level = 5
        flac_compression_level = max(0, min(8, flac_compression_level))

        return RunConfig(
            input_root=input_root,
            output_root=output_root,
            output_type=output_type,
            wav_keep_original_bit_depth=self.wav_keep_original_bit_depth_var.get(),
            wav_bit_depth=wav_bit_depth,
            flac_encoding_depth=flac_encoding_depth,
            flac_compression_level=flac_compression_level,
            mp3_mode=mp3_mode,
            mp3_bitrate_kbps=mp3_bitrate,
            track_name_overrides=dict(self.track_name_overrides),
        )

    def _set_running_state(self, running: bool) -> None:
        state = tk.DISABLED if running else tk.NORMAL
        readonly_state = "disabled" if running else "readonly"

        self._is_running = running
        self.run_button.config(state=state)
        self.pause_button.config(state=(tk.NORMAL if running else tk.DISABLED))
        self.stop_button.config(state=(tk.NORMAL if running else tk.DISABLED))
        self.track_names_button.config(state=state)
        self.open_output_button.config(state=state)
        self.clear_log_button.config(state=state)

        self.input_entry.config(state=state)
        self.output_entry.config(state=state)
        self.input_browse_button.config(state=state)
        self.output_browse_button.config(state=state)
        self.output_type_combo.config(state=readonly_state)
        self._update_output_controls()

        if running:
            self.progress.start(12)
        else:
            self.progress.stop()
            self._is_paused = False
            self.pause_button.config(text="Pause Conversion")

    def _toggle_pause_conversion(self) -> None:
        if not self._is_running or self._pause_event is None:
            return

        if self._is_paused:
            self._pause_event.clear()
            self._is_paused = False
            self.pause_button.config(text="Pause Conversion")
            self._set_status("Running conversion...", tone="info")
            self._append_log("Conversion resumed.")
            return

        self._pause_event.set()
        self._is_paused = True
        self.pause_button.config(text="Resume Conversion")
        self._set_status("Conversion paused.", tone="warning")
        self._append_log("Conversion paused.")

    def _stop_conversion(self) -> None:
        if not self._is_running or self._stop_event is None:
            return

        if self._stop_event.is_set():
            return

        self._stop_event.set()
        if self._pause_event is not None:
            self._pause_event.clear()
        self._is_paused = False
        self.pause_button.config(text="Pause Conversion")
        self._set_status("Stopping conversion...", tone="warning")
        self._append_log("Stop requested. Waiting for current operation to finish...")

    def _on_output_type_changed(self, _event: object | None = None) -> None:
        self._update_output_controls()

    def _on_wav_keep_original_changed(self) -> None:
        self._update_output_controls()

    def _update_output_controls(self) -> None:
        output_type = self.output_type_var.get().strip().lower()
        controls_enabled = not self._is_running

        self.wav_options_frame.grid_remove()
        self.flac_options_frame.grid_remove()
        self.mp3_options_frame.grid_remove()

        if output_type == "wav":
            self.wav_options_frame.grid(row=1, column=0, columnspan=4, sticky="ew", pady=(4, 6))
            keep_original_enabled = controls_enabled
            self.wav_keep_original_check.config(state=(tk.NORMAL if keep_original_enabled else tk.DISABLED))

            wav_combo_enabled = controls_enabled and (not self.wav_keep_original_bit_depth_var.get())
            self.wav_bit_depth_combo.config(state=("readonly" if wav_combo_enabled else "disabled"))

            self.flac_encoding_depth_combo.config(state="disabled")
            self.flac_compression_combo.config(state="disabled")
            self.mp3_mode_combo.config(state="disabled")
            self.mp3_bitrate_combo.config(state="disabled")
            self._refresh_decorative_knobs()
            return

        if output_type == "flac":
            self.flac_options_frame.grid(row=1, column=0, columnspan=4, sticky="ew", pady=(4, 6))
            combo_state = "readonly" if controls_enabled else "disabled"
            self.flac_encoding_depth_combo.config(state=combo_state)
            self.flac_compression_combo.config(state=combo_state)

            self.wav_keep_original_check.config(state=tk.DISABLED)
            self.wav_bit_depth_combo.config(state="disabled")
            self.mp3_mode_combo.config(state="disabled")
            self.mp3_bitrate_combo.config(state="disabled")
            self._refresh_decorative_knobs()
            return

        if output_type == "mp3":
            self.mp3_options_frame.grid(row=1, column=0, columnspan=4, sticky="ew", pady=(4, 6))
            combo_state = "readonly" if controls_enabled else "disabled"
            self.mp3_mode_combo.config(state=combo_state)
            self.mp3_bitrate_combo.config(state=combo_state)

            self.wav_keep_original_check.config(state=tk.DISABLED)
            self.wav_bit_depth_combo.config(state="disabled")
            self.flac_encoding_depth_combo.config(state="disabled")
            self.flac_compression_combo.config(state="disabled")
            self._refresh_decorative_knobs()

    def _start_conversion(self) -> None:
        if self._is_running:
            return

        config = self._build_config()
        if config is None:
            return

        self._save_settings()
        self._append_log("=" * 70)
        self._append_log(
            f"Starting run: {config.input_root} -> {config.output_root} (output: {config.output_type.upper()})"
        )
        if config.output_type == "wav":
            if config.wav_keep_original_bit_depth:
                self._append_log("WAV settings: keep original bit depth")
            else:
                self._append_log(f"WAV settings: forced bit depth {config.wav_bit_depth}-bit PCM")
        if config.output_type == "flac":
            self._append_log(
                f"FLAC settings: encoding depth={config.flac_encoding_depth}, compression={config.flac_compression_level}"
            )
        if config.output_type == "mp3":
            self._append_log(
                "MP3 settings: mode={mode}, bitrate={bitrate}k".format(
                    mode=config.mp3_mode.upper(),
                    bitrate=config.mp3_bitrate_kbps,
                )
            )
        if config.track_name_overrides:
            self._append_log(f"Track naming: {len(config.track_name_overrides)} custom name(s).")
        self._set_status("Running conversion...", tone="info")
        self._stop_event = threading.Event()
        self._pause_event = threading.Event()
        self._is_paused = False
        self.pause_button.config(text="Pause Conversion")
        self._set_running_state(True)

        self._worker_thread = threading.Thread(target=self._run_conversion_worker, args=(config,), daemon=True)
        self._worker_thread.start()

    def _run_conversion_worker(self, config: RunConfig) -> None:
        logger = logging.getLogger("wing-xlive-gui")
        logger.handlers.clear()
        logger.propagate = False

        handler = QueueLoggingHandler(self._messages)
        handler.setFormatter(logging.Formatter("%(levelname)s: %(message)s"))
        logger.addHandler(handler)
        logger.setLevel(logging.DEBUG)

        try:
            summary = convert_xlive_folder(
                input_root=config.input_root,
                output_root=config.output_root,
                recursive=True,
                dry_run=False,
                strict=False,
                mode="auto",
                block_frames=AUTO_BLOCK_FRAMES,
                output_settings=OutputSettings(
                    format=config.output_type,
                    wav_keep_original_bit_depth=config.wav_keep_original_bit_depth,
                    wav_bit_depth=config.wav_bit_depth,
                    flac_encoding_depth=config.flac_encoding_depth,
                    flac_compression_level=config.flac_compression_level,
                    mp3_mode=config.mp3_mode,
                    mp3_bitrate_kbps=config.mp3_bitrate_kbps,
                    track_name_overrides=config.track_name_overrides,
                ),
                logger=logger,
                conversion_control=ConversionControl(
                    stop_event=self._stop_event,
                    pause_event=self._pause_event,
                ),
            )
            self._messages.put(("done", (True, summary, config.output_type, "completed")))
        except ConversionCancelled as exc:
            logger.info("%s", exc)
            self._messages.put(("done", (False, str(exc), config.output_type, "stopped")))
        except Exception as exc:  # pylint: disable=broad-except
            logger.error("Conversion failed: %s", exc)
            logger.debug(traceback.format_exc())
            self._messages.put(("done", (False, str(exc), config.output_type, "failed")))

    def _process_messages(self) -> None:
        while True:
            try:
                kind, payload = self._messages.get_nowait()
            except queue.Empty:
                break

            if kind == "log":
                self._append_log(str(payload))
            elif kind == "done":
                if isinstance(payload, tuple) and len(payload) == 4:
                    success, result, output_type, run_state = payload
                else:
                    success, result, output_type = payload
                    run_state = "completed" if success else "failed"
                self._finalize_run(bool(success), result, str(output_type), str(run_state))

        self.root.after(100, self._process_messages)

    def _finalize_run(self, success: bool, result: object, output_type: str, run_state: str) -> None:
        self._set_running_state(False)
        self._stop_event = None
        self._pause_event = None

        if success:
            assert isinstance(result, ConversionSummary)
            self._append_log("Done.")
            self._append_log(
                "Summary: takes={takes}, tracks={tracks}, clips={clips}, skipped={skipped}".format(
                    takes=result.takes_converted,
                    tracks=result.tracks_written,
                    clips=result.clips_consumed,
                    skipped=len(result.skipped_files),
                )
            )

            if output_type == "wav":
                self._set_status("Conversion completed successfully.", tone="success")
                messagebox.showinfo(APP_NAME, "Conversion completed successfully.")
            else:
                self._set_status(f"Conversion completed successfully ({output_type.upper()}).", tone="success")
                messagebox.showinfo(
                    APP_NAME,
                    f"Conversion completed successfully.\n\n"
                    f"Output type: {output_type.upper()}",
                )
        elif run_state == "stopped":
            self._set_status("Conversion stopped.", tone="warning")
            messagebox.showinfo(APP_NAME, "Conversion stopped.")
        else:
            self._set_status("Conversion failed.", tone="error")
            messagebox.showerror(APP_NAME, f"Conversion failed:\n\n{result}")

    def _open_output_folder(self) -> None:
        output_text = self.output_var.get().strip()
        if not output_text:
            messagebox.showwarning(APP_NAME, "Choose an output folder first.")
            return

        output_path = Path(output_text)
        if not output_path.exists():
            messagebox.showwarning(APP_NAME, "Output folder does not exist yet.")
            return

        try:
            os.startfile(str(output_path))
        except Exception as exc:  # pylint: disable=broad-except
            messagebox.showerror(APP_NAME, f"Could not open folder:\n\n{exc}")

    def _settings_payload(self) -> dict[str, object]:
        return {
            "input_folder": self.input_var.get().strip(),
            "output_folder": self.output_var.get().strip(),
            "output_type": self.output_type_var.get().strip().lower(),
            "wav_keep_original_bit_depth": self.wav_keep_original_bit_depth_var.get(),
            "wav_bit_depth": self.wav_bit_depth_var.get().strip(),
            "flac_encoding_depth": self.flac_encoding_depth_var.get().strip(),
            "flac_compression": self.flac_compression_var.get().strip(),
            "mp3_mode": MP3_MODE_LABEL_TO_VALUE.get(self.mp3_mode_var.get(), "cbr"),
            "mp3_bitrate": self.mp3_bitrate_var.get().strip(),
            "ui_scale": self.ui_scale_var.get().strip() or "Auto",
            "track_name_profile_name": self.track_name_profile_name,
            "track_name_overrides": {str(key): value for key, value in sorted(self.track_name_overrides.items())},
        }

    def _save_settings(self) -> None:
        try:
            SETTINGS_PATH.parent.mkdir(parents=True, exist_ok=True)
            SETTINGS_PATH.write_text(json.dumps(self._settings_payload(), indent=2), encoding="utf-8")
        except Exception:
            pass

    def _load_settings(self) -> None:
        if not SETTINGS_PATH.exists():
            return

        try:
            payload = json.loads(SETTINGS_PATH.read_text(encoding="utf-8"))
        except Exception:
            return

        self.input_var.set(str(payload.get("input_folder", "")))
        self.output_var.set(str(payload.get("output_folder", "")))

        output_type = str(payload.get("output_type", "wav")).lower()
        if output_type in SUPPORTED_OUTPUT_TYPES:
            self.output_type_var.set(output_type)

        saved_mp3_mode = str(payload.get("mp3_mode", "cbr")).lower()
        self.mp3_mode_var.set(MP3_MODE_VALUE_TO_LABEL.get(saved_mp3_mode, MP3_MODE_VALUE_TO_LABEL["cbr"]))

        saved_ui_scale = str(payload.get("ui_scale", "Auto")).strip()
        self.ui_scale_var.set(saved_ui_scale if saved_ui_scale in UI_SCALE_OPTIONS else "Auto")

        saved_mp3_bitrate = str(payload.get("mp3_bitrate", "320")).strip()
        self.mp3_bitrate_var.set(saved_mp3_bitrate if saved_mp3_bitrate in {str(v) for v in MP3_BITRATE_CHOICES} else "320")

        self.wav_keep_original_bit_depth_var.set(bool(payload.get("wav_keep_original_bit_depth", True)))
        saved_wav_bit_depth = str(payload.get("wav_bit_depth", "24")).strip()
        self.wav_bit_depth_var.set(saved_wav_bit_depth if saved_wav_bit_depth in {str(v) for v in WAV_BIT_DEPTH_CHOICES} else "24")

        saved_flac_encoding_depth = str(payload.get("flac_encoding_depth", "24")).strip()
        self.flac_encoding_depth_var.set(
            saved_flac_encoding_depth if saved_flac_encoding_depth in FLAC_ENCODING_DEPTH_CHOICES else "24"
        )

        saved_flac_compression = str(payload.get("flac_compression", "5-default")).strip()
        self.flac_compression_var.set(
            saved_flac_compression if saved_flac_compression in FLAC_COMPRESSION_CHOICES else "5-default"
        )

        saved_track_name_overrides = payload.get("track_name_overrides", {})
        self.track_name_profile_name = str(payload.get("track_name_profile_name", "")).strip()
        parsed_track_overrides: dict[int, str] = {}
        if isinstance(saved_track_name_overrides, dict):
            for raw_key, raw_value in saved_track_name_overrides.items():
                try:
                    track_number = int(raw_key)
                except (TypeError, ValueError):
                    continue

                if not 1 <= track_number <= TRACK_NAME_SLOT_COUNT:
                    continue

                name = str(raw_value).strip()
                if not name:
                    continue

                parsed_track_overrides[track_number] = name

        self.track_name_overrides = parsed_track_overrides

        self._apply_ui_scale(persist=False)
        self._update_output_controls()
        self._refresh_decorative_knobs()

    def _on_close(self) -> None:
        if self._is_running:
            should_close = messagebox.askyesno(
                APP_NAME,
                "Conversion is still running. Close the app anyway?",
            )
            if not should_close:
                return
            self._stop_conversion()

        self._save_settings()
        self.root.destroy()


def main() -> None:
    _enable_windows_dpi_awareness()
    root = tk.Tk()
    WingXLiveConverterApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
