# PC UART protocol

The PC UART API keeps the existing `BLUP` version-1 frame envelope:

```text
"BLUP" | version:u8 | command:u8 | sequence:u16 | payload-length:u16 |
payload | CRC16:u16
```

All multi-byte fields are big-endian. The frame CRC is CRC16-CCITT with an
initial value of `0xFFFF` and covers the header after `BLUP` plus the payload.

## Operation selection

The hub starts in `Idle`. The PC must send `SelectOperation` (`0x01`) with one
operation byte before sending service commands:

- `1`: firmware bridge
- `2`: local bulk file

`ReleaseOperation` (`0x02`) has no payload. A release is rejected while a local
bulk session is active. A completed bulk END automatically returns the hub to
Idle.

The MCU returns the existing two-byte response after each accepted UART frame:

```text
ACK/NACK | PC-UART error code
```

## Firmware bridge

When firmware bridge mode is selected, existing bootloader commands `20..26`
are forwarded to CAN unchanged. CAN replies are framed with the same `BLUP`
envelope and returned to the PC. The bridge remains limited to one CAN-FD
payload: command byte plus at most 63 payload bytes.

## Local bulk file

### START (`0x40`)

```text
file-type:u8 | file-size:u32 | file-CRC16:u16 | name-length:u8 | name:N bytes
```

The filename does not contain a trailing NUL. The MCU chooses the LittleFS
directory from the file type: `Config`, `Data`, `Image`, or `Icon`.

### DATA (`0x41`)

```text
frame-id:u32 | data-length:u16 | data-CRC16:u16 | data:N bytes
```

Frame IDs begin at zero and increase by one. Data length is `1..256`. The data
CRC uses CRC16-CCITT with initial value `0xFFFF` and covers only the data bytes.

### END (`0x42`)

END has no payload. The UART frontend calls `vEnd_BulkDataTransfer()`, which
queues an internal END marker and waits until every earlier accepted DATA frame
has been written before checking the complete-file length and CRC.

## Firmware-side validation

Set `PC_UART_API_ENABLE_BULK_VALIDATION_TEST` to `1U` in
`PC_UART_API_ProjDef.h`. On startup the test routes a 4097-byte generated file
through operation selection and bulk START/DATA/END, reads it back from
LittleFS, verifies every byte, and deletes the test file.
