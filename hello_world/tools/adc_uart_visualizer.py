#!/usr/bin/env python3
"""Live UART visualization tool for the custom NXP ADC API."""

from __future__ import annotations

import math
import queue
import random
import statistics
import threading
import time
import tkinter as tk
from collections import Counter, deque
from dataclasses import dataclass, field
from tkinter import messagebox, ttk

try:
    import serial
    from serial.tools import list_ports
except ImportError:  # pragma: no cover - checked when connecting
    serial = None
    list_ports = None


DESCRIPTOR_SIZE = 4
PACKET_SIZE = 6
STARTUP_IDLE_S = 0.25
STARTUP_MAGIC = b"ADCF"
MAX_CONFIG_DESCRIPTORS = 64
CONFIG_REQUEST_INTERVAL_S = 1.0
LEGACY_DISCOVERY_TIME_S = 1.0
LEGACY_DESCRIPTOR_CONFIRMATIONS = 3
RECONNECT_DELAY_MS = 2000
UART_RX_TIMEOUT_S = 3.0
MAX_EVENTS_PER_UI_TICK = 4000
MODULE_NAMES = {0: "ADC0", 1: "ADC1"}
RESOLUTION_BITS = {0: 12, 1: 16}
VALUE_TYPE_NAMES = {0: "ADC Value", 1: "Maximum", 2: "Minimum", 3: "Average", 4: "RMS"}
VALUE_TYPE_COLORS = {0: "#4aa3ff", 1: "#ef5350", 2: "#26a69a", 3: "#f6c344", 4: "#ab72df"}
CHANNEL_COLORS = (
    "#4aa3ff", "#ef5350", "#26a69a", "#f6c344", "#ab72df", "#ff8a65",
    "#66bb6a", "#ec407a", "#29b6f6", "#d4e157", "#7e57c2", "#ffa726",
)

BG = "#11151b"
PANEL = "#181e26"
PANEL_2 = "#202833"
BORDER = "#303a47"
TEXT = "#e8edf3"
MUTED = "#93a1b2"
GRID = "#2b3440"
GOOD = "#4fc38d"
WARN = "#f6c344"
BAD = "#ef6461"


@dataclass(frozen=True)
class ADCDescriptor:
    module: int
    channel: int
    resolution: int
    value_type: int

    @property
    def key(self) -> tuple[int, int]:
        return self.module, self.channel


@dataclass(frozen=True)
class ADCPacket:
    module: int
    channel: int
    resolution: int
    value_type: int
    measured_value: int
    timestamp: float

    @property
    def key(self) -> tuple[int, int]:
        return self.module, self.channel


class PacketDecoder:
    """Decode startup descriptors, then fixed six-byte measurement packets."""

    def __init__(self) -> None:
        self.buffer = bytearray()
        self.discarded_bytes = 0
        self.startup_complete = False
        self.descriptor_count = 0
        self.expected_descriptors: int | None = None

    @staticmethod
    def _valid(candidate: bytes) -> bool:
        return (
            candidate[0] in MODULE_NAMES
            and 0 <= candidate[1] < 32
            and candidate[2] in RESOLUTION_BITS
            and candidate[3] in VALUE_TYPE_NAMES
        )

    def finish_startup(self) -> None:
        self.startup_complete = True
        self.buffer.clear()

    def feed(self, data: bytes, timestamp: float | None = None) -> list[ADCDescriptor | ADCPacket]:
        self.buffer.extend(data)
        packets: list[ADCDescriptor | ADCPacket] = []
        now = time.monotonic() if timestamp is None else timestamp

        if not self.startup_complete:
            while not self.startup_complete:
                while self.expected_descriptors is None:
                    magic_index = self.buffer.find(STARTUP_MAGIC)
                    if magic_index < 0:
                        keep = min(len(self.buffer), len(STARTUP_MAGIC) - 1)
                        self.discarded_bytes += len(self.buffer) - keep
                        if keep:
                            self.buffer[:] = self.buffer[-keep:]
                        else:
                            self.buffer.clear()
                        return packets
                    if magic_index > 0:
                        del self.buffer[:magic_index]
                        self.discarded_bytes += magic_index
                    if len(self.buffer) < len(STARTUP_MAGIC) + 1:
                        return packets
                    del self.buffer[:len(STARTUP_MAGIC)]
                    descriptor_count = self.buffer.pop(0)
                    if descriptor_count == 0 or descriptor_count > MAX_CONFIG_DESCRIPTORS:
                        self.discarded_bytes += len(STARTUP_MAGIC) + 1
                        continue
                    self.expected_descriptors = descriptor_count

                required = self.expected_descriptors * DESCRIPTOR_SIZE
                if len(self.buffer) < required:
                    return packets

                candidates = [
                    self.buffer[index * DESCRIPTOR_SIZE:(index + 1) * DESCRIPTOR_SIZE]
                    for index in range(self.expected_descriptors)
                ]
                del self.buffer[:required]
                self.expected_descriptors = None
                if not all(self._valid(candidate) for candidate in candidates):
                    self.discarded_bytes += required
                    continue

                packets.extend(
                    ADCDescriptor(candidate[0], candidate[1], candidate[2], candidate[3])
                    for candidate in candidates
                )
                self.descriptor_count = len(candidates)
                self.startup_complete = True
            return packets

        while len(self.buffer) >= PACKET_SIZE:
            candidate = self.buffer[:PACKET_SIZE]
            if not self._valid(candidate):
                del self.buffer[0]
                self.discarded_bytes += 1
                continue

            del self.buffer[:PACKET_SIZE]
            packets.append(
                ADCPacket(
                    module=candidate[0],
                    channel=candidate[1],
                    resolution=candidate[2],
                    value_type=candidate[3],
                    measured_value=(candidate[4] << 8) | candidate[5],
                    timestamp=now,
                )
            )

        return packets


