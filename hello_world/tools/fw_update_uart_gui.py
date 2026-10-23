#!/usr/bin/env python3
"""PC UART operations tool.

The GUI provides separate operation views for the UART-to-CAN firmware bridge
and for transferring files into the MCU's local LittleFS storage.
"""

from __future__ import annotations

import json
import queue
import re
import struct
import threading
import time
import tkinter as tk
import zlib
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
TARGET_READY_TIMEOUT_S = 30.0
# Bulk transfers use blocking writes because the Windows MCU-Link VCOM driver
# can report false timeouts while it is still accepting a long transfer.
WRITE_TIMEOUT_S = None
FILE_TRANSFER_WRITE_TIMEOUT_S = 1.0
SERIAL_PACED_WRITE_CHUNK_SIZE = 8
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

CMD_FW_UP_REQ = 21
CMD_FW_UP_MSG = 22
CMD_FW_UP_END = 23
CMD_GET_LOST_PACKET_INFO = 26
CMD_RET_LOST_PACKET_INFO = 27
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

UART_ERROR_NAMES = {
    0: "None",
    1: "InvalidVersion",
    2: "InvalidLength",
    3: "FrameCRCMismatch",
    4: "CANTxFail",
    5: "InvalidCommand",
    6: "InvalidOperation",
    7: "OperationBusy",
    8: "ServiceUnavailable",
    9: "QueueFull",
    10: "InvalidFrame",
    11: "BulkStartFailed",
    12: "BulkTransferFailed",
    13: "BulkTimeout",
    14: "BulkCRCMismatch",
    15: "BulkSizeMismatch",
    16: "FileNotFound",
    17: "FileReadFailed",
}

CMD_SELECT_OPERATION = 0x01
CMD_RELEASE_OPERATION = 0x02
CMD_BULK_START = 0x40
CMD_BULK_DATA = 0x41
CMD_BULK_END = 0x42
CMD_CONFIG_READ = 0x43

OPERATION_FIRMWARE_BRIDGE = 1
OPERATION_LOCAL_BULK_FILE = 2

FILE_TYPE_CONFIG = 1
FILE_TYPE_DATA = 2
FILE_TYPE_IMAGE = 3
FILE_TYPE_ICON = 4
FILE_TYPE_IDS = {
    "Config": FILE_TYPE_CONFIG,
    "Data": FILE_TYPE_DATA,
    "Image": FILE_TYPE_IMAGE,
    "Icon": FILE_TYPE_ICON,
}

BULK_DATA_LENGTH = 256
MAX_BULK_FILE_NAME_BYTES = 29


# ---------------------------------------------------------------------------
# Source configuration file format
# ---------------------------------------------------------------------------

DEFAULT_NUMBER_OF_AUDIO_SOURCES = 4


def read_audio_source_count() -> int:
    """Read the source count from the firmware enum when the source tree exists."""
    header_path = (
        Path(__file__).resolve().parents[1]
        / "src"
        / "LVGL_Controller"
        / "LVGL_Display_Types.h"
    )
    try:
        header_text = header_path.read_text(encoding="utf-8")
        # Do not count enum entries that are currently disabled in C comments.
        header_text = re.sub(r"/\*.*?\*/", "", header_text, flags=re.DOTALL)
        header_text = re.sub(r"//.*", "", header_text)
        enum_start = header_text.index("eAudio_Src_0")
        enum_end = header_text.index("eNUMBER_OF_AUDIO_SOURCES", enum_start)
        source_ids = {
            int(source_id)
            for source_id in re.findall(
                r"eAudio_Src_(\d+)",
                header_text[enum_start:enum_end],
            )
        }
        if source_ids and source_ids == set(range(max(source_ids) + 1)):
            return len(source_ids)
    except (OSError, UnicodeError, ValueError):
        pass

    # The fallback keeps a packaged/standalone copy of the PC tool usable.
    return DEFAULT_NUMBER_OF_AUDIO_SOURCES


NUMBER_OF_AUDIO_SOURCES = read_audio_source_count()

SOURCE_CONFIG_MAGIC = b"SRCF"
SOURCE_CONFIG_VERSION = 2
SOURCE_CONFIG_JSON_SCHEMA_VERSION = 2
SOURCE_CONFIG_NAME_MAX_BYTES = 23

SOURCE_TYPE_IDS = {
    "None": 0,
    "Wired": 1,
    "Bluetooth": 2,
}
SOURCE_TYPE_NAMES = {source_type: name for name, source_type in SOURCE_TYPE_IDS.items()}

SOURCE_CONFIG_FLAG_ACTIVE = 1 << 0
SOURCE_CONFIG_FLAG_MUTED = 1 << 1
SOURCE_CONFIG_FLAG_SELECTED = 1 << 2
SOURCE_CONFIG_KNOWN_FLAGS = (
    SOURCE_CONFIG_FLAG_ACTIVE
    | SOURCE_CONFIG_FLAG_MUTED
    | SOURCE_CONFIG_FLAG_SELECTED
)

# Fixed 48-byte firmware record:
# magic, version, header size, total size, payload CRC32, source type,
# source ID, flags, volume, source-name length, reserved[3], source-name[24].
# The CRC covers bytes 16..47, protecting every source-specific field.
SOURCE_CONFIG_HEADER_SIZE = 24
SOURCE_CONFIG_RECORD = struct.Struct("<4sHHIIBBBBB3s24s")
SOURCE_CONFIG_CRC_OFFSET = 16


@dataclass(frozen=True)
class FirmwarePacket:
    packet_id: int
    offset: int
    valid_len: int
    payload: bytes


@dataclass(frozen=True)
class SerialConnectionSettings:
    port_name: str
    baud_rate: int
    start_delay_s: float
    inter_frame_delay_s: float
    byte_delay_s: float


@dataclass
class SourceConfiguration:
    source_type: int
    source_id: int
    source_name: str
    volume: int
    is_active: bool
    is_muted: bool
    is_selected: bool


def default_source_configuration(source_id: int) -> SourceConfiguration:
    return SourceConfiguration(
        source_type=SOURCE_TYPE_IDS["None"],
        source_id=source_id,
        source_name=f"Source {source_id + 1}",
        volume=0,
        is_active=False,
        is_muted=False,
        is_selected=False,
    )


