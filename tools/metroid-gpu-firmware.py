#!/usr/bin/env python3
"""Inspect a local Gen8 GMU image with the kernel's actual firmware parser.

Usage: python3 tools/metroid-gpu-firmware.py /path/to/gen80600_gmu.bin
Reads the image and compiles the parser in a temporary host library. Does not
download firmware, access a phone, install files, or authenticate the image.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


class Block(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint32) for name in ('address', 'size', 'offset', 'region')]


class Reservation(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint32) for name in ('address', 'size', 'persistent', 'region')]


class Plan(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint32) for name in ('count', 'reservation_count', 'metadata_mask')] + [
        ('versions', ctypes.c_uint32 * 5), ('blocks', Block * 128), ('reservations', Reservation * 64)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    args = parser.parse_args()
    if args.image.stat().st_size > 16 * 1024 * 1024:
        parser.error('Image exceeds the parser limit of 16 MiB')
    data = args.image.read_bytes()
    compiler = shutil.which('clang')
    if not compiler:
        parser.error('clang is required to build the same parser used by the kernel')
    sources = Path(__file__).resolve().parents[1] / 'xnu/metroid/gpu'
    with tempfile.TemporaryDirectory(prefix='metroid-gpu-firmware-') as temporary:
        library = Path(temporary) / ('parser.dylib' if sys.platform == 'darwin' else 'parser.so')
        subprocess.run([compiler, '-std=c11', '-DMG_HOST_TEST', '-shared', '-fPIC',
            '-Wall', '-Wextra', '-Werror', str(sources / 'metroid_gpu_fw.c'),
            '-o', str(library)], check=True)
        native = ctypes.CDLL(str(library))
        native.mg_fw_parse.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(Plan)]
        native.mg_fw_parse.restype = ctypes.c_int
        image = ctypes.create_string_buffer(data)
        plan = Plan()
        result = native.mg_fw_parse(image, len(data), ctypes.byref(plan))
        if result:
            errors = {-1: 'malformed image', -2: 'block count exceeds limit',
                      -3: 'unsupported block/region', -4: 'data lacks a preallocation record'}
            parser.error(f'GMU parser rejected image: {errors.get(result, result)}')
        regions = ['ITCM', 'CACHE', 'DTCM', 'UNCACHED', 'UNCACHED_EXT']
        def describe(item):
            entry = {name: getattr(item, name) for name, _ in item._fields_}
            entry['address'] = hex(entry['address'])
            entry['region'] = regions[entry['region']]
            return entry
        print(json.dumps({
            'target': 'Metroid Adreno825 Gen8.6.0',
            'expected_filename': 'gen80600_gmu.bin',
            'filename_matches': args.image.name == 'gen80600_gmu.bin',
            'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data),
            'validation': 'structural only; not firmware authentication or hardware compatibility',
            'versions': {name: hex(plan.versions[i]) for i, name in enumerate(
                ['core', 'core_dev', 'power', 'power_dev', 'hfi']) if plan.metadata_mask & (1 << i)},
            'blocks': [describe(plan.blocks[i]) for i in range(plan.count)],
            'reservations': [describe(plan.reservations[i]) for i in range(plan.reservation_count)],
        }, indent=2))


if __name__ == '__main__':
    main()
