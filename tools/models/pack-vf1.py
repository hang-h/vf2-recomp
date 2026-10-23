#!/usr/bin/env python3
"""Convert vf1-recomp's VF_DUMP_FIGHTER JSONL into a local VF2MESH package.

The binding file supplies measured VF2 object addresses and local alignment matrices.
No geometry, colours, ROM data or unverified fighter addresses belong in the repository.
"""
import argparse
import fnmatch
import json
import math
from pathlib import Path

IDENTITY = [1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0]


def transform(m, p):
    return [sum(m[3 * j + i] * p[j] for j in range(3)) + m[9 + i] for i in range(3)]


def inverse(m):
    # Gauss-Jordan: input and output use the games' column-major 3x4 layout.
    rows = [[m[3 * j + i] for j in range(3)] + [float(i == j) for j in range(3)] for i in range(3)]
    for col in range(3):
        pivot = max(range(col, 3), key=lambda k: abs(rows[k][col]))
        if abs(rows[pivot][col]) < 1e-10:
            raise ValueError("singular reference matrix")
        rows[col], rows[pivot] = rows[pivot], rows[col]
        scale = rows[col][col]
        rows[col] = [v / scale for v in rows[col]]
        for i in range(3):
            if i != col:
                scale = rows[i][col]
                rows[i] = [a - scale * b for a, b in zip(rows[i], rows[col])]
    result = [rows[i][3 + j] for j in range(3) for i in range(3)] + [0, 0, 0]
    result[9:] = [-v for v in transform(result, m[9:])]
    return result


def vector(values, size):
    if len(values) != size or any(not math.isfinite(v) or abs(v) > 1e6 for v in values):
        raise ValueError("invalid coordinate/matrix")
    return values


def pack(parts, bindings):
    if len({p['rec'] for p in parts}) != 1:
        raise ValueError("capture must contain exactly one fighter record")
    by_tag = {p['tag']: p for p in parts}
    if len(by_tag) != len(parts):
        raise ValueError("duplicate tags in capture")
    result, seen, total = [], set(), 0
    if not 1 <= len(bindings) <= 128:
        raise ValueError("expected 1..128 bindings")
    for binding in bindings:
        address = int(str(binding['object']), 0)
        if not 0x800000 <= address < 0x1000000 or address in seen:
            raise ValueError("expected a unique VF2 polygon ROM address")
        seen.add(address)
        selected = [p for p in parts if any(fnmatch.fnmatchcase(p['tag'], s) for s in binding['tags'])]
        if not selected:
            raise ValueError("binding selected no captured parts")
        reference = inverse(vector(by_tag[binding['reference']]['m'], 12))
        align = vector(binding.get('matrix', IDENTITY), 12)
        faces = []
        for p in selected:
            matrix = vector(p['m'], 12)
            points = [vector(transform(align, transform(reference, transform(matrix, vector(v, 3)))), 3) for v in p['p']]
            for quad in p['q']:
                if len(quad) != 5 or any(type(i) is not int or not 0 <= i < len(points) for i in quad[:4]):
                    raise ValueError("invalid polygon indices")
                colour = quad[4]
                if type(colour) is not int or not 0 <= colour <= 0xffff:
                    raise ValueError("invalid BGR555 colour")
                rgb = [((colour >> shift) & 31) * 255 // 31 for shift in (0, 5, 10)]
                colour = (rgb[0] << 16) | (rgb[1] << 8) | rgb[2]
                for indices in ((0, 1, 2), (0, 2, 3)):
                    tri = [points[quad[i]] for i in indices]
                    a, b = [[tri[j][k] - tri[0][k] for k in range(3)] for j in (1, 2)]
                    cross = [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]
                    if sum(v*v for v in cross) > 1e-20:
                        faces.append((colour, tri))
        total += len(faces)
        if not 1 <= len(faces) <= 16384 or total > 65536:
            raise ValueError("mesh exceeds package limits or has no triangles")
        result.append((address, faces))
    lines = [f"VF2MESH 1 {len(result)}"]
    for address, faces in result:
        lines.append(f"{address:x} {len(faces)}")
        for colour, tri in faces:
            lines.append(f"{colour:06x} " + ' '.join(format(v, '.9g') for p in tri for v in p))
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--bindings', type=Path)
    parser.add_argument('--out', type=Path)
    parser.add_argument('--list', action='store_true', help='list captured records, part tags and geometry counts')
    args = parser.parse_args()
    try:
        parts = [json.loads(line) for line in args.capture.read_text().splitlines() if line.strip()]
        if args.list:
            for p in parts:
                print(f"{p['rec']}  {p['tag']:>5}  {p['poly']:08x}  {len(p['q'])} polygons")
        else:
            if not args.bindings or not args.out:
                parser.error('--bindings and --out are required unless using --list')
            config = json.loads(args.bindings.read_text())
            if 'source_record' in config and {p['rec'] for p in parts} != {config['source_record']}:
                raise ValueError('capture does not match the preset source_record')
            result = pack(parts, config['bindings'])
            args.out.parent.mkdir(parents=True, exist_ok=True)
            args.out.write_text(result)
            print(f"Wrote {args.out}")
    except (OSError, ValueError, KeyError, TypeError, IndexError) as error:
        parser.exit(1, f"model package: {error}\n")


if __name__ == '__main__':
    main()
