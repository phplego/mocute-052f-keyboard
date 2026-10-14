"""Factory-loader connections. Retries stop before any erase/program operations."""
import asyncio
import os
import re
import sys
from contextlib import asynccontextmanager, contextmanager, suppress
from pathlib import Path
from bleak import BleakClient, BleakScanner
from bleak.backends.device import BLEDevice
from bleak.exc import BleakError
from dbus_fast import BusType, Message, MessageType
from dbus_fast.aio import MessageBus

ADAPTER = os.environ.get('MOCUTE_ADAPTER', 'hci0')


@contextmanager
def upload_lock():
    path = Path(__file__).resolve().parents[1] / '.pio/ota-upload.lock'
    path.parent.mkdir(exist_ok=True)
    with path.open('a') as lock:
        if sys.platform == 'linux':
            import fcntl
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                raise SystemExit('Another OTA upload is running.')
        yield


async def find_loader(address=None, timeout=3):
    # A connected loader does not advertise. Reuse its actual BlueZ object.
    if address is None or sys.platform != "linux":
        return await BleakScanner.find_device_by_filter(
            lambda d, a: (d.address.upper() == address.upper() if address else
                         (a.local_name or "").upper().startswith("MOCUTE-FS")),
            timeout=timeout, adapter=ADAPTER)
    if not re.fullmatch(r"(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}", address):
        raise ValueError("Invalid loader Bluetooth address")
    address = address.upper()
    bus = await MessageBus(bus_type=BusType.SYSTEM).connect()
    path = f'/org/bluez/{ADAPTER}/dev_{address.replace(":", "_")}'
    try:
        reply = await asyncio.wait_for(bus.call(Message(destination='org.bluez',
            path=path, interface='org.freedesktop.DBus.Properties', member='GetAll',
            signature='s', body=['org.bluez.Device1'])), 10)
        if reply.message_type != MessageType.ERROR:
            props = {key: value.value for key, value in reply.body[0].items()}
            if not props.get('Name', '').upper().startswith('MOCUTE-FS'):
                raise RuntimeError('The supplied address is not a MOCUTE OTA loader')
            if props.get('Paired'):
                print('Removing saved pairing for the factory loader only.', flush=True)
                result = await asyncio.wait_for(bus.call(Message(destination='org.bluez',
                    path=f'/org/bluez/{ADAPTER}', interface='org.bluez.Adapter1',
                    member='RemoveDevice', signature='o', body=[path])), 10)
                if result.message_type == MessageType.ERROR:
                    raise RuntimeError(f'Cannot remove loader pairing: {result.body}')
            elif props.get('Connected'):
                return BLEDevice(address, props.get('Name'),
                    {'path': path, 'props': props}, props.get('RSSI', -127))
    finally:
        bus.disconnect()
    return await BleakScanner.find_device_by_address(address,
        timeout=timeout, adapter=ADAPTER)


@asynccontextmanager
async def connect_loader(device, disconnected_callback=None):
    device = await find_loader(device.address, timeout=10)
    if device is None:
        raise RuntimeError("Factory OTA loader disappeared.")
    for attempt in range(3):
        client = BleakClient(device, timeout=20,
            disconnected_callback=disconnected_callback)
        try:
            await client.connect()
            break
        except (BleakError, TimeoutError, EOFError, OSError) as error:
            with suppress(Exception):
                await client.disconnect()
            if attempt == 2:
                raise
            print(f'Loader connection failed: {error}; retrying before writing.', flush=True)
            await asyncio.sleep(1 + attempt)
            device = await find_loader(device.address, timeout=10)
            if device is None:
                raise RuntimeError('Factory OTA loader disappeared.') from error
    try:
        yield client
    finally:
        with suppress(Exception):
            await client.disconnect()
