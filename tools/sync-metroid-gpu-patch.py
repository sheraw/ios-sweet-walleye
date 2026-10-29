#!/usr/bin/env python3
"""Regenerate/check the Adreno825 overlay from xnu/metroid/gpu sources."""
from pathlib import Path
import difflib
import sys

repo = Path(__file__).resolve().parents[1]
patch = repo / 'xnu/metroid/0033-metroid-gpu.patch'
prefix = '''--- a/bsd/conf/files
+++ b/bsd/conf/files
@@ -109,6 +109,9 @@
 bsd/dev/metroid_touch.c\t\t\tstandard
 bsd/dev/metroid_wifi.c\t\t\tstandard
 bsd/dev/metroid_usb.c\t\t\tstandard
+bsd/dev/metroid_gpu.c\t\t\tstandard
+bsd/dev/metroid_gpu_fw.c\t\t\tstandard
+bsd/dev/metroid_gpu_hfi.c\t\t\tstandard
 bsd/dev/mem.c\t\t\t\tstandard
 bsd/dev/munge.c \t\t\toptional config_requires_u32_munging
 bsd/dev/unix_startup.c\t\t\tstandard
--- a/bsd/kern/bsd_init.c
+++ b/bsd/kern/bsd_init.c
@@ -871,2 +871,7 @@
 #endif
 #endif /* NETWORKING */
+#if defined(ARM64_BOARD_CONFIG_SWEET)
+\textern void metroid_gpu_init(void);
+\tmetroid_gpu_init();
+#endif
+
'''
parts = [prefix]
for source in sorted((repo / 'xnu/metroid/gpu').glob('metroid_gpu*')):
    if source.suffix not in ('.h', '.c'):
        continue
    parts.extend(difflib.unified_diff([], source.read_text().splitlines(True),
        fromfile='/dev/null', tofile='b/bsd/dev/' + source.name))
new = ''.join(parts)
if sys.argv[1:] == ['--check']:
    if not patch.exists() or patch.read_text() != new:
        raise SystemExit('GPU overlay differs; run tools/sync-metroid-gpu-patch.py')
    print('PASS: GPU overlay matches canonical sources')
elif not sys.argv[1:]:
    patch.write_text(new)
else:
    raise SystemExit('Usage: sync-metroid-gpu-patch.py [--check]')
