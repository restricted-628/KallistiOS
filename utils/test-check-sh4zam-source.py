#!/usr/bin/env python3
"""Offline regression tests for the SH4ZAM dependency identity checker."""

import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location(
    "checker", Path(__file__).with_name("check-sh4zam-source.py"))
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


def git(root, *args):
    return subprocess.check_output(
        ["git", "-C", str(root), "-c", "user.name=Test",
         "-c", "user.email=test@example.invalid", "-c", "commit.gpgsign=false",
         *args], stderr=subprocess.PIPE).decode().strip()


class SourceIdentityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="kos-sh4zam-check-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.upstream = self.root / checker.SUBMODULE
        self.upstream.mkdir(parents=True)
        git(self.root, "init", "-q")
        git(self.upstream, "init", "-q")
        (self.upstream / "include/sh4zam").mkdir(parents=True)
        (self.upstream / "include/sh4zam/test.h").write_text("/* upstream */\n")
        (self.upstream / "LICENSE").write_text("Test license\n")
        git(self.upstream, "add", ".")
        git(self.upstream, "commit", "-qm", "Upstream fixture")
        self.revision = git(self.upstream, "rev-parse", "HEAD")
        (self.root / ".gitmodules").write_text(
            f'[submodule "{checker.SUBMODULE}"]\n'
            f'    path = {checker.SUBMODULE}\n'
            f'    url = {checker.UPSTREAM_URL}\n')
        self.headers = self.root / "addons/include/sh4zam"
        self.headers.parent.mkdir(parents=True)
        self.headers.symlink_to("../libsh4zam/upstream/include/sh4zam")
        (self.root / "addons/libsh4zam/LICENSE").write_text("Test license\n")
        git(self.root, "add", ".")

    def test_clean_pinned_source(self):
        self.assertEqual(checker.verify(self.root), self.revision)

    def test_missing_checkout(self):
        (self.upstream / ".git").rename(self.upstream / ".git-disabled")
        with self.assertRaisesRegex(ValueError, "SH4ZAM missing"):
            checker.verify(self.root)

    def test_tracked_source_edit(self):
        (self.upstream / "include/sh4zam/test.h").write_text("/* local patch */\n")
        with self.assertRaisesRegex(ValueError, "local modifications"):
            checker.verify(self.root)

    def test_untracked_source(self):
        (self.upstream / "extra.c").write_text("/* unreviewed */\n")
        with self.assertRaisesRegex(ValueError, "local modifications"):
            checker.verify(self.root)

    def test_revision_mismatch(self):
        git(self.upstream, "commit", "--allow-empty", "-qm", "Different revision")
        with self.assertRaisesRegex(ValueError, "pinned Git revision"):
            checker.verify(self.root)

    def test_non_gitlink(self):
        git(self.root, "update-index", "--force-remove", checker.SUBMODULE)
        with self.assertRaisesRegex(ValueError, "resolved Git submodule"):
            checker.verify(self.root)

    def test_wrong_repository(self):
        git(self.root, "config", "--file", ".gitmodules",
            f"submodule.{checker.SUBMODULE}.url", "https://example.invalid/other")
        with self.assertRaisesRegex(ValueError, "official repository"):
            checker.verify(self.root)

    def test_copied_headers(self):
        self.headers.unlink()
        self.headers.mkdir()
        with self.assertRaisesRegex(ValueError, "link directly"):
            checker.verify(self.root)

    def test_former_temporary_fork_rejected(self):
        git(self.root, "config", "--file", ".gitmodules",
            f"submodule.{checker.SUBMODULE}.url",
            "https://github.com/restricted-628/sh4zam.git")
        with self.assertRaisesRegex(ValueError, "official repository"):
            checker.verify(self.root)

    def test_changed_license(self):
        (self.root / "addons/libsh4zam/LICENSE").write_text("Wrong license\n")
        with self.assertRaisesRegex(ValueError, "license copy differs"):
            checker.verify(self.root)

    def test_old_source_tree(self):
        (self.root / "addons/libsh4zam/source").mkdir()
        with self.assertRaisesRegex(ValueError, "Obsolete vendored"):
            checker.verify(self.root)


if __name__ == "__main__":
    unittest.main()
