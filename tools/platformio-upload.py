from pathlib import Path
import os
import subprocess
from SCons.Script import AlwaysBuild, Import

Import("env")

def upload(source, target, env):
    root = Path(env["PROJECT_DIR"])
    python = root / ".venv" / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    if not python.is_file():
        print("Install upload dependencies: python3 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt")
        return 1
    command = [str(python), "-u", str(root / "tools/ota.py"), str(source[0]), "--quick-verify"]
    address = env.subst("$UPLOAD_PORT")
    if address:
        command += ["--address", address]
    return subprocess.call(command)

image = env.File("$BUILD_DIR/mocute-052f-keyboard.bin")
AlwaysBuild(env.Alias("upload", image, env.VerboseAction(upload, "Updating MOCUTE-052F over Bluetooth")))
