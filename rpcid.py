#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Find and replace Discord Application IDs in Windows emulator binaries.

The Discord Game SDK commonly embeds an application ID as a 64-bit integer,
while classic discord-rpc builds use ASCII or UTF-16 text.  This standalone,
stdlib-only utility finds both forms, optionally verifies candidates through
Discord's public endpoint, and patches a file without changing its size.

Examples::
    python rpcid.py find HD-Player.exe
    python rpcid.py find "C:\\Program Files\\LDPlayer9"
    python rpcid.py replace HD-Player.exe --new 1234567890123456789
    python rpcid.py name HD-Player.exe --set "My Player"
    python rpcid.py undo HD-Player.exe
    python rpcid.py selftest
"""
from __future__ import annotations

import argparse
import bisect
import datetime as _dt
import hashlib
import json
import os
import re
import shutil
import struct
import sys
import time

try:
    from urllib.request import Request, urlopen
    NETWORK = True
except Exception:                         # pragma: no cover
    NETWORK = False

VERSION = "1.2"
BACKUP_SUFFIX = ".rpcid-bak"
STATE_SUFFIX = ".rpcid.json"
DISCORD_EPOCH = 1420070400000
API = "https://discord.com/api/v9"
KNOWN = {"821361671729709086": "BlueStacks 5 / MSI App Player"}


def _say(prefix, text):
    print("  %-6s %s" % (prefix, text))
def good(text): _say("[ OK ]", text)
def warn(text): _say("[WARN]", text)
def fail(text): _say("[FAIL]", text)
def note(text): _say("[ -- ]", text)
def heading(text):
    print("\n%s\n%s" % (text, "-" * max(28, len(text))))


class PE:
    """Small PE reader used only to identify executable sections/resources."""
    def __init__(self, data):
        self.data, self.ok, self.sections = data, False, []
        self.pe = self.dd = 0
        self.machine, self.plus = 0, False
        if len(data) < 64 or data[:2] != b"MZ": return
        try:
            self.pe = struct.unpack_from("<I", data, 0x3c)[0]
            if data[self.pe:self.pe + 4] != b"PE\0\0": return
            self.machine, nsec = struct.unpack_from("<HH", data, self.pe + 4)
            optsz = struct.unpack_from("<H", data, self.pe + 20)[0]
            self.plus = struct.unpack_from("<H", data, self.pe + 24)[0] == 0x20b
            self.dd = self.pe + 24 + (112 if self.plus else 96)
            for i in range(nsec):
                off = self.pe + 24 + optsz + i * 40
                name = data[off:off + 8].rstrip(b"\0").decode("latin1")
                vsz, va, raw_size, raw = struct.unpack_from("<IIII", data, off + 8)
                chars = struct.unpack_from("<I", data, off + 36)[0]
                self.sections.append((name, va, vsz, raw, raw_size, chars))
            self.ok = True
        except (IndexError, struct.error, UnicodeError):
            pass

    @property
    def arch(self):
        return {0x8664: "x64", 0x14c: "x86", 0xaa64: "arm64"}.get(getattr(self, "machine", 0), "?")

    def section_of(self, offset):
        for name, _va, _vsz, raw, size, chars in self.sections:
            if raw <= offset < raw + size:
                return name, bool(chars & 0x20000000)
        return "headers", False

    def rva_to_offset(self, rva):
        for _n, va, vsz, raw, size, _c in self.sections:
            if va <= rva < va + max(vsz, size): return raw + rva - va
        return None

    def offset_to_rva(self, off):
        for _n, va, vsz, raw, size, _c in self.sections:
            if raw <= off < raw + size: return va + off - raw
        return None

    def directory(self, index):
        if not self.dd: return (0, 0)
        try: return struct.unpack_from("<II", self.data, self.dd + index * 8)
        except struct.error: return (0, 0)

    def signed(self): return self.ok and self.directory(4)[1] != 0

    def version_resource(self):
        rva, size = self.directory(2)
        base = self.rva_to_offset(rva) if rva else None
        if base is None: return None
        # Walk enough of the resource directory to locate RT_VERSION (16).
        result = None
        def walk(off, path):
            nonlocal result
            if result: return
            try: named, ids = struct.unpack_from("<HH", self.data, off + 12)
            except struct.error: return
            for i in range(named + ids):
                try: ident, child = struct.unpack_from("<II", self.data, off + 16 + i * 8)
                except struct.error: return
                if child & 0x80000000:
                    walk(base + (child & 0x7fffffff), path + (ident,))
                elif path and path[0] == 16:
                    try:
                        data_rva, data_size = struct.unpack_from("<II", self.data, base + child)
                    except struct.error: continue
                    data_off = self.rva_to_offset(data_rva)
                    if data_off is not None: result = (data_off, data_size)
        walk(base, ())
        return result


def _align4(n): return (n + 3) & ~3

def version_strings(data, base, size):
    """Return version-resource UTF-16 strings with offsets and fixed capacity."""
    out, end = {}, min(len(data), base + size)
    def walk(off, limit, path):
        while off + 6 <= limit:
            try: wlen, vlen, typ = struct.unpack_from("<HHH", data, off)
            except struct.error: return
            if wlen < 6 or off + wlen > limit + 3: return
            k = off + 6
            while k + 1 < limit and data[k:k + 2] != b"\0\0": k += 2
            key = data[off + 6:k].decode("utf-16-le", "replace")
            value = base + _align4(k + 2 - base)
            value_bytes = vlen * 2 if typ == 1 else vlen
            if path and path[0] == "StringFileInfo" and typ == 1:
                out[key] = {"off": value, "cap": vlen,
                            "value": data[value:value + value_bytes].decode("utf-16-le", "replace").rstrip("\0")}
            child = base + _align4(value + value_bytes - base)
            if child < off + wlen: walk(child, off + wlen, path + (key,))
            off = base + _align4(off + wlen - base)
    walk(base, end, ())
    return out


def set_version_string(buf, entry, text):
    if len(text) + 1 > entry["cap"]:
        return False, "\"%s\" is %d chars, slot takes %d" % (text, len(text), entry["cap"] - 1)
    blob = text.encode("utf-16-le") + b"\0" * (2 * (entry["cap"] - len(text)))
    buf[entry["off"]:entry["off"] + entry["cap"] * 2] = blob
    return True, "ok"


def plausible(value):
    if not (1 << 56) < value < (1 << 61): return False
    stamp = (value >> 22) + DISCORD_EPOCH
    return DISCORD_EPOCH < stamp < time.time() * 1000 + 86400000

def snowflake_date(value):
    try:
        return _dt.datetime.fromtimestamp(((int(value) >> 22) + DISCORD_EPOCH) / 1000, _dt.timezone.utc).strftime("%Y-%m-%d")
    except (ValueError, OverflowError): return "?"

def compiler_constant(value):
    if value & (value + 1) == 0 or value & (value - 1) == 0: return True
    h = "%016x" % value
    if "00000000" in h or "ffffffff" in h or len(set(h)) <= 3: return True
    if any(len(set(h[i:i + n] for i in range(0, 16, n))) == 1 for n in (2, 4, 8)): return True
    return value & 0x3fffff == 0


def _max_plausible():
    """Largest value plausible() can accept right now (snowflake clock bound)."""
    return (int(time.time() * 1000) + 86400000 - DISCORD_EPOCH) << 22


RIP_MODRM = b"\x05\x0d\x15\x1d\x25\x2d\x35\x3d"          # mod=00 rm=101 => [rip+disp32]


def harvest(data, pe=None):
    """Collect every Application-ID-shaped value in `data` with scored evidence.

    Forms: mov r64,imm64 immediates, bare little-endian qwords (at ANY file
    alignment - they used to be read only at 8-byte aligned offsets, which is
    why some builds were never detected), ASCII and UTF-16 decimal text.
    Bare qwords are only kept when something vouches for them (a RIP-relative
    instruction references them, they sit near Discord strings, the value
    repeats, or the ID is known), because plausible-looking junk qwords
    (timestamps, hashes) are otherwise everywhere in a large binary.
    """
    found = {}
    limit = len(data)
    if limit < 8:
        return []
    added = set()

    def add(value, kind, offset, **extra):
        if (kind, offset) in added: return
        added.add((kind, offset))
        rec = found.setdefault(str(value), {"id": str(value), "sites": []})
        site = {"kind": kind, "off": offset}; site.update(extra); rec["sites"].append(site)

    # Addresses the code actually references (RIP-relative).  Used both to
    # admit bare-qword candidates and as ranking evidence.
    rip = set()
    if pe is not None and pe.ok:
        for m in re.finditer(b"[\x48-\x4f][\x8b\x8d][" + RIP_MODRM + b"]", data):      # REX.W mov/lea r64,[rip+d]
            i = m.start()
            if i + 7 > limit: continue
            rva = pe.offset_to_rva(i)
            if rva is not None: rip.add(rva + 7 + struct.unpack_from("<i", data, i + 3)[0])
        for m in re.finditer(b"(?<![\x40-\x4f])[\x8b\x8d][" + RIP_MODRM + b"]", data):  # 32-bit operand form
            i = m.start()
            if i + 6 > limit: continue
            rva = pe.offset_to_rva(i)
            if rva is not None: rip.add(rva + 6 + struct.unpack_from("<i", data, i + 2)[0])

    # x64 mov r64, imm64 (48/49 B8..BF), including non-RAX registers.  Two
    # opcode bytes of one instruction can never also be the opcode pair of
    # another, so a non-overlapping regex finds every candidate at C speed.
    regs = ("rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15")
    for m in re.finditer(rb"[\x48\x49][\xb8-\xbf]", data):
        i = m.start()
        if i + 10 > limit: continue
        value = struct.unpack_from("<Q", data, i + 2)[0]
        if plausible(value) and not compiler_constant(value):
            add(value, "imm64", i + 2, reg=regs[(data[i + 1] - 0xB8) + (8 if data[i] == 0x49 else 0)], instr=i)

    # Where Discord-related data lives; bare qwords are only interesting here
    # (or where code references them, or for a known ID).
    lowered = data.lower()
    dpos = [m.start() for m in re.finditer(b"discord", lowered)]
    windows = []
    for p in dpos:
        lo, hi_ = max(0, p - 4096), min(limit, p + 4096)
        if windows and lo <= windows[-1][1]: windows[-1] = (windows[-1][0], max(windows[-1][1], hi_))
        else: windows.append((lo, hi_))

    claimed = {x for r in found.values() for s in r["sites"] if s["kind"] == "imm64" for x in range(s["off"] - 7, s["off"] + 8)}
    hi = min(0x1f, _max_plausible() >> 56)
    msb_class = b"[" + bytes(range(1, hi + 1)) + b"]" if hi >= 1 else None

    def qword_ok(off):
        if off < 0 or off + 8 > limit or off in claimed: return False
        blob = data[off:off + 8]
        value = struct.unpack_from("<Q", blob)[0]
        if not plausible(value) or compiler_constant(value): return False
        if all(32 <= b <= 126 for b in blob): return False
        add(value, "u64le", off)
        return True

    budget = [4096]                                    # hard cap on noise
    if msb_class:
        for lo, hi_ in windows:                        # near Discord strings
            for m in re.finditer(msb_class, data[lo:hi_]):
                if budget[0] <= 0: break
                if qword_ok(lo + m.start() - 7): budget[0] -= 1
        for rva in rip:                                # referenced by code
            off = pe.rva_to_offset(rva)
            if off is not None and qword_ok(off): budget[0] -= 1
    for kid in KNOWN:                                  # known IDs, any alignment
        packed = struct.pack("<Q", int(kid))
        pos = data.find(packed)
        while pos >= 0:
            if pos not in claimed: add(int(kid), "u64le", pos)
            pos = data.find(packed, pos + len(packed))

    # Decimal text forms.  Junk-shaped numbers (round values, repeated digit
    # patterns) are rejected here too, not just for integer forms.
    for m in re.finditer(rb"(?<![0-9])([0-9]{17,20})(?![0-9])", data):
        value = int(m.group(1))
        if plausible(value) and not compiler_constant(value):
            end, slack = m.end(), 0
            while end < limit and data[end] == 0: end += 1; slack += 1
            add(value, "ascii", m.start(), text=m.group(1).decode(), slack=slack)
    # UTF-16: match the WHOLE digit run, then reject runs longer than 20
    # digits.  The old {17,20} pattern silently read the first 20 pairs of a
    # longer number as a (wrong) ID.  A rejected run is retried two bytes
    # later, and "part of a longer number" needs TWO preceding digit pairs:
    # a single pair can be a phantom, e.g. the last digit + NUL of an ASCII
    # string sitting right in front of a UTF-16 one.
    def _digit_pair(b):
        return len(b) == 2 and 0x30 <= b[0] <= 0x39 and b[1] == 0
    pos, run_re = 0, re.compile(rb"(?:[0-9]\x00){17,}")
    while True:
        m = run_re.search(data, pos)
        if not m: break
        start, end, text = m.start(), m.end(), m.group().decode("utf-16-le")
        value = int(text)
        boundary = start >= 4 and _digit_pair(data[start - 2:start]) and _digit_pair(data[start - 4:start - 2])
        if not boundary and len(text) <= 20 and plausible(value) and not compiler_constant(value):
            tail, slack = end, 0
            while tail + 1 < limit and data[tail:tail + 2] == b"\0\0": tail += 2; slack += 1
            add(value, "utf16le", start, text=text, slack=slack)
            pos = end
        else:
            pos = start + 2

    # Drop obvious pattern noise; a known ID is never noise.
    for key in list(found):
        if key in KNOWN: continue
        rec = found[key]
        kinds = {s["kind"] for s in rec["sites"]}
        imm64_refs = sum(s["kind"] == "imm64" for s in rec["sites"])
        u64le_refs = sum(s["kind"] == "u64le" for s in rec["sites"])
        if imm64_refs > 3 and not kinds & {"ascii", "utf16le"}: del found[key]
        elif u64le_refs > 3 and not kinds & {"ascii", "utf16le", "imm64"}: del found[key]

    def near_discord(off):
        i = bisect.bisect_left(dpos, off)
        for j in (i - 1, i):
            if 0 <= j < len(dpos) and abs(dpos[j] - off) <= 4096:
                if pe is not None and pe.ok:
                    return pe.section_of(off)[0] == pe.section_of(dpos[j])[0]
                return True
        return False

    for rec in found.values():
        kinds = {s["kind"] for s in rec["sites"]}
        score = (4 if kinds & {"ascii", "utf16le"} else 0) + (3 if "imm64" in kinds else 0) \
            + (1 if "u64le" in kinds else 0) + (10 if rec["id"] in KNOWN else 0) + (1 if dpos else 0)
        near = rip_ref = False
        for s in rec["sites"]:
            if pe and pe.ok:
                s["section"], code = pe.section_of(s["off"])
                if s["kind"] == "imm64" and code: score += 2
                if s["kind"] == "u64le":
                    rva = pe.offset_to_rva(s["off"])
                    if rva is not None and rva in rip: s["rip"] = True; rip_ref = True
            if not near and s["kind"] in ("imm64", "u64le") and near_discord(s["off"]): near = True
        if near: score += 2
        if rip_ref: score += 3
        imm64_refs = sum(s["kind"] == "imm64" for s in rec["sites"])
        u64le_refs = sum(s["kind"] == "u64le" for s in rec["sites"])
        if imm64_refs == 1: score += 2
        if u64le_refs >= 2: score += 2
        signals = int(bool(kinds & {"ascii", "utf16le"})) + int("imm64" in kinds) + int(rec["id"] in KNOWN) \
            + int(near) + int(rip_ref) + int(u64le_refs >= 2) + int(imm64_refs >= 2)
        rec.update(score=score, refs=len(rec["sites"]), date=snowflake_date(rec["id"]),
                   confident=signals >= 2,
                   # A bare qword may only be chosen automatically when it sits in
                   # Discord-related data; a mere code reference is not enough,
                   # because referenced timestamps and hashes are everywhere.
                   eligible=bool(kinds & {"ascii", "utf16le", "imm64"}) or rec["id"] in KNOWN
                   or signals >= 2 or (near and "u64le" in kinds))

    # Two bare-qword candidates whose bytes overlap cannot both be real
    # constants; one is usually the other shifted by a few bytes.  The one
    # with less evidence is demoted, and a dead tie demotes both - guessing
    # here is what used to make the detector show a wrong ID.
    sites = sorted(((s["off"], rec) for rec in found.values() for s in rec["sites"]
                    if rec["score"] >= 3 and {x["kind"] for x in rec["sites"]} == {"u64le"}), key=lambda x: x[0])
    for (off1, rec1), (off2, rec2) in zip(sites, sites[1:]):
        if off2 - off1 >= 8 or rec1 is rec2: continue
        if rec1["score"] > rec2["score"]: rec2["score"] = min(rec2["score"], 2)
        elif rec2["score"] > rec1["score"]: rec1["score"] = min(rec1["score"], 2)
        else: rec1["score"] = rec2["score"] = 2

    return sorted(found.values(), key=lambda r: (-r["score"], r["refs"], r["id"]))


def pick_candidate(recs):
    """The record automatic actions may act on: strong, eligible and un-tied."""
    strong = [r for r in recs if r["score"] >= 3]
    if not strong: return None, strong
    top = strong[0]
    if not top.get("eligible"): return None, strong
    if len(strong) > 1 and strong[1]["score"] >= top["score"]:
        return None, strong                     # two equally good guesses: refuse to guess
    return top, strong


_CACHE = {}
def lookup(app_id, timeout=6):
    if app_id in _CACHE: return _CACHE[app_id]
    if not NETWORK: return None
    try:
        req = Request(API + "/applications/" + app_id + "/rpc", headers={"User-Agent": "rpcid/" + VERSION})
        with urlopen(req, timeout=timeout) as response: result = json.loads(response.read().decode("utf-8", "replace"))
        _CACHE[app_id] = result; return result
    except Exception: _CACHE[app_id] = None; return None


def targets(path, recurse=True, cap=512 << 20):
    if os.path.isfile(path): return [path]
    result = []
    for root, dirs, files in os.walk(path):
        result.extend(os.path.join(root, f) for f in files if f.lower().endswith((".exe", ".dll")) and os.path.getsize(os.path.join(root, f)) <= cap)
        if not recurse: dirs[:] = []
    return sorted(result)

def read(path):
    with open(path, "rb") as f: return f.read()
def backup(path):
    dest = path + BACKUP_SUFFIX
    if not os.path.exists(dest): shutil.copy2(path, dest)
    return dest
def save(path, data):
    temp = path + ".rpcid-tmp"
    with open(temp, "wb") as f: f.write(data)
    os.replace(temp, path)
def fits(site, new):
    if site["kind"] in ("imm64", "u64le"): return True, "8-byte integer, any ID fits"
    growth = len(new) - len(site.get("text", ""))
    return (True, "same length or shorter") if growth <= 0 else ((True, "%d spare NULs" % site.get("slack", 0)) if site.get("slack", 0) >= growth + 1 else (False, "%d longer, %d spare NULs" % (growth, site.get("slack", 0))))
def site_value(data, site):
    """Read back the ID currently stored at a site, or None if it no longer holds one."""
    off, kind = site["off"], site["kind"]
    if kind in ("imm64", "u64le"):
        if off + 8 > len(data): return None
        return str(struct.unpack_from("<Q", data, off)[0])
    if kind == "ascii":
        end = off
        while end < len(data) and 0x30 <= data[end] <= 0x39: end += 1
        text = data[off:end].decode("ascii", "replace")
    else:
        end = off
        while end + 1 < len(data) and 0x30 <= data[end] <= 0x39 and data[end + 1] == 0: end += 2
        text = data[off:end].decode("utf-16-le", "replace")
    return text or None


def sites_for_id(data, app_id):
    """Every place `app_id` is stored right now, with freshly measured spare room.

    This is what makes re-patching an already patched file reliable: the sites are
    re-derived from the bytes on disk instead of trusted from an earlier scan.
    Matches never overlap: an occurrence starting inside an already matched span
    cannot be written without corrupting the first one.
    """
    app_id = str(app_id)
    sites, seen = [], set()

    def add(kind, off, **extra):
        key = (kind, off)
        if key in seen: return
        seen.add(key)
        site = {"kind": kind, "off": off}
        site.update(extra)
        sites.append(site)

    try: packed = struct.pack("<Q", int(app_id))
    except (ValueError, struct.error): packed = None
    if packed:
        pos = data.find(packed)
        while pos >= 0:
            add("u64le", pos)
            pos = data.find(packed, pos + len(packed))

    for enc, kind, width in (("ascii", "ascii", 1), ("utf-16-le", "utf16le", 2)):
        needle = app_id.encode(enc)
        pos = data.find(needle)
        while pos >= 0:
            before = data[pos - 2 * width:pos] if pos >= 2 * width else b""
            after = data[pos + len(needle):pos + len(needle) + width]
            # "Embedded in a longer number" needs two full chars of context on
            # the relevant side: a single digit char can be a phantom, e.g.
            # the last byte pair of a neighbouring ASCII ID string ("9\0")
            # sitting directly in front of a UTF-16 copy.
            if width == 1:
                digit_before = bool(before) and 0x30 <= before[-1] <= 0x39
                digit_after = len(after) == 1 and 0x30 <= after[0] <= 0x39
            else:
                digit_before = (len(before) == 4 and 0x30 <= before[2] <= 0x39 and before[3] == 0
                                and 0x30 <= before[0] <= 0x39 and before[1] == 0)
                digit_after = len(after) == 2 and 0x30 <= after[0] <= 0x39 and after[1] == 0
            if not digit_before and not digit_after:
                end, slack = pos + len(needle), 0
                while end + width <= len(data) and data[end:end + width] == b"\0" * width:
                    end += width; slack += 1
                add(kind, pos, text=app_id, slack=slack)
            pos = data.find(needle, pos + len(needle))
    return sites


def state_path(path): return path + STATE_SUFFIX

def load_state(path):
    try:
        with open(state_path(path), "r", encoding="utf-8") as f:
            state = json.load(f)
        return state if isinstance(state, dict) else {}
    except (OSError, ValueError):
        return {}

def state_history(state):
    """Every ID this tool has ever seen in this file, oldest first."""
    out = []
    for x in (state.get("history") or []) + [state.get("original_id"), state.get("previous_id"), state.get("current_id")]:
        x = str(x) if x is not None else ""
        if x and x not in out: out.append(x)
    return out

def save_state(path, original_id, current_id, count):
    state = load_state(path)
    history = state_history(state)
    for x in (original_id, current_id):
        x = str(x) if x is not None else ""
        if x and x not in history: history.append(x)
    state.update({"version": 1,
                  "tool": "rpcid " + VERSION,
                  "original_id": state.get("original_id") or original_id,
                  "current_id": current_id,
                  "previous_id": original_id,
                  "history": history,
                  "occurrences": count,
                  "updated": _dt.datetime.now(_dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")})
    try:
        with open(state_path(path), "w", encoding="utf-8") as f:
            json.dump(state, f, indent=2)
    except OSError as exc:
        warn("could not remember the new ID: %s" % exc)
    return state


def current_id_of(data, path):
    """The ID this file is patched to, if we can prove it is still in the bytes."""
    state = load_state(path)
    candidate = str(state.get("current_id") or "")
    if candidate and sites_for_id(data, candidate):
        return candidate, state
    return None, state


def leftover_ids(data, state, *keep):
    """Remembered IDs that are still in the file although they are not current.

    These are the copies an earlier patch could not overwrite because their slot
    was too small; reporting them keeps a partially patched file from looking
    healthy, and re-patching repairs them whenever the new ID fits.
    """
    keep = {str(x) for x in keep if x}
    out = []
    for old in state_history(state):
        if old in keep or old in out: continue
        if sites_for_id(data, old): out.append(old)
    return out


def write_sites(buf, sites, new):
    done, skipped = [], []
    for s in sites:
        ok, why = fits(s, new)
        if not ok: skipped.append((s, why)); continue
        if s["kind"] in ("imm64", "u64le"): struct.pack_into("<Q", buf, s["off"], int(new))
        else:
            enc, width = ("ascii", 1) if s["kind"] == "ascii" else ("utf-16-le", 2)
            span = (len(s["text"]) + s.get("slack", 0)) * width
            blob = new.encode(enc) + b"\0" * width
            buf[s["off"]:s["off"] + span] = (blob + b"\0" * span)[:span]
        done.append(s)
    return done, skipped


def cmd_find(a):
    paths = targets(a.path, not a.no_recurse)
    if not paths: fail("nothing to scan at %s" % a.path); return 1
    total = 0
    for path in paths:
        data = read(path)
        pe = PE(data)
        candidates = harvest(data, pe); strong = [r for r in candidates if r["score"] >= 3]
        if not candidates: note("%s: no ID-shaped values" % os.path.basename(path)); continue
        heading(os.path.basename(path))
        note("PE %s, %s bytes, %s" % (pe.arch, format(len(data), ","), "signed" if pe.signed() else "unsigned")
             if pe.ok else "%s bytes, not a PE image" % format(len(data), ","))
        patched, state = current_id_of(data, path)
        if patched:
            good("already patched: current ID %s (original %s)" % (patched, state.get("original_id") or "unknown"))
            note("   %d occurrence(s); patching again replaces exactly these" % len(sites_for_id(data, patched)))
            for old in leftover_ids(data, state, patched):
                warn("older ID %s still has %d occurrence(s) the last patch could not fit" % (old, len(sites_for_id(data, old))))
                note("   patching again with a shorter ID replaces those too")
        elif state.get("current_id"):
            warn("the remembered ID %s is no longer in this file" % state["current_id"])
        note("%d candidate(s), %d worth checking" % (len(candidates), len(strong)))
        hits = []
        if not a.no_net:
            for rec in strong[:a.max_lookups]:
                app = lookup(rec["id"])
                if app and app.get("name"): rec["app"] = app; hits.append(rec); good('%s  "%s"' % (rec["id"], app["name"]))
                time.sleep(.12)
        for rec in (hits or strong[:6]):
            for s in rec["sites"][:4]: note("   %s  %-7s @0x%-8x %s  created %s" % (rec["id"], s["kind"], s["off"], s.get("section", "?"), rec["date"]))
            if rec["id"] in KNOWN: note("   known: " + KNOWN[rec["id"]])
        total += len(hits or strong)
    if len(paths) > 1: note("scanned %d file(s), %d finding(s)" % (len(paths), total))
    return 0


def select_sites(data, path, a):
    """Decide which ID to overwrite. Order: --old, recorded patch state, heuristics."""
    if a.old:
        chosen = []
        for old in a.old:
            old = str(old).strip()
            # A short or malformed "old" value would match unrelated bytes all
            # over the file, so only a full 17-20 digit ID may be used here.
            if not re.fullmatch(r"\d{17,20}", old):
                warn('%s is not a 17-20 digit Application ID' % old); continue
            sites = sites_for_id(data, old)
            if sites: chosen.append({"id": old, "sites": sites, "source": "manual entry"})
            else: warn("%s is not present in the file" % old)
        return chosen
    current, state = current_id_of(data, path)
    if current:
        note("this file was patched before; current ID %s" % current)
        return [{"id": current, "sites": sites_for_id(data, current), "source": "previous patch"}]
    if state.get("current_id"):
        warn("the remembered ID %s is no longer in the file; scanning instead" % state["current_id"])
    recs = harvest(data, PE(data))
    pick, strong = pick_candidate(recs)
    if not pick:
        if strong:
            warn("%d possible ID(s) found (%s); none is confirmed" % (len(strong), ", ".join(r["id"] for r in strong[:3])))
            note("enter the current one in the advanced field to patch it")
        return []
    return [{"id": pick["id"], "sites": sites_for_id(data, pick["id"]) or pick["sites"], "source": "scan"}]


def cmd_replace(a):
    if not os.path.isfile(a.path): fail("no such file: " + a.path); return 1
    new_id = (a.new or "").strip()
    if not re.fullmatch(r"\d{17,20}", new_id): fail("Enter a Discord Application ID of 17 to 20 digits"); return 1
    if not plausible(int(new_id)) or compiler_constant(int(new_id)):
        fail("%s is not shaped like a Discord Application ID" % new_id); return 1
    data = read(a.path)
    state = load_state(a.path)
    selected = [r for r in select_sites(data, a.path, a) if r["sites"]]

    # Copies of older IDs that a previous patch could not fit are patched too,
    # so re-patching repairs a partially patched file instead of leaving the
    # very first ID behind forever.
    keep = {new_id} | {r["id"] for r in selected}
    for old in leftover_ids(data, state, *keep):
        selected.append({"id": old, "sites": sites_for_id(data, old), "source": "leftover from an earlier patch"})
        note("older ID %s was left behind by an earlier patch; it will be replaced as well" % old)

    # Already on the requested ID: succeed (and re-sync the state file) instead of failing.
    everything_is_new = bool(selected) and all(r["id"] == new_id for r in selected)
    if everything_is_new or (not a.old and not selected):
        existing = sites_for_id(data, new_id)
        if existing:
            good("%s is already the Application ID in this file (%d occurrence(s))" % (new_id, len(existing)))
            if not a.dry_run: save_state(a.path, state.get("original_id") or new_id, new_id, len(existing))
            return 0
    if not selected:
        fail("could not find an Application ID in that file"); note("if you know the current ID, enter it in the advanced field"); return 1
    primary = [r for r in selected if r["source"] != "leftover from an earlier patch"]
    if len(primary) > 1 and not a.all:
        warn("%d different IDs found; enter the current one in the advanced field" % len(primary)); return 1

    old_id = selected[0]["id"]
    all_sites = [s for r in selected for s in r["sites"]]
    heading("Replacing")
    for r in selected:
        note("%s -> %s (%d occurrence(s), via %s)" % (r["id"], new_id, len(r["sites"]), r["source"]))
    if a.dry_run: warn("preview only: nothing written"); return 0
    if not a.yes and input("  write it? [y/N] ").strip().lower()[:1] != "y": note("cancelled"); return 0

    buf = bytearray(data)
    done, skipped = write_sites(buf, all_sites, new_id)
    if not done: fail("nothing could be written"); return 1
    try:
        b = backup(a.path); save(a.path, buf)
    except PermissionError:
        fail("cannot write %s; close the emulator and try as Administrator" % a.path); return 2
    good("wrote %d occurrence(s), %d skipped" % (len(done), len(skipped)))
    note("a backup of the original file was created")

    # Verify straight from the written bytes - a scan can score a valid ID away.
    # Every ID this file has ever held is checked, not just the selected one,
    # so a leftover copy can never hide behind a "verified" result again.
    fresh = read(a.path)
    hits = sites_for_id(fresh, new_id)
    history = set(state_history(state)) | {r["id"] for r in selected} | {new_id}
    stale = [(old, len(sites_for_id(fresh, old))) for old in sorted(history - {new_id}) if sites_for_id(fresh, old)]
    if hits and not stale:
        good("verified: %s is in the file (%d occurrence(s))" % (new_id, len(hits)))
        save_state(a.path, old_id, new_id, len(hits))
        return 0
    if hits and stale:
        warn("PARTIAL: %s was written, but older copies remain" % new_id)
        for old, count in stale:
            warn("%d occurrence(s) of %s do not fit the new ID; use a shorter ID or Undo to restore the original" % (count, old))
        save_state(a.path, old_id, new_id, len(hits))
        return 0
    fail("could not verify the new ID"); return 1


def cmd_status(a):
    """Machine-readable state, used by the BSI Customizer UI."""
    out = {"ok": False, "path": a.path, "current_id": None, "original_id": None,
           "candidates": [], "patched": False, "name": None, "msg": "", "leftovers": []}
    if not os.path.isfile(a.path):
        out["msg"] = "no such file"
        print(json.dumps(out)); return 1
    data = read(a.path)
    current, state = current_id_of(data, a.path)
    out["original_id"] = state.get("original_id")
    if current:
        out.update(ok=True, patched=True, current_id=current,
                   msg="patched to %s" % current)
    else:
        recs = harvest(data, PE(data))
        pick, strong = pick_candidate(recs)
        out["candidates"] = [{"id": r["id"], "score": r["score"], "refs": r["refs"],
                              "known": KNOWN.get(r["id"])} for r in strong[:6]]
        if pick:
            out.update(ok=True, current_id=pick["id"],
                       msg="detected %s" % pick["id"])
        elif strong:
            out.update(ok=True, msg="%d possible ID(s) found; none confirmed - use Find to review them" % len(strong))
        else:
            out["msg"] = "no Application ID found"
    stale = leftover_ids(data, state, current, out["current_id"])
    if stale:
        out["leftovers"] = stale
        out["msg"] += "; %d older ID copy(ies) still present (%s)" % (len(stale), ", ".join(stale))
    if out["current_id"] and not a.no_net:
        app = lookup(out["current_id"])
        if app and app.get("name"):
            out["name"] = app["name"]
            out["msg"] += ' ("%s")' % app["name"]
    out["known"] = KNOWN.get(out["current_id"] or "")
    out["backup"] = os.path.isfile(a.path + BACKUP_SUFFIX)
    print(json.dumps(out))
    return 0 if out["ok"] else 1


def cmd_name(a):
    if not os.path.isfile(a.path): fail("no such file: " + a.path); return 1
    data, pe = read(a.path), PE(read(a.path)); resource = pe.version_resource() if pe.ok else None
    if not resource: fail("file has no readable version resource"); return 1
    strings = version_strings(data, *resource); heading("Names inside " + os.path.basename(a.path))
    for key, entry in strings.items(): note("%-18s %s (max %d chars)" % (key, repr(entry["value"]), entry["cap"] - 1))
    if not a.set: note("the Discord line comes from the Discord application name, not this Windows version resource"); return 0
    keys = [a.key] if a.key else ["FileDescription", "ProductName"]; buf = bytearray(data); changed = []
    for key in keys:
        if key not in strings: warn(key + " is not in this file"); continue
        ok, why = set_version_string(buf, strings[key], a.set)
        if ok: good('%s -> %s' % (key, repr(a.set))); changed.append(key)
        else: fail(key + ": " + why)
    if not changed or a.dry_run: return 0 if changed else 1
    if not a.yes and input("  write it? [y/N] ").strip().lower()[:1] != "y": note("cancelled"); return 0
    b = backup(a.path); save(a.path, buf); good("saved; a backup of the original file was created"); return 0

def cmd_undo(a):
    backup_path = a.path + BACKUP_SUFFIX
    if not os.path.isfile(backup_path): fail("no backup was found for this file"); return 1
    try: shutil.copy2(backup_path, a.path)
    except PermissionError: fail("cannot write " + a.path); return 2
    try:
        if os.path.isfile(state_path(a.path)): os.remove(state_path(a.path))
    except OSError: pass
    good("restored the original file"); return 0


def cmd_wizard(a):
    """Interactive flow retained for users who run rpcid.py without a command."""
    path = a.path or input("  file or folder to scan: ").strip().strip('"')
    if not path or not os.path.exists(path): fail("nothing to scan"); return 1
    a.path = path
    cmd_find(a)
    new = input("\n  your Application ID (enter = stop here): ").strip()
    if not new: return 0
    a.new, a.yes = new, False
    return cmd_replace(a)


def cmd_selftest(_a):
    import tempfile, shutil
    known, other = 821361671729709086, 1234567890123456789
    checks = []
    def check(label, value): checks.append((label, bool(value)))
    check("real snowflakes are accepted", plausible(known) and plausible(other))
    check("small values are rejected", not plausible(123456))
    blob = b"\x90" * 12 + b"\x48\xb8" + struct.pack("<Q", known) + b"X" * 8 + str(known).encode() + b"\0\0\0" + str(other).encode("utf-16-le") + b"\0\0"
    recs = {r["id"]: r for r in harvest(blob)}
    check("integer, ASCII and UTF-16 forms are found", str(known) in recs and str(other) in recs)
    check("ASCII form is classified", any(s["kind"] == "ascii" for s in recs[str(known)]["sites"]))
    replacement = str(known + 1)
    buf = bytearray(blob); done, skipped = write_sites(buf, recs[str(known)]["sites"], replacement)
    check("replacement preserves size", done and not skipped and len(buf) == len(blob))
    check("new value is detectable", replacement in {r["id"] for r in harvest(bytes(buf))})
    # Re-patching an already patched image must target the current ID, not a stale one.
    again = str(known + 2)
    sites = sites_for_id(bytes(buf), replacement)
    check("patched ID is re-locatable", len(sites) >= 2)
    buf2 = bytearray(buf); done2, skipped2 = write_sites(buf2, sites, again)
    check("second patch writes every site", done2 and not skipped2)
    check("second patch is readable back", len(sites_for_id(bytes(buf2), again)) == len(sites))
    check("old ID is gone after re-patch", not sites_for_id(bytes(buf2), replacement))
    short = str(known)[:17]                      # genuinely shorter than the slot
    buf3 = bytearray(buf2); write_sites(buf3, sites_for_id(bytes(buf2), again), short)
    check("shorter ID then longer ID round-trips",
          bool(sites_for_id(bytes(buf3), short)) and len(bytes(buf3)) == len(blob))
    longer = str(other)                          # 19 digits fits the 18+3 slot
    buf4 = bytearray(buf3); write_sites(buf4, sites_for_id(bytes(buf3), short), longer)
    check("longer ID reuses the spare NULs", bool(sites_for_id(bytes(buf4), longer)) and len(bytes(buf4)) == len(blob))

    # Bare qwords used to be read only at 8-byte aligned offsets.
    unaligned = b"\x90" * 61 + struct.pack("<Q", known) + b"\x90" * 64
    check("misaligned qword ID is found", str(known) in {r["id"] for r in harvest(unaligned)})
    sdkish = b"\x90" * 32 + b"discord" + b"\x00" * 29 + struct.pack("<Q", other) * 2 + b"\x90" * 64
    pick, strong = pick_candidate(harvest(sdkish))
    check("bare qword near Discord strings is the pick", pick and pick["id"] == str(other) and len(strong) == 1)
    # Text forms used to skip the junk-number filter and could out-rank the real ID.
    decoy = b"\x90" * 16 + struct.pack("<Q", known) + str(1 << 60).encode() + b"\x00" + b"\x90" * 8
    check("round text numbers are ignored", str(1 << 60) not in {r["id"] for r in harvest(decoy)})
    longrun = ("1" * 21).encode("utf-16-le") + b"\0\0" + str(other).encode("utf-16-le") + b"\0\0"
    check("overlong UTF-16 numbers are not misread", "1" * 21 not in {r["id"] for r in harvest(b"\x90" * 8 + longrun)})
    # Overlapping matches of the same pattern must not be reported as separate sites.
    padded = struct.pack("<Q", known) * 2
    check("overlapping byte matches collapse", len(sites_for_id(padded, str(known))) == 2)
    # A byte-shifted ghost overlapping the real qword must never win the pick.
    ghost = b"\x00\x00" + struct.pack("<Q", other) + b"\x00" * 6
    ghost_blob = b"\x90" * 32 + b"discord" + b"\x00" * 24 + ghost + b"\x90" * 64
    gpick, _gstrong = pick_candidate(harvest(ghost_blob))
    check("shifted ghost qword never wins", not gpick or gpick["id"] == str(other))

    # A real PE layout: a misaligned .rdata qword referenced by RIP-relative
    # code next to Discord strings must be detected with reference evidence.
    head = bytearray(0x200)
    head[0:2] = b"MZ"
    struct.pack_into("<I", head, 0x3c, 0x40)
    head[0x40:0x44] = b"PE\0\0"
    struct.pack_into("<HH", head, 0x44, 0x8664, 2)
    struct.pack_into("<H", head, 0x54, 0xf0)
    struct.pack_into("<H", head, 0x58, 0x20b)
    for i, (name, va, raw, chars) in enumerate(((".text", 0x1000, 0x200, 0x60000020),
                                                (".rdata", 0x1200, 0x400, 0x40000040))):
        off = 0x148 + i * 40
        head[off:off + 8] = name.encode().ljust(8, b"\x00")
        struct.pack_into("<IIII", head, off + 8, 0x200, va, 0x200, raw)
        struct.pack_into("<I", head, off + 36, chars)
    code = b"\x48\x8b\x05" + struct.pack("<i", 0x1203 - 0x1007) + b"\xcc" * 8     # mov rax,[rip+disp] -> rva 0x1203
    rdata = b"\xcc\xcc\xcc" + struct.pack("<Q", other) + b"\x00" * 20 + b"discord"
    image = bytes(head) + code.ljust(0x200, b"\xcc") + rdata.ljust(0x200, b"\x00")
    image_pe = PE(image)
    check("synthetic PE parses", image_pe.ok and image_pe.arch == "x64")
    pe_pick, _pe_strong = pick_candidate(harvest(image, image_pe))
    check("RIP-referenced misaligned qword is detected",
          pe_pick and pe_pick["id"] == str(other) and any(s.get("rip") for s in pe_pick["sites"]))

    # End-to-end re-patch: a slot too small for the first new ID must be
    # reported as leftover and repaired by a later, shorter patch.
    import io as _io, contextlib as _cl
    def json_status(path):
        with _cl.redirect_stdout(_io.StringIO()) as capture:
            main(["status", path, "--no-net"])
        return json.loads(capture.getvalue())
    tmp = tempfile.mkdtemp()
    try:
        exe = os.path.join(tmp, "HD-Player.exe")
        orig = (b"\x48\xb8" + struct.pack("<Q", known) + b"\x90" * 8
                + str(known).encode() + b"\x00" * 2
                + str(known).encode("utf-16-le") + b"\x00\x00" + b"\x90" * 16)
        with open(exe, "wb") as f: f.write(orig)
        rc = main(["replace", exe, "--new", str(other), "--yes", "--no-net"])
        check("tight slot patch keeps the size", rc == 0 and os.path.getsize(exe) == len(orig))
        out = json_status(exe)
        check("status reports the leftover old ID", out["current_id"] == str(other) and out["leftovers"] == [str(known)])
        short2 = str(other)[:18]
        rc = main(["replace", exe, "--new", short2, "--yes", "--no-net"])
        out2 = json_status(exe)
        check("re-patch repairs the leftover copies", rc == 0 and out2["leftovers"] == [] and out2["current_id"] == short2)
        check("nothing of the original ID survives", not sites_for_id(read(exe), str(known)))
        rc = main(["replace", exe, "--new", short2, "--old", "123", "--yes", "--no-net"])
        check("malformed manual old ID is rejected", rc == 1)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    for label, result in checks: (good if result else fail)(label)
    print("\n%d checks - %s" % (len(checks), "ALL PASSED" if all(x[1] for x in checks) else "FAILURES"))
    return 0 if all(x[1] for x in checks) else 1


def main(argv=None):
    parser = argparse.ArgumentParser(prog="rpcid", description="Find and replace Discord Application IDs in emulator binaries.")
    parser.add_argument("command", nargs="?", default="wizard", choices=("find", "replace", "name", "undo", "status", "selftest", "wizard"))
    parser.add_argument("path", nargs="?"); parser.add_argument("--new"); parser.add_argument("--old", action="append"); parser.add_argument("--set"); parser.add_argument("--key"); parser.add_argument("--all", action="store_true"); parser.add_argument("--max-lookups", type=int, default=200); parser.add_argument("--no-net", action="store_true"); parser.add_argument("--no-recurse", action="store_true"); parser.add_argument("--dry-run", action="store_true"); parser.add_argument("--yes", "-y", action="store_true"); parser.add_argument("--version", action="version", version="rpcid " + VERSION)
    a = parser.parse_args(argv)
    if a.command not in ("selftest", "wizard") and not a.path: parser.error(a.command + " needs a file or folder")
    return {"find": cmd_find, "replace": cmd_replace, "name": cmd_name, "undo": cmd_undo, "status": cmd_status, "selftest": cmd_selftest, "wizard": cmd_wizard}[a.command](a)

if __name__ == "__main__": sys.exit(main())
