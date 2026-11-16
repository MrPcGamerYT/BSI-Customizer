import collections, glob, os, re, subprocess
QT = "/home/user/.venvqt/lib/python3.11/site-packages/PySide6/Qt"
OUT = "/home/user/fakelib"
os.makedirs(OUT, exist_ok=True)
binaries = (glob.glob(os.path.join(QT, "lib", "lib*.so*"))
            + glob.glob(os.path.join(QT, "plugins", "**", "*.so"), recursive=True))
wanted = collections.defaultdict(set); plain = set()
for lib in binaries:
    r = subprocess.run(["nm", "-D", "--undefined-only", lib], capture_output=True, text=True).stdout
    for line in r.splitlines():
        parts = line.split()
        if not parts: continue
        token = parts[-1]
        if "@" in token:
            name, _, ver = token.partition("@")
            (wanted[name].add(ver) if ver else plain.add(name))
        else: plain.add(token)
OWNERS = [("libdbus-1.so.3", ["dbus_"]), ("libfontconfig.so.1", ["Fc"]),
          ("libfreetype.so.6", ["FT_"]), ("libxkbcommon.so.0", ["xkb_"]),
          ("libX11.so.6", ["X"]), ("libxcb.so.1", ["xcb_"]),
          ("libGL.so.1", ["glX", "gl"]), ("libEGL.so.1", ["egl"])]
for soname, prefixes in OWNERS:
    syms = sorted(s for s in (set(plain) | set(wanted)) if any(s.startswith(p) for p in prefixes))
    if not syms: continue
    base = re.sub(r"\W", "_", soname); src = "/tmp/h_%s.c" % base
    tags = collections.defaultdict(set)
    for s in syms:
        for v in wanted.get(s, ()):
            if not re.match(r"^(GLIBC|GCC|CXXABI|ZLIB)", v): tags[v].add(s)
    lines = ["/* generated */"]
    for s in syms:
        lines.append("long __impl_%s(void *a, ...) { (void)a; return 0; }" % s)
        vs = [v for v in wanted.get(s, ()) if not re.match(r"^(GLIBC|GCC|CXXABI|ZLIB)", v)]
        if vs:
            for v in sorted(vs): lines.append('__asm__(".symver __impl_%s,%s@%s");' % (s, s, v))
        else: lines.append("long %s(void *a, ...) { (void)a; return 0; }" % s)
    open(src, "w").write("\n".join(lines) + "\n")
    cmd = ["gcc", "-shared", "-fPIC", "-O0", "-Wl,-soname," + soname]
    if tags:
        vmap = "/tmp/h_%s.map" % base; nodes = sorted(tags)
        with open(vmap, "w") as f:
            f.write("%s {\n  global: *;\n  local: *;\n};\n" % nodes[0])
            for v in nodes[1:]: f.write("%s {\n  global: *;\n} %s;\n" % (v, nodes[0]))
        cmd.append("-Wl,--version-script=" + vmap)
    cmd += ["-o", os.path.join(OUT, soname), src]
    r = subprocess.run(cmd, capture_output=True, text=True)
    print("%-24s %3d %s" % (soname, len(syms), "ok" if r.returncode == 0 else "FAIL"))
