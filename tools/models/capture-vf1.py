#!/usr/bin/env python3
"""Capture P1 Akira's geometry/colours using the sibling VF1 engine and the user's ZIP."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rom', type=Path)
    parser.add_argument('--vf1-root', type=Path, default=root.parent / 'vf1-recomp')
    parser.add_argument('--vf1-bin', type=Path)
    parser.add_argument('--out', type=Path, default=root / 'build/vf1-capture')
    args = parser.parse_args()
    sibling, out = args.vf1_root.resolve(), args.out.resolve()
    binary = (args.vf1_bin or sibling / 'build/xcode-mac/Build/Products/Debug/VF1 Recomp.app/Contents/MacOS/VF1 Recomp').resolve()
    if not binary.is_file() or not (sibling / 'tools/romset.py').is_file():
        parser.error('build vf1-recomp first, or pass --vf1-root and --vf1-bin')
    out.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, str(sibling / 'tools/romset.py'), str(args.rom.resolve()), str(out / 'regions')], check=True)
    script = out / 'akira-input.txt'
    script.write_text('wait 240\npress 0 10 6\nwait 120\npress 1 01 6\nwait 1400\n')
    capture = out / 'akira.jsonl'
    capture.unlink(missing_ok=True)
    env = dict(os.environ, VF_HIDDEN='1', VF_SCRIPT=str(script), VF_QUIT_AFTER='1400',
               VF_DUMP_FIGHTER=f'1300:{capture}')
    with (out / 'capture.log').open('w') as log:
        subprocess.run([str(binary), str(out / 'regions'), '--interp', '--scale', '1'],
                       cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180)
    parts = [json.loads(line) for line in capture.read_text().splitlines() if line.strip()]
    if not parts or {p['rec'] for p in parts} != {'fd4f99'}:
        raise ValueError(f'capture is not P1 Akira; inspect {out / "capture.log"}')
    print(f'Captured {len(parts)} Akira parts to {capture}')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        sys.exit(f'VF1 capture: {error}')
