# Third-party source and dependencies

## WCH sources included in the publication set

The sources under `firmware/keyboard/vendor/` and WCH-derived HAL sources
`src/mcu.c` and `src/rtc.c` retain their original copyright notices:
Nanjing Qinheng Microelectronics Co., Ltd. Their file headers specify
`SPDX-License-Identifier: Apache-2.0`; the license text is in
`LICENSE`.

Source snapshot used during development:
[SoCXin/CH573](https://github.com/SoCXin/CH573/tree/c10f7341c92647eca9a3545a5cd3943121e79d21/src/EVT/EXAM),
commit `c10f7341c92647eca9a3545a5cd3943121e79d21`.
This repository is a mirror of SDK material, not the chip manufacturer.
The copied SDK sources have per-file license identifiers; no repository-wide
LICENSE or NOTICE file was found in that snapshot.

Project modifications include stripped HAL support, polled RTC startup, SysTick without interrupts,
HID-only service registration/table changes and configuration for this
application. Vendor headers were converted from their original GBK encoding to
UTF-8. Original notices remain in each copied file; changed copies are marked.

## External build dependencies — not redistributed

The build uses the SDK linker script, SDK headers, `startup_CH573.S`, `CH57x_sys.c`, `CH57x_adc.c`, `CH57x_clk.c`,
`LIBCH57xBLE.a` and `libISP573.a` from the SDK downloaded by PlatformIO. The binary
libraries implement the BLE stack and chip ISP/flash operations. Their
redistribution terms have not been established by this project. The file-level
Apache identifiers on the included sources do not by themselves establish
redistribution rights for those libraries or a linked BIN image.

The SDK linker script has no file-level license in the tested snapshot, so it
is not included. The PlatformIO builder adapts its downloaded copy at build time.
The compiler and SDK are fetched automatically into dependency storage; neither
is committed here. Generated BIN/ELF files are excluded from the source export.

## Original project material

Project-original code, scripts, documentation and control-map illustrations are
licensed under Apache-2.0; see `LICENSE` and `NOTICE`. The pinout illustration
in `docs/images/` has editable SVG source. Controller contours were drawn manually
using a product render as a visual reference; labels and GPIO data are project
work. No source photograph or render is embedded or redistributed. The project
license covers project-authored material and grants no rights to third-party
artwork, trademarks or product designs.

## Material not included

Factory update images, loader reads/disassembly, vendor manuals, product renders
and photographs are not redistributed in this source release.
