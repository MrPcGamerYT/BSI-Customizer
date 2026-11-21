#!/usr/bin/env python3
"""Analyse a before/after pair of HD-Player.exe images.

Reports:
  * byte-diff regions between the two files
  * every zlib stream (offset, extent, decompressed length, content hash)
  * for each diff region: whether it intersects a zlib stream (CORRUPTION if
    the stream bytes changed: the deflate encoding is broken or its adler32
    no longer matches -> qUncompress fails -> the player's resource load
    breaks)
  * whether every zlib stream still decompresses to the same bytes
"""
import sys, zlib, hashlib


def zlib_streams(data):
    """[(start, end, out_bytes)] for every valid zlib stream (loose inflate)."""
    streams = []
    pos = 0
    n = len(data)
    while True:
        i = data.find(b'\x78', pos)
        if i < 0 or i + 1 >= n:
            break
        if data[i + 1] not in (0x01, 0x9C, 0xDA):
            pos = i + 1
            continue
        # inside a known stream? skip
        if any(s <= i < e for s, e, _ in streams):
            pos = i + 1
            continue
        d = zlib.decompressobj()
        try:
            out = d.decompress(data[i:])
            # unused_data: bytes after the stream end
            end = n - len(d.unused_data)
            if out or end > i + 2:
                streams.append((i, end, out))
                pos = end
                continue
        except zlib.error:
            pass
        pos = i + 1
    return streams


def diff_regions(a, b):
    regions = []
    n = min(len(a), len(b))
    i = 0
    while i < n:
        if a[i] != b[i]:
            j = i
            while j < n and a[j] != b[j]:
                j += 1
            regions.append((i, j))
            i = j
        else:
            i += 1
    return regions


def main(before_path, after_path):
    a = open(before_path, 'rb').read()
    b = open(after_path, 'rb').read()
    print(f'before: {len(a)} bytes  after: {len(b)} bytes')
    if len(a) != len(b):
        print('SIZE CHANGED!')

    sa = zlib_streams(a)
    sb = zlib_streams(b)
    print(f'zlib streams: before={len(sa)} after={len(sb)}')

    byhash = {hashlib.sha256(o).hexdigest(): (s, e, len(o)) for s, e, o in sa}
    changed_streams = []
    for s, e, o in sb:
        h = hashlib.sha256(o).hexdigest()
        if h not in byhash:
            changed_streams.append((s, e, len(o)))
    lost = len(sa) - (len(sb) - len(changed_streams))
    if changed_streams or lost:
        print(f'!! STREAM CONTENT CHANGED: {len(changed_streams)} changed/new, {lost} lost')
        for s, e, l in changed_streams[:10]:
            print(f'   stream @0x{s:x}-0x{e:x} outlen={l}')
    else:
        print('all zlib streams decompress identically')

    regs = diff_regions(a, b)
    print(f'diff regions: {len(regs)}')
    bad = 0
    for s, e in regs:
        hits = [(ts, te) for ts, te, _ in sa if not (e <= ts or s >= te)]
        tag = 'INSIDE-ZLIB-STREAM!!' if hits else 'ok'
        if hits:
            bad += 1
        print(f'  [0x{s:x}..0x{e:x}) len={e-s} {tag}')
    print('RESULT:', 'CORRUPTION' if (bad or changed_streams or lost) else 'CLEAN')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
