#!/usr/bin/env python3
"""Flash an APP image through the preserved MOCUTE-052F CH573 BLE IAP.

It refuses unknown targets and never writes outside the OTA staging range. The loader
reports a 0x28000 relocation offset: it receives the raw image at 0x1000 and
makes it executable at 0x29000.
"""

import argparse
import asyncio
import hashlib
import math
from pathlib import Path

from bleak import BleakClient
from ota_connection import find_loader, connect_loader, upload_lock


SERVICE_UUID = "00001000-e619-419b-bc43-821e71a409b7"
CHAR_UUID = "00001004-e619-419b-bc43-821e71a409b7"
APP_START = 0x1000
EXPECTED_RELOCATION_OFFSET = 0x28000
EXPECTED_BLOCK_SIZE = 0x1000
EXPECTED_TARGET = "MOCUTE-VTF13-052-B"


def frame(command: int, length: int = 0) -> bytearray:
    result = bytearray(20)
    result[0] = command
    result[1] = length
    return result


GET_NAME = bytes(frame(0x8A, 18))
GET_INFO = bytes(frame(0x84, 18))
END = bytes(frame(0x83, 18))


async def read_reply(client: BleakClient, expected_length: int) -> bytes:
    last_error = None
    for _ in range(20):
        try:
            reply = bytes(await client.read_gatt_char(CHAR_UUID))
            if len(reply) == expected_length:
                return reply
        except Exception as error:
            last_error = error
        await asyncio.sleep(0.05)
    raise RuntimeError(f"OTA loader did not reply: {last_error}")


async def request(client: BleakClient, payload: bytes) -> bytes:
    await client.write_gatt_char(CHAR_UUID, payload, response=False)
    await asyncio.sleep(0.05)
    return await read_reply(client, 20 if payload[0] in (0x8A, 0x84) else 2)


def address_frame(command: int, address: int, data: bytes) -> bytes:
    if address % 16:
        raise ValueError("address must be 16-byte aligned")
    if len(data) != 16:
        raise ValueError("program and verify frames require exactly 16 bytes")
    result = frame(command, 16)
    word_address = address // 16
    result[2] = word_address & 0xFF
    result[3] = (word_address >> 8) & 0xFF
    result[4:20] = data
    return bytes(result)


def erase_frame(address: int, blocks: int) -> bytes:
    result = frame(0x81)
    word_address = address // 16
    result[2] = word_address & 0xFF
    result[3] = (word_address >> 8) & 0xFF
    result[4] = blocks & 0xFF
    result[5] = (blocks >> 8) & 0xFF
    return bytes(result)


def parse_info(raw: bytes) -> tuple[int, int]:
    if len(raw) != 20:
        raise RuntimeError(f"unexpected GET INFO length: {len(raw)}")
    return int.from_bytes(raw[1:5], "little"), int.from_bytes(raw[5:7], "little")


async def main(image_path: Path, verify_only: bool = False, quick_verify: bool = False,
               device=None) -> None:
    image = image_path.read_bytes()
    if not image:
        raise SystemExit("refusing to flash an empty image")
    if len(image) > EXPECTED_RELOCATION_OFFSET:
        raise SystemExit(f"image is too large: {len(image)} bytes")

    digest = hashlib.sha256(image).hexdigest()
    print(f"Image: {image_path}")
    print(f"Size: {len(image)} bytes; SHA-256: {digest}")
    print("Scanning for MOCUTE-FS-OTA...")
    if device is None:
        device = await find_loader(timeout=30)
    if device is None:
        raise SystemExit("OTA loader not found")

    print(f"Found: {device.name} ({device.address})")
    disconnected = asyncio.Event()
    async with connect_loader(device, lambda _: disconnected.set()) as client:
        disconnected.clear()
        if client.services.get_service(SERVICE_UUID) is None:
            raise SystemExit("expected CH573 OTA service is absent")

        target_raw = await request(client, GET_NAME)
        target = target_raw.rstrip(b"\0").decode("ascii", errors="replace").strip()
        info_raw = await request(client, GET_INFO)
        relocation_offset, block_size = parse_info(info_raw)

        print(
            f"Target: {target}; relocation: 0x{relocation_offset:05x}; "
            f"block: {block_size}"
        )
        if target != EXPECTED_TARGET:
            raise SystemExit(f"refusing unknown target: {target!r}")
        if (
            relocation_offset != EXPECTED_RELOCATION_OFFSET
            or block_size != EXPECTED_BLOCK_SIZE
        ):
            raise SystemExit("refusing unexpected loader memory layout")

        if not verify_only:
            blocks = math.ceil(len(image) / block_size)
            print(f"Erasing {blocks} block(s) from 0x{APP_START:04x}...")
            erase_reply = await request(client, erase_frame(APP_START, blocks))
            if erase_reply[0] != 0:
                raise RuntimeError(f"erase failed: {erase_reply.hex(' ')}")

            padded = image + bytes([0xFF]) * (-len(image) % 16)
            total_frames = len(padded) // 16
            print(f"Programming {total_frames} frame(s)...")
            for index in range(total_frames):
                address = APP_START + index * 16
                chunk = padded[index * 16 : index * 16 + 16]
                await client.write_gatt_char(
                    CHAR_UUID, address_frame(0x80, address, chunk), response=False
                )
                await asyncio.sleep(0.012)
                if (index + 1) % 256 == 0 or index + 1 == total_frames:
                    print(f"  {index + 1}/{total_frames}")

        padded = image + bytes([0xFF]) * (-len(image) % 16)
        offsets = ([0, (len(padded)//32)*16, len(padded)-16] if quick_verify
                   else range(0, len(padded), 16))
        print("Checking three sample frames..." if quick_verify else "Verifying every frame...", flush=True)
        for offset in offsets:
            reply = await request(client, address_frame(
                0x82, APP_START + offset, padded[offset:offset + 16]))
            if not reply or reply[0] != 0:
                raise RuntimeError(
                    f"VERIFY failed at protocol address 0x{APP_START + offset:05x}: "
                    f"{reply.hex(' ')}; END was NOT sent")
            if offset % 4096 == 0:
                print(f"  verified {offset}/{len(padded)} bytes", flush=True)
        print("Sample verification passed (not a full-image check)." if quick_verify else "Flash verification passed.", flush=True)
        if verify_only:
            return

        print("Finishing update; the controller should reboot...")
        await client.write_gatt_char(CHAR_UUID, END, response=False)
        try:
            await asyncio.wait_for(disconnected.wait(), timeout=15.0)
        except asyncio.TimeoutError:
            raise RuntimeError("Image verified, but no disconnect after END; reboot is unconfirmed")

    print("Verification step passed; END sent; device disconnected. Application boot still needs checking.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("--verify-only", action="store_true",
                        help="Compare flash only; no erase/program/END (verify may latch loader status)")
    parser.add_argument("--quick-verify", action="store_true")
    parser.add_argument("--address", help="Bluetooth address of the factory OTA loader")
    args = parser.parse_args()
    try:
        async def run():
            device = await find_loader(args.address, timeout=30)
            if device is None:
                raise SystemExit("OTA loader not found; enter X+Y+Power first")
            await main(args.image.resolve(), args.verify_only, args.quick_verify, device)
        with upload_lock():
            asyncio.run(run())
    except (EOFError, TimeoutError) as error:
        raise SystemExit(f'Host connection interrupted ({type(error).__name__}). '
                         'No automatic write retry; rerun upload to erase and write the full image.')
