#!/usr/bin/env python3
"""Host diagnostics for a paired MOCUTE-052F Custom keyboard (protocol build 6)."""
import argparse
import asyncio
import struct
from pathlib import Path
from radio import RadioClient

UUID = '925dd002-8a86-4ab8-ae90-620fb46c19e4'

async def main(args):
    async with RadioClient() as client:
        async def command(data):
            await client.write_gatt_char(UUID, data, response=True)
            reply = bytes(await client.read_gatt_char(UUID))
            if len(reply) != 20 or reply[:2] != bytes([6, data[0]]) or reply[3] != 0:
                raise RuntimeError(f'Unexpected response: {reply.hex()}')
            return reply
        if args.command == 'ping':
            reply = await command(b'\x01')
            print(f'build={reply[0]} state={reply[2]} ticks={struct.unpack_from("<I", reply, 4)[0]}')
        elif args.command == 'led':
            print((await command(bytes([2, args.value]))).hex())
        elif args.command == 'reset':
            await command(b'\x04')
            print('Reset requested; this is NOT an OTA request.')
        elif args.command == 'clock':
            r=await command(b'\x08')
            ticks,ctl=struct.unpack_from('<II',r,4)
            cf,cr=struct.unpack_from('<HH',r,16)
            print(f'ticks={ticks} SysTick_control=0x{ctl:x} '
                  f'SysTick_IRQ={r[12]} RTC_IRQ={r[13]} '
                  f'RTC_mode=0x{r[14]:x} RTC_flags=0x{r[15]:x} tune={cf},{cr}')
        elif args.command in ('status', 'watch'):
            count=150 if args.command == 'watch' else 1
            previous=None
            for _ in range(count):
                r=await command(b'\x06')
                inputs,x,y,rc,errors,enabled,reason,cf,cr=struct.unpack_from('<IHHBBBBHH',r,4)
                h=await command(b'\x07')
                value=(inputs,x//16,y//16,rc,errors,enabled)
                if value != previous:
                    print(f'state={r[2]} controls=0x{inputs:x} adc={x},{y} '
                          f'HID_enabled={enabled} last_send=0x{rc:02x} errors=0x{errors:02x} '
                          f'calibration={cf},{cr} disconnect=0x{reason:02x} '
                          f'report={h[4:12].hex()} sent={int.from_bytes(h[12:14],"little")} '
                          f'failures={int.from_bytes(h[14:16],"little")}',flush=True)
                    previous=value
                if count>1: await asyncio.sleep(.2)
        elif args.command == 'dump':
            if args.start % 16 or args.size <= 0 or args.size % 16 or args.start < 0 or args.start + args.size > 0x78000:
                raise SystemExit('Expected aligned flash range inside 0..0x78000')
            result = bytearray()
            for address in range(args.start, args.start + args.size, 16):
                reply = await command(b'\x03' + struct.pack('<I', address))
                result.extend(reply[4:20])
            args.output.write_bytes(result)
            print(f'Saved {len(result)} bytes to {args.output}')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('ping')
    commands.add_parser('reset')
    commands.add_parser('status')
    commands.add_parser('clock')
    commands.add_parser('watch')
    led = commands.add_parser('led'); led.add_argument('value', type=int, choices=[0, 1])
    dump = commands.add_parser('dump')
    dump.add_argument('start', type=lambda s: int(s, 0))
    dump.add_argument('size', type=lambda s: int(s, 0))
    dump.add_argument('output', type=Path)
    try:
        asyncio.run(main(parser.parse_args()))
    except TimeoutError:
        raise SystemExit('MOCUTE did not respond; press Power after OTA.')
    except RuntimeError as error:
        raise SystemExit(str(error))