def validate_source_configuration(configuration: SourceConfiguration) -> bytes:
    if configuration.source_type not in SOURCE_TYPE_NAMES:
        raise ValueError(
            f"Source {configuration.source_id}: Source Type must match "
            "eAudioSrc_Type_t"
        )
    if not 0 <= configuration.source_id < NUMBER_OF_AUDIO_SOURCES:
        raise ValueError(
            f"Source ID {configuration.source_id} is outside 0..{NUMBER_OF_AUDIO_SOURCES - 1}"
        )

    if not configuration.source_name.strip():
        raise ValueError(f"Source {configuration.source_id}: Source Name cannot be empty")
    if configuration.source_name != configuration.source_name.strip():
        raise ValueError(
            f"Source {configuration.source_id}: Source Name cannot start or end with whitespace"
        )
    if "\0" in configuration.source_name:
        raise ValueError(f"Source {configuration.source_id}: Source Name cannot contain NUL")

    source_name = configuration.source_name.strip().encode("utf-8")
    if len(source_name) > SOURCE_CONFIG_NAME_MAX_BYTES:
        raise ValueError(
            f"Source {configuration.source_id}: Source Name is {len(source_name)} UTF-8 bytes; "
            f"maximum is {SOURCE_CONFIG_NAME_MAX_BYTES}"
        )

    if not 0 <= configuration.volume <= 100:
        raise ValueError(
            f"Source {configuration.source_id}: Volume must be between 0 and 100"
        )

    if configuration.is_active and configuration.source_type == SOURCE_TYPE_IDS["None"]:
        raise ValueError(
            f"Source {configuration.source_id}: An active source cannot have Source Type None"
        )
    if configuration.is_selected and not configuration.is_active:
        raise ValueError(
            f"Source {configuration.source_id}: A selected source must also be active"
        )

    return source_name


def validate_all_source_configurations(
    configurations: list[SourceConfiguration],
) -> None:
    if len(configurations) != NUMBER_OF_AUDIO_SOURCES:
        raise ValueError(
            f"Expected {NUMBER_OF_AUDIO_SOURCES} source configurations, "
            f"found {len(configurations)}"
        )

    for expected_source_id, configuration in enumerate(configurations):
        if configuration.source_id != expected_source_id:
            raise ValueError(
                f"Source slot {expected_source_id} contains source ID "
                f"{configuration.source_id}"
            )
        validate_source_configuration(configuration)

    selected_sources = [
        configuration.source_id
        for configuration in configurations
        if configuration.is_selected
    ]
    if len(selected_sources) > 1:
        raise ValueError(
            "Only one source may be selected; selected source IDs: "
            + ", ".join(str(source_id) for source_id in selected_sources)
        )


def encode_source_configuration(configuration: SourceConfiguration) -> bytes:
    source_name = validate_source_configuration(configuration)
    source_name_field = source_name.ljust(SOURCE_CONFIG_NAME_MAX_BYTES + 1, b"\0")

    flags = 0
    if configuration.is_active:
        flags |= SOURCE_CONFIG_FLAG_ACTIVE
    if configuration.is_muted:
        flags |= SOURCE_CONFIG_FLAG_MUTED
    if configuration.is_selected:
        flags |= SOURCE_CONFIG_FLAG_SELECTED

    source_payload = struct.pack(
        "<BBBBB3s24s",
        configuration.source_type,
        configuration.source_id,
        flags,
        configuration.volume,
        len(source_name),
        b"\0\0\0",
        source_name_field,
    )
    payload_crc32 = zlib.crc32(source_payload) & 0xFFFFFFFF
    return SOURCE_CONFIG_RECORD.pack(
        SOURCE_CONFIG_MAGIC,
        SOURCE_CONFIG_VERSION,
        SOURCE_CONFIG_HEADER_SIZE,
        SOURCE_CONFIG_RECORD.size,
        payload_crc32,
        configuration.source_type,
        configuration.source_id,
        flags,
        configuration.volume,
        len(source_name),
        b"\0\0\0",
        source_name_field,
    )


def decode_source_configuration(record: bytes) -> SourceConfiguration:
    if len(record) != SOURCE_CONFIG_RECORD.size:
        raise ValueError(
            f"Source configuration record must be {SOURCE_CONFIG_RECORD.size} bytes"
        )

    (
        magic,
        version,
        header_size,
        total_size,
        expected_crc32,
        source_type,
        source_id,
        flags,
        volume,
        source_name_length,
        reserved,
        source_name_field,
    ) = SOURCE_CONFIG_RECORD.unpack(record)

    if magic != SOURCE_CONFIG_MAGIC:
        raise ValueError("Invalid source configuration magic")
    if version != SOURCE_CONFIG_VERSION:
        raise ValueError(f"Unsupported source configuration version: {version}")
    if header_size != SOURCE_CONFIG_HEADER_SIZE:
        raise ValueError(f"Invalid source configuration header size: {header_size}")
    if total_size != SOURCE_CONFIG_RECORD.size:
        raise ValueError(f"Invalid source configuration record size: {total_size}")
    if flags & ~SOURCE_CONFIG_KNOWN_FLAGS:
        raise ValueError(f"Source {source_id}: Unknown flag bits 0x{flags:02X}")
    if reserved != b"\0\0\0":
        raise ValueError(f"Source {source_id}: Reserved bytes must be zero")
    if source_name_length > SOURCE_CONFIG_NAME_MAX_BYTES:
        raise ValueError(f"Source {source_id}: Invalid source-name length")

    actual_crc32 = zlib.crc32(record[SOURCE_CONFIG_CRC_OFFSET:]) & 0xFFFFFFFF
    if actual_crc32 != expected_crc32:
        raise ValueError(
            f"Source {source_id}: CRC32 mismatch "
            f"(stored 0x{expected_crc32:08X}, calculated 0x{actual_crc32:08X})"
        )

    encoded_name = source_name_field[:source_name_length]
    if any(source_name_field[source_name_length:]):
        raise ValueError(f"Source {source_id}: Source-name padding must be zero")
    try:
        source_name = encoded_name.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ValueError(f"Source {source_id}: Source Name is not valid UTF-8") from exc

    configuration = SourceConfiguration(
        source_type=source_type,
        source_id=source_id,
        source_name=source_name,
        volume=volume,
        is_active=bool(flags & SOURCE_CONFIG_FLAG_ACTIVE),
        is_muted=bool(flags & SOURCE_CONFIG_FLAG_MUTED),
        is_selected=bool(flags & SOURCE_CONFIG_FLAG_SELECTED),
    )
    validate_source_configuration(configuration)
    return configuration


