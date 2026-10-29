#!/bin/bash
# Build the boot shim on Windows without any aarch64 binutils.
#
# Upstream shim/build.sh wants aarch64-linux-gnu-{gcc,objcopy,readelf,nm} and
# mkbootimg/cpio. This machine has none of them, so:
#   compile/link  -> zig cc (clang + lld, aarch64-freestanding)
#   objcopy/nm    -> ../../../walleye-port/build/elftool.py (ELF64 reader)
#   cpio+gzip     -> ../../../walleye-port/build/mkramdisk.py
#   mkbootimg     -> ../../../walleye-port/build/mkbootimg.py
#
# Two hardware findings are baked in, both learned the hard way on the device:
#
#   1. The linked binary must carry a real arm64 Image header. start.S puts
#      `_head` in .text.head but zig's assembler folds it, so `_head` and
#      `_start` both land at 0x0 and the header never exists. mkimage.py
#      synthesises it (code0 = `b +64`, image_size = 64+payload, magic ARM\x64).
#   2. The kernel payload must be COMPRESSED. A raw arm64 Image is rejected by
#      this bootloader with "Failed to load/authenticate boot image: Load Error"
#      and it lands back in fastboot in ~2 s. gzip works (stock uses LZ4).
#      The DTB is appended right after the compressed stream, as in stock.
#
# usage:  ./build-zig.sh            # normal build
#         EXTRA=-DSHIM_STAGE0 ./build-zig.sh   # stage-0 entry probe
set -euo pipefail
cd "$(dirname "$0")"

PY=${PY:-env -u PYTHONPATH -u PYTHONHOME /c/Users/shera/AppData/Local/Microsoft/WindowsApps/python.exe}
ZL="$PY -m ziglang"
TOOLS=${TOOLS:-../../../walleye-port/build}
DTB=${DTB:-../../../dts/walleye.dtb}
EXTRA=${EXTRA:-}

CF="-target aarch64-freestanding -Xclang -target-feature -Xclang +el3 -O2 \
    -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only -mstrict-align \
    -fno-stack-protector -Wall -Wextra"

mkdir -p out
echo "== compile =="
for f in start.S shim.c console.c mmu.c fdt.c loader.c ufs.c probe.c; do
	$ZL cc $CF $EXTRA -c "$f" -o "out/${f%.*}.o"
	echo "   ${f} ok"
done
$ZL cc $CF $EXTRA -fno-builtin -c compat.c -o out/compat.o
echo "   compat.c ok"

echo "== link =="
$ZL cc $CF -static -T shim.ld -Wl,--build-id=none \
	out/start.o out/shim.o out/console.o out/mmu.o out/fdt.o out/loader.o \
	out/ufs.o out/probe.o out/compat.o -o out/shim.elf

echo "== headers =="
abs=$($PY "$TOOLS/elftool.py" abs64 out/shim.elf)
[ "$abs" = 0 ] || { echo "error: $abs absolute relocations, image is not position independent" >&2; exit 1; }
echo "   absolute relocations: $abs"

$PY "$TOOLS/elftool.py" 2bin out/shim.elf out/Image.raw
end=$($PY "$TOOLS/elftool.py" end out/shim.elf)
truncate -s "$end" out/Image.raw
mv out/Image.raw out/Image
echo "   image.size = $end (0x$(printf %x "$end"))"

$PY "$TOOLS/mkimage.py" out/Image out/Image.hdr >/dev/null

echo "== kernel payload: gzip(image) + dtb =="
$PY - out/Image.hdr "$DTB" out/kernel.payload <<'PYEOF'
import gzip, sys
img, dtb_path, out = sys.argv[1], sys.argv[2], sys.argv[3]
blob = gzip.compress(open(img, 'rb').read(), 9, mtime=0)
dtb = open(dtb_path, 'rb').read()
assert dtb[:4] == b'\xd0\x0d\xfe\xed', 'not an FDT'
open(out, 'wb').write(blob + dtb)
print('   gzip %d B + dtb %d B = %d B' % (len(blob), len(dtb), len(blob) + len(dtb)))
PYEOF

echo "== ramdisk =="
$PY "$TOOLS/mkramdisk.py" out/ramdisk.bin

echo "== boot.img =="
$PY "$TOOLS/mkbootimg.py" --header_version 0 \
	--kernel out/kernel.payload --ramdisk out/ramdisk.bin \
	--base 0 --kernel_offset 0x8000 --ramdisk_offset 0x1000000 \
	--tags_offset 0x100 --pagesize 4096 \
	--os_version 17.0.0 --os_patch_level 2035-06 \
	--cmdline "console=null" -o out/shim-boot.img

ls -l out/shim.elf out/Image out/kernel.payload out/shim-boot.img
