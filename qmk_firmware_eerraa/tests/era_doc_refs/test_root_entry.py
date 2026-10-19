"""Root routing pointers resolve from the Git root, not the component."""

import importlib.util
from pathlib import Path
import unittest

COMPONENT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "era_doc_refs", COMPONENT / "keyboards/era/common/tools/era_doc_refs.py")
DOCS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DOCS)


class RootEntryTests(unittest.TestCase):
    def test_root_relative_link_and_common_files(self):
        body = "[firmware](qmk_firmware_eerraa/AGENTS.md)\n`.gitmodules`\n"
        self.assertEqual(DOCS.root_entry_findings(
            body, {"qmk_firmware_eerraa/AGENTS.md", ".gitmodules"}), [])

    def test_component_relative_link_is_not_root_relative(self):
        findings = DOCS.root_entry_findings(
            "[manual](keyboards/era/common/docs/manuals/guide.md)",
            {"qmk_firmware_eerraa/keyboards/era/common/docs/manuals/guide.md"})
        self.assertEqual(len(findings), 1)

    def test_missing_link_and_literal_file_are_refused(self):
        findings = DOCS.root_entry_findings("[missing](gone.md)\n`missing.yml`", set())
        self.assertEqual(len(findings), 2)

    def test_external_links_and_commands_are_not_local_file_claims(self):
        self.assertEqual(DOCS.root_entry_findings(
            "[spec](https://example.com/spec.md)\n`git status`\n", set()), [])


if __name__ == "__main__":
    unittest.main()
