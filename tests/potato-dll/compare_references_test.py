"""Synthetic/offline tests: no network, original payload or DLL execution."""
from collections import Counter
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import Mock, patch

SPEC = importlib.util.spec_from_file_location("potato_reference_comparison", Path(__file__).with_name("compare_references.py"))
review = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(review)


def pe_data(marker=b"authored synthetic test bytes"):
    data = bytearray(256)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3c, 64)
    data[64:68] = b"PE\0\0"
    struct.pack_into("<H", data, 68, 0x8664)
    struct.pack_into("<H", data, 86, 0x2000)
    struct.pack_into("<H", data, 88, 0x20b)
    return bytes(data) + marker


class Info:
    def __init__(self, name, data=b"", directory=False, symlink=False, size=None):
        self.filename = name
        self.data = data
        self.file_size = len(data) if size is None else size
        self.directory = directory
        self.symlink = symlink

    def isdir(self):
        return self.directory

    def is_symlink(self):
        return self.symlink


class Archive:
    def __init__(self, infos):
        self.infos = infos
        self.reads = []
        self.password = None

    def __enter__(self):
        return self

    def __exit__(self, *args):
        pass

    def setpassword(self, password):
        self.password = password

    def infolist(self):
        return self.infos

    def read(self, info):
        self.reads.append(info.filename)
        return info.data


def inspected(rows):
    return {"identity": {"sha256": "synthetic-archive-only"},
            "expandedFileBytes": sum(len(data) for _, data in rows),
            "members": [{"name": name, "bytes": len(data), "sha256": review.sha(data), "data": data}
                        for name, data in rows]}


