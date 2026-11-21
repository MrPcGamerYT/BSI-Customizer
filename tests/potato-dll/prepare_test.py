"""Non-executing tests: synthetic bytes/forms, no network or supplied code."""
from contextlib import redirect_stdout
import hashlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("potato_prepare", Path(__file__).with_name("prepare.py"))
prep = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(prep)


def form(file_id=prep.DRIVE_FILE_ID, action="https://drive.usercontent.google.com/download"):
    return ('<html><form id="download-form" action="' + action + '">'
            '<input type="hidden" name="id" value="' + file_id + '">'
            '<input type="hidden" name="export" value="download">'
            '<input type="hidden" name="confirm" value="t">'
            '<input type="hidden" name="uuid" value="synthetic-test-value">'
            '</form></html>')


class Response(io.BytesIO):
    def __init__(self, content, content_type):
        super().__init__(content)
        self.headers = {"Content-Type": content_type}


class Opener:
    def __init__(self, responses):
        self.responses = iter(responses)
        self.requests = []

    def open(self, request, timeout):
        self.requests.append(request.full_url)
        return next(self.responses)


class PreparationTests(unittest.TestCase):
    def test_fixed_original_id_and_confirmation_host(self):
        parser = prep.DriveDownloadForm()
        parser.feed(form())
        self.assertTrue(parser.url().startswith("https://drive.usercontent.google.com/download?"))
        self.assertIn("id=" + prep.DRIVE_FILE_ID, parser.url())
        self.assertIn("uuid=synthetic-test-value", parser.url())

    def test_wrong_id_host_or_page_cannot_supply_data(self):
        for html in (form(file_id="unrelated"), form(action="https://example.invalid/download"),
                     '<html>Permission required</html>'):
            parser = prep.DriveDownloadForm()
            parser.feed(html)
            with self.assertRaises(ValueError):
                parser.url()

    def test_unknown_or_duplicate_hidden_field_refused(self):
        for field in ('name="destination" value="elsewhere"',
                      'name="id" value="' + prep.DRIVE_FILE_ID + '"'):
            html = form().replace('</form>', '<input type="hidden" ' + field + '></form>')
            with self.assertRaises(ValueError):
                prep.DriveDownloadForm().feed(html)

    def test_confirmation_then_exact_synthetic_hash(self):
        # A synthetic byte sequence, NOT a DLL/archive and never parsed as one.
        content = b"synthetic test data" + bytes(344442 - len(b"synthetic test data"))
        opener = Opener([Response(form().encode(), "text/html"), Response(content, "application/octet-stream")])
        with tempfile.TemporaryDirectory() as temp:
            target = Path(temp) / "data.bin"
            with patch.object(prep.urllib.request, "build_opener", return_value=opener), \
                    patch.object(prep, "ARCHIVE_SHA256", hashlib.sha256(content).hexdigest()), \
                    redirect_stdout(io.StringIO()):
                prep.download_original(target)
            self.assertEqual(target.read_bytes(), content)
            self.assertEqual(len(opener.requests), 2)
            self.assertIn("drive.usercontent.google.com/download?", opener.requests[1])

    def test_no_write_on_wrong_bytes_or_permission_page(self):
        for body, kind in ((b"not the original", "application/octet-stream"),
                           (b"<html>Permission required</html>", "text/html")):
            opener = Opener([Response(body, kind)])
            with tempfile.TemporaryDirectory() as temp:
                target = Path(temp) / "never-written.bin"
                with patch.object(prep.urllib.request, "build_opener", return_value=opener), redirect_stdout(io.StringIO()):
                    with self.assertRaises(ValueError):
                        prep.download_original(target)
                self.assertFalse(target.exists())

    def test_local_wrong_archive_refused_before_parsing(self):
        with tempfile.TemporaryDirectory() as temp:
            target = Path(temp) / "wrong.rar"
            target.write_bytes(b"not a RAR archive")
            with self.assertRaises(ValueError):
                prep.archive_inventory(target)


if __name__ == "__main__":
    unittest.main()
