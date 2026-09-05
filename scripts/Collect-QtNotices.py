#!/usr/bin/env python3
"""Collect Qt licensing/attribution text as data; never execute downloaded code.

The source archives are cached only in the ignored build tree. Read ZIP members
in memory instead of extracting source paths. Notices include a conservative
superset of third-party code in the deployed Qt modules.
"""
import argparse
import hashlib
import json
import posixpath
import re
import urllib.request
import zipfile
from pathlib import Path

MODULES = ("qtbase", "qtdeclarative", "qtsvg", "qtimageformats", "qtshadertools")


def license_names(value):
    if isinstance(value, str):
        return [value] if value else []
    if isinstance(value, list):
        return [name for item in value for name in license_names(item)]
    return []


def collect(version: str, output: Path, cache: Path):
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ValueError("Qt version must be numeric")
    output.mkdir(parents=True, exist_ok=True)
    cache.mkdir(parents=True, exist_ok=True)
    sources = []
    notices = [f"Qt {version}: licensing and third-party attribution notices\n",
               "Qt is copyright The Qt Company Ltd. and other contributors.\n"
               "These dynamically linked libraries can be inspected/replaced using\n"
               "the portable EXE's --extract-to option. See the SPDX license texts\n"
               "below and the upstream source URLs for the corresponding sources.\n"
               "Notices intentionally include a superset (including build/test code);\n"
               "their inclusion does not assert that every component is shipped.\n"]
    for module in MODULES:
        archive = cache / f"{module}-{version}.zip"
        url = f"https://codeload.github.com/qt/{module}/zip/refs/tags/v{version}"
        if not archive.is_file():
            pending = archive.with_suffix(".download")
            try:
                with urllib.request.urlopen(url, timeout=120) as response, pending.open("wb") as target:
                    total = 0
                    while data := response.read(1024 * 1024):
                        total += len(data)
                        if total > 512 * 1024 * 1024:
                            raise ValueError("Qt source archive exceeds the safety limit")
                        target.write(data)
                pending.replace(archive)
            finally:
                pending.unlink(missing_ok=True)
        sources.append({"module": module, "version": version, "url": url,
                        "sha256": hashlib.sha256(archive.read_bytes()).hexdigest()})
        with zipfile.ZipFile(archive) as z:
            names = set(z.namelist())
            root = next(iter(z.infolist())).filename.split("/")[0]

            def text(name):
                info = z.getinfo(name)
                if info.file_size > 4 * 1024 * 1024:
                    raise ValueError(f"Oversized notice: {name}")
                data = z.read(name)
                try:
                    return data.decode("utf-8-sig")
                except UnicodeDecodeError:
                    return data.decode("latin1")

            notices.append(f"\n{'=' * 78}\n{module} {version}\nSource: {url}\n")
            # Full module license texts, not merely links to SPDX identifiers.
            for name in sorted(names):
                if name.startswith(root + "/LICENSES/") and not name.endswith("/"):
                    notices.append(f"\n--- {name.split('/', 1)[1]} ---\n{text(name)}\n")
            for name in sorted(names):
                if not name.endswith("/qt_attribution.json") or "/src/" not in name:
                    continue
                entries = json.loads(text(name), strict=False)
                if isinstance(entries, dict):
                    entries = [entries]
                for entry in entries:
                    notices.append("\n--- Third-party component ---\n" + json.dumps(entry, ensure_ascii=False, indent=2) + "\n")
                    for license_file in license_names(entry.get("LicenseFile")):
                        candidates = [posixpath.normpath(posixpath.join(posixpath.dirname(name), license_file)),
                                      posixpath.normpath(posixpath.join(root, license_file))]
                        match = next((candidate for candidate in candidates
                                      if candidate.startswith(root + "/") and candidate in names), None)
                        if not match:
                            raise ValueError(f"Missing license text: {name}: {license_file}")
                        notices.append(f"\n{license_file}:\n{text(match)}\n")
        print(f"Collected Qt notices: {module} {version}")
    (output / "QT-NOTICES.txt").write_text("\n".join(notices), encoding="utf-8")
    (output / "SOURCES.json").write_text(json.dumps(sources, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    args = parser.parse_args()
    collect(args.version, args.output, args.cache)
