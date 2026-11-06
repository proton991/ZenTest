"""Repository check regressions that need real Git repositories or scan folders."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import check_common
import check_no_exceptions

GIT = ["git", "-c", "user.name=test", "-c", "user.email=test@example.invalid", "-c", "protocol.file.allow=always"]


def git(folder, *arguments):
    subprocess.run([*GIT, *arguments], cwd=folder, check=True, capture_output=True)


class ChangedFilesTest(unittest.TestCase):
    def test_reports_submodule_edits_and_commits_after_the_pin(self):
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as folder:
            origin = Path(folder) / "origin"
            engine = Path(folder) / "engine"
            for repository in (origin, engine):
                repository.mkdir()
                git(repository, "init", "-q", "-b", "main")
            (origin / "edited.cpp").write_text("int a;\n", encoding="utf-8")
            git(origin, "add", "-A")
            git(origin, "commit", "-q", "-m", "first")
            git(engine, "submodule", "add", "-q", origin.as_uri(), "ZenTest")
            git(engine, "commit", "-q", "-m", "pin")

            submodule = engine / "ZenTest"
            (submodule / "committed.cpp").write_text("int b;\n", encoding="utf-8")
            git(submodule, "add", "-A")
            git(submodule, "commit", "-q", "-m", "after the pin")
            (submodule / "edited.cpp").write_text("int c;\n", encoding="utf-8")
            (submodule / "untracked.cpp").write_text("int d;\n", encoding="utf-8")

            with patch.object(check_common, "ROOT", engine), patch.object(check_common, "SUBPROJECTS", ("ZenTest",)):
                changed = check_common.changed_files()
            self.assertEqual(changed, ["ZenTest/committed.cpp", "ZenTest/edited.cpp", "ZenTest/untracked.cpp"])


class ExceptionScanTest(unittest.TestCase):
    def scan(self, root):
        with patch.object(check_no_exceptions, "ROOT", root):
            return check_no_exceptions.main()

    def test_ignores_standalone_build_folders_but_not_owned_sources(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            third_party = root / "ZenTest/build/_deps/library-src"
            third_party.mkdir(parents=True)
            (third_party / "throws.cpp").write_text("void f() { throw 1; }\n", encoding="utf-8")
            (third_party / "not_utf8.h").write_bytes(b"// \xff\n")
            self.assertEqual(self.scan(root), 0)
            (root / "ZenTest/owned.cpp").write_text("void f() { throw 1; }\n", encoding="utf-8")
            self.assertEqual(self.scan(root), 1)


if __name__ == "__main__":
    unittest.main()
