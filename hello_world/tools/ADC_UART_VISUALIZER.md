# ADC UART Visualizer

Run:

```powershell
python .\hello_world\tools\adc_uart_visualizer.py
```

The only non-standard Python dependency is `pyserial`:

```powershell
python -m pip install pyserial
```

Use **Simulate** to test the complete UI without an MCU connection.

Selecting a COM port or baud rate automatically opens the UART connection. Use
**Disconnect** to close it manually. Immediately after connecting, the PC sends
the single byte `A` to request the current ADC channel configuration.

## Startup Configuration

Immediately after the PC connects, the MCU sends the ASCII bytes `ADCF`, one byte
containing the descriptor count, then one four-byte descriptor for every enabled
channel/value-type combination:

| Byte | Meaning |
|---|---|
| 0 | ADC module: `0 = ADC0`, `1 = ADC1` |
| 1 | ADC channel: `0..31` |
| 2 | Resolution: `0 = 12-bit`, `1 = 16-bit` |
| 3 | Value type: `0 = ADC Value`, `1 = Max`, `2 = Min`, `3 = Avg`, `4 = RMS` |

The MCU also sends these descriptors during ADC initialization. The `A` request
allows the PC to reconnect after MCU startup without requiring an MCU reset.

Measurements not matching an exact descriptor from the current configuration are
discarded. This prevents unrelated shared-UART traffic from creating false channels.

For compatibility with older MCU firmware that does not answer the `A` request with
an `ADCF` frame, the visualizer can discover descriptors that repeat at least three
times during one second of valid measurement traffic. One-off UART noise is ignored.

## Measurement Packet

After the startup silent gap, each measurement packet is exactly six bytes:

| Byte | Meaning |
|---|---|
| 0 | ADC module |
| 1 | ADC channel |
| 2 | Resolution |
| 3 | Value type |
| 4 | Measured value MSB |
| 5 | Measured value LSB |

The PC tool expects the measured value to be a resolution-normalized raw code:

- 12-bit: `0..4095`
- 16-bit: `0..65535`

The current packet format has no start marker or checksum. The decoder validates the
four metadata bytes and slides through invalid UART data, but a metadata-like noise
sequence can still be accepted as a packet. Add framing and a checksum before using
the stream for production measurements.

## Behavior

- Startup descriptors create channels and graphs before measurements arrive.
- Each configured channel has its own visualization area.
- Every value type configured for a channel is displayed as a separate graph
  inside that channel's visualization area.
- Every channel has a visibility checkbox and direct known-input-voltage field.
- Only ADC modules reported by startup descriptors appear in the reference selector.
- Reference voltage is configured independently for each reported ADC module.
- Dashed graph lines represent the configured input voltage.
- Average, variance, standard deviation, and maximum absolute deviation from the
  configured input are computed independently for every channel/value-type graph.
- Serial errors or three seconds without UART data trigger automatic reconnection.
  Use **Disconnect** to stop automatic reconnection.
