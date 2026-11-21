#!/usr/bin/env python3
"""Download an Actions artifact ZIP and verify the exact, single EXE inside it.

CI/developer tool only; never executes an archive member. Auth is sent only to
api.github.com, not to its signed storage redirect. Local verification needs no
network, token, third-party module or Windows runtime.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener
import zipfile
import zlib

MAX_BYTES = 512 * 1024 * 1024
CHUNK_BYTES = 1024 * 1024


class VerificationError(Exception):
    pass


def digest(value):
    value = value.removeprefix("sha256:").lower()
    if not re.fullmatch(r"[0-9a-f]{64}", value):
        raise VerificationError("Expected a complete SHA-256 digest")
    return value


def file_digest(path):
    result = hashlib.sha256()
    size = 0
    with Path(path).open("rb") as source:
        while block := source.read(CHUNK_BYTES):
            size += len(block)
            if size > MAX_BYTES:
                raise VerificationError("File exceeds the verification size limit")
            result.update(block)
    return result.hexdigest()


def verify_archive(archive, source, archive_sha256, source_sha256):
    archive, source = Path(archive), Path(source)
    archive_sha256, source_sha256 = digest(archive_sha256), digest(source_sha256)
    source_size, archive_size = source.stat().st_size, archive.stat().st_size
    if not 0 < source_size <= MAX_BYTES or not 0 < archive_size <= MAX_BYTES:
        raise VerificationError("Empty or oversized EXE/download")
    with source.open("rb") as stream:
        if source.suffix.lower() != ".exe" or stream.read(2) != b"MZ":
            raise VerificationError("The expected source is not an EXE")
    if file_digest(source) != source_sha256:
        raise VerificationError("The source EXE changed after its recorded build hash")
    with archive.open("rb") as stream:
        if stream.read(4) != b"PK\x03\x04":
            raise VerificationError("Download is not a normal ZIP (possibly an HTML/error page)")
    if file_digest(archive) != archive_sha256:
        raise VerificationError("Downloaded ZIP SHA-256 differs from the uploaded artifact digest")
    try:
        with zipfile.ZipFile(archive) as zipped:
            entries = zipped.infolist()
            if len(entries) != 1 or entries[0].filename != source.name or entries[0].is_dir():
                raise VerificationError("ZIP must contain exactly the expected EXE, with no folders or extra entries")
            entry = entries[0]
            if entry.flag_bits & 1 or entry.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
                raise VerificationError("Encrypted or unsupported ZIP encoding")
            if stat.S_ISLNK(entry.external_attr >> 16):
                raise VerificationError("ZIP member is a symlink, not the expected EXE")
            if entry.file_size != source_size:
                raise VerificationError("ZIP member size differs from the tested EXE")
            actual, count = hashlib.sha256(), 0
            with zipped.open(entry) as member:
                while block := member.read(CHUNK_BYTES):
                    count += len(block)
                    if count > source_size:
                        raise VerificationError("ZIP expanded beyond the expected EXE size")
                    actual.update(block)
            # Reading to EOF also checks the member's ZIP CRC. A same-size wrong
            # EXE still fails SHA-256; a corrupt CRC must not be called success.
            if count != source_size or actual.hexdigest() != source_sha256:
                raise VerificationError("Extracted EXE SHA-256 differs from the tested build")
    except (zipfile.BadZipFile, zlib.error, EOFError, NotImplementedError, RuntimeError) as error:
        raise VerificationError("ZIP structure, decompression or CRC verification failed") from error
    return {
        "archive": archive.name, "archiveBytes": archive_size, "archiveSha256": archive_sha256,
        "file": source.name, "fileBytes": source_size, "fileSha256": source_sha256,
        "verified": True,
    }


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


class HttpsRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        secure_url(newurl)
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def secure_url(url):
    parsed = urlsplit(url)
    if parsed.scheme != "https" or not parsed.hostname or parsed.username or parsed.password or parsed.port not in (None, 443):
        raise VerificationError("Artifact service returned an unsafe download redirect")


def download_artifact(repository, artifact_id, token, destination):
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository or ""):
        raise VerificationError("Expected an explicit owner/repository")
    if not re.fullmatch(r"[1-9][0-9]{0,19}", str(artifact_id)) or not token:
        raise VerificationError("Expected an artifact ID and CI GitHub token")
    destination = Path(destination)
    if destination.exists() or destination.is_symlink():
        raise VerificationError("Refusing to overwrite an existing artifact download")
    request = Request(f"https://api.github.com/repos/{repository}/actions/artifacts/{artifact_id}/zip", headers={
        "Authorization": f"Bearer {token}", "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28", "User-Agent": "BSI-artifact-integrity-check",
    })
    try:
        with build_opener(NoRedirect()).open(request, timeout=60):
            raise VerificationError("Artifact API did not return a signed download redirect")
    except HTTPError as error:
        if error.code != 302:
            error.close()
            raise VerificationError(f"Artifact API returned HTTP {error.code}") from None
        location = error.headers.get("Location", "")
        error.close()
    secure_url(location)
    # Deliberately a NEW request: never forward the GitHub token to storage.
    request = Request(location, headers={"User-Agent": "BSI-artifact-integrity-check"})
    owned = False
    try:
        with build_opener(HttpsRedirect()).open(request, timeout=60) as response:
            expected_size = response.headers.get("Content-Length")
            if expected_size is not None and (not expected_size.isdigit() or not 0 < int(expected_size) <= MAX_BYTES):
                raise VerificationError("Download has an invalid or oversized Content-Length")
            destination.parent.mkdir(parents=True, exist_ok=True)
            with destination.open("xb") as target:
                owned = True
                count = 0
                while block := response.read(CHUNK_BYTES):
                    count += len(block)
                    if count > MAX_BYTES:
                        raise VerificationError("Artifact download exceeds the size limit")
                    target.write(block)
            if count == 0 or (expected_size is not None and count != int(expected_size)):
                raise VerificationError("Artifact download was empty or truncated")
    except BaseException:
        if owned:
            destination.unlink(missing_ok=True)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--archive-sha256", required=True)
    parser.add_argument("--source-sha256", required=True)
    parser.add_argument("--artifact-id")
    parser.add_argument("--repository", default=os.environ.get("GITHUB_REPOSITORY"))
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    try:
        # Validate digests before making any network request.
        digest(args.archive_sha256)
        digest(args.source_sha256)
        if args.report and (args.report.exists() or args.report.is_symlink()):
            raise VerificationError("Refusing to overwrite an existing verification report")
        if args.artifact_id:
            download_artifact(args.repository, args.artifact_id,
                              os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN"), args.archive)
        result = verify_archive(args.archive, args.source, args.archive_sha256, args.source_sha256)
        result["artifactId"] = args.artifact_id
        if args.report:
            with args.report.open("x", encoding="utf-8") as report:
                json.dump(result, report, indent=2)
        print(json.dumps(result, indent=2))
        print(f"PASS: downloaded ZIP structure/CRC and exact single EXE size/SHA-256: {result['file']}")
        if os.environ.get("GITHUB_ACTIONS"):
            message = f"{result['file']} | ZIP bytes={result['archiveBytes']} | ZIP SHA256={result['archiveSha256']} | EXE SHA256={result['fileSha256']}"
            print("::notice title=Verified artifact download::" + message.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A"))
        return 0
    except (VerificationError, OSError, ValueError, URLError) as error:
        # Transport exceptions can carry a signed storage URL. Never print it
        # (nor an Authorization header/token) into Actions logs or annotations.
        message = str(error) if isinstance(error, VerificationError) else type(error).__name__
        print(f"Artifact verification FAILED: {message}", file=sys.stderr)
        if os.environ.get("GITHUB_ACTIONS"):
            print("::error title=Artifact integrity check::" + message.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A"))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
