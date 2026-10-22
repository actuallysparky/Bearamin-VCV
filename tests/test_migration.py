# SPDX-License-Identifier: GPL-3.0-or-later
import copy
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("migrate_patch", Path(__file__).parents[1] / "tools/migrate_patch.py")
migration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(migration)


class MigrationTests(unittest.TestCase):
    def fixture(self):
        return {"version": "2.6.6", "modules": [
            {"id": 4, "plugin": "SlowLFOPrototype", "model": "SlowLFO", "version": "2.0.6",
             "pos": [10, 0], "data": {"phase": 0.37, "periodSeconds": 86400, "chargeDurationSeconds": 25}},
            {"id": 9, "plugin": "BearaminVirtual", "model": "BearaminVirtual", "params": [{"id": 0, "value": 0.7}],
             "data": {"port": "saved-port", "rotation": 2, "mirrorX": True}},
            {"id": 12, "plugin": "Other", "model": "SlowLFO", "data": {"plugin": "SlowLFOPrototype"}}],
            "cables": [{"outputModuleId": 4, "outputId": 2, "inputModuleId": 12, "inputId": 0}]}

    def test_only_the_two_plugin_identifiers_change(self):
        original = self.fixture()
        revised, count = migration.migrate_json(json.dumps(original))
        expected = copy.deepcopy(original)
        expected["modules"][0]["plugin"] = expected["modules"][1]["plugin"] = "Bearamin"
        self.assertEqual(count, 2)
        self.assertEqual(json.loads(revised), expected)

    def test_module_preset(self):
        preset = self.fixture()["modules"][1]
        result, count = migration.migrate_json(json.dumps(preset))
        self.assertEqual(count, 1)
        expected = dict(preset, plugin="Bearamin")
        self.assertEqual(json.loads(result), expected)

    def test_original_and_existing_output_are_protected(self):
        with tempfile.TemporaryDirectory() as tmp:
            source, target = Path(tmp)/"old.vcv", Path(tmp)/"new.vcv"
            source.write_text(json.dumps(self.fixture()))
            before = source.read_bytes()
            self.assertEqual(migration.migrate_file(source, target), 2)
            self.assertEqual(source.read_bytes(), before)
            output = target.read_bytes()
            with self.assertRaises(FileExistsError): migration.migrate_file(source, source)
            with self.assertRaises(FileExistsError): migration.migrate_file(source, target)
            self.assertEqual(target.read_bytes(), output)

    def test_rack_archive_keeps_module_storage_and_members(self):
        raw = io.BytesIO()
        members = {"patch.json": json.dumps(self.fixture()).encode(),
                   "modules/9/state.bin": b"\x00\xffunchanged storage", "other.json": b'{"plugin":"Other"}'}
        with tarfile.open(fileobj=raw, mode="w") as archive:
            for name, data in members.items():
                info = tarfile.TarInfo(name); info.size = len(data); info.mtime = 123456; info.mode = 0o640
                archive.addfile(info, io.BytesIO(data))
        packed = subprocess.run(["zstd", "-q", "-c"], input=raw.getvalue(), capture_output=True, check=True).stdout
        revised, count = migration.convert(packed)
        self.assertEqual(count, 2)
        unpacked = subprocess.run(["zstd", "-q", "-d", "-c"], input=revised, capture_output=True, check=True).stdout
        with tarfile.open(fileobj=io.BytesIO(unpacked), mode="r:") as archive:
            self.assertEqual(archive.getnames(), list(members))
            for name, data in members.items():
                info = archive.getmember(name)
                self.assertEqual((info.mtime, info.mode), (123456, 0o640))
                actual = archive.extractfile(info).read()
                if name == "patch.json": self.assertEqual(json.loads(actual)["modules"][0]["plugin"], "Bearamin")
                else: self.assertEqual(actual, data)

    def test_unknown_or_malformed_files_leave_no_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            source, target = Path(tmp)/"old.vcv", Path(tmp)/"new.vcv"
            for payload in (b"broken", b"[]", b'{"modules":[]}'):
                source.write_bytes(payload)
                with self.assertRaises(ValueError): migration.migrate_file(source, target)
                self.assertFalse(target.exists())


if __name__ == "__main__":
    unittest.main()
