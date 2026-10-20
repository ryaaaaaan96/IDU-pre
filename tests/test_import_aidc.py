import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock


spec = importlib.util.spec_from_file_location(
    "import_aidc", Path(__file__).resolve().parents[1] / "tools/import_aidc.py")
importer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(importer)


def region(name, content="old rows\n"):
    return (f"/* BEGIN IMPORTED TABLE: {name} */\n"
            f"{content}/* END IMPORTED TABLE: {name} */\n")


def snapshot(directory):
    return {str(path.relative_to(directory)): path.read_bytes()
            for path in directory.rglob("*") if path.is_file()}


class ImportTablesTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.templates = self.root / "templates"
        self.templates.mkdir()
        self.output = self.root / "output"
        self.contents = {
            "tables/defaults.c": "manual defaults\n" + region("defaults"),
            "tables/process.c": "manual points before\n" + region("points") + "manual points after\n",
            "tables/transmit.c": "manual mapping\n" + region("transmit"),
            "tables/collect.c": region("fields") + "manual polls\n" + region("collect"),
            "device.c": "manual device\n" + region("devices"),
        }
        for filename, content in self.contents.items():
            target = self.templates / filename
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content)
        self.rows = {name: f"new {name} rows\n" for name in importer.TABLE_FILES}
        self.symbols = "#define EXAMPLE_SIGNAL 42\n"
        self.metadata = {"source_root": "/reference", "point_count": 7}

    def write(self, report_path=None):
        importer.write_tables(self.output, self.rows, self.symbols, self.metadata,
                              template_dir=self.templates, report_path=report_path)

    def test_empty_destination_uses_templates_and_updates_multiple_regions(self):
        self.write()
        expected = (region("fields", self.rows["fields"]) + "manual polls\n" +
                    region("collect", self.rows["collect"]))
        self.assertEqual((self.output / "tables/collect.c").read_text(), expected)
        self.assertEqual((self.output / "inc/imported_signals.h").read_text(), self.symbols)
        self.assertEqual(json.loads((self.output / "migration.json").read_text()),
                         self.metadata)
        self.assertFalse((self.output / "generated").exists())
        self.assertEqual(list(self.output.rglob("*.inc")), [])
        for filename, content in self.contents.items():
            self.assertEqual((self.templates / filename).read_text(), content)

    def test_existing_manual_content_is_preserved_and_reimport_is_stable(self):
        (self.output / "tables").mkdir(parents=True)
        manual = ("// custom CRLF prefix\r\n" + region("points") +
                  "// custom CRLF suffix\r\n")
        (self.output / "tables/process.c").write_bytes(manual.encode())
        self.write()
        expected = manual.replace("old rows\n", self.rows["points"])
        self.assertEqual((self.output / "tables/process.c").read_bytes(), expected.encode())
        before = snapshot(self.output)
        self.write()
        self.assertEqual(snapshot(self.output), before)

    def test_missing_marker_in_last_destination_does_not_write_any_files(self):
        self.write()
        target = self.output / "device.c"
        target.write_text(target.read_text().replace("/* END IMPORTED TABLE: devices */", ""))
        before = snapshot(self.output)
        self.rows = {name: "changed rows\n" for name in importer.TABLE_FILES}
        with self.assertRaisesRegex(ValueError, "device.c.*devices"):
            self.write()
        self.assertEqual(snapshot(self.output), before)

    def test_duplicate_or_reversed_markers_are_rejected(self):
        invalid = [
            region("devices") + region("devices"),
            "/* END IMPORTED TABLE: devices */\n/* BEGIN IMPORTED TABLE: devices */\n",
            region("devices").replace("/* END IMPORTED TABLE: devices */",
                                      "/* BEGIN IMPORTED TABLE: devices */"),
        ]
        for content in invalid:
            with self.subTest(content=content):
                (self.templates / "device.c").write_text(content)
                with self.assertRaises(ValueError):
                    self.write()
                self.assertFalse(self.output.exists())

    def test_overlapping_regions_are_rejected(self):
        for end_order in (("fields", "collect"), ("collect", "fields")):
            with self.subTest(end_order=end_order):
                content = "/* BEGIN IMPORTED TABLE: fields */\n/* BEGIN IMPORTED TABLE: collect */\n"
                content += "".join(f"/* END IMPORTED TABLE: {name} */\n" for name in end_order)
                (self.templates / "tables/collect.c").write_text(content)
                with self.assertRaisesRegex(ValueError, "overlap"):
                    self.write()
                self.assertFalse(self.output.exists())

    def test_missing_template_does_not_create_partial_destination(self):
        (self.templates / "device.c").unlink()
        with self.assertRaises(FileNotFoundError):
            self.write()
        self.assertFalse(self.output.exists())

    def test_repository_output_writes_report_to_project_docs(self):
        project = self.root / "project"
        self.output = project / "app/cfg"
        with mock.patch.object(importer, "CONFIG_DIR", self.output):
            self.write()
        report = project / "docs/point-table-import.json"
        self.assertEqual(json.loads(report.read_text()), self.metadata)
        self.assertFalse((self.output / "migration.json").exists())
        self.assertFalse((self.output / "generated").exists())

    def test_custom_report_overrides_repository_default(self):
        project = self.root / "project"
        self.output = project / "app/cfg"
        report = self.root / "custom/report.json"
        with mock.patch.object(importer, "CONFIG_DIR", self.output):
            self.write(report_path=report)
        self.assertEqual(json.loads(report.read_text()), self.metadata)
        self.assertFalse((project / "docs/point-table-import.json").exists())
        self.assertFalse((self.output / "migration.json").exists())

    def test_invalid_marker_preserves_existing_custom_report(self):
        report = self.root / "custom/report.json"
        self.write(report_path=report)
        target = self.output / "device.c"
        target.write_text(target.read_text().replace("/* END IMPORTED TABLE: devices */", ""))
        before = snapshot(self.root)
        self.metadata = {"point_count": 99}
        with self.assertRaisesRegex(ValueError, "device.c.*devices"):
            self.write(report_path=report)
        self.assertEqual(snapshot(self.root), before)

    def test_report_cannot_replace_a_generated_table_or_symbol_header(self):
        before = snapshot(self.root)
        for relative in ("tables/process.c", "tables/../tables/process.c",
                         "inc/imported_signals.h"):
            with self.subTest(relative=relative):
                with self.assertRaisesRegex(ValueError, "Report path conflicts with table output"):
                    self.write(report_path=self.output / relative)
                self.assertEqual(snapshot(self.root), before)
                self.assertFalse(self.output.exists())


