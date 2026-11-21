"""Hash-pinned DATA preparation only; never loads a supplied DLL.

The separate, explicitly opted-in Windows job is allowed to execute the
original DLLs only after a verified outbound block and process containment.
No original DLL/archive/Mesa binary is a Git or uploaded-artifact deliverable.
"""
import argparse
import hashlib
from html.parser import HTMLParser
import http.cookiejar
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import urllib.error
import urllib.parse
import urllib.request

STAGE = "arguments"
ARCHIVE_SHA256 = "80751bed1151e7f5bbcf186b51b8aada3b446af0278e5575384de7fa9eeb8b17"
DRIVE_FILE_ID = "1QGBvjTpW0mRNPwv2eqj77UEsLQtNDV7Y"
MESA_URL = "https://github.com/pal1000/mesa-dist-win/releases/download/26.2.7/mesa3d-26.2.7-release-mingw.7z"
MESA_SHA256 = "19c74aad7b09e682473b8de4c35b47e6498de97b71cae5511a231b8eb02f2b22"
MESA_BYTES = 55829223
PREFIX = "POTATO FF DLL 7FARFF/"
# IDs describe the exact uploaded bytes, not GPU/player compatibility.
VARIANTS = {
    "Intel/Bajo/dinput8.dll": ("25da83f0c7f5d6052e67e279cb427d20643593321405f017331ebd450e5fdcf8", "dinput", 1.5, False, 243712),
    "Intel/Fuerte/dinput8.dll": ("c956f2bd04b22212ecb8e65a0397c40addf64be136d8d1cd906e473661113554", "dinput", 3.0, False, 243712),
    "Nvidia  AMD/BS MSI 4/forte/dinput8.dll": ("c956f2bd04b22212ecb8e65a0397c40addf64be136d8d1cd906e473661113554", "dinput", 3.0, False, 243712),
    "Nvidia  AMD/BS MSI 4/leve/dinput8.dll": ("289e1eb34e815b886f4c2585c37efc78c489f58fd7b28942cfd5aeb1ec38b282", "dinput", 1.5, True, 243712),
    "Nvidia  AMD/BS MSI 5/forte/version.dll": ("7440db72c09af59e8a3249e17fff0a1e964e4409bc6ec7b9c1686b35368fac22", "version", 3.0, True, 245248),
    "Nvidia  AMD/BS MSI 5/leve/version.dll": ("b68e5b3586ffd2a57c680a0426818e71b06eadc3ce60b80507f42679892b3558", "version", 1.5, True, 245248),
}


def require(okay, message):
    if not okay:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def archive_inventory(path):
    require(path.is_file() and not path.is_symlink(), "Regular archive required")
    require(path.stat().st_size == 344442, "Different archive size")
    require(sha(path.read_bytes()) == ARCHIVE_SHA256, "Different original archive SHA-256")
    # Fixed-size/hash rejection needs stdlib only. Synthetic ordinary-CI tests
    # must not depend on a parser/decompressor or download the private payload.
    import rarfile
    with rarfile.RarFile(path) as archive:
        infos = archive.infolist()
        require(len(infos) == 17, "Different archive inventory")
        require(sum(i.file_size for i in infos) == 1465344, "Different expanded size")
        seen, files = set(), {}
        for info in infos:
            name = PurePosixPath(info.filename.replace("\\", "/"))
            require(not name.is_absolute() and ".." not in name.parts and ":" not in str(name)
                    and not info.is_symlink(), "Unsafe archive entry")
            key = str(name).casefold()
            require(key not in seen, "Duplicate archive entry")
            seen.add(key)
            if info.isdir():
                continue
            require(str(name).startswith(PREFIX), "Unexpected archive prefix")
            relative = str(name)[len(PREFIX):]
            require(relative in VARIANTS and info.file_size == VARIANTS[relative][4], "Unexpected DLL member")
            files[relative] = info.filename
        require(set(files) == set(VARIANTS), "Missing original DLL")
        return files


