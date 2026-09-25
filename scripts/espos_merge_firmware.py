#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# Copied unchanged (below this note) from espOS 0.10.3, tools/espos_merge_firmware.py:
# the registry packages don't include it. Re-copy when espOS changes it.
"""Merge an ESP-IDF build's flash images into one binary.

Reads the offset/file map the build already worked out
(`flasher_args.json`), so the offsets here can never drift from what the
partition table and bootloader actually need -- which is the whole reason
this is not a hand-written list of addresses.

    tools/espos_merge_firmware.py --build-dir build --chip esp32c6 \
        --out firmware-merged.bin

A merged image is what a USB flash wants: one file at offset 0, no
partition table to line up by hand. The OTA image is the app binary on its
own, which the build already produced.

    --require <name>   fail if that partition is missing from the build

Use `--require` for a data partition whose absence still yields a BOOTABLE
image, because those are the ones that go unnoticed: an espOS firmware
merged without its `storage` partition boots and serves a placeholder page
that reads as a firmware bug, and one merged without a wake-word model
boots with a dead wake word that no OTA can repair.
"""
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path,
                         help="e.g. build")
    parser.add_argument("--chip", required=True, help="e.g. esp32p4")
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument(
        "--require", action="append", default=[], metavar="NAME",
        help="fail if this partition is missing from the build (repeatable). "
             "Matched against the flasher_args path, so 'storage', "
             "'srmodels' or a filename all work. Use it for a partition "
             "whose absence still leaves a bootable image -- those are the "
             "ones nobody notices.")
    args = parser.parse_args()

    flasher_args_path = args.build_dir / "flasher_args.json"
    if not flasher_args_path.is_file():
        print(f"error: {flasher_args_path} not found — build the firmware first",
              file=sys.stderr)
        return 1

    flasher_args = json.loads(flasher_args_path.read_text())
    # The app's own path, so an unresolvable entry can be told apart from a
    # data partition that simply was not built.
    app_rel = flasher_args.get("app", {}).get("file", "")
    flash_settings = flasher_args["flash_settings"]
    flash_files = flasher_args["flash_files"]

    # Every file, because a build lays its outputs out in several places and
    # resolve() matches by basename. NOT printed: a build dir holds thousands
    # of objects and a pip venv, and dumping them buried the one line that
    # mattered -- the error saying which partition was missing.
    all_files = [p for p in args.build_dir.rglob("*") if p.is_file()]

    # flasher_args.json's paths mirror idf.py's build/bootloader/,
    # build/partition_table/ layout; resolve by role as well so a renamed
    # output still lands.
    #
    # Skipping otadata is harmless: all-0xFF means "boot the first OTA slot",
    # which is what a freshly flashed device should do. Skipping a DATA
    # partition often is not, and the trouble is that it still boots -- hence
    # --require, which turns "not produced by this build" into an error for
    # the partitions a project cannot ship without.
    by_basename = {p.name: p for p in all_files}
    mandatory_roles = {
        "bootloader": "bootloader.bin",
        "partition": "partitions.bin",
    }
    optional_roles = {
        "ota_data": "ota_data_initial.bin",
        "srmodels": "srmodels.bin",
        "model": "srmodels.bin",
    }

    def resolve(rel_path: str) -> Path | None:
        direct = args.build_dir / rel_path
        if direct.is_file():
            return direct
        found = by_basename.get(Path(rel_path).name)
        if found is not None:
            return found
        lower = rel_path.lower()
        for keyword, override_name in mandatory_roles.items():
            if keyword in lower:
                if override_name in by_basename:
                    return by_basename[override_name]
                print(f"error: could not locate '{rel_path}' (role: {keyword}) "
                      f"anywhere under {args.build_dir}", file=sys.stderr)
                sys.exit(1)
        for keyword, override_name in optional_roles.items():
            if keyword in lower:
                return by_basename.get(override_name)
        # What is left is either the app image or a data partition this
        # build did not produce. Distinguish them by asking flasher_args
        # which offset the app lives at, rather than guessing from the name:
        # a project is free to call its app anything, and treating an absent
        # data partition as a missing app produced the confusing
        # "could not locate 'storage.bin' (app image)".
        if rel_path == app_rel:
            if "firmware.bin" in by_basename:
                return by_basename["firmware.bin"]
            print(f"error: could not locate the app image '{rel_path}' "
                  f"anywhere under {args.build_dir}", file=sys.stderr)
            sys.exit(1)
        return None  # a data partition this build did not produce

    cmd = [
        "esptool.py", "--chip", args.chip, "merge_bin",
        "-o", str(args.out),
        "--flash_mode", flash_settings["flash_mode"],
        "--flash_freq", flash_settings["flash_freq"],
        "--flash_size", flash_settings["flash_size"],
    ]
    for offset, rel_path in sorted(flash_files.items(), key=lambda kv: int(kv[0], 16)):
        resolved = resolve(rel_path)
        if resolved is None:
            lower = rel_path.lower()
            wanted = [r for r in args.require if r.lower() in lower]
            if wanted:
                print(f"error: --require {wanted[0]} was given, but '{rel_path}' "
                      f"was not produced by this build. Merging without it "
                      f"would still yield a bootable image -- which is exactly "
                      f"why this is an error rather than a warning.",
                      file=sys.stderr)
                return 1
            print(f"skipping {offset} ({rel_path}): not produced by this build")
            continue
        cmd += [offset, str(resolved)]

    print("+", " ".join(cmd))
    subprocess.run(cmd, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