class ReferenceComparisonTests(unittest.TestCase):
    def inspect(self, infos, identity_change=None):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "authored-data.bin"
            path.write_bytes(b"synthetic archive placeholder, never decompressed")
            archive = Archive(infos)
            self.archive = archive
            self.factory = Mock(return_value=archive)
            identity = {"bytes": path.stat().st_size, "sha256": review.sha(path.read_bytes()), "entries": len(infos)}
            identity.update(identity_change or {})
            return review.inspect_archive(path, identity, "synthetic test password", self.factory)

    def test_renamed_repack_matches_full_bytes_and_duplicate_counts(self):
        a, b = pe_data(b"a"), pe_data(b"b")
        previous = inspected([("Intel/strong.dll", a), ("MSI4/strong.dll", a), ("MSI5/light.dll", b)])
        new = inspected([("renamed/intel.dll", a), ("renamed/msi.dll", b), ("renamed/duplicate.dll", a)])
        report = review.compare(previous, new)
        self.assertTrue(report["identicalDllContents"])
        self.assertEqual(report["dllFiles"], 3)
        self.assertEqual(report["uniqueDllByteSequences"], 2)
        self.assertEqual(report["members"][0]["previousMembers"], ["Intel/strong.dll", "MSI4/strong.dll"])

    def test_same_name_different_bytes_is_not_match(self):
        report = review.compare(inspected([("version.dll", pe_data(b"old"))]),
                                inspected([("version.dll", pe_data(b"new"))]))
        self.assertFalse(report["identicalDllContents"])
        self.assertFalse(report["allDllBytesPreviouslyReviewed"])

    def test_same_unique_set_with_missing_duplicate_is_not_identical(self):
        data = pe_data()
        report = review.compare(inspected([("a.dll", data), ("b.dll", data)]), inspected([("new.dll", data)]))
        self.assertTrue(report["sameUniqueDllSet"])
        self.assertFalse(report["sameDllMultiplicity"])
        self.assertFalse(report["identicalDllContents"])

    def test_dropped_distinct_variant_is_not_identical(self):
        a, b = pe_data(b"a"), pe_data(b"b")
        report = review.compare(inspected([("a.dll", a), ("b.dll", b)]), inspected([("new.dll", a)]))
        self.assertTrue(report["allDllBytesPreviouslyReviewed"])
        self.assertFalse(report["sameUniqueDllSet"])
        self.assertFalse(report["identicalDllContents"])

    def test_full_byte_comparison_not_just_hash_equality(self):
        previous, new = inspected([("old.dll", pe_data(b"a"))]), inspected([("new.dll", pe_data(b"b"))])
        new["members"][0]["sha256"] = previous["members"][0]["sha256"]  # Deliberately fake hash collision.
        report = review.compare(previous, new)
        self.assertTrue(report["sameUniqueDllSet"])
        self.assertFalse(report["identicalDllContents"])

    def test_report_is_json_metadata_not_payload(self):
        result = self.inspect([Info("root/", directory=True), Info("root/version.dll", pe_data())])
        report = review.compare(result, copy.deepcopy(result))
        text = json.dumps(report)
        self.assertNotIn("authored synthetic test bytes", text)
        self.assertNotIn('"data"', text)
        self.assertEqual(self.archive.reads, ["root/version.dll"])
        self.assertEqual(self.archive.password, "synthetic test password")

    def test_wrong_archive_size_or_hash_refused_before_parser(self):
        for changed in ({"bytes": 1}, {"sha256": "0" * 64}):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                self.inspect([Info("version.dll", pe_data())], changed)
            self.factory.assert_not_called()

    def test_symlink_archive_refused_before_parser(self):
        with patch.object(Path, "is_symlink", return_value=True), self.assertRaises(ValueError):
            self.inspect([Info("version.dll", pe_data())])
        self.factory.assert_not_called()

    def test_empty_password_refused_before_parser(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "data.bin"
            path.write_bytes(b"data")
            identity = {"bytes": 4, "sha256": hashlib.sha256(b"data").hexdigest(), "entries": 1}
            factory = Mock()
            with self.assertRaises(ValueError):
                review.inspect_archive(path, identity, "", factory)
            factory.assert_not_called()

    def test_wrong_or_excessive_entry_count(self):
        for infos, override in (([Info("a.dll", pe_data())], {"entries": 2}),
                                ([Info(str(i) + ".dll", pe_data()) for i in range(101)], {})):
            with self.subTest(entries=len(infos)), self.assertRaises(ValueError):
                self.inspect(infos, override)
            self.assertEqual(self.archive.reads, [])

    def test_member_and_total_expanded_bounds_before_decompression(self):
        for infos in ([Info("a.dll", size=-1)], [Info("a.dll", size=review.MAX_MEMBER_BYTES + 1)],
                      [Info(str(i) + ".dll", size=review.MAX_MEMBER_BYTES) for i in range(9)]):
            with self.subTest(entries=len(infos)), self.assertRaises(ValueError):
                self.inspect(infos)
            self.assertEqual(self.archive.reads, [])

    def test_all_paths_validated_before_any_member_is_read(self):
        for name in ("../escape.dll", "/absolute.dll", "C:\\bad.dll", "dir/../escape.dll", "\\\\server\\a.dll", "a\0.dll", "."):
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.inspect([Info("good.dll", pe_data()), Info(name, pe_data())])
            self.assertEqual(self.archive.reads, [])

    def test_member_symlinks_refused(self):
        with self.assertRaises(ValueError):
            self.inspect([Info("version.dll", pe_data(), symlink=True)])
        self.assertEqual(self.archive.reads, [])

    def test_case_and_separator_normalized_duplicates_refused(self):
        with self.assertRaises(ValueError):
            self.inspect([Info("ROOT\\VERSION.dll", pe_data()), Info("root/version.DLL", pe_data())])
        self.assertEqual(self.archive.reads, [])

    def test_directory_cannot_claim_payload(self):
        with self.assertRaises(ValueError):
            self.inspect([Info("folder/", directory=True, size=1)])
        self.assertEqual(self.archive.reads, [])

    def test_scripts_and_other_unexpected_files_not_executed_or_read(self):
        with self.assertRaises(ValueError):
            self.inspect([Info("version.dll", pe_data()), Info("install.ps1", b"never run")])
        self.assertEqual(self.archive.reads, [])

    def test_wrong_expanded_size_refused(self):
        with self.assertRaises(ValueError):
            self.inspect([Info("version.dll", pe_data(), size=10)])

    def test_invalid_pe_architecture_dll_flag_or_magic_refused(self):
        variants = [b"not a PE", bytes(100)]
        for offset, value in ((68, 0x14c), (86, 0), (88, 0x10b)):
            data = bytearray(pe_data())
            struct.pack_into("<H", data, offset, value)
            variants.append(bytes(data))
        data = bytearray(pe_data())
        struct.pack_into("<I", data, 0x3c, 0xffffffff)
        variants.append(bytes(data))
        for data in variants:
            with self.subTest(bytes=len(data)), self.assertRaises(ValueError):
                self.inspect([Info("version.dll", data)])

    def test_directory_only_cannot_be_vacuously_identical(self):
        with self.assertRaises(ValueError):
            self.inspect([Info("empty/", directory=True)])


class RecordedEvidenceTests(unittest.TestCase):
    def test_committed_report_matches_both_pinned_references_and_all_previous_variants(self):
        # Metadata only: no original archive/DLL is needed or fetched in CI.
        root = Path(__file__).resolve().parents[2]
        report = json.loads((root / "docs/evidence/potato-anshu-reference-comparison.json").read_text(encoding="utf-8"))
        prep_spec = importlib.util.spec_from_file_location("potato_previous_metadata", Path(__file__).with_name("prepare.py"))
        prep = importlib.util.module_from_spec(prep_spec)
        prep_spec.loader.exec_module(prep)
        self.assertEqual(report["previousArchive"], review.PREVIOUS)
        self.assertEqual(report["newArchive"], review.NEW)
        self.assertEqual(report["dllFiles"], 6)
        self.assertEqual(report["uniqueDllByteSequences"], 5)
        self.assertEqual(report["expandedFileBytes"], 1465344)
        self.assertTrue(report["identicalDllContents"])
        self.assertEqual(Counter(row[0] for row in prep.VARIANTS.values()),
                         Counter(row["sha256"] for row in report["members"]))
        for row in report["members"]:
            self.assertTrue(row["sameCompleteBytesAsPrevious"])
            self.assertEqual(set(row["previousMembers"]),
                             {prep.PREFIX + name for name, value in prep.VARIANTS.items() if value[0] == row["sha256"]})


if __name__ == "__main__":
    unittest.main()