@dataclass
class ChannelState:
    module: int
    channel: int
    visible: bool = True
    reference_voltage: float = 3.3
    input_voltage: float = 0.0
    resolution: int = 0
    color: str = "#4aa3ff"
    last_raw: int = 0
    last_voltage: float = 0.0
    last_seen: float = 0.0
    series: dict[int, deque[tuple[float, float]]] = field(default_factory=dict)
    configured_value_types: set[int] = field(default_factory=set)
    max_deviation: dict[int, float] = field(default_factory=dict)

    @property
    def key(self) -> tuple[int, int]:
        return self.module, self.channel

    @property
    def label(self) -> str:
        return f"ADC{self.module} CH{self.channel}"

    @property
    def resolution_bits(self) -> int:
        return RESOLUTION_BITS[self.resolution]

    def raw_to_voltage(self, raw: int, resolution: int | None = None) -> float:
        resolution = self.resolution if resolution is None else resolution
        full_scale = (1 << RESOLUTION_BITS[resolution]) - 1
        return (raw / full_scale) * self.reference_voltage

    def update(self, packet: ADCPacket, history_seconds: float) -> None:
        self.resolution = packet.resolution
        self.last_raw = packet.measured_value
        self.last_voltage = self.raw_to_voltage(packet.measured_value, packet.resolution)
        self.last_seen = packet.timestamp

        series = self.series.setdefault(packet.value_type, deque())
        series.append((packet.timestamp, self.last_voltage))
        cutoff = packet.timestamp - history_seconds
        while series and series[0][0] < cutoff:
            series.popleft()

        deviation = abs(self.last_voltage - self.input_voltage)
        self.max_deviation[packet.value_type] = max(self.max_deviation.get(packet.value_type, 0.0), deviation)

    def stats(self, value_type: int) -> tuple[float, float, float, float]:
        values = [value for _, value in self.series.get(value_type, ())]
        if not values:
            return 0.0, 0.0, 0.0, self.max_deviation.get(value_type, 0.0)
        average = statistics.fmean(values)
        variance = statistics.pvariance(values) if len(values) > 1 else 0.0
        return average, variance, math.sqrt(variance), self.max_deviation.get(value_type, 0.0)


