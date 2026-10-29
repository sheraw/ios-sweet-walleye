#!/usr/bin/env python3
"""Exercise the actual GPU firmware/HFI C implementation; no phone access.

Optionally pass a patched XNU tree to test staged sources rather than canonical.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

repo = Path(__file__).resolve().parents[2]
sources = Path(sys.argv[1]).resolve() / 'bsd/dev' if len(sys.argv) > 1 else repo / 'xnu/metroid/gpu'
with tempfile.TemporaryDirectory(prefix='metroid-gpu-test-') as temp:
    binary = Path(temp) / 'gpu-test'
    subprocess.run(['clang', '-std=c11', '-DMG_HOST_TEST', '-Wall', '-Wextra', '-Werror',
        '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        '-I' + str(sources), str(repo / 'tools/tests/metroid_gpu_model.c'),
        str(sources / 'metroid_gpu_fw.c'), str(sources / 'metroid_gpu_hfi.c'),
        '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
