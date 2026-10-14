"""Diagnostics over the existing BlueZ HID link, without disconnecting it."""
import asyncio
import os
import re
from dbus_fast import Message, MessageType, BusType, Variant
from dbus_fast.aio import MessageBus

ADDRESS = os.environ.get('MOCUTE_ADDRESS', '')
ADAPTER = os.environ.get('MOCUTE_ADAPTER', 'hci0')


def device_path():
    if not re.fullmatch(r'(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}', ADDRESS):
        raise ValueError('Set MOCUTE_ADDRESS to your paired keyboard Bluetooth address')
    if not re.fullmatch(r'hci[0-9]+', ADAPTER):
        raise ValueError('MOCUTE_ADAPTER must be hci0, hci1, ...')
    return f'/org/bluez/{ADAPTER}/dev_{ADDRESS.upper().replace(":", "_")}' 

class RadioClient:
    """Use BlueZ's existing HID link; closing diagnostics keeps it connected."""
    async def __aenter__(self):
        self.path=device_path()
        self.bus=await MessageBus(bus_type=BusType.SYSTEM).connect()
        props=await self.call(self.path,'org.freedesktop.DBus.Properties','GetAll','s',['org.bluez.Device1'])
        if not props[0]['Connected'].value:
            try:
                await self.call(self.path,'org.bluez.Device1','Connect')
            except Exception:
                self.bus.disconnect()
                raise
        self.chars={}
        for _ in range(40):
            result=await self.call('/','org.freedesktop.DBus.ObjectManager','GetManagedObjects')
            self.chars={interfaces['org.bluez.GattCharacteristic1']['UUID'].value:path
                for path,interfaces in result[0].items()
                if path.startswith(self.path+'/') and 'org.bluez.GattCharacteristic1' in interfaces}
            if '925dd002-8a86-4ab8-ae90-620fb46c19e4' in self.chars:
                return self
            await asyncio.sleep(.25)
        self.bus.disconnect()
        raise RuntimeError('MOCUTE diagnostic characteristic not resolved')

    async def call(self,path,interface,member,signature='',body=None):
        reply=await asyncio.wait_for(self.bus.call(Message(destination='org.bluez',
            path=path,interface=interface,member=member,signature=signature,body=body or [])),20)
        if reply.message_type==MessageType.ERROR:
            raise RuntimeError(f'{reply.error_name}: {reply.body}')
        return reply.body

    async def write_gatt_char(self,uuid,data,response=True):
        await self.call(self.chars[uuid],'org.bluez.GattCharacteristic1','WriteValue',
            'aya{sv}',[bytes(data),{'type':Variant('s','request' if response else 'command')}])

    async def read_gatt_char(self,uuid):
        return bytes((await self.call(self.chars[uuid],'org.bluez.GattCharacteristic1',
            'ReadValue','a{sv}',[{}]))[0])

    async def __aexit__(self,*exc):
        self.bus.disconnect()