class ReferenceSnapshotTest(unittest.TestCase):
    def test_snapshot_renders_current_tables_with_direct_callbacks(self):
        project = Path(__file__).resolve().parents[1]
        metadata = json.loads((project / 'docs/point-table-import.json').read_text())
        symbols = importer.Symbols()
        symbols.read((project / 'app/cfg/inc/imported_signals.h').read_text())
        symbols.expr.update({k[5:]: v for k, v in list(symbols.expr.items())
                             if k.startswith('AIDC_')})
        rows = importer.render_reference_tables(metadata, symbols.value)
        self.assertEqual(set(rows), set(importer.TABLE_FILES))
        for name, content in rows.items():
            path = project / 'app/cfg' / importer.TABLE_FILES[name]
            text = path.read_text()
            self.assertEqual(importer.replace_imported_tables(text, {name: content}, path), text)
        self.assertIn('.pf_sig_proc=idu_serial_setting', rows['points'])
        self.assertIn('.pf_sig_proc=idu_firmware_version', rows['points'])
        self.assertIn('.pf_sig_proc=idu_unsupported_service', rows['points'])
        self.assertNotIn('idu_aidc_process', rows['points'])
        self.assertNotIn('point_metadata.c', importer.TABLE_FILES.values())


if __name__ == "__main__":
    unittest.main()
