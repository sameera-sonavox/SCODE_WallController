#!/usr/bin/env python3

import math
import queue
import time
import unittest

import adc_uart_visualizer
from adc_uart_visualizer import ADCDescriptor, ADCPacket, ChannelState, PacketDecoder, UARTWorker


class PacketDecoderTests(unittest.TestCase):
    def test_decodes_startup_descriptor(self) -> None:
        decoder = PacketDecoder()
        items = decoder.feed(b"ADCF" + bytes((1, 0, 7, 1, 3)))

        self.assertEqual(items, [ADCDescriptor(0, 7, 1, 3)])

    def test_ignores_valid_looking_noise_before_configuration_header(self) -> None:
        decoder = PacketDecoder()
        items = decoder.feed(bytes((1, 3, 1, 0)) + b"ADCF" + bytes((1, 0, 7, 1, 3)))

        self.assertEqual(items, [ADCDescriptor(0, 7, 1, 3)])

    def test_recovers_from_corrupted_configuration_block(self) -> None:
        decoder = PacketDecoder()
        corrupted = b"ADCF" + bytes((1, 0xFF, 7, 1, 3))
        valid = b"ADCF" + bytes((1, 0, 7, 1, 3))

        items = decoder.feed(corrupted + valid)

        self.assertEqual(items, [ADCDescriptor(0, 7, 1, 3)])

    def test_decodes_packet_and_big_endian_value(self) -> None:
        decoder = PacketDecoder()
        decoder.finish_startup()
        packets = decoder.feed(bytes((1, 31, 0, 4, 0x12, 0x34)), timestamp=10.0)

        self.assertEqual(len(packets), 1)
        self.assertEqual(packets[0].module, 1)
        self.assertEqual(packets[0].channel, 31)
        self.assertEqual(packets[0].measured_value, 0x1234)

    def test_recovers_after_invalid_byte(self) -> None:
        decoder = PacketDecoder()
        decoder.finish_startup()
        packets = decoder.feed(bytes((0xFF, 0, 1, 1, 0, 0x03, 0x09)))

        self.assertEqual(len(packets), 1)
        self.assertEqual(packets[0].measured_value, 777)
        self.assertEqual(decoder.discarded_bytes, 1)


class ChannelStateTests(unittest.TestCase):
    def test_converts_normalized_12_bit_code_to_voltage(self) -> None:
        state = ChannelState(module=0, channel=1, reference_voltage=3.3)
        packet = ADCPacket(0, 1, 0, 0, 769, time.monotonic())
        state.update(packet, 10.0)

        self.assertTrue(math.isclose(state.last_voltage, 0.6197, rel_tol=0.002))

    def test_statistics_are_independent_per_channel(self) -> None:
        first = ChannelState(module=0, channel=0, reference_voltage=3.3)
        second = ChannelState(module=1, channel=0, reference_voltage=3.3)
        now = time.monotonic()

        for raw in (1000, 1010, 1020):
            first.update(ADCPacket(0, 0, 0, 0, raw, now), 10.0)
        for raw in (2000, 2010, 2020):
            second.update(ADCPacket(1, 0, 0, 0, raw, now), 10.0)

        self.assertNotEqual(first.stats(0)[0], second.stats(0)[0])

    def test_stats_and_max_deviation_are_per_value_type(self) -> None:
        state = ChannelState(module=0, channel=0, reference_voltage=3.3, input_voltage=1.0)
        now = time.monotonic()
        state.update(ADCPacket(0, 0, 0, 1, 1000, now), 10.0)
        state.update(ADCPacket(0, 0, 0, 2, 2000, now), 10.0)

        self.assertNotEqual(state.stats(1)[0], state.stats(2)[0])
        self.assertNotEqual(state.stats(1)[3], state.stats(2)[3])


