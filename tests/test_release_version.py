#!/usr/bin/env python3
"""Pure unit tests for semantic version tag ordering and reruns."""
import importlib.util
import sys
from pathlib import Path
import unittest

MOD = Path(__file__).resolve().parents[1] / "tools" / "publish_release.py"
spec = importlib.util.spec_from_file_location("publish_release", MOD)
publish_release = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = publish_release
spec.loader.exec_module(publish_release)


class VersionTests(unittest.TestCase):
    def test_first_version(self):
        self.assertEqual(publish_release.choose_tag({}, "a" * 40), (0, False))

    def test_increment_last_numeric_component(self):
        tags = {0: "a" * 40, 2: "b" * 40, 9: "c" * 40}
        self.assertEqual(publish_release.choose_tag(tags, "d" * 40), (10, False))

    def test_rerun_reuses_existing_version(self):
        tags = {0: "a" * 40, 1: "b" * 40}
        self.assertEqual(publish_release.choose_tag(tags, "a" * 40), (0, True))

    def test_remote_tag_filtering(self):
        raw = (
            "a" * 40 + "\trefs/tags/v1.0.0.0\n"
            + "b" * 40 + "\trefs/tags/v1.0.0.13\n"
            + "c" * 40 + "\trefs/tags/v2.0.0\n"
            + "d" * 40 + "\trefs/tags/v1.0.0.foo\n"
        )
        self.assertEqual(
            publish_release.read_version_tags(raw),
            {0: "a" * 40, 13: "b" * 40}
        )


if __name__ == "__main__":
    unittest.main()
