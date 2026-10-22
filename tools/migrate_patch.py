#!/usr/bin/env python3
"""Copy a legacy Rack patch/preset to the Bearamin collection, without overwriting."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import copy
import io
import json
from pathlib import Path
import subprocess
import tarfile

LEGACY = {
    ("SlowLFOPrototype", "SlowLFO"): ("Bearamin", "SlowLFO"),
    ("BearaminVirtual", "BearaminVirtual"): ("Bearamin", "BearaminVirtual"),
}
ZSTD_MAGIC = b"\x28\xb5\x2f\xfd"


def migrate_json(payload):
    document = json.loads(payload)
    if not isinstance(document, dict):
        raise ValueError("Expected a Rack patch or module preset object")
    changed = 0
    modules = document.get("modules", [document])
    if not isinstance(modules, list):
        raise ValueError("Invalid Rack module list")
    for module in modules:
        if not isinstance(module, dict):
            continue
        target = LEGACY.get((module.get("plugin"), module.get("model")))
        if target:
            module["plugin"], module["model"] = target
            changed += 1
    return (json.dumps(document, ensure_ascii=False, indent=2) + "\n").encode(), changed


def convert(payload):
    if not payload.startswith(ZSTD_MAGIC):
        return migrate_json(payload)
    # Read/write the archive in memory; never extract its paths to the filesystem.
    raw = subprocess.run(["zstd", "-q", "-d", "-c"], input=payload,
                         stdout=subprocess.PIPE, check=True).stdout
    result = io.BytesIO()
    with tarfile.open(fileobj=io.BytesIO(raw), mode="r:") as source:
        patches = [m for m in source.getmembers() if m.name in ("patch.json", "./patch.json")]
        if len(patches) != 1 or not patches[0].isfile():
            raise ValueError("Expected exactly one regular patch.json in the Rack archive")
        target = patches[0]
        revised, changed = migrate_json(source.extractfile(target).read())
        with tarfile.open(fileobj=result, mode="w", format=tarfile.PAX_FORMAT) as output:
            for member in source.getmembers():
                meta = copy.deepcopy(member)
                if member is target:
                    meta.size = len(revised)
                    meta.pax_headers.pop("size", None)
                    output.addfile(meta, io.BytesIO(revised))
                else:
                    stream = source.extractfile(member) if member.isfile() else None
                    output.addfile(meta, stream)
    packed = subprocess.run(["zstd", "-q", "-c"], input=result.getvalue(),
                            stdout=subprocess.PIPE, check=True).stdout
    return packed, changed


def migrate_file(source, destination):
    source, destination = Path(source), Path(destination)
    if source.resolve() == destination.resolve() or destination.exists():
        raise FileExistsError("Choose a new output file; originals are never overwritten")
    converted, changed = convert(source.read_bytes())
    if not changed:
        raise ValueError("No legacy SloFO or Bearamin Virtual modules found; no output written")
    with destination.open("xb") as output:
        output.write(converted)
    return changed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    try:
        count = migrate_file(args.source, args.destination)
    except (OSError, ValueError, tarfile.TarError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Migration stopped: {error}\n")
    print(f"Copied {count} legacy module(s) to {args.destination}; original unchanged")


if __name__ == "__main__":
    main()