class UARTWorker(threading.Thread):
    def __init__(self, port_name: str, baud_rate: int, output: queue.Queue[object]) -> None:
        super().__init__(daemon=True)
        self.port_name = port_name
        self.baud_rate = baud_rate
        self.output = output
        self.stop_event = threading.Event()
        self.decoder = PacketDecoder()
        self.discovery_decoder = PacketDecoder()
        self.discovery_decoder.finish_startup()
        self.discovery_counts: Counter[tuple[int, int, int, int]] = Counter()
        self.discovery_start_time: float | None = None

    def run(self) -> None:
        if serial is None:
            self.output.put(("error", "Missing pyserial. Install with: python -m pip install pyserial"))
            return

        try:
            with serial.Serial(self.port_name, self.baud_rate, timeout=0.1) as uart:
                uart.reset_input_buffer()
                uart.reset_output_buffer()
                self.output.put(("connected", self.port_name))
                uart.write(b"A")
                uart.flush()
                last_rx_time = time.monotonic()
                last_config_request_time = last_rx_time
                configuration_reported = False
                while not self.stop_event.is_set():
                    if (
                        not self.decoder.startup_complete
                        and (time.monotonic() - last_config_request_time) >= CONFIG_REQUEST_INTERVAL_S
                    ):
                        uart.write(b"A")
                        uart.flush()
                        last_config_request_time = time.monotonic()

                    data = uart.read(max(PACKET_SIZE, uart.in_waiting))
                    if data:
                        last_rx_time = time.monotonic()
                        if not self.decoder.startup_complete:
                            discovered_packets = self.discovery_decoder.feed(data)
                            if discovered_packets and self.discovery_start_time is None:
                                self.discovery_start_time = last_rx_time
                            for packet in discovered_packets:
                                self.discovery_counts[
                                    (packet.module, packet.channel, packet.resolution, packet.value_type)
                                ] += 1

                        for item in self.decoder.feed(data):
                            self.output.put(item)
                        if self.decoder.startup_complete and not configuration_reported:
                            self.output.put(("configured", self.port_name))
                            configuration_reported = True
                        elif (
                            not self.decoder.startup_complete
                            and self.discovery_start_time is not None
                            and (last_rx_time - self.discovery_start_time) >= LEGACY_DISCOVERY_TIME_S
                        ):
                            confirmed = sorted(
                                descriptor
                                for descriptor, count in self.discovery_counts.items()
                                if count >= LEGACY_DESCRIPTOR_CONFIRMATIONS
                            )
                            if confirmed:
                                for descriptor in confirmed:
                                    self.output.put(ADCDescriptor(*descriptor))
                                self.decoder.finish_startup()
                                self.output.put(("configured", f"{self.port_name} (discovered stream)"))
                                configuration_reported = True
                    elif (
                        not self.decoder.startup_complete
                        and self.decoder.descriptor_count > 0
                        and (time.monotonic() - last_rx_time) >= STARTUP_IDLE_S
                    ):
                        self.decoder.finish_startup()
                        self.output.put(("configured", self.port_name))
                        configuration_reported = True
                    elif (time.monotonic() - last_rx_time) >= UART_RX_TIMEOUT_S:
                        raise TimeoutError(f"No UART data received for {UART_RX_TIMEOUT_S:g} seconds")
                self.output.put(("disconnected", self.port_name))
        except Exception as exc:  # pragma: no cover - hardware dependent
            self.output.put(("error", f"{type(exc).__name__}: {exc}"))

    def stop(self) -> None:
        self.stop_event.set()


class SimulatorWorker(threading.Thread):
    def __init__(self, output: queue.Queue[object]) -> None:
        super().__init__(daemon=True)
        self.output = output
        self.stop_event = threading.Event()
        self.phase = 0.0

    def run(self) -> None:
        self.output.put(("connected", "Simulation"))
        descriptors = (
            ADCDescriptor(0, 0, 1, 0),
            ADCDescriptor(0, 0, 1, 1),
            ADCDescriptor(0, 0, 1, 2),
            ADCDescriptor(0, 1, 0, 0),
            ADCDescriptor(0, 1, 0, 3),
            ADCDescriptor(0, 1, 0, 4),
            ADCDescriptor(1, 3, 1, 0),
            ADCDescriptor(1, 3, 1, 3),
            ADCDescriptor(1, 3, 1, 4),
        )
        for descriptor in descriptors:
            self.output.put(descriptor)
        self.output.put(("configured", "Simulation"))
        next_stats = time.monotonic()

        while not self.stop_event.is_set():
            now = time.monotonic()
            self.phase += 0.045
            samples = (
                (0, 0, 1, 1.02 + 0.34 * math.sin(self.phase) + random.gauss(0.0, 0.008)),
                (0, 1, 0, 0.62 + 0.05 * math.sin(self.phase * 0.37) + random.gauss(0.0, 0.004)),
                (1, 3, 1, 2.18 + 0.18 * math.sin(self.phase * 0.71) + random.gauss(0.0, 0.006)),
            )

            for module, channel, resolution, voltage in samples:
                full_scale = (1 << RESOLUTION_BITS[resolution]) - 1
                raw = max(0, min(full_scale, round(voltage / 3.3 * full_scale)))
                self.output.put(ADCPacket(module, channel, resolution, 0, raw, now))

                if now >= next_stats:
                    self.output.put(ADCPacket(module, channel, resolution, 3, raw, now))
                    self.output.put(ADCPacket(module, channel, resolution, 4, raw, now))

            if now >= next_stats:
                next_stats = now + 0.15

            time.sleep(0.025)

        self.output.put(("disconnected", "Simulation"))

    def stop(self) -> None:
        self.stop_event.set()


