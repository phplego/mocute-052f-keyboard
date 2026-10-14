# MOCUTE-052F hardware reference

Target: CH573F, PCB `MOCUTE052_F 20210830`. These connections were recovered and
checked on one physical controller; another board revision needs verification.

| Control / function | CH573F pin | Electrical interpretation |
| --- | --- | --- |
| A | PB22 | Pressed = low; internal pull-up |
| B | PB12 | Pressed = low; internal pull-up |
| X | PB13 | Pressed = low; internal pull-up |
| Y | PB14 | Pressed = low; internal pull-up |
| Menu | PB15 | Pressed = low; internal pull-up |
| Front OK trigger | PA10 | Pressed = low; internal pull-up |
| Power / Back | PA14 | Pressed = high |
| Key/Game switch | PA11 | Key = low, Game = high; internal pull-up |
| Joystick horizontal | PA12 / ADC2 | Below centre = left; above centre = right |
| Joystick vertical | PA13 / ADC3 | Below centre = down; above centre = up |
| Installed indicator LED | PB7 | High = on |
| Power latch | PB23 | Keep high while the application runs |

The joystick has two analog axes, not four digital switches. Firmware samples
its neutral position at startup and uses thresholds relative to that position.
Directions count as held keys until the axis returns through the release
threshold. The Key/Game input is initialized but does not affect bindings.

## Application layout

| Region | Placement |
| --- | --- |
| Application flash | `0x29000`, maximum `0x28000` bytes |
| Application RAM | `0x20003800`, 18 KiB |
| Bond storage used by the SDK | DataFlash `0x77e00`–`0x77fff` |

The linker script is designed for the retained factory loader. The project's
application does not replace or include that loader. The loader accepts update
addresses beginning at `0x1000` and adds its `0x28000` relocation internally.
Do not treat that OTA protocol address as the application's execution address.
