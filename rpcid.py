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

VERSION = "1.0"
BACKUP_SUFFIX = ".rpcid-bak"
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

    def directory(self, index):
        try: return struct.unpack_from("<II", self.data, self.dd + index * 8)
        except struct.error: return (0, 0)

    def signed(self): return self.directory(4)[1] != 0

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


def harvest(data, pe=None):
    found = {}
    def add(value, kind, offset, **extra):
        key = str(value)
        rec = found.setdefault(key, {"id": key, "sites": []})
        site = {"kind": kind, "off": offset}; site.update(extra); rec["sites"].append(site)
    # x64 mov r64, imm64 (48/49 B8..BF), including non-RAX registers.
    for i in range(max(0, len(data) - 9)):
        if data[i] not in (0x48, 0x49) or not 0xB8 <= data[i + 1] <= 0xBF: continue
        value = struct.unpack_from("<Q", data, i + 2)[0]
        if plausible(value) and not compiler_constant(value):
            regs = ("rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15")
            add(value, "imm64", i + 2, reg=regs[(data[i + 1] - 0xB8) + (8 if data[i] == 0x49 else 0)], instr=i)
    claimed = {x for r in found.values() for s in r["sites"] if s["kind"] == "imm64" for x in range(s["off"] - 7, s["off"] + 8)}
    for off in range(0, max(0, len(data) - 7), 8):
        if off in claimed: continue
        value = struct.unpack_from("<Q", data, off)[0]
        if plausible(value) and not compiler_constant(value) and not all(32 <= b <= 126 for b in data[off:off + 8]): add(value, "u64le", off)
    for m in re.finditer(rb"(?<![0-9])([0-9]{17,20})(?![0-9])", data):
        value = int(m.group(1))
        if plausible(value):
            end, slack = m.end(), 0
            while end < len(data) and data[end] == 0: end += 1; slack += 1
            add(value, "ascii", m.start(), text=m.group(1).decode(), slack=slack)
    for m in re.finditer(rb"(?:[0-9]\0){17,20}", data):
        text = m.group().decode("utf-16-le")
        if plausible(int(text)):
            end, slack = m.end(), 0
            while end + 1 < len(data) and data[end:end + 2] == b"\0\0": end += 2; slack += 1
            add(int(text), "utf16le", m.start(), text=text, slack=slack)
    for key in list(found):
        if sum(s["kind"] == "imm64" for s in found[key]["sites"]) > 3 and not any(s["kind"] in ("ascii", "utf16le") for s in found[key]["sites"]): del found[key]
    discordish = b"discord" in data.lower()
    for rec in found.values():
        kinds = {s["kind"] for s in rec["sites"]}; score = (4 if kinds & {"ascii", "utf16le"} else 0) + (3 if "imm64" in kinds else 0) + (10 if rec["id"] in KNOWN else 0) + (1 if discordish else 0)
        for s in rec["sites"]:
            if pe and pe.ok:
                s["section"], code = pe.section_of(s["off"])
                if s["kind"] == "imm64" and code: score += 2
        if sum(s["kind"] == "imm64" for s in rec["sites"]) == 1: score += 2
        rec.update(score=score, refs=len(rec["sites"]), date=snowflake_date(rec["id"]))
    return sorted(found.values(), key=lambda r: (-r["score"], r["refs"], r["id"]))


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
        data, pe, candidates = read(path), PE(read(path)), None
        candidates = harvest(data, pe); strong = [r for r in candidates if r["score"] >= 3]
        if not candidates: note("%s: no ID-shaped values" % os.path.basename(path)); continue
        heading(os.path.basename(path)); note("PE %s, %s bytes, %s" % (pe.arch, format(len(data), ","), "signed" if pe.signed() else "unsigned"))
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


