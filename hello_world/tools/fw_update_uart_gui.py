#!/usr/bin/env python3
"""PC UART operations tool.

The GUI provides separate operation views for the UART-to-CAN firmware bridge
and for transferring files into the MCU's local LittleFS storage.
"""

from __future__ import annotations

import queue
import struct
import threading
import time
import tkinter as tk
from dataclasses import dataclass
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

try:
    import serial
except ImportError:  # pragma: no cover - dependency is checked at runtime
    serial = None


# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------

DEFAULT_COM_PORT = "COM9"
DEFAULT_BAUD_RATE = 115200
DEFAULT_IMAGE = r".\hello_world\debug\zephyr\zephyr.signed.bin"

HW_REV = 0x01
LIB_REV = 0x0001
FW_REV = 0x0001

ACK_TIMEOUT_S = 2.0
WRITE_TIMEOUT_S = 2.0
START_DELAY_S = 3.0
INTER_FRAME_DELAY_S = 0.05
TX_BYTE_DELAY_S = 0.001
PROBE_TIMEOUT_S = 8.0
PROBE_RETRY_DELAY_S = 0.25
PROBE_WRITE_RETRY_COUNT = 3
MAX_AUTO_RECOVERY_ROUNDS = 5
MAX_LOST_PACKET_INFO_PAGES = 64


# ---------------------------------------------------------------------------
# Bootloader protocol values
# ---------------------------------------------------------------------------

CMD_FW_UP_REQ = 20
CMD_FW_UP_MSG = 21
CMD_FW_UP_END = 22
CMD_GET_LOST_PACKET_INFO = 25
CMD_RET_LOST_PACKET_INFO = 26
VALID_BOOTLOADER_REPLY_COMMANDS = {
    CMD_FW_UP_REQ,
    CMD_FW_UP_MSG,
    CMD_FW_UP_END,
    CMD_GET_LOST_PACKET_INFO,
    CMD_RET_LOST_PACKET_INFO,
}

BOOTLOADER_ACK = 10
BOOTLOADER_NACK = 11
BOOTLOADER_ERROR_MISSING_PACKETS_PENDING = 17

FW_UP_MSG_PAYLOAD_LEN = 50
FW_UP_MSG_DATA_LEN = 48
MAX_FW_SIZE_24BIT = 0xFFFFFF


# ---------------------------------------------------------------------------
# UART bridge framing
# ---------------------------------------------------------------------------

UART_SOF = b"BLUP"
UART_VERSION = 1
UART_ACK = 0x06
UART_NACK = 0x15

CMD_SELECT_OPERATION = 0x01
CMD_RELEASE_OPERATION = 0x02
CMD_BULK_START = 0x40
CMD_BULK_DATA = 0x41
CMD_BULK_END = 0x42

OPERATION_FIRMWARE_BRIDGE = 1
OPERATION_LOCAL_BULK_FILE = 2

FILE_TYPE_DATA = 2
FILE_TYPE_IMAGE = 3
FILE_TYPE_ICON = 4
FILE_TYPE_IDS = {
    "Data": FILE_TYPE_DATA,
    "Image": FILE_TYPE_IMAGE,
    "Icon": FILE_TYPE_ICON,
}

BULK_DATA_LENGTH = 256
MAX_BULK_FILE_NAME_BYTES = 29


@dataclass(frozen=True)
class FirmwarePacket:
    packet_id: int
    offset: int
    valid_len: int
    payload: bytes


def crc16_ccitt(data: bytes, initial: int = 0xFFFF) -> int:
    crc = initial
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def build_uart_frame(command: int, sequence: int, payload: bytes) -> bytes:
    header = UART_SOF + struct.pack(">BBHH", UART_VERSION, command, sequence, len(payload))
    frame_crc = crc16_ccitt(header[4:] + payload)
    return header + payload + struct.pack(">H", frame_crc)


def build_fw_update_request(image: bytes) -> bytes:
    image_size = len(image)
    image_crc = crc16_ccitt(image)

    if image_size == 0:
        raise ValueError("Firmware image is empty")
    if image_size > MAX_FW_SIZE_24BIT:
        raise ValueError(f"Firmware image is too large for 24-bit size field: {image_size} bytes")

    return bytes(
        [
            (image_size >> 16) & 0xFF,
            (image_size >> 8) & 0xFF,
            image_size & 0xFF,
            HW_REV & 0xFF,
            (LIB_REV >> 8) & 0xFF,
            LIB_REV & 0xFF,
            (FW_REV >> 8) & 0xFF,
            FW_REV & 0xFF,
            (image_crc >> 8) & 0xFF,
            image_crc & 0xFF,
        ]
    )


def build_fw_packets(image: bytes) -> list[FirmwarePacket]:
    packets: list[FirmwarePacket] = []

    for packet_id, offset in enumerate(range(0, len(image), FW_UP_MSG_DATA_LEN)):
        chunk = image[offset : offset + FW_UP_MSG_DATA_LEN]
        valid_len = len(chunk)

        if len(chunk) < FW_UP_MSG_DATA_LEN:
            chunk = chunk + bytes([0xFF] * (FW_UP_MSG_DATA_LEN - len(chunk)))

        payload = struct.pack(">H", packet_id) + chunk
        if len(payload) != FW_UP_MSG_PAYLOAD_LEN:
            raise RuntimeError("Internal packet formatting error")

        packets.append(FirmwarePacket(packet_id, offset, valid_len, payload))

    return packets