def dll_info(data, relative):
    import pefile
    expected, kind, lod, minimum, size = VARIANTS[relative]
    require(len(data) == size and sha(data) == expected, "Original DLL bytes differ")
    pe = pefile.PE(data=data)
    try:
        require(pe.FILE_HEADER.Machine == 0x8664 and pe.FILE_HEADER.Characteristics & 0x2000,
                "Original must be AMD64 DLL")
        anchor = "proxy_DirectInput8Create" if kind == "dinput" else "proxy_VerQueryValueW"
        exports = [s for s in pe.DIRECTORY_ENTRY_EXPORT.symbols if s.name == anchor.encode()]
        require(len(exports) == 1 and exports[0].address and not exports[0].forwarder,
                "Exact native import anchor missing")
        section = pe.get_section_by_rva(exports[0].address)
        require(section is not None and section.Characteristics & 0x20000000,
                "Import anchor must be executable code, not data")
        return {"sha256": expected, "proxyKind": kind, "expectedLod": lod,
                "forcesMinimumLod": minimum, "bytes": size, "anchor": anchor,
                "archiveMember": PREFIX + relative,
                "directDependencies": [i.dll.decode("ascii") for i in pe.DIRECTORY_ENTRY_IMPORT]}
    finally:
        pe.close()


class DriveDownloadForm(HTMLParser):
    """Read only Drive's fixed-file confirmation form, never page scripts."""
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.in_form = False
        self.action = None
        self.fields = {}

    def handle_starttag(self, tag, attributes):
        attrs = dict(attributes)
        if tag == "form":
            self.in_form = attrs.get("id") == "download-form"
            if self.in_form:
                require(self.action is None, "Multiple Drive download forms refused")
                self.action = attrs.get("action")
        elif tag == "input" and self.in_form and attrs.get("type") == "hidden":
            name, value = attrs.get("name"), attrs.get("value", "")
            require(name in {"id", "export", "confirm", "uuid", "at", "authuser"}
                    and len(value) <= 2048 and name not in self.fields,
                    "Unexpected Drive confirmation field")
            self.fields[name] = value

    def handle_endtag(self, tag):
        if tag == "form":
            self.in_form = False

    def url(self):
        require(self.action == "https://drive.usercontent.google.com/download",
                "Drive did not supply the expected public-file confirmation form")
        require(self.fields.get("id") == DRIVE_FILE_ID and self.fields.get("export") == "download",
                "Drive confirmation changed the original file identity")
        return self.action + "?" + urllib.parse.urlencode(self.fields)


def download_original(path):
    # Encrypted RAR/DLL archives may receive Drive's download-confirmation HTML
    # even at 344 kB. Retain its cookies and submit only its fixed-ID form.
    # Do not execute HTML/scripts, acquire credentials, alter sharing, accept an
    # alternate payload or continue if the exact archive hash does not match.
    opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
    url = "https://drive.google.com/uc?id=" + DRIVE_FILE_ID + "&export=download"
    visited = set()
    for attempt in range(3):
        require(url not in visited, "Repeated Drive confirmation refused")
        visited.add(url)
        request = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 BSI-owned-WGL-probe"})
        with opener.open(request, timeout=60) as response:
            content = response.read(344443)
            is_html = "text/html" in response.headers.get("Content-Type", "").lower()
        if len(content) == 344442 and sha(content) == ARCHIVE_SHA256:
            with path.open("xb") as file:
                file.write(content)
            return
        print("::notice title=Potato original download::" + json.dumps({
            "attempt": attempt + 1, "responseBytesAtMost": len(content), "htmlResponse": is_html,
            "exactOriginalHash": False}), flush=True)
        require(is_html and len(content) <= 344442, "Downloaded data is not the exact reviewed original")
        form = DriveDownloadForm()
        form.feed(content.decode("utf-8", errors="strict"))
        url = form.url()
    raise ValueError("Drive confirmation did not yield the exact reviewed original")


def download(url, path, expected_size, digest):
    # Only fixed reviewed URLs are used by main; streaming cap also refuses an
    # error page/oversize response before an archive is ever parsed/executed.
    with urllib.request.urlopen(url, timeout=60) as response, path.open("xb") as file:
        total = 0
        while True:
            chunk = response.read(1024 * 1024)
            if not chunk:
                break
            total += len(chunk)
            require(total <= expected_size, "Oversize download refused")
            file.write(chunk)
    require(path.stat().st_size == expected_size and sha(path.read_bytes()) == digest,
            "Downloaded data is not the exact reviewed file")