def cmd_replace(a):
    if not os.path.isfile(a.path): fail("no such file: " + a.path); return 1
    if not re.fullmatch(r"\d{17,20}", a.new or ""): fail("--new must be a 17-20 digit Discord Application ID"); return 1
    data, pe = read(a.path), PE(read(a.path)); recs = harvest(data, pe)
    if a.old:
        selected = [r for r in recs if r["id"] in a.old]
        for old in a.old:
            if not any(r["id"] == old for r in selected):
                sites = []
                for kind, enc in (("u64le", None), ("ascii", "ascii"), ("utf16le", "utf-16-le")):
                    needle = struct.pack("<Q", int(old)) if not enc else old.encode(enc); pos = data.find(needle)
                    while pos >= 0:
                        sites.append({"kind": kind, "off": pos, "text": old, "slack": 0}); pos = data.find(needle, pos + 1)
                if sites: selected.append({"id": old, "sites": sites, "score": 9})
    else:
        strong = [r for r in recs if r["score"] >= 3]; selected = [r for r in strong if r["id"] in KNOWN] or strong[:1]
    if not selected: fail("could not find an Application ID in that file"); note("if you know it, pass --old <id>"); return 1
    if len(selected) > 1 and not a.all: warn("%d different IDs found; pick one with --old or use --all" % len(selected)); return 1
    all_sites = [s for r in selected for s in r["sites"]]; heading("Replacing")
    for r in selected: note("%s -> %s (%d occurrence(s))" % (r["id"], a.new, len(r["sites"])))
    if a.dry_run: warn("--dry-run: nothing written"); return 0
    if not a.yes and input("  write it? [y/N] ").strip().lower()[:1] != "y": note("cancelled"); return 0
    buf = bytearray(data); done, skipped = write_sites(buf, all_sites, a.new)
    if not done: fail("nothing could be written"); return 1
    try: b = backup(a.path); save(a.path, buf)
    except PermissionError: fail("cannot write %s; close the emulator and try as Administrator" % a.path); return 2
    good("wrote %d occurrence(s), %d skipped" % (len(done), len(skipped))); note("backup " + os.path.basename(b))
    if any(r["id"] == a.new for r in harvest(read(a.path), PE(read(a.path)))): good("verified: %s is in the file" % a.new)
    else: fail("could not verify the new ID")
    return 0


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
    b = backup(a.path); save(a.path, buf); good("saved, backup " + os.path.basename(b)); return 0

def cmd_undo(a):
    backup_path = a.path + BACKUP_SUFFIX
    if not os.path.isfile(backup_path): fail("no backup at " + backup_path); return 1
    try: shutil.copy2(backup_path, a.path)
    except PermissionError: fail("cannot write " + a.path); return 2
    good("restored " + os.path.basename(a.path)); return 0


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
    for label, result in checks: (good if result else fail)(label)
    print("\n%d checks - %s" % (len(checks), "ALL PASSED" if all(x[1] for x in checks) else "FAILURES"))
    return 0 if all(x[1] for x in checks) else 1


def main(argv=None):
    parser = argparse.ArgumentParser(prog="rpcid", description="Find and replace Discord Application IDs in emulator binaries.")
    parser.add_argument("command", nargs="?", default="wizard", choices=("find", "replace", "name", "undo", "selftest", "wizard"))
    parser.add_argument("path", nargs="?"); parser.add_argument("--new"); parser.add_argument("--old", action="append"); parser.add_argument("--set"); parser.add_argument("--key"); parser.add_argument("--all", action="store_true"); parser.add_argument("--max-lookups", type=int, default=200); parser.add_argument("--no-net", action="store_true"); parser.add_argument("--no-recurse", action="store_true"); parser.add_argument("--dry-run", action="store_true"); parser.add_argument("--yes", "-y", action="store_true"); parser.add_argument("--version", action="version", version="rpcid " + VERSION)
    a = parser.parse_args(argv)
    if a.command not in ("selftest", "wizard") and not a.path: parser.error(a.command + " needs a file or folder")
    return {"find": cmd_find, "replace": cmd_replace, "name": cmd_name, "undo": cmd_undo, "selftest": cmd_selftest, "wizard": cmd_wizard}[a.command](a)

if __name__ == "__main__": sys.exit(main())
