"""Compile the real numerical collector: full/basic/off, ASan/UBSan, no RTOS mocks."""
from pathlib import Path
import os
import platform
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
compiler = shlex.split(os.environ.get("CC", "clang"))
platform_flags = []
if platform.system() == "Darwin":
    sdk = subprocess.check_output(
        ["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True,
    ).strip()
    platform_flags = ["-isysroot", sdk]

with tempfile.TemporaryDirectory(prefix="rt-diagnostics-test-") as temporary:
    for enabled, detailed in ((1, 1), (1, 0), (0, 0)):
        executable = Path(temporary) / f"test-{enabled}-{detailed}"
        flags = [
            "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
            "-fsanitize=address,undefined", *platform_flags,
            f"-DCONFIG_RT_DIAGNOSTICS={enabled}",
            f"-DCONFIG_RT_DIAGNOSTICS_DETAILED_TIMING={detailed}",
            "-I" + str(root / "include"),
        ]
        subprocess.run([
            *compiler, *flags, str(root / "src/diagnostics.c"),
            str(root / "tests/test_diagnostics.c"), "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)
