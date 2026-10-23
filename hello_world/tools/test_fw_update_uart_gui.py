#!/usr/bin/env python3

import unittest
import zlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fw_update_uart_gui as tool


class RecordingSerialPort:
    def __init__(self) -> None:
        self.writes: list[bytes] = []

    def write(self, data: bytes) -> int:
        self.writes.append(bytes(data))
        return len(data)


class SourceConfigurationTests(unittest.TestCase):
    def test_reads_only_active_firmware_source_ids(self) -> None:
        self.assertEqual(tool.NUMBER_OF_AUDIO_SOURCES, 4)

    def test_binary_record_round_trip(self) -> None:
        configuration = tool.SourceConfiguration(
            source_type=tool.SOURCE_TYPE_IDS["Bluetooth"],
            source_id=2,
            source_name="Lobby Bluetooth",
            volume=56,
            is_active=True,
            is_muted=False,
            is_selected=True,
        )

        record = tool.encode_source_configuration(configuration)

        self.assertEqual(len(record), 48)
        self.assertEqual(tool.decode_source_configuration(record), configuration)
        stored_crc = int.from_bytes(record[12:16], "little")
        self.assertEqual(
            stored_crc,
            zlib.crc32(record[tool.SOURCE_CONFIG_CRC_OFFSET:]) & 0xFFFFFFFF,
        )

    def test_binary_record_detects_corruption(self) -> None:
        record = bytearray(
            tool.encode_source_configuration(tool.default_source_configuration(0))
        )
        record[-1] ^= 0x01

        with self.assertRaisesRegex(ValueError, "CRC32 mismatch"):
            tool.decode_source_configuration(bytes(record))

    def test_active_source_requires_a_type(self) -> None:
        configuration = tool.default_source_configuration(0)
        configuration.is_active = True

        with self.assertRaisesRegex(ValueError, "cannot have Source Type None"):
            tool.validate_source_configuration(configuration)

    def test_selected_source_must_be_active(self) -> None:
        configuration = tool.default_source_configuration(0)
        configuration.is_selected = True

        with self.assertRaisesRegex(ValueError, "must also be active"):
            tool.validate_source_configuration(configuration)

    def test_only_one_source_may_be_selected(self) -> None:
        configurations = [
            tool.default_source_configuration(source_id)
            for source_id in range(tool.NUMBER_OF_AUDIO_SOURCES)
        ]
        for configuration in configurations[:2]:
            configuration.source_type = tool.SOURCE_TYPE_IDS["Wired"]
            configuration.is_active = True
            configuration.is_selected = True

        with self.assertRaisesRegex(ValueError, "Only one source may be selected"):
            tool.validate_all_source_configurations(configurations)

    def test_json_round_trip(self) -> None:
        configuration = tool.SourceConfiguration(
            source_type=tool.SOURCE_TYPE_IDS["Wired"],
            source_id=1,
            source_name="Lectern",
            volume=42,
            is_active=True,
            is_muted=True,
            is_selected=False,
        )

        document = tool.source_configuration_to_json(configuration)

        self.assertEqual(document["sourceType"], "Wired")
        self.assertEqual(tool.source_configuration_from_json(document), configuration)

    def test_config_transfer_accepts_matching_source_filename(self) -> None:
        configuration = tool.default_source_configuration(0)
        record = tool.encode_source_configuration(configuration)

        tool.validate_bulk_file_selection(
            tool.FILE_TYPE_CONFIG,
            "source_00.bin",
            record,
        )

    def test_config_transfer_rejects_image_binary(self) -> None:
        with self.assertRaisesRegex(ValueError, "48 bytes|record"):
            tool.validate_bulk_file_selection(
                tool.FILE_TYPE_CONFIG,
                "BluetoothIcon.bin",
                b"not a source configuration",
            )

    def test_nonblocking_windows_write_submits_one_complete_frame(self) -> None:
        client = tool.BridgeClient.__new__(tool.BridgeClient)
        client.port = RecordingSerialPort()
        client.write_timeout_s = 0
        client.byte_delay_s = 0.001
        frame = b"BLUP\x01\x01\x00\x00\x00\x01\x02\x00\x00"

        client._write_raw_once(frame)

        self.assertEqual(client.port.writes, [frame])


if __name__ == "__main__":
    unittest.main()
