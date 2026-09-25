#!/usr/bin/env python3
"""Add a release to the OTA version manifest that espOS boards check.

The format is espOS's (espos_ota_manifest.h, docs/ota.md upstream):

    {"schema": 1, "app": "<project name>",
     "builds": [{"target": "esp32s3", "channel": "stable", "version": "v0.1.0",
                 "url": "https://...-ota.bin", "size": 1576960,
                 "sha256": "...", "notes": "..."}]}

A board picks the highest version on its channel (default "stable") for its
app and target. A full release is added to both "stable" and "beta", so beta
boards are offered it too; a pre-release to "beta" only. Re-adding a version
replaces its entries. Only the newest KEEP builds per channel are kept, so
the file stays far below the 16 KB espOS reads.

    update_ota_manifest.py --manifest manifest.json --version v0.1.0 \
        --url https://.../signalk-espOS-8relay-v0.1.0-ota.bin \
        --image dist/signalk-espOS-8relay-v0.1.0-ota.bin [--prerelease] \
        [--notes "..."]
"""

import argparse
import hashlib
import json
import os
import re
import sys

APP = "signalk-espos-8relay"  # project() in CMakeLists.txt: what the board reports
TARGET = "esp32s3"
KEEP = 3
MANIFEST_MAX = 16384  # CONFIG_ESPOS_OTA_MANIFEST_MAX default
URL_MAX = 255         # ESPOS_OTA_URL_MAX - 1
NOTES_MAX = 127       # ESPOS_OTA_NOTES_MAX - 1
VERSION_MAX = 31      # ESPOS_OTA_VERSION_MAX - 1


def version_key(v):
    """Sort key matching espos_ota_version_cmp(): leading v skipped, up to
    four numeric parts, then a version with a suffix ("-rc.1") before the
    same core without one, then the suffix as text."""
    m = re.match(r"^[vV]?(\d+(?:\.\d+){0,3})(.*)$", v)
    if not m:
        return ((), 0, v)
    core = tuple(int(p) for p in m.group(1).split("."))
    core += (0,) * (4 - len(core))
    rest = m.group(2)
    return (core, 0 if rest else 1, rest)


def add_build(manifest, version, url, size, sha256, notes, prerelease, keep=KEEP):
    if len(version) > VERSION_MAX:
        raise ValueError(f"version longer than {VERSION_MAX} characters: {version}")
    if len(url) > URL_MAX:
        raise ValueError(f"URL longer than {URL_MAX} characters: {url}")
    # The board's buffer counts bytes, not characters.
    if len(notes.encode()) > NOTES_MAX:
        while len(notes.encode()) > NOTES_MAX - 3:
            notes = notes[:-1]
        notes = notes.rstrip() + "..."
    channels = ["beta"] if prerelease else ["stable", "beta"]
    builds = [b for b in manifest.get("builds", []) if b.get("version") != version]
    for ch in channels:
        builds.append({"target": TARGET, "channel": ch, "version": version, "url": url,
                       "size": size, "sha256": sha256, "notes": notes})
    kept = []
    for ch in sorted({b.get("channel", "stable") for b in builds}):
        mine = [b for b in builds if b.get("channel", "stable") == ch]
        mine.sort(key=lambda b: version_key(b["version"]), reverse=True)
        kept += mine[:keep]
    kept.sort(key=lambda b: (b.get("channel", "stable"), version_key(b["version"])), reverse=True)
    return {"schema": 1, "app": APP, "builds": kept}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--manifest", required=True, help="read (if it exists) and rewrite this file")
    ap.add_argument("--version", required=True)
    ap.add_argument("--url", required=True, help="absolute URL of the OTA image")
    ap.add_argument("--image", required=True, help="the OTA image, for size and SHA-256")
    ap.add_argument("--notes", default="")
    ap.add_argument("--prerelease", action="store_true")
    ap.add_argument("--keep", type=int, default=KEEP)
    a = ap.parse_args(argv)

    manifest = {}
    if os.path.exists(a.manifest):
        with open(a.manifest) as f:
            manifest = json.load(f)
        if manifest.get("schema", 1) != 1 or manifest.get("app", APP) != APP:
            sys.exit(f"{a.manifest}: not a schema-1 manifest for {APP}")
    with open(a.image, "rb") as f:
        data = f.read()
    out = add_build(manifest, a.version, a.url, len(data), hashlib.sha256(data).hexdigest(),
                    a.notes, a.prerelease, a.keep)
    text = json.dumps(out, indent=2, ensure_ascii=False) + "\n"
    if len(text.encode()) > MANIFEST_MAX:
        sys.exit(f"manifest would be {len(text.encode())} bytes; espOS reads at most {MANIFEST_MAX}")
    with open(a.manifest, "w") as f:
        f.write(text)
    stable = [b["version"] for b in out["builds"] if b["channel"] == "stable"]
    beta = [b["version"] for b in out["builds"] if b["channel"] == "beta"]
    print(f"{a.manifest}: stable {stable or '-'}, beta {beta or '-'}")


if __name__ == "__main__":
    main()