class UARTWorkerTests(unittest.TestCase):
    def test_discovers_repeated_legacy_measurement_descriptors(self) -> None:
        packet = bytes((0, 6, 1, 1, 0x12, 0x34))

        class StreamingUART:
            in_waiting = len(packet)
            reads = 0

            def __init__(self, *_args, **_kwargs) -> None:
                pass

            def __enter__(self):
                return self

            def __exit__(self, *_args) -> None:
                pass

            def reset_input_buffer(self) -> None:
                pass

            def reset_output_buffer(self) -> None:
                pass

            def write(self, _data: bytes) -> None:
                pass

            def flush(self) -> None:
                pass

            def read(self, _size: int) -> bytes:
                time.sleep(0.002)
                StreamingUART.reads += 1
                return packet if StreamingUART.reads <= 8 else b""

        class StreamingSerialModule:
            Serial = StreamingUART

        old_serial = adc_uart_visualizer.serial
        old_discovery_time = adc_uart_visualizer.LEGACY_DISCOVERY_TIME_S
        old_confirmations = adc_uart_visualizer.LEGACY_DESCRIPTOR_CONFIRMATIONS
        old_timeout = adc_uart_visualizer.UART_RX_TIMEOUT_S
        adc_uart_visualizer.serial = StreamingSerialModule()
        adc_uart_visualizer.LEGACY_DISCOVERY_TIME_S = 0.005
        adc_uart_visualizer.LEGACY_DESCRIPTOR_CONFIRMATIONS = 3
        adc_uart_visualizer.UART_RX_TIMEOUT_S = 0.03
        events: queue.Queue[object] = queue.Queue()
        try:
            worker = UARTWorker("COM_TEST", 115200, events)
            worker.start()
            worker.join(timeout=0.5)
        finally:
            adc_uart_visualizer.serial = old_serial
            adc_uart_visualizer.LEGACY_DISCOVERY_TIME_S = old_discovery_time
            adc_uart_visualizer.LEGACY_DESCRIPTOR_CONFIRMATIONS = old_confirmations
            adc_uart_visualizer.UART_RX_TIMEOUT_S = old_timeout

        emitted = []
        while not events.empty():
            emitted.append(events.get_nowait())
        self.assertIn(ADCDescriptor(0, 6, 1, 1), emitted)
        self.assertIn(("configured", "COM_TEST (discovered stream)"), emitted)

    def test_reports_error_when_open_uart_stops_receiving(self) -> None:
        class SilentUART:
            in_waiting = 0
            write_count = 0

            def __init__(self, *_args, **_kwargs) -> None:
                pass

            def __enter__(self):
                return self

            def __exit__(self, *_args) -> None:
                pass

            def reset_input_buffer(self) -> None:
                pass

            def reset_output_buffer(self) -> None:
                pass

            def write(self, _data: bytes) -> None:
                SilentUART.write_count += 1

            def flush(self) -> None:
                pass

            def read(self, _size: int) -> bytes:
                time.sleep(0.002)
                return b""

        class SilentSerialModule:
            Serial = SilentUART

        old_serial = adc_uart_visualizer.serial
        old_timeout = adc_uart_visualizer.UART_RX_TIMEOUT_S
        old_request_interval = adc_uart_visualizer.CONFIG_REQUEST_INTERVAL_S
        adc_uart_visualizer.serial = SilentSerialModule()
        adc_uart_visualizer.UART_RX_TIMEOUT_S = 0.03
        adc_uart_visualizer.CONFIG_REQUEST_INTERVAL_S = 0.005
        events: queue.Queue[object] = queue.Queue()
        try:
            worker = UARTWorker("COM_TEST", 115200, events)
            worker.start()
            worker.join(timeout=0.5)
        finally:
            adc_uart_visualizer.serial = old_serial
            adc_uart_visualizer.UART_RX_TIMEOUT_S = old_timeout
            adc_uart_visualizer.CONFIG_REQUEST_INTERVAL_S = old_request_interval

        self.assertFalse(worker.is_alive())
        self.assertEqual(events.get_nowait(), ("connected", "COM_TEST"))
        error = events.get_nowait()
        self.assertEqual(error[0], "error")
        self.assertIn("No UART data received", error[1])
        self.assertGreater(SilentUART.write_count, 1)


if __name__ == "__main__":
    unittest.main()