class BridgeClient:
    def __init__(
        self,
        port_name: str,
        baud_rate: int,
        log_queue: queue.Queue[object],
        start_delay_s: float,
        inter_frame_delay_s: float,
        byte_delay_s: float,
        log_target: str = "firmware",
    ):
        if serial is None:
            raise RuntimeError("Missing dependency: pyserial. Install with: python -m pip install pyserial")

        self.log_queue = log_queue
        self.log_target = log_target
        self.sequence = 0
        self.selected_operation: int | None = None
        self.inter_frame_delay_s = inter_frame_delay_s
        self.byte_delay_s = byte_delay_s
        self.port = serial.Serial(
            port_name,
            baud_rate,
            timeout=ACK_TIMEOUT_S,
            write_timeout=WRITE_TIMEOUT_S,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False,
        )
        time.sleep(start_delay_s)
        self.port.reset_input_buffer()
        self.port.reset_output_buffer()

    def close(self) -> None:
        if self.port and self.port.is_open:
            if self.selected_operation is not None:
                try:
                    self.release_operation()
                except Exception as exc:
                    self._log(f"Could not release PC UART operation during close: {exc}", "bridge")
            self.port.close()

    def _log(self, text: str, category: str = "info") -> None:
        if self.log_target == "file":
            self.log_queue.put(("file_log", category, text))
        else:
            self.log_queue.put((category, text))

    def _read_exact(self, length: int, timeout_s: float) -> bytes:
        deadline = time.monotonic() + timeout_s
        data = bytearray()

        while len(data) < length and time.monotonic() < deadline:
            chunk = self.port.read(length - len(data))
            if chunk:
                data.extend(chunk)

        return bytes(data)

    def _read_uart_frame_after_sof_first_byte(self, timeout_s: float) -> tuple[int, int, bytes] | None:
        sof_tail = self._read_exact(3, timeout_s)
        if sof_tail != b"LUP":
            self._log(f"UART text/noise: B{sof_tail.decode(errors='replace')}", "bridge")
            return None

        header_tail = self._read_exact(6, timeout_s)
        if len(header_tail) != 6:
            self._log("UART frame timeout while reading header", "bridge")
            return None

        version, command, seq_hi, seq_lo, len_hi, len_lo = header_tail
        if version != UART_VERSION:
            self._log(f"UART frame invalid version: {version}", "bridge")
            return None

        sequence = (seq_hi << 8) | seq_lo
        payload_len = (len_hi << 8) | len_lo
        payload = self._read_exact(payload_len, timeout_s)
        crc_bytes = self._read_exact(2, timeout_s)
        if len(payload) != payload_len or len(crc_bytes) != 2:
            self._log("UART frame timeout while reading payload/CRC", "bridge")
            return None

        received_crc = (crc_bytes[0] << 8) | crc_bytes[1]
        calculated_crc = crc16_ccitt(header_tail + payload)
        if received_crc != calculated_crc:
            self._log(f"UART frame CRC mismatch: rx=0x{received_crc:04X}, calc=0x{calculated_crc:04X}", "bridge")
            return None

        self._log_slave_frame(command, sequence, payload)
        return command, sequence, payload

    def _log_slave_frame(self, command: int, sequence: int, payload: bytes) -> None:
        if command not in VALID_BOOTLOADER_REPLY_COMMANDS:
            payload_text = " ".join(f"{byte:02X}" for byte in payload)
            self._log(
                f"Ignored non-bootloader CAN frame: cmd={command}, seq={sequence}, payload=[{payload_text}]",
                "bridge",
            )
            return

        if len(payload) >= 2 and payload[0] in (BOOTLOADER_ACK, BOOTLOADER_NACK):
            status = "ACK" if payload[0] == BOOTLOADER_ACK else "NACK"
            category = "slave" if payload[0] == BOOTLOADER_ACK else "error"
            self._log(f"Slave {status}: cmd={command}, seq={sequence}, error={payload[1]}", category)
            return

        if command == CMD_RET_LOST_PACKET_INFO and len(payload) >= 3:
            self._log(
                f"Slave lost-packet info: pending={payload[0]}, frame_seq={payload[1]}, info_bytes={payload[2]}",
                "recovery",
            )
            return

        payload_text = " ".join(f"{byte:02X}" for byte in payload)
        self._log(f"Slave frame: cmd={command}, seq={sequence}, payload=[{payload_text}]", "slave")

    def _read_bridge_response(self, command: int, sequence: int) -> tuple[int, int]:
        deadline = time.monotonic() + ACK_TIMEOUT_S

        while time.monotonic() < deadline:
            first = self.port.read(1)
            if not first:
                continue

            status = first[0]
            if status in (UART_ACK, UART_NACK):
                error = self._read_exact(1, ACK_TIMEOUT_S)
                if len(error) != 1:
                    raise TimeoutError(f"Timeout waiting for bridge error byte: cmd={command}, seq={sequence}")
                return status, error[0]

            if first == b"B":
                self._read_uart_frame_after_sof_first_byte(ACK_TIMEOUT_S)
                continue

            if 32 <= status <= 126:
                self._log(f"UART text/noise: {chr(status)}", "bridge")
            else:
                self._log(f"UART byte/noise: 0x{status:02X}", "bridge")

        raise TimeoutError(f"Timeout waiting for bridge ACK: cmd={command}, seq={sequence}")

    def wait_for_slave_frame(self, expected_command: int, timeout_s: float = 3.0) -> tuple[int, int, bytes] | None:
        deadline = time.monotonic() + timeout_s

        while time.monotonic() < deadline:
            first = self.port.read(1)
            if not first:
                continue

            if first == b"B":
                frame = self._read_uart_frame_after_sof_first_byte(timeout_s)
                if frame is not None and frame[0] == expected_command:
                    return frame
                continue

            byte = first[0]
            if byte in (UART_ACK, UART_NACK):
                error = self._read_exact(1, ACK_TIMEOUT_S)
                if len(error) == 1:
                    category = "bridge" if byte == UART_ACK else "error"
                    self._log(f"Bridge {'ACK' if byte == UART_ACK else 'NACK'} while waiting for slave frame: error={error[0]}", category)
                continue

            if 32 <= byte <= 126:
                self._log(f"UART text/noise: {chr(byte)}", "bridge")
            else:
                self._log(f"UART byte/noise: 0x{byte:02X}", "bridge")

        return None

    def send_pc_uart_command(self, command: int, payload: bytes = b"") -> tuple[int, int]:
        sequence = self.sequence
        self.sequence = (self.sequence + 1) & 0xFFFF

        frame = build_uart_frame(command, sequence, payload)
        self.write_raw(frame)

        status, error_code = self._read_bridge_response(command, sequence)
        if status == UART_ACK:
            if self.inter_frame_delay_s > 0:
                time.sleep(self.inter_frame_delay_s)
            return status, error_code

        if status == UART_NACK:
            raise RuntimeError(f"Bridge NACK: cmd={command}, seq={sequence}, error={error_code}")

        raise RuntimeError(f"Unexpected bridge response 0x{status:02X}: cmd={command}, seq={sequence}")

    def send_bootloader_frame(self, command: int, payload: bytes = b"") -> tuple[int, int]:
        if self.selected_operation != OPERATION_FIRMWARE_BRIDGE:
            raise RuntimeError("Firmware Bridge operation is not selected")
        return self.send_pc_uart_command(command, payload)

    def select_operation(self, operation: int) -> None:
        self.send_pc_uart_command(CMD_SELECT_OPERATION, bytes([operation]))
        self.selected_operation = operation
        self._log(f"Selected PC UART operation {operation}", "bridge")

    def release_operation(self) -> None:
        self.send_pc_uart_command(CMD_RELEASE_OPERATION)
        self.selected_operation = None
        self._log("Released PC UART operation", "bridge")

    def send_bulk_start(self, file_type: int, file_name: str, file_data: bytes) -> None:
        encoded_name = file_name.encode("utf-8")
        if not encoded_name or len(encoded_name) > MAX_BULK_FILE_NAME_BYTES:
            raise ValueError(
                f"File name must contain 1 to {MAX_BULK_FILE_NAME_BYTES} UTF-8 bytes"
            )
        if not file_data:
            raise ValueError("Cannot transfer an empty file")
        if len(file_data) > 0xFFFFFFFF:
            raise ValueError("File is too large for the 32-bit bulk-transfer size field")
        if file_type not in FILE_TYPE_IDS.values():
            raise ValueError(f"Unsupported file type: {file_type}")
        if self.selected_operation != OPERATION_LOCAL_BULK_FILE:
            raise RuntimeError("Local Bulk File operation is not selected")

        payload = struct.pack(
            ">BIHB",
            file_type,
            len(file_data),
            crc16_ccitt(file_data),
            len(encoded_name),
        ) + encoded_name
        self.send_pc_uart_command(CMD_BULK_START, payload)

    def send_bulk_data(self, frame_id: int, data: bytes) -> None:
        if self.selected_operation != OPERATION_LOCAL_BULK_FILE:
            raise RuntimeError("Local Bulk File operation is not selected")
        if not data or len(data) > BULK_DATA_LENGTH:
            raise ValueError(f"Bulk frame data length must be 1 to {BULK_DATA_LENGTH} bytes")

        payload = struct.pack(">IHH", frame_id, len(data), crc16_ccitt(data)) + data
        self.send_pc_uart_command(CMD_BULK_DATA, payload)

    def send_bulk_end(self) -> None:
        if self.selected_operation != OPERATION_LOCAL_BULK_FILE:
            raise RuntimeError("Local Bulk File operation is not selected")
        self.send_pc_uart_command(CMD_BULK_END)
        # The MCU returns its hub to Idle after finalizing a bulk transfer.
        self.selected_operation = None

    def write_raw(self, data: bytes) -> None:
        try:
            self._write_raw_once(data)
        except serial.SerialTimeoutException as exc:
            self._log("Serial TX stalled while writing to bridge; reset the bridge board if this repeats", "error")
            try:
                self.port.reset_output_buffer()
            except Exception:
                pass
            raise TimeoutError("Serial write timeout") from exc

    def _write_raw_once(self, data: bytes) -> None:
        if self.byte_delay_s <= 0:
            written = self.port.write(data)
            if written != len(data):
                raise TimeoutError(f"Serial short write: {written}/{len(data)} bytes")
        else:
            for byte in data:
                written = self.port.write(bytes([byte]))
                if written != 1:
                    raise TimeoutError("Serial short write while sending byte")
                time.sleep(self.byte_delay_s)
        self.port.flush()

    def _write_probe_byte(self) -> bool:
        for _attempt in range(PROBE_WRITE_RETRY_COUNT):
            try:
                self.port.write(b"?")
                self.port.flush()
                return True
            except serial.SerialTimeoutException:
                self._log("Probe write timeout; retrying", "bridge")
                try:
                    self.port.reset_output_buffer()
                except Exception:
                    pass
                time.sleep(PROBE_RETRY_DELAY_S)

        return False

    def probe(self) -> bool:
        deadline = time.monotonic() + PROBE_TIMEOUT_S
        next_probe_time = 0.0

        while time.monotonic() < deadline:
            now = time.monotonic()
            if now >= next_probe_time:
                if not self._write_probe_byte():
                    return False
                next_probe_time = now + PROBE_RETRY_DELAY_S

            response = self.port.read(1)
            if not response:
                continue
            if response == b"!":
                return True
            byte = response[0]
            if 32 <= byte <= 126:
                self._log(f"UART text/noise during probe: {chr(byte)}", "bridge")
            else:
                self._log(f"UART byte/noise during probe: 0x{byte:02X}", "bridge")

        return False

    def enable_debug_next_frame(self) -> bool:
        self.write_raw(b"D")
        return self.port.read(1) == b"d"

    def read_debug_bytes(self, length: int = 32) -> bytes:
        return self.port.read(length)