class ChannelRow(ttk.Frame):
    def __init__(self, master: tk.Misc, state: ChannelState) -> None:
        super().__init__(master, style="Panel.TFrame", padding=(2, 3))
        self.state = state
        self.visible_var = tk.BooleanVar(value=state.visible)
        self.input_var = tk.StringVar(value=f"{state.input_voltage:.4g}")
        self.measured_var = tk.StringVar(value="--")

        ttk.Checkbutton(self, variable=self.visible_var, command=self._set_visibility).grid(row=0, column=0, padx=(0, 3))
        ttk.Label(self, text=MODULE_NAMES[state.module], style="Panel.TLabel", width=5).grid(row=0, column=1)
        ttk.Label(self, text=f"CH{state.channel}", style="Panel.TLabel", width=5).grid(row=0, column=2)
        ttk.Label(self, text=f"{state.resolution_bits}-bit", style="Panel.TLabel", width=6).grid(row=0, column=3)
        ttk.Label(self, textvariable=self.measured_var, style="Panel.TLabel", width=10).grid(row=0, column=4)
        input_entry = ttk.Entry(self, textvariable=self.input_var, width=8)
        input_entry.grid(row=0, column=5, padx=(5, 0))
        input_entry.bind("<Return>", self._set_input_voltage)
        input_entry.bind("<FocusOut>", self._set_input_voltage)

    def _set_visibility(self) -> None:
        self.state.visible = self.visible_var.get()

    def _set_input_voltage(self, _event: tk.Event | None = None) -> None:
        try:
            value = float(self.input_var.get())
            if value < 0.0:
                raise ValueError
            self.state.input_voltage = value
            self.state.max_deviation.clear()
        except ValueError:
            self.input_var.set(f"{self.state.input_voltage:.4g}")

    def refresh(self) -> None:
        self.measured_var.set(f"{self.state.last_voltage:.4f} V")


class GraphPanel(ttk.Frame):
    def __init__(self, master: tk.Misc, state: ChannelState, value_type: int) -> None:
        super().__init__(master, style="Panel.TFrame", padding=(10, 8))
        self.state = state
        self.value_type = value_type

        header = ttk.Frame(self, style="Panel.TFrame")
        header.pack(fill="x", pady=(0, 5))
        ttk.Label(header, text=VALUE_TYPE_NAMES[value_type], style="GraphTitle.TLabel").pack(side="left")
        self.summary = ttk.Label(header, text="", style="Muted.TLabel")
        self.summary.pack(side="right")

        self.canvas = tk.Canvas(
            self,
            height=190,
            background=BG,
            highlightthickness=1,
            highlightbackground=BORDER,
        )
        self.canvas.pack(fill="both", expand=True)

    def redraw(self, now: float, history_seconds: float) -> None:
        canvas = self.canvas
        width = max(canvas.winfo_width(), 400)
        height = max(canvas.winfo_height(), 190)
        canvas.delete("all")

        left, top, right, bottom = 58, 12, width - 15, height - 28
        plot_w, plot_h = right - left, bottom - top
        series = self.state.series.get(self.value_type, ())

        for index in range(5):
            y = top + (plot_h * index / 4)
            canvas.create_line(left, y, right, y, fill=GRID)
        for index in range(6):
            x = left + (plot_w * index / 5)
            canvas.create_line(x, top, x, bottom, fill=GRID)

        if not series:
            canvas.create_text(width / 2, height / 2, text="Waiting for data", fill=MUTED, font=("Segoe UI", 10))
            self.summary.configure(text=f"Input {self.state.input_voltage:.5f} V")
            return

        values = [value for _, value in series]
        values.append(self.state.input_voltage)
        value_min, value_max = min(values), max(values)
        padding = max((value_max - value_min) * 0.15, 0.02)
        value_min = max(0.0, value_min - padding)
        value_max += padding
        if value_max <= value_min:
            value_max = value_min + 0.1

        for index in range(5):
            value = value_max - ((value_max - value_min) * index / 4)
            y = top + (plot_h * index / 4)
            canvas.create_text(left - 7, y, text=f"{value:.3f}", fill=MUTED, anchor="e", font=("Consolas", 8))

        cutoff = now - history_seconds
        points: list[float] = []
        for timestamp, value in series:
            x = left + ((timestamp - cutoff) / history_seconds) * plot_w
            y = bottom - ((value - value_min) / (value_max - value_min)) * plot_h
            points.extend((x, y))
        if len(points) >= 4:
            canvas.create_line(*points, fill=VALUE_TYPE_COLORS[self.value_type], width=2, smooth=False)

        target_y = bottom - ((self.state.input_voltage - value_min) / (value_max - value_min)) * plot_h
        if top <= target_y <= bottom:
            canvas.create_line(left, target_y, right, target_y, fill="#ffffff", dash=(4, 5))

        canvas.create_text(left, bottom + 13, text=f"-{history_seconds:g}s", fill=MUTED, anchor="w", font=("Consolas", 8))
        canvas.create_text(right, bottom + 13, text="now", fill=MUTED, anchor="e", font=("Consolas", 8))
        average, variance, stddev, max_deviation = self.state.stats(self.value_type)
        self.summary.configure(
            text=(
                f"Input {self.state.input_voltage:.5f} V  |  Avg {average:.5f} V  |  "
                f"Std {stddev:.6f} V  |  Var {variance:.8f} V^2  |  Max dev {max_deviation:.5f} V"
            )
        )


