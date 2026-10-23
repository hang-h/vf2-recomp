#!/usr/bin/env python3
"""Run a scripted VF2 match with VF1 models prepared at startup from the two ZIPs."""
import argparse
import os
from pathlib import Path
import subprocess
import shutil
import sys


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('vf2_rom', type=Path)
    parser.add_argument('--vf1-rom', type=Path, help='defaults to vf.zip beside vf2.zip')
    parser.add_argument('--out', type=Path, default=root / 'build/runtime-vf1-demo')
    parser.add_argument('--scale', type=int, choices=range(1, 5), default=2)
    args = parser.parse_args()
    rom, out = args.vf2_rom.resolve(), args.out.resolve()
    vf1 = (args.vf1_rom or rom.with_name('vf.zip')).resolve()
    if not rom.is_file():
        parser.error('vf2_rom must be your local vf2.zip')
    if not vf1.is_file():
        parser.error('put vf.zip beside vf2.zip, or pass --vf1-rom /path/to/vf.zip')
    roms = out / 'roms'
    roms.mkdir(parents=True, exist_ok=True)
    for source, name in [(rom, 'vf2.zip'), (vf1, 'vf.zip')]:
        target = roms / name
        if target.resolve() != source:
            shutil.copy2(source, target)
    subprocess.run(['make', '-j8', 'build/bin/vf2run'], cwd=root, check=True)
    env = dict(os.environ)
    env.pop('VF2_MODEL_OVERRIDES', None)
    with (out / 'match.log').open('w') as log:
        subprocess.run([str(root / 'build/bin/vf2run'), str(roms / 'vf2.zip'), '2400', '--scale', str(args.scale),
                        '--script', str(root / 'tools/models/akira-demo.txt'),
                        '--png', '1700,1740,1800,1850,1900,1950,2000,2100,2300',
                        '--out', str(out)], cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    print(f'Match screenshots: {out}\nVF1 models were prepared by VF2 directly from {roms / "vf.zip"}')


if __name__ == '__main__':
    try:
        main()
    except (OSError, subprocess.SubprocessError) as error:
        sys.exit(f'VF1-on-VF2 demo: {error}')
