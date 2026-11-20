"""Offline integrity/transport tests. Fake EXE bytes are never executed."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch
from urllib.error import HTTPError
import zipfile

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/Verify-ArtifactDownload.py"
spec = importlib.util.spec_from_file_location("artifact_verifier", SCRIPT)
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)


def sha(data):
    return hashlib.sha256(data).hexdigest()


class Response(io.BytesIO):
    def __init__(self, data, length=None):
        super().__init__(data)
        self.headers = {} if length is None else {"Content-Length": str(length)}


class ArchiveTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="bsi-artifact-integrity-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "BSI-Customizer-test-win64.exe"
        self.data = b"MZ" + bytes(range(256)) * 100
        self.source.write_bytes(self.data)
        self.archive = self.root / "download.zip"

    def zipped(self, entries=None, method=zipfile.ZIP_DEFLATED):
        with zipfile.ZipFile(self.archive, "w", compression=method) as out:
            for name, data in entries or [(self.source.name, self.data)]:
                out.writestr(name, data)
        return self.archive.read_bytes()

    def verify(self):
        return verifier.verify_archive(self.archive, self.source, sha(self.archive.read_bytes()), sha(self.data))

    def test_deflated_and_stored_single_exe(self):
        for method in (zipfile.ZIP_DEFLATED, zipfile.ZIP_STORED):
            with self.subTest(method=method):
                self.zipped(method=method)
                result = self.verify()
                self.assertTrue(result["verified"])
                self.assertEqual(result["fileBytes"], len(self.data))
                self.assertEqual(result["fileSha256"], sha(self.data))
                self.assertEqual(result["archiveSha256"], sha(self.archive.read_bytes()))

    def test_download_archive_hash_mismatch(self):
        self.zipped()
        with self.assertRaisesRegex(verifier.VerificationError, "uploaded artifact digest"):
            verifier.verify_archive(self.archive, self.source, "0" * 64, sha(self.data))

    def test_changed_build_source_is_refused(self):
        self.zipped()
        self.source.write_bytes(self.data + b"changed")
        with self.assertRaisesRegex(verifier.VerificationError, "source EXE changed"):
            self.verify()

    def test_html_download_is_not_called_corrupt_exe(self):
        self.archive.write_bytes(b"<!doctype html><title>Sign in to GitHub</title>")
        with self.assertRaisesRegex(verifier.VerificationError, "HTML/error page"):
            self.verify()

    def test_truncated_zip_with_matching_download_hash(self):
        self.archive.write_bytes(self.zipped()[:-15])
        with self.assertRaisesRegex(verifier.VerificationError, "ZIP structure"):
            self.verify()

    def test_crc_corruption_is_detected_even_with_matching_outer_hash(self):
        data = bytearray(self.zipped(method=zipfile.ZIP_STORED))
        # Flip a stored member byte, leaving its central-directory CRC intact.
        data[30 + len(self.source.name) + 20] ^= 1
        self.archive.write_bytes(data)
        with self.assertRaisesRegex(verifier.VerificationError, "CRC"):
            self.verify()

    def test_wrong_single_member_contents_are_detected_by_sha256(self):
        self.zipped([(self.source.name, b"MZ" + b"x" * (len(self.data) - 2))])
        with self.assertRaisesRegex(verifier.VerificationError, "Extracted EXE SHA-256"):
            self.verify()

    def test_size_mismatch_is_refused_before_expansion(self):
        self.zipped([(self.source.name, self.data * 2)])
        with self.assertRaisesRegex(verifier.VerificationError, "member size"):
            self.verify()

    def test_wrong_names_folders_extra_entries_and_duplicates_refuse(self):
        cases = (
            [("wrong.exe", self.data)],
            [("../" + self.source.name, self.data)],
            [("nested/" + self.source.name, self.data)],
            [(self.source.name, self.data), ("extra.txt", b"unrelated")],
            [("folder/", b"")],
        )
        for entries in cases:
            with self.subTest(names=[name for name, _ in entries]):
                self.zipped(entries)
                with self.assertRaisesRegex(verifier.VerificationError, "exactly the expected EXE"):
                    self.verify()
        # Two differently named entries also exercise the duplicate-file-count
        # gate without relying on ZipFile's intentional duplicate-name warning.
        self.zipped([(self.source.name, self.data), (self.source.name.upper(), self.data)])
        with self.assertRaises(verifier.VerificationError):
            self.verify()

    def test_symlink_and_uncommon_compression_are_refused(self):
        link = zipfile.ZipInfo(self.source.name)
        link.create_system = 3
        link.external_attr = (stat.S_IFLNK | 0o777) << 16
        self.zipped([(link, self.data)])
        with self.assertRaisesRegex(verifier.VerificationError, "symlink"):
            self.verify()
        self.zipped(method=zipfile.ZIP_BZIP2)
        with self.assertRaisesRegex(verifier.VerificationError, "unsupported ZIP"):
            self.verify()

    def test_empty_nonexe_and_size_limits(self):
        self.zipped()
        self.source.write_bytes(b"")
        with self.assertRaisesRegex(verifier.VerificationError, "Empty or oversized"):
            self.verify()
        self.source.write_bytes(b"<html>not an EXE</html>")
        with self.assertRaisesRegex(verifier.VerificationError, "not an EXE"):
            self.verify()
        self.source.write_bytes(self.data)
        with patch.object(verifier, "MAX_BYTES", 10):
            with self.assertRaisesRegex(verifier.VerificationError, "oversized"):
                self.verify()

    def test_digest_format_and_prefix(self):
        self.assertEqual(verifier.digest("sha256:" + "A" * 64), "a" * 64)
        for value in ("", "0" * 63, "0" * 65, "z" * 64):
            with self.assertRaises(verifier.VerificationError):
                verifier.digest(value)

    def test_optimized_cli_still_verifies_and_records_json(self):
        self.zipped()
        report = self.root / "verified.json"
        args = [sys.executable, "-O", "-S", str(SCRIPT), "--archive", str(self.archive),
                "--source", str(self.source), "--archive-sha256", sha(self.archive.read_bytes()),
                "--source-sha256", sha(self.data), "--report", str(report)]
        result = subprocess.run(args, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(json.loads(report.read_text())["verified"])
        original = report.read_bytes()
        result = subprocess.run(args, capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("overwrite", result.stderr)
        self.assertEqual(report.read_bytes(), original)
        report.unlink()
        self.archive.write_bytes(self.archive.read_bytes()[:-10])
        result = subprocess.run(args, capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(report.exists())


class DownloadTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="bsi-download-test-")
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "download.zip"

    def openers(self, body=b"ZIP fixture", length=None, location="https://artifact-storage.example/zip?signature=secret"):
        api, storage = Mock(), Mock()
        api.open.side_effect = HTTPError("https://api.github.com/test", 302, "Found", {"Location": location}, io.BytesIO())
        storage.open.return_value = Response(body, length)
        return api, storage

    def test_token_only_reaches_github_not_signed_storage(self):
        api, storage = self.openers(length=len(b"ZIP fixture"))
        with patch.object(verifier, "build_opener", side_effect=[api, storage]):
            verifier.download_artifact("owner/repo", "123", "private-test-token", self.path)
        self.assertEqual(self.path.read_bytes(), b"ZIP fixture")
        self.assertEqual(api.open.call_args.args[0].get_header("Authorization"), "Bearer private-test-token")
        self.assertIsNone(storage.open.call_args.args[0].get_header("Authorization"))

    def test_insecure_redirect_is_refused_before_contacting_storage(self):
        for location in ("http://storage.example/file", "https://user:password@storage.example/file", "", "https://storage.example:123/file"):
            with self.subTest(location=location):
                api, storage = self.openers(location=location)
                with patch.object(verifier, "build_opener", side_effect=[api, storage]):
                    with self.assertRaisesRegex(verifier.VerificationError, "unsafe download redirect"):
                        verifier.download_artifact("owner/repo", "123", "token", self.path)
                self.assertFalse(storage.open.called)
                self.assertFalse(self.path.exists())

    def test_truncated_empty_and_oversized_transfers_remove_partial_files(self):
        for body, length in ((b"short", 100), (b"", 0), (b"payload", "invalid")):
            with self.subTest(body=body, length=length):
                api, storage = self.openers(body=body, length=length)
                with patch.object(verifier, "build_opener", side_effect=[api, storage]):
                    with self.assertRaises(verifier.VerificationError):
                        verifier.download_artifact("owner/repo", "123", "token", self.path)
                self.assertFalse(self.path.exists())
        api, storage = self.openers(body=b"unbounded response")
        with patch.object(verifier, "build_opener", side_effect=[api, storage]), patch.object(verifier, "MAX_BYTES", 5):
            with self.assertRaisesRegex(verifier.VerificationError, "size limit"):
                verifier.download_artifact("owner/repo", "123", "token", self.path)
        self.assertFalse(self.path.exists())

    def test_existing_download_is_not_overwritten(self):
        self.path.write_bytes(b"keep existing download")
        with patch.object(verifier, "build_opener") as opener:
            with self.assertRaisesRegex(verifier.VerificationError, "overwrite"):
                verifier.download_artifact("owner/repo", "123", "token", self.path)
        opener.assert_not_called()
        self.assertEqual(self.path.read_bytes(), b"keep existing download")

    def test_invalid_ids_repositories_and_missing_token_never_access_network(self):
        for repo, artifact_id, token in (("invalid", "123", "token"), ("owner/repo", "../zip", "token"),
                                         ("owner/repo", "0", "token"), ("owner/repo", "123", "")):
            with patch.object(verifier, "build_opener") as opener:
                with self.assertRaises(verifier.VerificationError):
                    verifier.download_artifact(repo, artifact_id, token, self.path)
            opener.assert_not_called()

    def test_api_errors_do_not_create_downloads(self):
        api = Mock()
        api.open.side_effect = HTTPError("https://api.github.com/test", 403, "Forbidden", {}, io.BytesIO())
        with patch.object(verifier, "build_opener", return_value=api):
            with self.assertRaisesRegex(verifier.VerificationError, "HTTP 403"):
                verifier.download_artifact("owner/repo", "123", "token", self.path)
        self.assertFalse(self.path.exists())


if __name__ == "__main__":
    unittest.main()
