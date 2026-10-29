#!/usr/bin/env python3
"""Stage reviewed common patches, then Metroid overlays, for darwin-xnu-build."""
from pathlib import Path
import shutil
import sys

repo = Path(__file__).resolve().parents[1]
destination = Path(sys.argv[1])
destination.mkdir(parents=True, exist_ok=True)
manifest = repo / 'xnu/metroid/common-patches.txt'
for line in manifest.read_text().splitlines():
    name = line.strip()
    if not name or name.startswith('#'):
        continue
    source = repo / 'xnu/patches' / name
    if source.parent != repo / 'xnu/patches' or source.suffix != '.patch':
        raise SystemExit(f'Invalid patch name: {name}')
    shutil.copy2(source, destination / name)
# These dependencies run before patches in darwin-xnu-build.
for number in ('0019', '0020', '0026'):
    for source in (repo / 'xnu/patches').glob(f'{number}-*.sh'):
        shutil.copy2(source, destination / source.name)
# Apply overlays after all common patches, avoiding duplicate numeric prefixes.
for source in sorted((repo / 'xnu/metroid').glob('*.patch')):
    shutil.copy2(source, destination / ('9000-' + source.name))
