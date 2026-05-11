#!/usr/bin/env python3
"""
Send an MCUboot signed firmware image to an FRDM board over UART.

The receiving board can later act as a UART-to-CAN bridge by unpacking each
UART frame and forwarding `cmd + payload` as one CAN FD bootloader frame.
"""

from __future__ import annotations

import argparse
import struct
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError:
    serial = None


# ---------------------------------------------------------------------------
# User configuration
# ---------------------------------------------------------------------------

COM_PORT = "COM7"
BAUD_RATE = 115200
FIRMWARE_IMAGE = r"..\debug\zephyr\zephyr.signed.bin"

HW_REV = 0x01
LIB_REV = 0x0001
FW_REV = 0x0001

WAIT_FOR_ACK = False
ACK_TIMEOUT_S = 2.0
WRITE_TIMEOUT_S = 10.0
START_DELAY_S = 1.0
INTER_FRAME_DELAY_S = 0.002
TX_BYTE_DELAY_S = 0.0


# ---------------------------------------------------------------------------
# Bootloader protocol values
# ---------------------------------------------------------------------------

CMD_FW_UP_REQ = 20
CMD_FW_UP_MSG = 21
CMD_FW_UP_END = 22

FW_UP_REQ_PAYLOAD_LEN = 10
FW_UP_MSG_PAYLOAD_LEN = 50
FW_UP_MSG_DATA_LEN = 48

MAX_FW_SIZE_24BIT = 0xFFFFFF


# ---------------------------------------------------------------------------
# UART transport framing
# ---------------------------------------------------------------------------

UART_SOF = b"BLUP"
UART_VERSION = 1
UART_ACK = 0x06
UART_NACK = 0x15


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
    crc_input = header[4:] + payload
    frame_crc = crc16_ccitt(crc_input)
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


def iter_fw_data_packets(image: bytes):
    packet_id = 0
    for offset in range(0, len(image), FW_UP_MSG_DATA_LEN):
        chunk = image[offset : offset + FW_UP_MSG_DATA_LEN]
        if len(chunk) < FW_UP_MSG_DATA_LEN:
            chunk = chunk + bytes([0xFF] * (FW_UP_MSG_DATA_LEN - len(chunk)))

        payload = struct.pack(">H", packet_id) + chunk
        if len(payload) != FW_UP_MSG_PAYLOAD_LEN:
            raise RuntimeError("Internal packet formatting error")

        yield packet_id, payload
        packet_id = (packet_id + 1) & 0xFFFF


def wait_for_ack(port, command: int, sequence: int) -> None:
    response = port.read(2)
    if len(response) != 2:
        raise TimeoutError(f"Timeout waiting for ACK: cmd={command}, seq={sequence}")

    status, error_code = response[0], response[1]
    if status == UART_ACK:
        return
    if status == UART_NACK:
        raise RuntimeError(f"NACK received: cmd={command}, seq={sequence}, error={error_code}")

    raise RuntimeError(f"Unexpected UART response 0x{status:02X}: cmd={command}, seq={sequence}")


def write_bytes(port, data: bytes, byte_delay: float) -> None:
    if byte_delay <= 0:
        port.write(data)
        return

    for byte in data:
        port.write(bytes([byte]))
        time.sleep(byte_delay)


def send_frame(port, command: int, sequence: int, payload: bytes, wait_ack: bool, byte_delay: float) -> None:
    frame = build_uart_frame(command, sequence, payload)
    write_bytes(port, frame, byte_delay)
    port.flush()

    if wait_ack:
        wait_for_ack(port, command, sequence)

    if INTER_FRAME_DELAY_S > 0:
        time.sleep(INTER_FRAME_DELAY_S)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Send signed firmware image over UART")
    parser.add_argument("--port", default=COM_PORT, help="Serial COM port, for example COM7")
    parser.add_argument("--baud", type=int, default=BAUD_RATE, help="UART baud rate")
    parser.add_argument("--image", default=FIRMWARE_IMAGE, help="Path to signed firmware image")
    parser.add_argument("--probe", action="store_true", help="Send a one-byte UART probe and expect a one-byte response")
    parser.add_argument("--req-debug", action="store_true", help="Send only FWUpReq and print bridge parser debug bytes")
    parser.add_argument("--wait-ack", action="store_true", default=WAIT_FOR_ACK, help="Wait for 2-byte ACK/NACK after each frame")
    parser.add_argument("--start-delay", type=float, default=START_DELAY_S, help="Delay after opening the COM port before sending")
    parser.add_argument("--delay", type=float, default=INTER_FRAME_DELAY_S, help="Delay between frames in seconds")
    parser.add_argument("--byte-delay", type=float, default=TX_BYTE_DELAY_S, help="Delay between individual UART bytes in seconds")
    parser.add_argument("--write-timeout", type=float, default=WRITE_TIMEOUT_S, help="UART write timeout in seconds")
    return parser.parse_args()


