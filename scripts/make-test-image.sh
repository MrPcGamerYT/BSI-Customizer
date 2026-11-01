#!/bin/sh
# Regenerates tests/fixtures/root-256k.vhd and root-256k.vhdx — tiny Android-
# style disks that the regression suite mounts, edits and checks with e2fsck.
#
# Both containers carry a real ext4 made by mke2fs (not a hand-assembled
# approximation), with the features the hardcode debloater supports and none it
# does not: 256 KiB, 1 KiB blocks, no journal, no metadata checksums, no 64-bit,
# no flex_bg. The VHD is fixed. The VHDX fixtures use a 1 MiB block size,
# clean logs and no parent; the dynamic image has a sparse 4,097 MiB virtual
# disk crossing a BAT chunk boundary, while the fixed image is 2 MiB.
#
# Requires e2fsprogs 1.46+ and Python 3. Run from the repository root. Two runs
# differ only in the timestamps mke2fs stamps into the superblock, so compare
# the filesystem, not the bytes.
set -eu

out=${1:-tests/fixtures/root-256k.vhd}
out_vhdx=${2:-tests/fixtures/root-256k.vhdx}
out_fixed_vhdx=${3:-tests/fixtures/root-256k-fixed.vhdx}
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

python3 - "$raw" "$out" "$out_vhdx" "$out_fixed_vhdx" <<'PY'
import pathlib
import struct
import sys

raw = pathlib.Path(sys.argv[1]).read_bytes()

def put_le16(buffer, offset, value):
    struct.pack_into('<H', buffer, offset, value)

def put_le32(buffer, offset, value):
    struct.pack_into('<I', buffer, offset, value)

def put_le64(buffer, offset, value):
    struct.pack_into('<Q', buffer, offset, value)

def put_be32(buffer, offset, value):
    struct.pack_into('>I', buffer, offset, value)

def put_be64(buffer, offset, value):
    struct.pack_into('>Q', buffer, offset, value)

def crc32c(data):
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82F63B78 if crc & 1 else 0)
    return crc ^ 0xFFFFFFFF

def with_crc(buffer, checksum_offset):
    put_le32(buffer, checksum_offset, 0)
    put_le32(buffer, checksum_offset, crc32c(buffer))

# A fixed VHD footer follows the raw ext4 bytes.
footer = bytearray(512)
footer[0:8] = b'conectix'
put_be32(footer, 8, 2)                       # features
put_be32(footer, 12, 0x00010000)             # version
put_be64(footer, 16, 0xFFFFFFFFFFFFFFFF)     # fixed: no dynamic header
footer[28:32] = b'BSIC'                      # creator
put_be64(footer, 40, len(raw))               # original size
put_be64(footer, 48, len(raw))               # current size
put_be32(footer, 56, 0x000A0000)             # geometry
put_be32(footer, 60, 2)                      # fixed disk
put_be32(footer, 64, (~sum(footer)) & 0xFFFFFFFF)
pathlib.Path(sys.argv[2]).write_bytes(raw + footer)
print(f'wrote {sys.argv[2]} ({len(raw) + 512} bytes)')

# Synthetic dynamic VHDX: 4,097 MiB virtual size, only the first 1 MiB
# payload block allocated. The sparse tail crosses a BAT chunk boundary. The
# VHDX header and region-table copies carry
# real CRC-32C checksums; offsets follow the 1 MiB alignment rules.
MiB = 1 << 20
vhdx = bytearray(5 * MiB)
vhdx[:8] = b'vhdxfile'
creator = 'BSI VHDX regression fixture'.encode('utf-16le')
vhdx[8:8 + len(creator)] = creator
for at, sequence in ((64 * 1024, 1), (128 * 1024, 2)):
    header = bytearray(4096)
    header[:4] = b'head'
    put_le64(header, 8, sequence)
    put_le16(header, 64, 0)       # log format version
    put_le16(header, 66, 1)       # VHDX version
    put_le32(header, 68, MiB)     # log length
    put_le64(header, 72, MiB)     # log offset
    with_crc(header, 4)
    vhdx[at:at + len(header)] = header

bat_guid = bytes.fromhex('6677c22d23f600429d64115e9bfd4a08')
metadata_guid = bytes.fromhex('06a27c8b90479a4bb8fe575f050f886e')
for at in (192 * 1024, 256 * 1024):
    regions = bytearray(64 * 1024)
    regions[:4] = b'regi'
    put_le32(regions, 8, 2)
    regions[16:32] = bat_guid
    put_le64(regions, 32, 3 * MiB)
    put_le32(regions, 40, MiB)
    put_le32(regions, 44, 1)
    regions[48:64] = metadata_guid
    put_le64(regions, 64, 2 * MiB)
    put_le32(regions, 72, MiB)
    put_le32(regions, 76, 1)
    with_crc(regions, 4)
    vhdx[at:at + len(regions)] = regions

metadata = bytearray(MiB)
metadata[:8] = b'metadata'
put_le16(metadata, 10, 5)
items = [
    ('3767a1ca36fa434db3b633f0aa44e76b', 64 * 1024, 8, 6,
     struct.pack('<II', MiB, 0)),
    ('2442a52f1bcd7648b2115dbed83bf4b8', 64 * 1024 + 8, 8, 6,
     struct.pack('<Q', 4097 * MiB)),
    ('1dbf41816fa90947ba47f233a8faab5f', 64 * 1024 + 16, 4, 6,
     struct.pack('<I', 512)),
    ('c748a3cd5d4471449cc9e9885251c556', 64 * 1024 + 20, 4, 6,
     struct.pack('<I', 512)),
    ('ab12cabee6b2234593efc309e000c746', 64 * 1024 + 24, 16, 6,
     bytes.fromhex('00112233445566778899aabbccddeeff')),
]
for index, (guid, offset, length, flags, value) in enumerate(items):
    entry = 32 + index * 32
    metadata[entry:entry + 16] = bytes.fromhex(guid)
    put_le32(metadata, entry + 16, offset)
    put_le32(metadata, entry + 20, length)
    put_le32(metadata, entry + 24, flags)
    metadata[offset:offset + length] = value
vhdx[2 * MiB:3 * MiB] = metadata

bat = bytearray(MiB)
put_le64(bat, 0, 4 * MiB | 6)  # fully present payload block 0
# entry 1 is the unallocated payload block; entry 2 is its chunk bitmap
vhdx[3 * MiB:4 * MiB] = bat
vhdx[4 * MiB:4 * MiB + len(raw)] = raw
pathlib.Path(sys.argv[3]).write_bytes(vhdx)
print(f'wrote {sys.argv[3]} ({len(vhdx)} bytes)')

# A separate 2 MiB fixed VHDX uses the same BAT layout but requires every
# payload block to be present. Derive it by restoring the small virtual size,
# appending the all-zero second block, setting LEAVE_BLOCKS_ALLOCATED, and
# mapping BAT entry 1 to the appended block.
fixed_vhdx = bytearray(vhdx)
put_le64(fixed_vhdx, 2 * MiB + 64 * 1024 + 8, 2 * MiB)
fixed_vhdx.extend(bytes(MiB))
put_le32(fixed_vhdx, 2 * MiB + 64 * 1024 + 4, 1)
put_le64(fixed_vhdx, 3 * MiB + 8, 5 * MiB | 6)
pathlib.Path(sys.argv[4]).write_bytes(fixed_vhdx)
print(f'wrote {sys.argv[4]} ({len(fixed_vhdx)} bytes)')
PY

if command -v e2fsck > /dev/null 2>&1; then
    e2fsck -fn "$out" > /dev/null && echo "e2fsck: clean"
fi