def prepare(archive, mesa, work, sevenzip):
    global STAGE
    STAGE = "archive-inventory"
    files = archive_inventory(archive)
    require(mesa.is_file() and mesa.stat().st_size == MESA_BYTES and sha(mesa.read_bytes()) == MESA_SHA256,
            "Different Mesa software-driver package")
    members = work / "members"
    members.mkdir(exist_ok=False)
    unique = {}
    STAGE = "original-member-data-extraction"
    for relative, member in files.items():
        result = subprocess.run([sevenzip, "x", "-so", "-p70K", str(archive), member],
                                capture_output=True, check=True, timeout=30)
        info = dll_info(result.stdout, relative)
        if info["sha256"] not in unique:
            # Data-only name until the offline runner stages the actual DLL.
            (members / (info["sha256"] + ".bin")).write_bytes(result.stdout)
            unique[info["sha256"]] = info
    STAGE = "mesa-x64-data-extraction"
    mesa_dir = work / "mesa"
    mesa_dir.mkdir(exist_ok=False)
    # Trusted package pinned above, never its per-app deploy/system scripts.
    subprocess.run([sevenzip, "x", "-y", "-o" + str(mesa_dir), str(mesa), "x64/*"],
                   check=True, timeout=90, stdout=subprocess.DEVNULL)
    candidates = list(mesa_dir.rglob("opengl32.dll"))
    print("::notice title=Pinned Mesa WGL data::" + json.dumps({
        "openglProviderCandidates": len(candidates), "x64DirectoryPresent": (mesa_dir / "x64").is_dir(),
        "extractedFileCount": sum(p.is_file() for p in mesa_dir.rglob("*"))}), flush=True)
    require(len(candidates) == 1 and "x64" in candidates[0].parts, "Missing unique x64 Mesa WGL provider")
    provider = candidates[0].parent
    dlls = [{"name": p.name, "sha256": sha(p.read_bytes())} for p in sorted(provider.glob("*.dll"))]
    require(any(d["name"].lower() == "opengl32.dll" for d in dlls), "Mesa provider inventory")
    manifest = {"archiveSha256": ARCHIVE_SHA256, "mesaArchiveSha256": MESA_SHA256,
                "mesaProviderDirectory": str(provider.resolve()), "mesaDlls": dlls,
                "variants": list(unique.values()), "execution": "none; data preparation only"}
    (work / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print("PASS: exact original archive, six members/five unique AMD64 DLLs, native anchors and pinned Mesa bytes prepared as data only")


def main():
    global STAGE
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--mesa", type=Path)
    parser.add_argument("--download", action="store_true", help="Fetch only the two fixed, hash-pinned reference files")
    parser.add_argument("--sevenzip", default=shutil.which("7z") or "7z")
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=False)
    archive, mesa = args.archive, args.mesa
    if args.download:
        require(archive is None and mesa is None, "No alternate data URLs/paths in download mode")
        archive, mesa = args.work / "original.rar", args.work / "mesa.7z"
        # The user's existing public file link, no Drive credentials, permission
        # changes or re-upload. Failure to fetch/verify stops before execution.
        # Exact webContentLink re-read from the connected Drive metadata.
        STAGE = "original-archive-download"
        download_original(archive)
        STAGE = "mesa-archive-download"
        download(MESA_URL, mesa, MESA_BYTES, MESA_SHA256)
    require(archive is not None and mesa is not None, "Both exact archives are required")
    prepare(archive.resolve(), mesa.resolve(), args.work.resolve(), args.sevenzip)


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        # Make a small non-sensitive diagnostic available through GitHub's
        # check-annotation API too; do not require fetching private raw logs or
        # disclose signed redirect URLs, paths or response bodies.
        detail = type(error).__name__
        if isinstance(error, urllib.error.HTTPError):
            detail += " HTTP " + str(error.code)
        elif isinstance(error, subprocess.CalledProcessError):
            detail += " tool exit " + str(error.returncode)
        elif isinstance(error, ValueError):
            detail += ": " + str(error)
        elif isinstance(error, OSError):
            detail += " OS code " + str(error.errno)
        detail = detail.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
        print("::error title=Potato data preparation::" + STAGE + ": " + detail, flush=True)
        raise SystemExit(1)
