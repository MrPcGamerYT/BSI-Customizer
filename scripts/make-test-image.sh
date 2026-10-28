#!/bin/sh
# Regenerates tests/fixtures/root-256k.vhd — the tiny Android-style disk the
# regression suite mounts, edits and proves against e2fsck.
#
# It is a real ext4 made by mke2fs (not a hand-assembled approximation), with
# the features the hardcode debloater supports and none it does not: 256 KiB,
# 1 KiB blocks, no journal, no metadata checksums, no 64-bit, no flex_bg.
# The VHD wrapper is a fixed disk: the raw image followed by a 512-byte
# `conectix` footer.
#
# Requires e2fsprogs 1.46+.  Run from the repository root.  Two runs differ
# only in the timestamps mke2fs stamps into the superblock, so compare the
# filesystem, not the bytes.
set -eu

out=${1:-tests/fixtures/root-256k.vhd}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

raw="$work/root.img"
dd if=/dev/zero of="$raw" bs=1024 count=256 status=none
mke2fs -q -F -t ext4 -b 1024 -I 128 -m 0 \
    -U 30313233-3435-3637-3839-3a3b3c3d3e3f \
    -O extent,filetype,sparse_super,^has_journal,^metadata_csum,^64bit \
    -O ^resize_inode,^dir_index,^flex_bg,^huge_file,^extra_isize,^large_file \
    -E lazy_itable_init=0,lazy_journal_init=0,nodiscard "$raw"

printf 'hello world\n' > "$work/hello.txt"
debugfs -w -R "write $work/hello.txt hello.txt" "$raw" > /dev/null

python3 - "$raw" "$out" <<'PY'
import sys

raw = open(sys.argv[1], 'rb').read()
footer = bytearray(512)
footer[0:8] = b'conectix'

def be32(offset, value):
    footer[offset:offset + 4] = value.to_bytes(4, 'big')

def be64(offset, value):
    footer[offset:offset + 8] = value.to_bytes(8, 'big')

be32(8, 2)                      # features
be32(12, 0x00010000)            # version
be64(16, 0xFFFFFFFFFFFFFFFF)    # no dynamic header: the data sits at offset 0
footer[28:32] = b'BSIC'         # creator
be64(40, len(raw))              # original size
be64(48, len(raw))              # current size
be32(56, 0x000A0000)            # geometry
be32(60, 2)                     # fixed disk
be32(64, (~sum(footer)) & 0xFFFFFFFF)

open(sys.argv[2], 'wb').write(raw + bytes(footer))
print(f'wrote {sys.argv[2]} ({len(raw) + 512} bytes)')
PY

if command -v e2fsck > /dev/null 2>&1; then
    e2fsck -fn "$out" > /dev/null && echo "e2fsck: clean"
fi
