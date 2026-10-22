#!/usr/bin/env python3
"""Verify the distributable manifest, artwork, binary and license notices."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import io
import json
from pathlib import Path
import subprocess
import tarfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    manifest = json.loads((root / "plugin.json").read_text())
    slug = manifest["slug"]
    raw = subprocess.run(["zstd", "-q", "-d", "-c", str(args.package)],
                         stdout=subprocess.PIPE, check=True).stdout
    with tarfile.open(fileobj=io.BytesIO(raw), mode="r:") as archive:
        names = archive.getnames()
        assert not any("__MACOSX" in n or "/._" in n or n.startswith(("/", "../")) for n in names)
        assert set(n.split("/")[0] for n in names) == {slug}
        binaries = [n for n in names if n in {f"{slug}/plugin.{ext}" for ext in ("so", "dylib", "dll")}]
        assert len(binaries) == 1
        assert archive.getmember(binaries[0]).size > 0
        for relative in ("plugin.json", "LICENSE", "LICENSE-BEARFIELD", "res/SlowLFO.svg", "res/BearaminVirtual.svg"):
            assert archive.extractfile(f"{slug}/{relative}").read() == (root / relative).read_bytes(), relative
    assert {(m["slug"], m["name"]) for m in manifest["modules"]} == {
        ("SlowLFO", "SloFO"), ("BearaminVirtual", "Bearamin Lite")}
    print(f"Verified {args.package.name}: both modules, binary, artwork and GPL/MIT notices")


if __name__ == "__main__":
    main()
