"""Unit tests for scripts/update_ota_manifest.py (run: python3 -m unittest
discover -s test/scripts)."""

import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "..", "..", "scripts", "update_ota_manifest.py")
sys.path.insert(0, os.path.dirname(SCRIPT))
import update_ota_manifest as m  # noqa: E402

URL = "https://example.invalid/fw-{}-ota.bin"


def add(man, v, pre, notes=""):
    return m.add_build(man, v, URL.format(v), 100, "ab" * 32, notes, pre)


def versions(man, ch):
    return [b["version"] for b in man["builds"] if b["channel"] == ch]


class VersionOrder(unittest.TestCase):
    def test_numeric_not_text(self):
        self.assertGreater(m.version_key("v0.0.10"), m.version_key("v0.0.9"))

    def test_leading_v_ignored(self):
        self.assertEqual(m.version_key("v1.2.3")[0], m.version_key("1.2.3")[0])

    def test_suffix_before_final(self):
        self.assertLess(m.version_key("v0.1.0-rc.1"), m.version_key("v0.1.0"))
        self.assertGreater(m.version_key("v0.1.0-rc.1"), m.version_key("v0.0.9"))


class AddBuild(unittest.TestCase):
    def test_prerelease_goes_to_beta_only(self):
        man = add({}, "v0.0.7", True)
        self.assertEqual(versions(man, "beta"), ["v0.0.7"])
        self.assertEqual(versions(man, "stable"), [])
        self.assertEqual(man["app"], "signalk-espos-8relay")
        self.assertEqual(man["schema"], 1)
        self.assertEqual(man["builds"][0]["target"], "esp32s3")

    def test_full_release_goes_to_both(self):
        man = add(add({}, "v0.0.7", True), "v0.1.0", False)
        self.assertEqual(versions(man, "stable"), ["v0.1.0"])
        self.assertEqual(versions(man, "beta"), ["v0.1.0", "v0.0.7"])

    def test_readding_a_version_replaces_it(self):
        man = add({}, "v0.1.0", False)
        man = m.add_build(man, "v0.1.0", URL.format("x"), 200, "cd" * 32, "", False)
        self.assertEqual(versions(man, "stable"), ["v0.1.0"])
        self.assertEqual(man["builds"][0]["size"], 200)

    def test_keeps_newest_per_channel(self):
        man = {}
        for v in ["v0.1.0", "v0.1.1", "v0.1.2", "v0.1.10", "v0.1.9"]:
            man = add(man, v, False)
        self.assertEqual(versions(man, "stable"), ["v0.1.10", "v0.1.9", "v0.1.2"])

    def test_notes_fit_the_boards_buffer_in_bytes(self):
        man = add({}, "v0.1.0", False, notes="Überprüfung – " * 20)
        notes = man["builds"][0]["notes"]
        self.assertLessEqual(len(notes.encode()), m.NOTES_MAX)
        self.assertTrue(notes.endswith("..."))
        man = add({}, "v0.1.1", False, notes="short")
        self.assertEqual(man["builds"][0]["notes"], "short")

    def test_too_long_url_is_refused(self):
        with self.assertRaises(ValueError):
            m.add_build({}, "v0.1.0", "https://x/" + "a" * 300, 1, "", "", False)


class CommandLine(unittest.TestCase):
    def test_writes_size_and_sha256_of_the_image(self):
        with tempfile.TemporaryDirectory() as d:
            img = os.path.join(d, "fw.bin")
            with open(img, "wb") as f:
                f.write(b"\x00" * 1000)
            man = os.path.join(d, "manifest.json")
            for v, pre in [("v0.0.7", True), ("v0.1.0", False)]:
                args = [sys.executable, SCRIPT, "--manifest", man, "--version", v,
                        "--url", URL.format(v), "--image", img, "--notes", "n"]
                subprocess.run(args + (["--prerelease"] if pre else []), check=True,
                               capture_output=True)
            with open(man) as f:
                out = json.load(f)
            b = out["builds"][0]
            self.assertEqual(b["size"], 1000)
            self.assertEqual(b["sha256"],
                             "541b3e9daa09b20bf85fa273e5cbd3e80185aa4ec298e765db87742b70138a53")
            self.assertEqual(versions(out, "stable"), ["v0.1.0"])
            self.assertEqual(versions(out, "beta"), ["v0.1.0", "v0.0.7"])


if __name__ == "__main__":
    unittest.main()