def validate_bulk_file_selection(file_type: int, file_name: str, file_data: bytes) -> None:
    """Reject file/type combinations that the MCU cannot safely accept."""
    if file_type != FILE_TYPE_CONFIG:
        return

    configuration = decode_source_configuration(file_data)
    expected_name = f"source_{configuration.source_id:02d}.bin"
    if file_name.lower() != expected_name.lower():
        raise ValueError(
            "Config transfers must use the source record filename matching the "
            f"record ID. Expected {expected_name}, received {file_name}."
        )


def source_configuration_to_json(configuration: SourceConfiguration) -> dict[str, object]:
    return {
        "sourceType": SOURCE_TYPE_NAMES[configuration.source_type],
        "sourceId": configuration.source_id,
        "sourceName": configuration.source_name,
        "isActive": configuration.is_active,
        "isMuted": configuration.is_muted,
        "isSelected": configuration.is_selected,
        "volume": configuration.volume,
    }


def source_configuration_from_json(data: object) -> SourceConfiguration:
    if not isinstance(data, dict):
        raise ValueError("Every source configuration must be a JSON object")

    required_fields = {
        "sourceType",
        "sourceId",
        "sourceName",
        "isActive",
        "isMuted",
        "isSelected",
        "volume",
    }
    missing_fields = required_fields.difference(data)
    if missing_fields:
        raise ValueError(f"Source configuration is missing: {', '.join(sorted(missing_fields))}")

    for field_name in ("isActive", "isMuted", "isSelected"):
        if not isinstance(data[field_name], bool):
            raise ValueError(f"{field_name} must be true or false")

    for field_name in ("sourceId", "volume"):
        if isinstance(data[field_name], bool) or not isinstance(data[field_name], int):
            raise ValueError(f"{field_name} must be a whole number")

    for field_name in ("sourceType", "sourceName"):
        if not isinstance(data[field_name], str):
            raise ValueError(f"{field_name} must be text")

    source_type_name = data["sourceType"]
    if source_type_name not in SOURCE_TYPE_IDS:
        raise ValueError(
            f"sourceType must be one of: {', '.join(SOURCE_TYPE_IDS)}"
        )

    configuration = SourceConfiguration(
        source_type=SOURCE_TYPE_IDS[source_type_name],
        source_id=data["sourceId"],
        source_name=data["sourceName"],
        volume=data["volume"],
        is_active=data["isActive"],
        is_muted=data["isMuted"],
        is_selected=data["isSelected"],
    )
    validate_source_configuration(configuration)
    return configuration


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
        write_timeout_s: float | None = WRITE_TIMEOUT_S,
    ):
        if serial is None:
            raise RuntimeError("Missing dependency: pyserial. Install with: python -m pip install pyserial")

        self.log_queue = log_queue
        self.log_target = log_target
        self.sequence = 0
        self.selected_operation: int | None = None
        self.pending_slave_frames: list[tuple[int, int, bytes]] = []
        self.inter_frame_delay_s = inter_frame_delay_s
        self.byte_delay_s = byte_delay_s
        self.write_timeout_s = write_timeout_s
        self.port = serial.Serial(
            port_name,
            baud_rate,
            timeout=ACK_TIMEOUT_S,
            write_timeout=write_timeout_s,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False,
        )
        time.sleep(start_delay_s)
        self.port.reset_input_buffer()
        self.port.reset_output_buffer()

    def close(self) -> None:
        if self.port and self.port.is_open:
            try:
                if self.selected_operation is not None:
                    try:
                        self.release_operation()
                    except Exception as exc:
                        self._log(f"Could not release PC UART operation during close: {exc}", "bridge")
            finally:
                # A write_timeout=0 operation can still be pending in the
                # Windows driver.  Cancel it before pyserial tries to restore
                # COM timeouts; otherwise Serial.close() itself can block and
                # leave the port owned by this worker.
                try:
                    self.port.cancel_write()
                except Exception:
                    pass
                try:
                    self.port.cancel_read()
                except Exception:
                    pass
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
        if command == CMD_CONFIG_READ:
            source_id = payload[0] if payload else -1
            self._log(
                f"Configuration readback frame: source={source_id}, bytes={max(0, len(payload) - 1)}",
                "bridge",
            )
            return

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
                frame = self._read_uart_frame_after_sof_first_byte(ACK_TIMEOUT_S)
                if frame is not None:
                    self.pending_slave_frames.append(frame)
                continue

            if 32 <= status <= 126:
                self._log(f"UART text/noise: {chr(status)}", "bridge")
            else:
                self._log(f"UART byte/noise: 0x{status:02X}", "bridge")

        raise TimeoutError(f"Timeout waiting for bridge ACK: cmd={command}, seq={sequence}")

    def wait_for_slave_frame(self, expected_command: int, timeout_s: float = 3.0) -> tuple[int, int, bytes] | None:
        for index, frame in enumerate(self.pending_slave_frames):
            if frame[0] == expected_command:
                return self.pending_slave_frames.pop(index)

        deadline = time.monotonic() + timeout_s

        while time.monotonic() < deadline:
            first = self.port.read(1)
            if not first:
                continue

            if first == b"B":
                frame = self._read_uart_frame_after_sof_first_byte(timeout_s)
                if frame is not None and frame[0] == expected_command:
                    return frame
                if frame is not None:
                    self.pending_slave_frames.append(frame)
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

    def wait_for_slave_ack(self, expected_command: int, timeout_s: float) -> None:
        response = self.wait_for_slave_frame(expected_command, timeout_s)
        if response is None:
            raise TimeoutError(f"Timeout waiting for target ACK: cmd={expected_command}")

        _command, _sequence, payload = response
        if len(payload) < 2:
            raise RuntimeError(
                f"Malformed target response: cmd={expected_command}, payload_len={len(payload)}"
            )
        if payload[0] == BOOTLOADER_ACK:
            return
        if payload[0] == BOOTLOADER_NACK:
            raise RuntimeError(
                f"Target NACK: cmd={expected_command}, error={payload[1]}"
            )
        raise RuntimeError(
            f"Invalid target response status {payload[0]}: cmd={expected_command}"
        )

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
            error_name = UART_ERROR_NAMES.get(error_code, "UnknownError")
            raise RuntimeError(
                f"Bridge NACK: cmd={command}, seq={sequence}, "
                f"error={error_code} ({error_name})"
            )

        raise RuntimeError(f"Unexpected bridge response 0x{status:02X}: cmd={command}, seq={sequence}")

    def send_bootloader_frame(self, command: int, payload: bytes = b"") -> tuple[int, int]:
        if self.selected_operation != OPERATION_FIRMWARE_BRIDGE:
            raise RuntimeError("Firmware Bridge operation is not selected")
        return self.send_pc_uart_command(command, payload)

    def select_operation(self, operation: int) -> None:
        self.send_pc_uart_command(CMD_SELECT_OPERATION, bytes([operation]))
        self.selected_operation = operation
        self._log(f"Selected PC UART operation {operation}", "bridge")

    def ping_uart_receiver(self, timeout_s: float = ACK_TIMEOUT_S) -> bool:
        """Test the MCU UART receive thread without invoking the BLUP parser."""
        self.write_raw(b"?")
        self._log("PC-to-MCU probe byte was accepted by the COM driver", "bridge")
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            response = self.port.read(1)
            if response == b"!":
                self._log("MCU UART receiver returned the probe reply", "bridge")
                return True
            if response:
                byte = response[0]
                if 32 <= byte <= 126:
                    self._log(f"UART text/noise during receiver probe: {chr(byte)}", "bridge")
                else:
                    self._log(f"UART byte/noise during receiver probe: 0x{byte:02X}", "bridge")
        return False

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

    def read_source_configuration_record(self, source_id: int) -> bytes:
        if self.selected_operation != OPERATION_LOCAL_BULK_FILE:
            raise RuntimeError("Local Bulk File operation is not selected")
        if not 0 <= source_id < NUMBER_OF_AUDIO_SOURCES:
            raise ValueError(f"Invalid source ID: {source_id}")

        self.send_pc_uart_command(CMD_CONFIG_READ, bytes([source_id]))
        response = self.wait_for_slave_frame(CMD_CONFIG_READ, timeout_s=3.0)
        if response is None:
            raise TimeoutError(
                f"Timeout waiting for source {source_id:02d} configuration readback"
            )

        _command, _sequence, payload = response
        expected_length = 1 + SOURCE_CONFIG_RECORD.size
        if len(payload) != expected_length:
            raise RuntimeError(
                f"Source {source_id:02d} readback has {len(payload)} bytes; "
                f"expected {expected_length}"
            )
        if payload[0] != source_id:
            raise RuntimeError(
                f"Source readback ID mismatch: received {payload[0]}, expected {source_id}"
            )

        record = payload[1:]
        configuration = decode_source_configuration(record)
        if configuration.source_id != source_id:
            raise RuntimeError(
                f"Record source ID mismatch: received {configuration.source_id}, "
                f"expected {source_id}"
            )
        return record

    def write_raw(self, data: bytes) -> None:
        try:
            self._write_raw_once(data)
        except serial.SerialTimeoutException as exc:
            self._log(
                "MCU-Link VCOM PC-to-board TX is stalled; unplug and reconnect "
                "the MCU-Link USB cable before retrying",
                "error",
            )
            try:
                self.port.reset_output_buffer()
            except Exception:
                pass
            raise TimeoutError("Serial write timeout") from exc

    def _write_raw_once(self, data: bytes) -> None:
        # A zero timeout is reserved for explicit non-blocking diagnostics.
        # Production transfers use a finite write timeout so a queued Windows
        # overlapped write is not mistaken for data that reached MCU-Link.
        if self.write_timeout_s == 0 or self.byte_delay_s <= 0:
            written = self.port.write(data)
            if written != len(data):
                raise TimeoutError(f"Serial short write: {written}/{len(data)} bytes")
        else:
            # Do not issue one Windows/USB driver transaction per UART byte.
            # The UART baud rate already serializes bytes on the wire, while
            # small chunks retain the configured receiver pacing without
            # creating hundreds of overlapped COM writes per frame.
            for offset in range(0, len(data), SERIAL_PACED_WRITE_CHUNK_SIZE):
                chunk = data[offset : offset + SERIAL_PACED_WRITE_CHUNK_SIZE]
                written = self.port.write(chunk)
                if written != len(chunk):
                    raise TimeoutError(
                        f"Serial short write: {written}/{len(chunk)} bytes"
                    )
                if offset + len(chunk) < len(data):
                    time.sleep(self.byte_delay_s * len(chunk))
        # A blocking Windows write has already completed when port.write()
        # returns.  Avoid flush() in watchdog mode because some MCU-Link VCOM
        # drivers keep out_waiting asserted and can block there indefinitely.
        if self.write_timeout_s != 0:
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

        self.source_configurations = [
            default_source_configuration(source_id)
            for source_id in range(NUMBER_OF_AUDIO_SOURCES)
        ]
        self.current_source_index = 0
        self.source_selector_values = tuple(
            f"Source {source_id:02d}" for source_id in range(NUMBER_OF_AUDIO_SOURCES)
        )
        self.source_selector = tk.StringVar(value=self.source_selector_values[0])
        self.source_type = tk.StringVar()
        self.source_name = tk.StringVar()
        self.source_volume = tk.StringVar()
        self.source_is_active = tk.BooleanVar()
        self.source_is_muted = tk.BooleanVar()
        self.source_is_selected = tk.BooleanVar()
        self.source_config_status = tk.StringVar(
            value=f"Editing 1 of {NUMBER_OF_AUDIO_SOURCES} source configurations."
        )

        self.image: bytes = b""
        self.packets: list[FirmwarePacket] = []
        self.selected_missing: set[int] = set()
        self.file_transfer_data: bytes = b""
        self.file_transfer_source: Path | None = None
        self.log_queue: queue.Queue[object] = queue.Queue()
        self.worker: threading.Thread | None = None
        self.operation_frames: dict[str, ttk.Frame] = {}
        self.operation_buttons: dict[str, ttk.Button] = {}
        self.file_transfer_selection: ttk.LabelFrame | None = None
        self.source_config_frame: ttk.LabelFrame | None = None
        self.file_transfer_file_widgets: list[tk.Widget] = []
        self.send_transfer_file_button: ttk.Button | None = None
        self.clear_file_messages_button: ttk.Button | None = None

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
        self.file_transfer_selection = selection

        ttk.Label(selection, text="File type").grid(row=0, column=0, sticky=tk.W, pady=(0, 8))
        type_box = ttk.Combobox(
            selection,
            textvariable=self.file_transfer_type,
            values=tuple(FILE_TYPE_IDS),
            state="readonly",
            width=16,
        )
        type_box.grid(row=0, column=1, sticky=tk.W, padx=8, pady=(0, 8))
        type_box.bind("<<ComboboxSelected>>", self._on_file_transfer_type_changed)

        file_label = ttk.Label(selection, text="File")
        file_label.grid(row=1, column=0, sticky=tk.W)
        file_entry = ttk.Entry(selection, textvariable=self.file_transfer_path)
        file_entry.grid(row=1, column=1, sticky=tk.EW, padx=8)
        browse_button = ttk.Button(
            selection,
            text="Browse",
            command=self.browse_transfer_file,
        )
        browse_button.grid(row=1, column=2, padx=4)
        load_button = ttk.Button(
            selection,
            text="Load",
            command=self.load_transfer_file,
        )
        load_button.grid(row=1, column=3, padx=4)
        self.file_transfer_file_widgets = [
            file_label,
            file_entry,
            browse_button,
            load_button,
        ]

        self._build_source_configuration_editor(parent)

        actions = ttk.Frame(parent, padding=(0, 10, 0, 8))
        actions.pack(fill=tk.X)
        self.send_transfer_file_button = ttk.Button(
            actions,
            text="Send File",
            command=self.send_transfer_file,
        )
        self.send_transfer_file_button.pack(side=tk.LEFT)
        self.clear_file_messages_button = ttk.Button(
            actions,
            text="Clear Messages",
            command=self.clear_file_messages,
        )
        self.clear_file_messages_button.pack(side=tk.LEFT, padx=8)

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

        self._load_source_configuration_into_editor(0)
        self._update_file_transfer_type_view()

    def _build_source_configuration_editor(self, parent: ttk.Frame) -> None:
        frame = ttk.LabelFrame(parent, text="Source Configuration Data", padding=10)
        frame.columnconfigure(1, weight=1)
        frame.columnconfigure(3, weight=1)
        self.source_config_frame = frame

        navigation = ttk.Frame(frame)
        navigation.grid(row=0, column=0, columnspan=4, sticky=tk.EW, pady=(0, 8))
        navigation.columnconfigure(2, weight=1)

        ttk.Button(
            navigation,
            text="Previous",
            command=lambda: self._navigate_source_configuration(-1),
        ).grid(row=0, column=0, padx=(0, 6))

        source_selector = ttk.Combobox(
            navigation,
            textvariable=self.source_selector,
            values=self.source_selector_values,
            state="readonly",
            width=16,
        )
        source_selector.grid(row=0, column=1, sticky=tk.W)
        source_selector.bind("<<ComboboxSelected>>", self._select_source_configuration)

        ttk.Label(
            navigation,
            text=f"Total sources: {NUMBER_OF_AUDIO_SOURCES}",
        ).grid(row=0, column=2, padx=12, sticky=tk.W)

        ttk.Button(
            navigation,
            text="Next",
            command=lambda: self._navigate_source_configuration(1),
        ).grid(row=0, column=3, padx=(6, 0))

        ttk.Label(frame, text="Source Type").grid(row=1, column=0, sticky=tk.W, pady=4)
        ttk.Combobox(
            frame,
            textvariable=self.source_type,
            values=tuple(SOURCE_TYPE_IDS),
            state="readonly",
            width=16,
        ).grid(
            row=1, column=1, sticky=tk.W, padx=(8, 18), pady=4
        )

        ttk.Label(frame, text="Source Name").grid(row=1, column=2, sticky=tk.W, pady=4)
        ttk.Entry(frame, textvariable=self.source_name).grid(
            row=1, column=3, sticky=tk.EW, padx=(8, 0), pady=4
        )

        ttk.Label(frame, text="Volume").grid(row=2, column=0, sticky=tk.W, pady=4)
        ttk.Spinbox(
            frame,
            textvariable=self.source_volume,
            from_=0,
            to=100,
            width=8,
        ).grid(row=2, column=1, sticky=tk.W, padx=(8, 18), pady=4)

        ttk.Label(frame, text="Source ID").grid(row=2, column=2, sticky=tk.W, pady=4)
        self.source_id_label = ttk.Label(frame)
        self.source_id_label.grid(row=2, column=3, sticky=tk.W, padx=(8, 0), pady=4)

        flags = ttk.Frame(frame)
        flags.grid(row=3, column=0, columnspan=4, sticky=tk.W, pady=(6, 8))
        ttk.Checkbutton(
            flags,
            text="Is Active",
            variable=self.source_is_active,
        ).pack(side=tk.LEFT, padx=(0, 18))
        ttk.Checkbutton(
            flags,
            text="Is Muted",
            variable=self.source_is_muted,
        ).pack(side=tk.LEFT, padx=(0, 18))
        ttk.Checkbutton(
            flags,
            text="Is Selected",
            variable=self.source_is_selected,
        ).pack(side=tk.LEFT)

        actions = ttk.Frame(frame)
        actions.grid(row=4, column=0, columnspan=4, sticky=tk.W, pady=(0, 6))
        ttk.Button(
            actions,
            text="Save JSON Project",
            command=self._save_source_configuration_project,
        ).pack(side=tk.LEFT, padx=(0, 6))
        ttk.Button(
            actions,
            text="Load JSON Project",
            command=self._load_source_configuration_project,
        ).pack(side=tk.LEFT, padx=6)
        ttk.Button(
            actions,
            text=f"Generate {NUMBER_OF_AUDIO_SOURCES} Binary Files",
            command=self._generate_source_configuration_files,
        ).pack(side=tk.LEFT, padx=6)
        ttk.Button(
            actions,
            text="Write Current Source to MCU",
            command=self._send_current_source_configuration,
        ).pack(side=tk.LEFT, padx=6)
        ttk.Button(
            actions,
            text=f"Read Back {NUMBER_OF_AUDIO_SOURCES} Binary Files",
            command=self._read_source_configuration_files,
        ).pack(side=tk.LEFT, padx=6)

        ttk.Label(frame, textvariable=self.source_config_status).grid(
            row=5, column=0, columnspan=4, sticky=tk.W
        )

    def _on_file_transfer_type_changed(self, _event=None) -> None:
        self.file_transfer_source = None
        self.file_transfer_data = b""
        self.file_transfer_path.set("")
        self.file_transfer_progress.set(0.0)
        self.file_transfer_percent.set("0%")
        self.file_transfer_status.set(
            f"Select a {self.file_transfer_type.get()} file to transfer."
        )
        self._update_file_transfer_type_view()

    def _update_file_transfer_type_view(self) -> None:
        if self.source_config_frame is None or self.file_transfer_selection is None:
            return

        if self.file_transfer_type.get() == "Config":
            for widget in self.file_transfer_file_widgets:
                widget.grid_remove()
            if self.send_transfer_file_button is not None:
                self.send_transfer_file_button.pack_forget()
            self.source_config_frame.pack(
                fill=tk.X,
                pady=(8, 0),
                after=self.file_transfer_selection,
            )
        else:
            self.source_config_frame.pack_forget()
            for widget in self.file_transfer_file_widgets:
                widget.grid()
            if (
                self.send_transfer_file_button is not None
                and not self.send_transfer_file_button.winfo_manager()
            ):
                pack_options: dict[str, object] = {"side": tk.LEFT}
                if self.clear_file_messages_button is not None:
                    pack_options["before"] = self.clear_file_messages_button
                self.send_transfer_file_button.pack(**pack_options)

    def _load_source_configuration_into_editor(self, source_index: int) -> None:
        configuration = self.source_configurations[source_index]
        self.current_source_index = source_index
        self.source_selector.set(self.source_selector_values[source_index])
        self.source_type.set(SOURCE_TYPE_NAMES[configuration.source_type])
        self.source_name.set(configuration.source_name)
        self.source_volume.set(str(configuration.volume))
        self.source_id_label.configure(text=str(configuration.source_id))
        self.source_is_active.set(configuration.is_active)
        self.source_is_muted.set(configuration.is_muted)
        self.source_is_selected.set(configuration.is_selected)
        self.source_config_status.set(
            f"Editing source {source_index + 1} of {NUMBER_OF_AUDIO_SOURCES} "
            f"(ID {configuration.source_id})."
        )

    def _commit_source_configuration_editor(self, show_error: bool = True) -> bool:
        try:
            try:
                volume = int(self.source_volume.get().strip())
            except ValueError as exc:
                raise ValueError("Volume must be a whole number") from exc

            source_type_name = self.source_type.get()
            if source_type_name not in SOURCE_TYPE_IDS:
                raise ValueError("Select a valid Source Type")

            configuration = SourceConfiguration(
                source_type=SOURCE_TYPE_IDS[source_type_name],
                source_id=self.current_source_index,
                source_name=self.source_name.get().strip(),
                volume=volume,
                is_active=self.source_is_active.get(),
                is_muted=self.source_is_muted.get(),
                is_selected=self.source_is_selected.get(),
            )
            validate_source_configuration(configuration)
            self.source_configurations[self.current_source_index] = configuration
            self.source_config_status.set(
                f"Source ID {configuration.source_id} is valid and retained in the editor."
            )
            return True
        except Exception as exc:
            if show_error:
                messagebox.showerror("Invalid source configuration", str(exc))
            return False

    def _navigate_source_configuration(self, direction: int) -> None:
        if not self._commit_source_configuration_editor():
            return

        new_index = (self.current_source_index + direction) % NUMBER_OF_AUDIO_SOURCES
        self._load_source_configuration_into_editor(new_index)

    def _select_source_configuration(self, _event=None) -> None:
        selected_value = self.source_selector.get()
        try:
            new_index = self.source_selector_values.index(selected_value)
        except ValueError:
            self.source_selector.set(self.source_selector_values[self.current_source_index])
            return

        if new_index == self.current_source_index:
            return

        if not self._commit_source_configuration_editor():
            self.source_selector.set(self.source_selector_values[self.current_source_index])
            return

        self._load_source_configuration_into_editor(new_index)

    def _validate_all_source_configurations(self) -> None:
        validate_all_source_configurations(self.source_configurations)

    def _save_source_configuration_project(self) -> None:
        if not self._commit_source_configuration_editor():
            return

        try:
            self._validate_all_source_configurations()
            path_text = filedialog.asksaveasfilename(
                title="Save source configuration project",
                defaultextension=".json",
                filetypes=[("JSON configuration project", "*.json"), ("All files", "*.*")],
                initialfile="source_configurations.json",
            )
            if not path_text:
                return

            document = {
                "schemaVersion": SOURCE_CONFIG_JSON_SCHEMA_VERSION,
                "sourceCount": NUMBER_OF_AUDIO_SOURCES,
                "sources": [
                    source_configuration_to_json(configuration)
                    for configuration in self.source_configurations
                ],
            }
            output_path = Path(path_text).expanduser().resolve()
            temporary_path = output_path.with_name(output_path.name + ".tmp")
            temporary_path.write_text(
                json.dumps(document, indent=4, ensure_ascii=False) + "\n",
                encoding="utf-8",
            )
            temporary_path.replace(output_path)
            self.source_config_status.set(f"Saved JSON project: {output_path}")
            self.append_file_log(f"Saved source configuration project: {output_path}")
        except Exception as exc:
            messagebox.showerror("Save failed", str(exc))

    def _load_source_configuration_project(self) -> None:
        path_text = filedialog.askopenfilename(
            title="Load source configuration project",
            filetypes=[("JSON configuration project", "*.json"), ("All files", "*.*")],
        )
        if not path_text:
            return

        try:
            input_path = Path(path_text).expanduser().resolve()
            document = json.loads(input_path.read_text(encoding="utf-8"))
            if not isinstance(document, dict):
                raise ValueError("Configuration project root must be a JSON object")
            if document.get("schemaVersion") != SOURCE_CONFIG_JSON_SCHEMA_VERSION:
                raise ValueError(
                    f"Unsupported JSON schema version: {document.get('schemaVersion')}"
                )
            if document.get("sourceCount") != NUMBER_OF_AUDIO_SOURCES:
                raise ValueError(
                    f"Project sourceCount must be {NUMBER_OF_AUDIO_SOURCES}"
                )

            source_documents = document.get("sources")
            if not isinstance(source_documents, list):
                raise ValueError("Project sources must be a JSON array")
            if len(source_documents) != NUMBER_OF_AUDIO_SOURCES:
                raise ValueError(
                    f"Project must contain exactly {NUMBER_OF_AUDIO_SOURCES} sources"
                )

            loaded_configurations: list[SourceConfiguration | None] = [
                None
            ] * NUMBER_OF_AUDIO_SOURCES
            for source_document in source_documents:
                configuration = source_configuration_from_json(source_document)
                if loaded_configurations[configuration.source_id] is not None:
                    raise ValueError(f"Duplicate source ID {configuration.source_id}")
                loaded_configurations[configuration.source_id] = configuration

            if any(configuration is None for configuration in loaded_configurations):
                raise ValueError("Project does not define every required source ID")

            self.source_configurations = [
                configuration
                for configuration in loaded_configurations
                if configuration is not None
            ]
            self._validate_all_source_configurations()
            self._load_source_configuration_into_editor(0)
            self.source_config_status.set(f"Loaded JSON project: {input_path}")
            self.append_file_log(f"Loaded source configuration project: {input_path}")
        except Exception as exc:
            messagebox.showerror("Load failed", str(exc))

    def _generate_source_configuration_files(self) -> None:
        if not self._commit_source_configuration_editor():
            return

        try:
            self._validate_all_source_configurations()
            directory_text = filedialog.askdirectory(
                title="Select output directory for source configuration files"
            )
            if not directory_text:
                return

            output_directory = Path(directory_text).expanduser().resolve()
            output_directory.mkdir(parents=True, exist_ok=True)
            output_paths = [
                output_directory / f"source_{source_id:02d}.bin"
                for source_id in range(NUMBER_OF_AUDIO_SOURCES)
            ]
            existing_paths = [path for path in output_paths if path.exists()]
            if existing_paths and not messagebox.askyesno(
                "Replace existing files",
                f"{len(existing_paths)} source configuration file(s) already exist. Replace them?",
            ):
                return

            for configuration, output_path in zip(self.source_configurations, output_paths):
                encoded_configuration = encode_source_configuration(configuration)
                decoded_configuration = decode_source_configuration(encoded_configuration)
                if decoded_configuration != configuration:
                    raise ValueError(
                        f"Source {configuration.source_id}: Generated binary verification failed"
                    )
                temporary_path = output_path.with_name(output_path.name + ".tmp")
                temporary_path.write_bytes(encoded_configuration)
                temporary_path.replace(output_path)

            self.source_config_status.set(
                f"Generated {NUMBER_OF_AUDIO_SOURCES} binary files in {output_directory}"
            )
            self.append_file_log(
                f"Generated {NUMBER_OF_AUDIO_SOURCES} source configuration files in "
                f"{output_directory}"
            )
            messagebox.showinfo(
                "Configuration files generated",
                f"Generated {NUMBER_OF_AUDIO_SOURCES} source configuration files in:\n"
                f"{output_directory}",
            )
        except Exception as exc:
            messagebox.showerror("Generation failed", str(exc))

    def _send_current_source_configuration(self) -> None:
        if not self._commit_source_configuration_editor():
            return

        try:
            self._validate_all_source_configurations()
            configuration = self.source_configurations[self.current_source_index]
            file_data = encode_source_configuration(configuration)
            file_name = f"source_{configuration.source_id:02d}.bin"
            validate_bulk_file_selection(FILE_TYPE_CONFIG, file_name, file_data)
        except Exception as exc:
            messagebox.showerror("Configuration transfer failed", str(exc))
            return

        self.file_transfer_type.set("Config")
        self.file_transfer_path.set("")
        self.file_transfer_source = None
        self.file_transfer_data = b""
        self.start_worker(
            lambda: self._send_transfer_file_worker(
                "Config",
                FILE_TYPE_CONFIG,
                file_name,
                file_data,
            )
        )

    def _read_source_configuration_files(self) -> None:
        directory_text = filedialog.askdirectory(
            title="Select directory for source configuration readback"
        )
        if not directory_text:
            return

        output_directory = Path(directory_text).expanduser().resolve()
        existing_paths = [
            output_directory / f"source_{source_id:02d}.bin"
            for source_id in range(NUMBER_OF_AUDIO_SOURCES)
            if (output_directory / f"source_{source_id:02d}.bin").exists()
        ]
        if existing_paths and not messagebox.askyesno(
            "Replace existing readback files",
            f"{len(existing_paths)} source configuration file(s) already exist. Replace them?",
        ):
            return

        self.start_worker(
            lambda: self._read_source_configuration_files_worker(output_directory)
        )

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
            requested_path = self.file_transfer_path.get().strip()
            if not requested_path:
                raise ValueError(
                    "No transfer file is selected. Use Browse for a file transfer, "
                    "or use Send Current Source for the displayed configuration."
                )

            source = Path(requested_path).expanduser().resolve()
            if not source.exists():
                raise ValueError(f"Transfer file does not exist: {source}")
            if not source.is_file():
                raise ValueError(f"Transfer path is not a file: {source}")

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
            messagebox.showerror(
                "Invalid file type",
                "Select Config, Data, Image, or Icon.",
            )
            return

        source = self.file_transfer_source
        if source is None:
            return

        # Freeze the selected inputs before the worker starts so later UI edits
        # cannot alter a transfer that is already in progress.
        file_data = self.file_transfer_data
        file_name = source.name
        try:
            validate_bulk_file_selection(file_type, file_name, file_data)
        except Exception as exc:
            messagebox.showerror("Invalid transfer file", str(exc))
            return

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

        # Tk variables must only be read by the Tk/UI thread.  Workers use this
        # immutable snapshot; calling StringVar.get() from a worker can block
        # indefinitely on Windows while Tk is servicing another callback.
        try:
            self._worker_connection_settings = SerialConnectionSettings(
                port_name=self.port_name.get().strip(),
                baud_rate=int(self.baud_rate.get()),
                start_delay_s=float(self.start_delay.get()),
                inter_frame_delay_s=float(self.inter_frame_delay.get()),
                byte_delay_s=float(self.byte_delay.get()),
            )
        except ValueError as exc:
            messagebox.showerror("Invalid UART setting", str(exc))
            return

        if not self._worker_connection_settings.port_name:
            messagebox.showerror("Invalid UART setting", "Enter a COM port name.")
            return

        self.worker = threading.Thread(target=target, daemon=True)
        self.worker.start()

    def _open_client(
        self,
        operation: int = OPERATION_FIRMWARE_BRIDGE,
        write_timeout_s: float | None = WRITE_TIMEOUT_S,
        byte_delay_s: float | None = None,
        probe_receiver: bool = False,
    ) -> BridgeClient:
        settings = self._worker_connection_settings
        log_target = "file" if operation == OPERATION_LOCAL_BULK_FILE else "firmware"
        if operation == OPERATION_LOCAL_BULK_FILE:
            self.log_queue.put(
                (
                    "file_log",
                    "bridge",
                    f"Opening {settings.port_name} at {settings.baud_rate} baud",
                )
            )

        client = BridgeClient(
            settings.port_name,
            settings.baud_rate,
            self.log_queue,
            settings.start_delay_s,
            settings.inter_frame_delay_s,
            settings.byte_delay_s if byte_delay_s is None else byte_delay_s,
            log_target,
            write_timeout_s,
        )
        try:
            if probe_receiver:
                self.log_queue.put(("file_log", "bridge", "Probing PC-to-MCU UART receiver"))
                if not client.ping_uart_receiver():
                    raise RuntimeError(
                        "PC-to-MCU UART probe failed: MCU did not return '!' for '?'"
                    )
                self.log_queue.put(("file_log", "bridge", "PC-to-MCU UART receiver probe passed"))
            if operation == OPERATION_LOCAL_BULK_FILE:
                self.log_queue.put(
                    ("file_log", "bridge", f"Selecting PC UART operation {operation}")
                )
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
            client = self._open_client(
                OPERATION_LOCAL_BULK_FILE,
                write_timeout_s=FILE_TRANSFER_WRITE_TIMEOUT_S,
            )
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

    def _read_source_configuration_files_worker(
        self,
        output_directory: Path,
    ) -> None:
        client: BridgeClient | None = None
        total_bytes = NUMBER_OF_AUDIO_SOURCES * SOURCE_CONFIG_RECORD.size
        records: list[bytes] = []
        configurations: list[SourceConfiguration] = []

        self.log_queue.put(("file_progress", 0, total_bytes))
        self.log_queue.put(("file_status", "Reading source configurations from external flash..."))
        self.log_queue.put(
            (
                "file_log",
                "info",
                f"Starting readback of {NUMBER_OF_AUDIO_SOURCES} source configuration files",
            )
        )

        try:
            client = self._open_client(
                OPERATION_LOCAL_BULK_FILE,
                write_timeout_s=FILE_TRANSFER_WRITE_TIMEOUT_S,
            )
            transferred = 0
            for source_id in range(NUMBER_OF_AUDIO_SOURCES):
                record = client.read_source_configuration_record(source_id)
                configuration = decode_source_configuration(record)
                records.append(record)
                configurations.append(configuration)
                transferred += len(record)
                self.log_queue.put(("file_progress", transferred, total_bytes))
                self.log_queue.put(
                    (
                        "file_log",
                        "recovery",
                        f"Read source_{source_id:02d}.bin ({len(record)} bytes)",
                    )
                )

            validate_all_source_configurations(configurations)
            output_directory.mkdir(parents=True, exist_ok=True)
            for source_id, record in enumerate(records):
                output_path = output_directory / f"source_{source_id:02d}.bin"
                temporary_path = output_path.with_name(output_path.name + ".tmp")
                temporary_path.write_bytes(record)
                temporary_path.replace(output_path)

            self.log_queue.put(
                (
                    "source_config_readback",
                    configurations,
                    str(output_directory),
                )
            )
            self.log_queue.put(
                (
                    "file_log",
                    "recovery",
                    f"Readback completed and saved to {output_directory}",
                )
            )
            self.log_queue.put(
                (
                    "file_status",
                    f"Readback complete: {NUMBER_OF_AUDIO_SOURCES} configuration files",
                )
            )
        except Exception as exc:
            self.log_queue.put(("file_log", "error", f"ERROR: {exc}"))
            self.log_queue.put(("file_status", f"Readback failed: {exc}"))
        finally:
            if client is not None:
                client.close()

    def _send_update_worker(self, omit_ids: set[int]) -> None:
        try:
            with_bridge = self._open_client()
            try:
                self.log_queue.put(("info", "Starting firmware update request"))
                with_bridge.send_bootloader_frame(CMD_FW_UP_REQ, build_fw_update_request(self.image))
                with_bridge.wait_for_slave_ack(CMD_FW_UP_REQ, TARGET_READY_TIMEOUT_S)
                self.log_queue.put(("info", "Target is ready to receive firmware data"))

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
                elif len(item) == 3 and item[0] == "source_config_readback":
                    _event, configurations, output_directory = item
                    self.source_configurations = list(configurations)
                    self._load_source_configuration_into_editor(0)
                    self.source_config_status.set(
                        f"Read back configurations from external flash; saved to {output_directory}"
                    )
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