class ChannelPanel(ttk.Frame):
    def __init__(self, master: tk.Misc, state: ChannelState) -> None:
        super().__init__(master, style="Panel.TFrame", padding=(10, 9))
        self.state = state
        self.graphs: dict[int, GraphPanel] = {}
        header = ttk.Frame(self, style="Panel.TFrame")
        header.pack(fill="x", pady=(0, 7))
        ttk.Label(
            header,
            text=f"{state.label}  |  {state.resolution_bits}-bit",
            style="GraphTitle.TLabel",
        ).pack(side="left")
        self.detail = ttk.Label(header, text="", style="Muted.TLabel")
        self.detail.pack(side="right")

    def add_graph(self, value_type: int) -> None:
        if value_type in self.graphs:
            return
        graph = GraphPanel(self, self.state, value_type)
        graph.pack(fill="x", expand=True, pady=(0, 8))
        self.graphs[value_type] = graph

    def redraw(self, now: float, history_seconds: float) -> None:
        self.detail.configure(
            text=f"Reference {self.state.reference_voltage:.4f} V  |  Input {self.state.input_voltage:.4f} V"
        )
        for graph in self.graphs.values():
            graph.redraw(now, history_seconds)


class ADCVisualizer(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("ADC UART Visualizer")
        self.geometry("1480x900")
        self.minsize(1120, 700)
        self.configure(background=BG)

        self.events: queue.Queue[object] = queue.Queue()
        self.worker: UARTWorker | SimulatorWorker | None = None
        self.channels: dict[tuple[int, int], ChannelState] = {}
        self.channel_rows: dict[tuple[int, int], ChannelRow] = {}
        self.channel_panels: dict[tuple[int, int], ChannelPanel] = {}
        self.configured_descriptors: set[tuple[int, int, int, int]] = set()
        self.module_reference_voltages = {0: 3.3, 1: 3.3}
        self.packet_count = 0
        self.last_rate_count = 0
        self.last_rate_time = time.monotonic()
        self.history_seconds = tk.DoubleVar(value=10.0)
        self.port_var = tk.StringVar()
        self.baud_var = tk.StringVar(value="115200")
        self.status_var = tk.StringVar(value="Disconnected")
        self.rate_var = tk.StringVar(value="0 packets/s")
        self.module_var = tk.StringVar()
        self.module_ref_var = tk.StringVar(value="3.300")
        self.auto_reconnect_enabled = False
        self.reconnect_job: str | None = None
        self.simulation_active = False

        self._configure_styles()
        self._build_ui()
        self._refresh_ports()
        self.after(30, self._process_events)
        self.after(100, self._redraw)
        self.protocol("WM_DELETE_WINDOW", self._on_close)

    def _configure_styles(self) -> None:
        style = ttk.Style(self)
        style.theme_use("clam")
        style.configure(".", background=BG, foreground=TEXT, fieldbackground=PANEL_2, bordercolor=BORDER)
        style.configure("TFrame", background=BG)
        style.configure("Panel.TFrame", background=PANEL)
        style.configure("TLabel", background=BG, foreground=TEXT, font=("Segoe UI", 9))
        style.configure("Panel.TLabel", background=PANEL, foreground=TEXT)
        style.configure("Title.TLabel", background=BG, foreground=TEXT, font=("Segoe UI Semibold", 18))
        style.configure("GraphTitle.TLabel", background=PANEL, foreground=TEXT, font=("Segoe UI Semibold", 11))
        style.configure("Muted.TLabel", background=PANEL, foreground=MUTED, font=("Segoe UI", 8))
        style.configure("Status.TLabel", background=BG, foreground=MUTED, font=("Segoe UI", 9))
        style.configure("TButton", background=PANEL_2, foreground=TEXT, padding=(10, 6), borderwidth=1)
        style.map("TButton", background=[("active", "#2a3542")])
        style.configure("Accent.TButton", background="#2376c9", foreground="#ffffff")
        style.map("Accent.TButton", background=[("active", "#2e8be4")])
        style.configure("Treeview", background=PANEL, foreground=TEXT, fieldbackground=PANEL, rowheight=25, borderwidth=0)
        style.configure("Treeview.Heading", background=PANEL_2, foreground=TEXT, relief="flat")
        style.map("Treeview", background=[("selected", "#294b69")])
        style.configure("TEntry", fieldbackground=PANEL_2, foreground=TEXT)
        style.configure("TCombobox", fieldbackground=PANEL_2, foreground=TEXT)

    def _build_ui(self) -> None:
        toolbar = ttk.Frame(self, padding=(14, 11))
        toolbar.pack(fill="x")
        ttk.Label(toolbar, text="ADC UART Monitor", style="Title.TLabel").pack(side="left", padx=(0, 22))

        ttk.Label(toolbar, text="Port").pack(side="left", padx=(0, 5))
        self.port_combo = ttk.Combobox(toolbar, textvariable=self.port_var, width=13, state="readonly")
        self.port_combo.pack(side="left")
        self.port_combo.bind("<<ComboboxSelected>>", self._schedule_auto_connect)
        ttk.Button(toolbar, text="Refresh", command=self._refresh_ports).pack(side="left", padx=5)

        ttk.Label(toolbar, text="Baud").pack(side="left", padx=(10, 5))
        baud_combo = ttk.Combobox(
            toolbar,
            textvariable=self.baud_var,
            width=10,
            values=("115200", "230400", "460800", "921600", "1000000", "2000000"),
            state="readonly",
        )
        baud_combo.pack(side="left")
        baud_combo.bind("<<ComboboxSelected>>", self._schedule_auto_connect)

        self.connect_button = ttk.Button(toolbar, text="Disconnect", command=self._stop_worker, state="disabled")
        self.connect_button.pack(side="left", padx=(10, 5))
        ttk.Button(toolbar, text="Simulate", command=self._toggle_simulation).pack(side="left", padx=5)

        ttk.Label(toolbar, text="History").pack(side="left", padx=(18, 5))
        ttk.Combobox(
            toolbar,
            textvariable=self.history_seconds,
            width=6,
            values=(2.0, 5.0, 10.0, 20.0, 30.0),
            state="readonly",
        ).pack(side="left")
        ttk.Label(toolbar, text="s").pack(side="left")

        ttk.Label(toolbar, textvariable=self.rate_var, style="Status.TLabel").pack(side="right")
        ttk.Label(toolbar, textvariable=self.status_var, style="Status.TLabel").pack(side="right", padx=(0, 20))

        divider = ttk.Separator(self)
        divider.pack(fill="x")

        body = ttk.Panedwindow(self, orient="horizontal")
        body.pack(fill="both", expand=True)

        sidebar = ttk.Frame(body, style="Panel.TFrame", padding=12)
        body.add(sidebar, weight=0)
        graph_host = ttk.Frame(body, padding=(10, 10, 12, 10))
        body.add(graph_host, weight=1)

        ttk.Label(sidebar, text="Channels", style="GraphTitle.TLabel").pack(anchor="w", pady=(0, 8))
        channel_header = ttk.Frame(sidebar, style="Panel.TFrame")
        channel_header.pack(fill="x")
        for text, width in (("Show", 5), ("Module", 6), ("CH", 5), ("Bits", 6), ("Measured", 10), ("Input V", 8)):
            ttk.Label(channel_header, text=text, style="Muted.TLabel", width=width).pack(side="left")

        channel_list_host = ttk.Frame(sidebar, style="Panel.TFrame")
        channel_list_host.pack(fill="x", pady=(2, 8))
        self.channel_canvas = tk.Canvas(channel_list_host, height=300, width=335, background=PANEL, highlightthickness=0)
        channel_scroll = ttk.Scrollbar(channel_list_host, orient="vertical", command=self.channel_canvas.yview)
        self.channel_canvas.configure(yscrollcommand=channel_scroll.set)
        channel_scroll.pack(side="right", fill="y")
        self.channel_canvas.pack(side="left", fill="both", expand=True)
        self.channel_rows_frame = ttk.Frame(self.channel_canvas, style="Panel.TFrame")
        self.channel_rows_window = self.channel_canvas.create_window((0, 0), window=self.channel_rows_frame, anchor="nw")
        self.channel_rows_frame.bind(
            "<Configure>",
            lambda _event: self.channel_canvas.configure(scrollregion=self.channel_canvas.bbox("all")),
        )
        self.channel_canvas.bind(
            "<Configure>",
            lambda event: self.channel_canvas.itemconfigure(self.channel_rows_window, width=event.width),
        )
        ttk.Button(sidebar, text="Clear measurements", command=self._clear_data).pack(fill="x", pady=(0, 12))

        ttk.Separator(sidebar).pack(fill="x", pady=(0, 13))
        ttk.Label(sidebar, text="ADC module reference", style="GraphTitle.TLabel").pack(anchor="w", pady=(0, 8))
        ref_row = ttk.Frame(sidebar, style="Panel.TFrame")
        ref_row.pack(fill="x")
        self.module_combo = ttk.Combobox(ref_row, textvariable=self.module_var, values=(), state="readonly", width=7)
        self.module_combo.pack(side="left")
        self.module_combo.bind("<<ComboboxSelected>>", self._load_module_reference)
        ttk.Entry(ref_row, textvariable=self.module_ref_var, width=10).pack(side="left", padx=6)
        ttk.Label(ref_row, text="V", style="Panel.TLabel").pack(side="left")
        ttk.Button(sidebar, text="Apply module reference", style="Accent.TButton", command=self._apply_module_reference).pack(fill="x", pady=(8, 8))

        ttk.Label(
            sidebar,
            text="Set each channel input voltage directly in its row. Dashed graph lines show that configured input. Statistics use the latest 250 ADC Value packets.",
            style="Muted.TLabel",
            justify="left",
            wraplength=270,
        ).pack(anchor="w", pady=(8, 0))

        self.graph_canvas = tk.Canvas(graph_host, background=BG, highlightthickness=0)
        graph_scroll = ttk.Scrollbar(graph_host, orient="vertical", command=self.graph_canvas.yview)
        self.graph_canvas.configure(yscrollcommand=graph_scroll.set)
        graph_scroll.pack(side="right", fill="y")
        self.graph_canvas.pack(side="left", fill="both", expand=True)
        self.graph_frame = ttk.Frame(self.graph_canvas)
        self.graph_window = self.graph_canvas.create_window((0, 0), window=self.graph_frame, anchor="nw")
        self.graph_frame.bind("<Configure>", self._update_graph_scrollregion)
        self.graph_canvas.bind("<Configure>", self._resize_graph_frame)
        self.graph_canvas.bind_all("<MouseWheel>", self._on_mousewheel)

    def _labeled_entry(self, parent: ttk.Frame, label: str, variable: tk.StringVar) -> None:
        ttk.Label(parent, text=label, style="Panel.TLabel").pack(anchor="w", pady=(4, 3))
        ttk.Entry(parent, textvariable=variable).pack(fill="x")

    def _update_graph_scrollregion(self, _event: tk.Event) -> None:
        self.graph_canvas.configure(scrollregion=self.graph_canvas.bbox("all"))

    def _resize_graph_frame(self, event: tk.Event) -> None:
        self.graph_canvas.itemconfigure(self.graph_window, width=event.width)

    def _on_mousewheel(self, event: tk.Event) -> None:
        self.graph_canvas.yview_scroll(int(-event.delta / 120), "units")

    def _refresh_ports(self) -> None:
        ports = [port.device for port in list_ports.comports()] if list_ports else []
        self.port_combo["values"] = ports
        if ports and self.port_var.get() not in ports:
            self.port_var.set(ports[0])
            self.auto_reconnect_enabled = True
            self.after(100, self._auto_connect)

    def _schedule_auto_connect(self, _event: tk.Event | None = None) -> None:
        self.auto_reconnect_enabled = True
        self.simulation_active = False
        self.after(100, self._auto_connect)

    def _auto_connect(self) -> None:
        self.reconnect_job = None
        if not self.auto_reconnect_enabled:
            return
        if not self.port_var.get():
            self._schedule_reconnect()
            return
        try:
            baud_rate = int(self.baud_var.get())
        except ValueError:
            messagebox.showerror("Baud rate", "Baud rate must be an integer.")
            return
        if self.worker:
            self._stop_worker(manual=False)
        self.worker = UARTWorker(self.port_var.get(), baud_rate, self.events)
        self.worker.start()
        self.status_var.set("Connecting...")

    def _toggle_simulation(self) -> None:
        if self.worker:
            self._stop_worker()
            return
        self.auto_reconnect_enabled = False
        self.simulation_active = True
        self.worker = SimulatorWorker(self.events)
        self.worker.start()

    def _stop_worker(self, manual: bool = True) -> None:
        if manual:
            self.auto_reconnect_enabled = False
            self.simulation_active = False
            if self.reconnect_job is not None:
                self.after_cancel(self.reconnect_job)
                self.reconnect_job = None
        if self.worker:
            worker = self.worker
            worker.stop()
            worker.join(timeout=0.5)
            self.worker = None
        try:
            while True:
                self.events.get_nowait()
        except queue.Empty:
            pass
        self.connect_button.configure(state="disabled")
        self.status_var.set("Disconnected")

    def _schedule_reconnect(self) -> None:
        if not self.auto_reconnect_enabled or self.reconnect_job is not None:
            return
        self.status_var.set("Disconnected - reconnecting...")
        self.reconnect_job = self.after(RECONNECT_DELAY_MS, self._auto_connect)

    def _process_events(self) -> None:
        processed = 0
        try:
            while processed < MAX_EVENTS_PER_UI_TICK:
                event = self.events.get_nowait()
                processed += 1
                if isinstance(event, ADCDescriptor):
                    self._accept_descriptor(event)
                elif isinstance(event, ADCPacket):
                    self._accept_packet(event)
                elif event[0] == "connected":
                    self._reset_configuration()
                    self.status_var.set(f"Reading startup configuration: {event[1]}")
                    self.connect_button.configure(state="normal")
                elif event[0] == "configured":
                    self.status_var.set(f"Streaming measurements: {event[1]}")
                elif event[0] == "disconnected":
                    self.status_var.set("Disconnected")
                    self.connect_button.configure(state="disabled")
                    self.worker = None
                    if not self.simulation_active:
                        self._schedule_reconnect()
                elif event[0] == "error":
                    self.status_var.set(f"Connection lost: {event[1]}")
                    self.worker = None
                    self.connect_button.configure(state="disabled")
                    self._schedule_reconnect()
        except queue.Empty:
            pass
        self.after(1 if processed == MAX_EVENTS_PER_UI_TICK else 30, self._process_events)

    def _accept_descriptor(self, descriptor: ADCDescriptor) -> None:
        self.configured_descriptors.add(
            (descriptor.module, descriptor.channel, descriptor.resolution, descriptor.value_type)
        )
        state = self._get_or_create_channel(descriptor.module, descriptor.channel, descriptor.resolution)
        state.configured_value_types.add(descriptor.value_type)
        self.channel_panels[state.key].add_graph(descriptor.value_type)

    def _accept_packet(self, packet: ADCPacket) -> None:
        descriptor = (packet.module, packet.channel, packet.resolution, packet.value_type)
        if descriptor not in self.configured_descriptors:
            return
        state = self._get_or_create_channel(packet.module, packet.channel, packet.resolution)
        state.configured_value_types.add(packet.value_type)

        state.update(packet, max(1.0, float(self.history_seconds.get())))
        self.packet_count += 1
        self.channel_panels[state.key].add_graph(packet.value_type)

    def _get_or_create_channel(self, module: int, channel: int, resolution: int) -> ChannelState:
        key = module, channel
        state = self.channels.get(key)
        if state is not None:
            state.resolution = resolution
            return state
        state = ChannelState(
            module=module,
            channel=channel,
            resolution=resolution,
            reference_voltage=self.module_reference_voltages[module],
            color=CHANNEL_COLORS[len(self.channels) % len(CHANNEL_COLORS)],
        )
        self.channels[key] = state
        row = ChannelRow(self.channel_rows_frame, state)
        row.pack(fill="x")
        self.channel_rows[key] = row
        panel = ChannelPanel(self.graph_frame, state)
        panel.pack(fill="x", expand=True, pady=(0, 12))
        self.channel_panels[key] = panel
        self._refresh_module_choices()
        return state

    def _refresh_module_choices(self) -> None:
        values = tuple(MODULE_NAMES[module] for module in sorted({state.module for state in self.channels.values()}))
        self.module_combo["values"] = values
        if values and self.module_var.get() not in values:
            self.module_var.set(values[0])
            self._load_module_reference()

    def _reset_configuration(self) -> None:
        for row in self.channel_rows.values():
            row.destroy()
        for panel in self.channel_panels.values():
            panel.destroy()
        self.channels.clear()
        self.channel_rows.clear()
        self.channel_panels.clear()
        self.configured_descriptors.clear()
        self.module_combo["values"] = ()
        self.module_var.set("")
        self.packet_count = 0
        self.last_rate_count = 0

    def _load_module_reference(self, _event: tk.Event | None = None) -> None:
        if not self.module_var.get():
            return
        module = 0 if self.module_var.get() == "ADC0" else 1
        self.module_ref_var.set(f"{self.module_reference_voltages[module]:.6g}")

    def _apply_module_reference(self) -> None:
        if not self.module_var.get():
            messagebox.showinfo("Module reference", "No ADC module is configured.")
            return
        try:
            reference = float(self.module_ref_var.get())
            if reference <= 0.0:
                raise ValueError
        except ValueError:
            messagebox.showerror("Module reference", "Reference voltage must be positive.")
            return
        module = 0 if self.module_var.get() == "ADC0" else 1
        old_reference = self.module_reference_voltages[module]
        self.module_reference_voltages[module] = reference
        scale = reference / old_reference
        for state in self.channels.values():
            if state.module != module:
                continue
            state.reference_voltage = reference
            state.last_voltage *= scale
            for value_type, series in state.series.items():
                state.series[value_type] = deque((timestamp, value * scale) for timestamp, value in series)
                state.max_deviation[value_type] = max(
                    (abs(value - state.input_voltage) for _, value in state.series[value_type]),
                    default=0.0,
                )

    def _clear_data(self) -> None:
        for state in self.channels.values():
            state.series.clear()
            state.max_deviation.clear()
        self.packet_count = 0

    def _redraw(self) -> None:
        now = time.monotonic()
        history = max(1.0, float(self.history_seconds.get()))
        for key, row in self.channel_rows.items():
            row.refresh()
            panel = self.channel_panels[key]
            if row.state.visible:
                if not panel.winfo_manager():
                    panel.pack(fill="x", expand=True, pady=(0, 12))
                panel.redraw(now, history)
            elif panel.winfo_manager():
                panel.pack_forget()

        elapsed = now - self.last_rate_time
        if elapsed >= 0.5:
            rate = (self.packet_count - self.last_rate_count) / elapsed
            self.rate_var.set(f"{rate:,.0f} packets/s  |  {self.packet_count:,} total")
            self.last_rate_count = self.packet_count
            self.last_rate_time = now

        self.after(100, self._redraw)

    def _on_close(self) -> None:
        self._stop_worker()
        self.destroy()


def main() -> None:
    app = ADCVisualizer()
    app.mainloop()


if __name__ == "__main__":
    main()
