from pathlib import Path
import re
from SCons.Script import Builder, Default, DefaultEnvironment

env = DefaultEnvironment()
project = Path(env.subst("$PROJECT_DIR"))
out = Path(env.subst("$BUILD_DIR"))
sdk = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env["PIOENV"] / "ch573-sdk" / "src/EVT/EXAM"
core = project / "firmware/keyboard"
cc = Path(env.PioPlatform().get_package_dir("toolchain-riscv32-esp")) / "bin/riscv32-esp-elf"
flags = ["-march=rv32imac_zicsr", "-mcmodel=medany", "-Os", "-fsigned-char",
         "-ffunction-sections", "-fdata-sections", "-Wall"]

env.Replace(
    CC=str(cc) + "-gcc", LINK=str(cc) + "-gcc", AS=str(cc) + "-gcc",
    OBJCOPY=str(cc) + "-objcopy", PROGSUFFIX=".elf",
    CCFLAGS=flags, ASFLAGS=flags,
    CPPPATH=[str(core / "include"), str(core / "vendor/include"),
             str(sdk / "SRC/StdPeriphDriver/inc"), str(sdk / "SRC/RVMSIS"),
             str(sdk / "BLE/LIB"), str(sdk / "BLE/HAL/include")],
)
env.ProcessFlags(env.get("BUILD_FLAGS"))

# Adapt the separately installed SDK script; do not redistribute its contents.
linker = out / "link.ld"
text = (sdk / "IAP/APP/Core/Ld/Link.ld").read_text()
text = re.sub(r"FLASH \(rx\) : ORIGIN = [^\n]+", "FLASH (rx) : ORIGIN = 0x29000, LENGTH = 0x28000", text)
text = re.sub(r"^.*PROVIDE\( __global_pointer\$.*\n", "", text, flags=re.M)
linker.write_text(text + "\nPROVIDE(__global_pointer$ = 0x20004800);\n")
env.Replace(LINKFLAGS=flags + ["-nostartfiles", "--specs=nano.specs", "--specs=nosys.specs",
                              "-Wl,--gc-sections", "-T", str(linker)])

sources = [sdk / "IAP/APP/Core/Startup/startup_CH573.S", core / "src/main.c",
           core / "diagnostics/control.c"]
sources += [project / name for name in env.GetProjectOption("custom_extra_sources", "").split()]
sources += [core / name for name in ["src/keyboard_app.c", "vendor/profile/hiddev.c",
           "vendor/profile/hidkbdservice.c", "src/mcu.c", "src/rtc.c"]]
sources += [sdk / ("SRC/StdPeriphDriver/" + name) for name in
            ["CH57x_sys.c", "CH57x_adc.c", "CH57x_clk.c"]]
objects = [env.Object(str(out / (source.stem + ".o")), str(source))[0] for source in sources]
program = env.Program(str(out / "mocute-052f-keyboard"), objects +
                      [str(sdk / "BLE/LIB/LIBCH57xBLE.a"), str(sdk / "SRC/StdPeriphDriver/libISP573.a")])
env.Depends(program, str(linker))
env.Append(BUILDERS={"ElfToBin": Builder(action="$OBJCOPY -O binary $SOURCE $TARGET", suffix=".bin")})
image = env.ElfToBin(str(out / "mocute-052f-keyboard"), program)

def check_image(source, target, env):
    if Path(str(target[0])).stat().st_size > 0x28000:
        raise RuntimeError("APP exceeds its flash region")

env.AddPostAction(image, check_image)
Default(image)