def resolve_image_path(path_text: str) -> Path:
    path = Path(path_text)
    if path.is_absolute():
        return path.resolve()

    cwd_path = (Path.cwd() / path).resolve()
    if cwd_path.exists():
        return cwd_path

    script_path = (Path(__file__).resolve().parent / path).resolve()
    return script_path


def main() -> int:
    global INTER_FRAME_DELAY_S

    args = parse_args()
    INTER_FRAME_DELAY_S = args.delay

    if serial is None:
        print("Missing dependency: pyserial. Install with: python -m pip install pyserial", file=sys.stderr)
        return 2

    image_path = resolve_image_path(args.image)
    image = image_path.read_bytes()
    image_crc = crc16_ccitt(image)

    print(f"UART port     : {args.port}")
    print(f"Baud rate     : {args.baud}")
    print(f"Image         : {image_path}")
    print(f"Image size    : {len(image)} bytes")
    print(f"Image CRC16   : 0x{image_crc:04X}")
    print(f"Wait for ACK  : {args.wait_ack}")

    req_payload = build_fw_update_request(image)
    total_packets = (len(image) + FW_UP_MSG_DATA_LEN - 1) // FW_UP_MSG_DATA_LEN

    with serial.Serial(args.port, args.baud, timeout=ACK_TIMEOUT_S, write_timeout=args.write_timeout) as port:
        if args.start_delay > 0:
            time.sleep(args.start_delay)

        port.reset_input_buffer()

        if args.probe:
            port.write(b"?")
            port.flush()
            response = port.read(1)
            if response == b"!":
                print("UART probe OK")
                return 0
            if len(response) == 0:
                print("UART probe timeout", file=sys.stderr)
                return 1
            print(f"UART probe unexpected response: 0x{response[0]:02X}", file=sys.stderr)
            return 1

        if args.req_debug:
            port.write(b"D")
            port.flush()
            response = port.read(1)
            if response != b"d":
                if len(response) == 0:
                    print("Debug enable timeout", file=sys.stderr)
                else:
                    print(f"Debug enable unexpected response: 0x{response[0]:02X}", file=sys.stderr)
                return 1

            send_frame(port, CMD_FW_UP_REQ, 0, req_payload, False, args.byte_delay)
            print("Sent FWUpReq debug frame")
            debug_bytes = port.read(32)
            if len(debug_bytes) == 0:
                print("No parser debug bytes received", file=sys.stderr)
                return 1

            printable = " ".join(chr(byte) if 32 <= byte <= 126 else f"0x{byte:02X}" for byte in debug_bytes)
            print(f"Bridge debug bytes: {printable}")
            return 0

        sequence = 0

        send_frame(port, CMD_FW_UP_REQ, sequence, req_payload, args.wait_ack, args.byte_delay)
        sequence += 1

        for packet_id, payload in iter_fw_data_packets(image):
            send_frame(port, CMD_FW_UP_MSG, sequence, payload, args.wait_ack, args.byte_delay)
            sequence += 1

            if packet_id == 0 or (packet_id + 1) % 64 == 0 or packet_id + 1 == total_packets:
                print(f"Sent packet {packet_id + 1}/{total_packets}")

        send_frame(port, CMD_FW_UP_END, sequence, b"", args.wait_ack, args.byte_delay)

    print("Firmware transfer completed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
