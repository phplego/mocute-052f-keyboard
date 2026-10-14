# BLE diagnostics

The keyboard includes a custom GATT service. The Linux client in `tools/`
uses BlueZ and preserves the keyboard connection when it exits.

Service UUID: `925dd001-8a86-4ab8-ae90-620fb46c19e4`.
Read/write characteristic: `925dd002-8a86-4ab8-ae90-620fb46c19e4`.
Replies are 20 bytes: build identifier, opcode, GAP state, status, then 16 payload bytes.

| Opcode | Function | Client command |
| --- | --- | --- |
| 01 | TMOS clock | `ping` |
| 02 | LED control | `led` |
| 03 | Aligned on-chip flash read | `dump` |
| 04 | Reset | `reset` |
| 06 | Inputs, ADC, HID state and calibration results | `status`, `watch` |
| 07 | Last HID report and counters | `status` |
| 08 | RTC/SysTick settings and tuning | `clock` |

Command encoding and payload parsing are in `tools/control.py`.
Opcode 05 is reserved and rejected by the public build.

The diagnostic characteristic does not require encryption or additional command
authorization. Flash reads can include pairing data; treat dumps as private.
Diagnostics require a running BLE stack and cannot report a failure before BLE startup.