class FirmwareUpdateGui:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title("PC UART Operations")
        self.root.geometry("1180x780")
        self.root.minsize(980, 680)

        self.image_path = tk.StringVar(value=DEFAULT_IMAGE)
        self.file_transfer_path = tk.StringVar()
        self.file_transfer_type = tk.StringVar(value="Image")
        self.port_name = tk.StringVar(value=DEFAULT_COM_PORT)
        self.baud_rate = tk.StringVar(value=str(DEFAULT_BAUD_RATE))
        self.start_delay = tk.StringVar(value=str(START_DELAY_S))
        self.inter_frame_delay = tk.StringVar(value=str(INTER_FRAME_DELAY_S))
        self.byte_delay = tk.StringVar(value=str(TX_BYTE_DELAY_S))
        self.status_text = tk.StringVar(value="Select a signed firmware image.")
        self.file_transfer_status = tk.StringVar(value="Select a file to transfer.")
        self.file_transfer_progress = tk.DoubleVar(value=0.0)
        self.file_transfer_percent = tk.StringVar(value="0%")

        self.image: bytes = b""
        self.packets: list[FirmwarePacket] = []
        self.selected_missing: set[int] = set()
        self.file_transfer_data: bytes = b""
        self.file_transfer_source: Path | None = None
        self.log_queue: queue.Queue[object] = queue.Queue()
        self.worker: threading.Thread | None = None
        self.operation_frames: dict[str, ttk.Frame] = {}
        self.operation_buttons: dict[str, ttk.Button] = {}

        self._build_layout()
        self.root.after(100, self._drain_log_queue)

    def _build_layout(self) -> None:
        connection = ttk.LabelFrame(self.root, text="UART Connection", padding=(10, 6))
        connection.pack(fill=tk.X, padx=10, pady=(10, 6))

        ttk.Label(connection, text="Port").grid(row=0, column=0, sticky=tk.W)
        ttk.Entry(connection, textvariable=self.port_name, width=12).grid(row=0, column=1, padx=(5, 18))
        ttk.Label(connection, text="Baud").grid(row=0, column=2, sticky=tk.W)
        ttk.Entry(connection, textvariable=self.baud_rate, width=10).grid(row=0, column=3, padx=(5, 18))
        ttk.Label(connection, text="Start delay (s)").grid(row=0, column=4, sticky=tk.W)
        ttk.Entry(connection, textvariable=self.start_delay, width=8).grid(row=0, column=5, padx=(5, 18))
        ttk.Label(connection, text="Frame delay (s)").grid(row=0, column=6, sticky=tk.W)
        ttk.Entry(connection, textvariable=self.inter_frame_delay, width=8).grid(row=0, column=7, padx=(5, 18))
        ttk.Label(connection, text="Byte delay (s)").grid(row=0, column=8, sticky=tk.W)
        ttk.Entry(connection, textvariable=self.byte_delay, width=8).grid(row=0, column=9, padx=(5, 0))

        body = ttk.Frame(self.root, padding=(10, 0, 10, 10))
        body.pack(fill=tk.BOTH, expand=True)
        body.columnconfigure(1, weight=1)
        body.rowconfigure(0, weight=1)

        navigation = ttk.LabelFrame(body, text="Operations", padding=8)
        navigation.grid(row=0, column=0, sticky=tk.NS, padx=(0, 8))

        firmware_button = ttk.Button(
            navigation,
            text="Firmware Update",
            width=20,
            command=lambda: self._show_operation_view("firmware"),
        )
        firmware_button.pack(fill=tk.X, pady=(0, 6))
        file_button = ttk.Button(
            navigation,
            text="File Transfer",
            width=20,
            command=lambda: self._show_operation_view("file"),
        )
        file_button.pack(fill=tk.X)
        self.operation_buttons = {"firmware": firmware_button, "file": file_button}

        content = ttk.Frame(body)
        content.grid(row=0, column=1, sticky=tk.NSEW)
        content.columnconfigure(0, weight=1)
        content.rowconfigure(0, weight=1)

        firmware_view = ttk.Frame(content)
        file_view = ttk.Frame(content)
        for view in (firmware_view, file_view):
            view.grid(row=0, column=0, sticky=tk.NSEW)
        self.operation_frames = {"firmware": firmware_view, "file": file_view}

        self._build_firmware_view(firmware_view)
        self._build_file_transfer_view(file_view)
        self._show_operation_view("firmware")

    def _show_operation_view(self, operation: str) -> None:
        self.operation_frames[operation].tkraise()
        for name, button in self.operation_buttons.items():
            button.state(["disabled"] if name == operation else ["!disabled"])

    def _build_firmware_view(self, parent: ttk.Frame) -> None:
        top = ttk.LabelFrame(parent, text="Firmware Image", padding=8)
        top.pack(fill=tk.X)

        ttk.Label(top, text="Image").grid(row=0, column=0, sticky=tk.W)
        ttk.Entry(top, textvariable=self.image_path).grid(row=0, column=1, sticky=tk.EW, padx=6)
        ttk.Button(top, text="Browse", command=self.browse_image).grid(row=0, column=2, padx=4)
        ttk.Button(top, text="Load", command=self.load_image).grid(row=0, column=3, padx=4)
        top.columnconfigure(1, weight=1)

        buttons = ttk.Frame(parent, padding=(0, 8, 0, 6))
        buttons.pack(fill=tk.X)
        ttk.Button(buttons, text="Send Update", command=self.send_update).pack(side=tk.LEFT, padx=(0, 6))
        ttk.Button(buttons, text="Clear Messages", command=self.clear_messages).pack(side=tk.LEFT, padx=6)
        ttk.Button(buttons, text="Clear Selection", command=self.clear_selection).pack(side=tk.LEFT, padx=6)

        ttk.Label(parent, textvariable=self.status_text, padding=(0, 0, 0, 4)).pack(fill=tk.X)

        main = ttk.PanedWindow(parent, orient=tk.HORIZONTAL)
        main.pack(fill=tk.BOTH, expand=True)

        left = ttk.Frame(main)
        right = ttk.Frame(main)
        main.add(left, weight=3)
        main.add(right, weight=2)

        ttk.Label(left, text="CAN Firmware Data Frames - selected rows are treated as missing frames").pack(anchor=tk.W)

        columns = ("missing", "packet_id", "offset", "length")
        self.packet_tree = ttk.Treeview(left, columns=columns, show="headings", selectmode="browse")
        self.packet_tree.heading("missing", text="Missing")
        self.packet_tree.heading("packet_id", text="Packet ID")
        self.packet_tree.heading("offset", text="Image Offset")
        self.packet_tree.heading("length", text="Bytes")
        self.packet_tree.column("missing", width=80, anchor=tk.CENTER)
        self.packet_tree.column("packet_id", width=100, anchor=tk.E)
        self.packet_tree.column("offset", width=120, anchor=tk.E)
        self.packet_tree.column("length", width=80, anchor=tk.E)
        self.packet_tree.pack(fill=tk.BOTH, expand=True)
        self.packet_tree.bind("<Double-1>", self.toggle_selected_packet)
        self.packet_tree.bind("<space>", self.toggle_selected_packet)

        frame_scroll = ttk.Scrollbar(left, orient=tk.VERTICAL, command=self.packet_tree.yview)
        self.packet_tree.configure(yscrollcommand=frame_scroll.set)
        frame_scroll.place(relx=1.0, rely=0.04, relheight=0.96, anchor=tk.NE)

        ttk.Label(right, text="Slave / Bridge Messages").pack(anchor=tk.W)
        self.log = tk.Text(right, height=20, wrap=tk.WORD, state=tk.DISABLED)
        self.log.tag_configure("info", foreground="#222222")
        self.log.tag_configure("bridge", foreground="#7A4D00")
        self.log.tag_configure("slave", foreground="#155EEF")
        self.log.tag_configure("recovery", foreground="#16803C")
        self.log.tag_configure("error", foreground="#B42318")
        self.log.pack(fill=tk.BOTH, expand=True)

    def _build_file_transfer_view(self, parent: ttk.Frame) -> None:
        selection = ttk.LabelFrame(parent, text="Local File Transfer", padding=10)
        selection.pack(fill=tk.X)
        selection.columnconfigure(1, weight=1)

        ttk.Label(selection, text="File type").grid(row=0, column=0, sticky=tk.W, pady=(0, 8))
        type_box = ttk.Combobox(
            selection,
            textvariable=self.file_transfer_type,
            values=tuple(FILE_TYPE_IDS),
            state="readonly",
            width=16,
        )
        type_box.grid(row=0, column=1, sticky=tk.W, padx=8, pady=(0, 8))

        ttk.Label(selection, text="File").grid(row=1, column=0, sticky=tk.W)
        ttk.Entry(selection, textvariable=self.file_transfer_path).grid(
            row=1, column=1, sticky=tk.EW, padx=8
        )
        ttk.Button(selection, text="Browse", command=self.browse_transfer_file).grid(row=1, column=2, padx=4)
        ttk.Button(selection, text="Load", command=self.load_transfer_file).grid(row=1, column=3, padx=4)

        actions = ttk.Frame(parent, padding=(0, 10, 0, 8))
        actions.pack(fill=tk.X)
        ttk.Button(actions, text="Send File", command=self.send_transfer_file).pack(side=tk.LEFT)
        ttk.Button(actions, text="Clear Messages", command=self.clear_file_messages).pack(side=tk.LEFT, padx=8)

        ttk.Label(parent, textvariable=self.file_transfer_status).pack(fill=tk.X, pady=(0, 6))

        progress_row = ttk.Frame(parent)
        progress_row.pack(fill=tk.X, pady=(0, 10))
        progress_row.columnconfigure(0, weight=1)
        ttk.Progressbar(
            progress_row,
            variable=self.file_transfer_progress,
            maximum=100.0,
            mode="determinate",
        ).grid(row=0, column=0, sticky=tk.EW)
        ttk.Label(progress_row, textvariable=self.file_transfer_percent, width=8, anchor=tk.E).grid(
            row=0, column=1, padx=(8, 0)
        )

        ttk.Label(parent, text="File Transfer Messages").pack(anchor=tk.W)
        self.file_log = tk.Text(parent, height=20, wrap=tk.WORD, state=tk.DISABLED)
        self.file_log.tag_configure("info", foreground="#222222")
        self.file_log.tag_configure("bridge", foreground="#7A4D00")
        self.file_log.tag_configure("slave", foreground="#155EEF")
        self.file_log.tag_configure("recovery", foreground="#16803C")
        self.file_log.tag_configure("error", foreground="#B42318")
        self.file_log.pack(fill=tk.BOTH, expand=True)

    def browse_image(self) -> None:
        path = filedialog.askopenfilename(
            title="Select signed firmware image",
            filetypes=[
                ("Signed firmware binaries", "*.bin"),
                ("All files", "*.*"),
            ],
        )
        if path:
            self.image_path.set(path)
            self.load_image()

    def resolve_image_path(self) -> Path:
        path = Path(self.image_path.get())
        if path.is_absolute():
            return path.resolve()

        cwd_path = (Path.cwd() / path).resolve()
        if cwd_path.exists():
            return cwd_path

        script_path = (Path(__file__).resolve().parent / path).resolve()
        return script_path

    def load_image(self) -> None:
        try:
            image_path = self.resolve_image_path()
            self.image = image_path.read_bytes()
            self.packets = build_fw_packets(self.image)
            self.selected_missing.clear()
            self.populate_packets()
            self.status_text.set(
                f"Loaded {image_path} | {len(self.image)} bytes | CRC 0x{crc16_ccitt(self.image):04X} | {len(self.packets)} CAN data frames"
            )
            self.append_log(f"Loaded image: {image_path}", "info")
            self.append_log(f"Image size: {len(self.image)} bytes, CRC16: 0x{crc16_ccitt(self.image):04X}", "info")
        except Exception as exc:
            messagebox.showerror("Load failed", str(exc))

    def populate_packets(self) -> None:
        self.packet_tree.delete(*self.packet_tree.get_children())
        for packet in self.packets:
            self.packet_tree.insert(
                "",
                tk.END,
                iid=str(packet.packet_id),
                values=(
                    "yes" if packet.packet_id in self.selected_missing else "",
                    packet.packet_id,
                    packet.offset,
                    packet.valid_len,
                ),
            )

    def toggle_selected_packet(self, _event=None) -> None:
        item = self.packet_tree.focus()
        if not item:
            return

        packet_id = int(item)
        if packet_id in self.selected_missing:
            self.selected_missing.remove(packet_id)
        else:
            self.selected_missing.add(packet_id)

        packet = self.packets[packet_id]
        self.packet_tree.item(
            item,
            values=(
                "yes" if packet_id in self.selected_missing else "",
                packet.packet_id,
                packet.offset,
                packet.valid_len,
            ),
        )
        self.status_text.set(f"Selected missing frames: {len(self.selected_missing)}")

    def clear_selection(self) -> None:
        self.selected_missing.clear()
        self.populate_packets()
        self.status_text.set("Selected missing frames cleared.")

    def clear_messages(self) -> None:
        self.log.configure(state=tk.NORMAL)
        self.log.delete("1.0", tk.END)
        self.log.configure(state=tk.DISABLED)

    def browse_transfer_file(self) -> None:
        path = filedialog.askopenfilename(
            title="Select file to transfer",
            filetypes=[
                ("Binary and image files", "*.bin *.raw *.png *.jpg *.jpeg *.bmp"),
                ("All files", "*.*"),
            ],
        )
        if path:
            self.file_transfer_path.set(path)
            self.load_transfer_file()

    def load_transfer_file(self) -> None:
        try:
            source = Path(self.file_transfer_path.get()).expanduser().resolve()
            data = source.read_bytes()
            encoded_name = source.name.encode("utf-8")
            if not data:
                raise ValueError("Selected file is empty")
            if len(encoded_name) > MAX_BULK_FILE_NAME_BYTES:
                raise ValueError(
                    f"File name is {len(encoded_name)} UTF-8 bytes; the MCU accepts at most "
                    f"{MAX_BULK_FILE_NAME_BYTES}"
                )

            self.file_transfer_source = source
            self.file_transfer_data = data
            self.file_transfer_path.set(str(source))
            self.file_transfer_progress.set(0.0)
            self.file_transfer_percent.set("0%")
            self.file_transfer_status.set(
                f"Loaded {source.name} | {len(data)} bytes | CRC16 0x{crc16_ccitt(data):04X}"
            )
            self.append_file_log(f"Loaded file: {source}")
            self.append_file_log(
                f"File size: {len(data)} bytes, CRC16: 0x{crc16_ccitt(data):04X}"
            )
        except Exception as exc:
            self.file_transfer_source = None
            self.file_transfer_data = b""
            messagebox.showerror("Load failed", str(exc))

    def ensure_transfer_file_loaded(self) -> bool:
        requested_path = self.file_transfer_path.get().strip()
        if (
            self.file_transfer_source is not None
            and self.file_transfer_data
            and requested_path == str(self.file_transfer_source)
        ):
            return True
        self.load_transfer_file()
        return self.file_transfer_source is not None and bool(self.file_transfer_data)

    def send_transfer_file(self) -> None:
        if not self.ensure_transfer_file_loaded():
            return

        file_type_name = self.file_transfer_type.get()
        file_type = FILE_TYPE_IDS.get(file_type_name)
        if file_type is None:
            messagebox.showerror("Invalid file type", "Select Data, Image, or Icon.")
            return

        source = self.file_transfer_source
        if source is None:
            return

        # Freeze the selected inputs before the worker starts so later UI edits
        # cannot alter a transfer that is already in progress.
        file_data = self.file_transfer_data
        file_name = source.name
        self.start_worker(
            lambda: self._send_transfer_file_worker(
                file_type_name,
                file_type,
                file_name,
                file_data,
            )
        )

    def clear_file_messages(self) -> None:
        self.file_log.configure(state=tk.NORMAL)
        self.file_log.delete("1.0", tk.END)
        self.file_log.configure(state=tk.DISABLED)

    def send_update(self) -> None:
        if not self.ensure_image_loaded():
            return

        omit_ids = set(self.selected_missing)
        self.start_worker(lambda: self._send_update_worker(omit_ids))

    def send_selected_missing(self) -> None:
        if not self.ensure_image_loaded():
            return
        if not self.selected_missing:
            messagebox.showinfo("No missing frames selected", "Select one or more missing frames first.")
            return

        ids = sorted(self.selected_missing)
        self.start_worker(lambda: self._send_packets_worker(ids, "manual retransmit"))

    def request_missing_ids(self) -> None:
        if not self.ensure_image_loaded():
            return
        self.start_worker(self._request_missing_ids_worker)

    def probe_bridge(self) -> None:
        self.start_worker(self._probe_bridge_worker)

    def debug_request(self) -> None:
        if not self.ensure_image_loaded():
            return
        self.start_worker(self._debug_request_worker)

    def ensure_image_loaded(self) -> bool:
        if self.image and self.packets:
            return True
        self.load_image()
        return bool(self.image and self.packets)

    def start_worker(self, target) -> None:
        if self.worker and self.worker.is_alive():
            messagebox.showwarning("Busy", "A transfer is already running.")
            return

        self.worker = threading.Thread(target=target, daemon=True)
        self.worker.start()

    def _open_client(self, operation: int = OPERATION_FIRMWARE_BRIDGE) -> BridgeClient:
        client = BridgeClient(
            self.port_name.get(),
            int(self.baud_rate.get()),
            self.log_queue,
            float(self.start_delay.get()),
            float(self.inter_frame_delay.get()),
            float(self.byte_delay.get()),
            "file" if operation == OPERATION_LOCAL_BULK_FILE else "firmware",
        )
        try:
            client.select_operation(operation)
        except Exception:
            client.close()
            raise
        return client

    def _send_transfer_file_worker(
        self,
        file_type_name: str,
        file_type: int,
        file_name: str,
        file_data: bytes,
    ) -> None:
        client: BridgeClient | None = None
        total_size = len(file_data)
        total_frames = (total_size + BULK_DATA_LENGTH - 1) // BULK_DATA_LENGTH
        started_at = time.monotonic()

        self.log_queue.put(("file_progress", 0, total_size))
        self.log_queue.put(("file_status", f"Opening UART for {file_type_name} transfer..."))
        self.log_queue.put(("file_log", "info", f"Starting {file_type_name} transfer: {file_name}"))

        try:
            client = self._open_client(OPERATION_LOCAL_BULK_FILE)
            client.send_bulk_start(file_type, file_name, file_data)
            self.log_queue.put(
                ("file_log", "bridge", f"Bulk START accepted: {total_size} bytes, {total_frames} frames")
            )

            transferred = 0
            for frame_id, offset in enumerate(range(0, total_size, BULK_DATA_LENGTH)):
                chunk = file_data[offset : offset + BULK_DATA_LENGTH]
                client.send_bulk_data(frame_id, chunk)
                transferred += len(chunk)
                self.log_queue.put(("file_progress", transferred, total_size))

                completed_frames = frame_id + 1
                if completed_frames == 1 or completed_frames % 64 == 0 or completed_frames == total_frames:
                    self.log_queue.put(
                        (
                            "file_log",
                            "info",
                            f"Transferred {completed_frames}/{total_frames} frames "
                            f"({transferred}/{total_size} bytes)",
                        )
                    )

            self.log_queue.put(("file_status", "Finalizing file on the MCU..."))
            client.send_bulk_end()

            elapsed_s = time.monotonic() - started_at
            rate_kib_s = (total_size / 1024.0) / elapsed_s if elapsed_s > 0 else 0.0
            self.log_queue.put(
                (
                    "file_log",
                    "recovery",
                    f"Transfer completed in {elapsed_s:.2f} s ({rate_kib_s:.1f} KiB/s)",
                )
            )
            self.log_queue.put(("file_status", f"Transfer complete: {file_name}"))
        except Exception as exc:
            self.log_queue.put(("file_log", "error", f"ERROR: {exc}"))
            self.log_queue.put(("file_status", f"Transfer failed: {exc}"))
        finally:
            if client is not None:
                client.close()

    def _send_update_worker(self, omit_ids: set[int]) -> None:
        try:
            with_bridge = self._open_client()
            try:
                self.log_queue.put(("info", "Starting firmware update request"))
                with_bridge.send_bootloader_frame(CMD_FW_UP_REQ, build_fw_update_request(self.image))

                sent_count = 0
                for packet in self.packets:
                    if packet.packet_id in omit_ids:
                        continue

                    with_bridge.send_bootloader_frame(CMD_FW_UP_MSG, packet.payload)
                    sent_count += 1

                    if sent_count == 1 or sent_count % 64 == 0 or sent_count == len(self.packets) - len(omit_ids):
                        self.log_queue.put(("info", f"Sent data frames: {sent_count}/{len(self.packets) - len(omit_ids)}"))

                if self._complete_update_with_recovery(with_bridge):
                    self.log_queue.put(("info", "Firmware update stream completed"))
            finally:
                with_bridge.close()
        except Exception as exc:
            self.log_queue.put(("error", f"ERROR: {exc}"))

    def _send_packets_worker(self, packet_ids: list[int], reason: str) -> None:
        try:
            with_bridge = self._open_client()
            try:
                self._send_packets_with_client(with_bridge, packet_ids, reason)
            finally:
                with_bridge.close()
        except Exception as exc:
            self.log_queue.put(("error", f"ERROR: {exc}"))

    def _request_missing_ids_worker(self) -> None:
        try:
            with_bridge = self._open_client()
            try:
                missing_ids = self._request_missing_ids_with_client(with_bridge)
                self._mark_missing_ids_from_worker(missing_ids)
            finally:
                with_bridge.close()
        except Exception as exc:
            self.log_queue.put(("error", f"ERROR: {exc}"))

    def _complete_update_with_recovery(self, client: BridgeClient) -> bool:
        recovery_round = 0

        while True:
            self.log_queue.put(("info", "Sending FW update end"))
            client.send_bootloader_frame(CMD_FW_UP_END, b"")
            end_response = client.wait_for_slave_frame(CMD_FW_UP_END, timeout_s=3.0)
            if end_response is None:
                self.log_queue.put(("bridge", "No slave response observed after FW update end"))
                return False

            _command, _sequence, payload = end_response
            if len(payload) >= 2 and payload[0] == BOOTLOADER_ACK:
                self.log_queue.put(("slave", "Slave accepted FW update end"))
                return True

            missing_pending = (
                len(payload) >= 2
                and payload[0] == BOOTLOADER_NACK
                and payload[1] == BOOTLOADER_ERROR_MISSING_PACKETS_PENDING
            )
            if not missing_pending:
                self.log_queue.put(("error", "Slave rejected FW update end"))
                return False

            recovery_round += 1
            if recovery_round > MAX_AUTO_RECOVERY_ROUNDS:
                self.log_queue.put(("error", "Automatic missing-packet recovery limit reached"))
                return False

            self.log_queue.put(("recovery", f"Missing packets detected; starting automatic recovery round {recovery_round}"))
            missing_ids = self._request_missing_ids_with_client(client)
            if not missing_ids:
                self.log_queue.put(("error", "Slave reported missing packets, but no packet IDs were received"))
                return False

            self._mark_missing_ids_from_worker(missing_ids)
            self._send_packets_with_client(client, missing_ids, "automatic slave-requested retransmit")
            self.log_queue.put(("recovery", "Missing packets retransmitted; checking FW update end again"))

    def _probe_bridge_worker(self) -> None:
        try:
            with_bridge = self._open_client()
            try:
                if with_bridge.probe():
                    self.log_queue.put(("bridge", "UART bridge probe OK"))
                else:
                    self.log_queue.put(("error", "UART bridge probe failed"))
            finally:
                with_bridge.close()
        except Exception as exc:
            self.log_queue.put(("error", f"ERROR: {exc}"))

    def _debug_request_worker(self) -> None:
        try:
            with_bridge = self._open_client()
            try:
                if not with_bridge.enable_debug_next_frame():
                    self.log_queue.put(("error", "Bridge debug enable failed"))
                    return

                request_frame = build_uart_frame(CMD_FW_UP_REQ, 0, build_fw_update_request(self.image))
                with_bridge.write_raw(request_frame)
                debug_bytes = with_bridge.read_debug_bytes()
                if not debug_bytes:
                    self.log_queue.put(("bridge", "No bridge parser debug bytes received"))
                    return

                printable = " ".join(chr(byte) if 32 <= byte <= 126 else f"0x{byte:02X}" for byte in debug_bytes)
                self.log_queue.put(("bridge", f"Bridge debug bytes: {printable}"))
            finally:
                with_bridge.close()
        except Exception as exc:
            self.log_queue.put(("error", f"ERROR: {exc}"))

    def _request_missing_ids_with_client(self, client: BridgeClient) -> list[int]:
        self.log_queue.put(("recovery", "Requesting missing packet IDs"))
        missing_ids: list[int] = []
        seen_ids: set[int] = set()
        remaining_count = 1
        page_count = 0

        while remaining_count > 0:
            page_count += 1
            if page_count > MAX_LOST_PACKET_INFO_PAGES:
                self.log_queue.put(("error", "Lost-packet-info pagination limit reached"))
                break

            client.send_bootloader_frame(CMD_GET_LOST_PACKET_INFO, b"")
            response = client.wait_for_slave_frame(CMD_RET_LOST_PACKET_INFO, timeout_s=3.0)
            if response is None:
                self.log_queue.put(("error", "No lost-packet-info response received from slave"))
                break

            _command, _sequence, payload = response
            page_ids, remaining_count, frame_seq = self._parse_lost_packet_info_payload(payload)
            for packet_id in page_ids:
                if packet_id not in seen_ids:
                    seen_ids.add(packet_id)
                    missing_ids.append(packet_id)

            if frame_seq is not None:
                client.send_bootloader_frame(CMD_RET_LOST_PACKET_INFO, bytes([BOOTLOADER_ACK, frame_seq]))
                self.log_queue.put(("recovery", f"ACKed lost-packet-info frame sequence {frame_seq}"))

            if not page_ids:
                break

        self.log_queue.put(("recovery", f"Slave requested {len(missing_ids)} missing frame(s): {missing_ids}"))
        return missing_ids

    def _parse_lost_packet_info_payload(self, payload: bytes) -> tuple[list[int], int, int | None]:
        if len(payload) < 3:
            self.log_queue.put(("error", "Lost-packet-info payload too short"))
            return [], 0, None

        pending_count = payload[0]
        frame_seq = payload[1]
        info_byte_count = payload[2]
        info = payload[3 : 3 + info_byte_count]
        missing_ids: list[int] = []

        for index in range(0, len(info) - 1, 2):
            missing_ids.append((info[index] << 8) | info[index + 1])

        if pending_count > len(missing_ids):
            self.log_queue.put(("recovery", f"Lost-packet-info page contains {len(missing_ids)} of {pending_count} pending IDs"))

        remaining_count = pending_count - len(missing_ids)
        return missing_ids, remaining_count, frame_seq

    def _send_packets_with_client(self, client: BridgeClient, packet_ids: list[int], reason: str) -> None:
        self.log_queue.put(("recovery", f"Sending {len(packet_ids)} missing frames ({reason})"))
        for index, packet_id in enumerate(packet_ids, start=1):
            if packet_id < 0 or packet_id >= len(self.packets):
                self.log_queue.put(("error", f"Skipping invalid packet ID {packet_id}"))
                continue

            client.send_bootloader_frame(CMD_FW_UP_MSG, self.packets[packet_id].payload)
            if index == 1 or index % 16 == 0 or index == len(packet_ids):
                self.log_queue.put(("recovery", f"Retransmitted {index}/{len(packet_ids)}"))
        self.log_queue.put(("recovery", "Missing-frame retransmission completed"))

    def _mark_missing_ids_from_worker(self, packet_ids: list[int]) -> None:
        for packet_id in packet_ids:
            if 0 <= packet_id < len(self.packets):
                self.selected_missing.add(packet_id)
        self.root.after(0, self.populate_packets)

    def append_log(self, text: str, category: str = "info") -> None:
        self.log.configure(state=tk.NORMAL)
        self.log.insert(tk.END, f"{time.strftime('%H:%M:%S')}  {text}\n", category)
        self.log.see(tk.END)
        self.log.configure(state=tk.DISABLED)

    def append_file_log(self, text: str, category: str = "info") -> None:
        self.file_log.configure(state=tk.NORMAL)
        self.file_log.insert(tk.END, f"{time.strftime('%H:%M:%S')}  {text}\n", category)
        self.file_log.see(tk.END)
        self.file_log.configure(state=tk.DISABLED)

    def _update_file_progress(self, transferred: int, total: int) -> None:
        percent = (100.0 * transferred / total) if total > 0 else 0.0
        self.file_transfer_progress.set(percent)
        self.file_transfer_percent.set(f"{percent:.1f}%")
        if transferred < total:
            self.file_transfer_status.set(f"Transferring: {transferred}/{total} bytes")

    def _drain_log_queue(self) -> None:
        while True:
            try:
                item = self.log_queue.get_nowait()
            except queue.Empty:
                break
            if isinstance(item, tuple):
                if len(item) == 3 and item[0] == "file_progress":
                    _event, transferred, total = item
                    self._update_file_progress(int(transferred), int(total))
                elif len(item) == 3 and item[0] == "file_log":
                    _event, category, text = item
                    self.append_file_log(str(text), str(category))
                elif len(item) == 2 and item[0] == "file_status":
                    _event, text = item
                    self.file_transfer_status.set(str(text))
                elif len(item) == 2:
                    category, text = item
                    self.append_log(str(text), str(category))
            else:
                text = str(item)
                category = "error" if text.startswith("ERROR:") else "info"
                self.append_log(text, category)
        self.root.after(100, self._drain_log_queue)


def main() -> None:
    root = tk.Tk()
    app = FirmwareUpdateGui(root)
    root.mainloop()


if __name__ == "__main__":
    main()
