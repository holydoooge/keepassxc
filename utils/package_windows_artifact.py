#!/usr/bin/env python3
"""Package the staged KeePassXC application as the release ZIP.

Kept as a file rather than inlined in the workflow so it can be run and tested
locally, and so no shell/PowerShell here-string quoting is involved.

The layout is asserted here because it has silently regressed before: a wrapper
directory or a second archive inside the ZIP makes the download useless without
failing anything.

Usage: package_windows_artifact.py <stage-dir> <output-zip>
"""

import sys
import zipfile
from pathlib import Path

REQUIRED_AT_ROOT = ("KeePassXC.exe", "Qt6Core.dll")


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2

    stage = Path(sys.argv[1]).resolve()
    out = Path(sys.argv[2]).resolve()

    if not stage.is_dir():
        print(f"error: staging directory does not exist: {stage}")
        return 1

    for name in REQUIRED_AT_ROOT:
        if not (stage / name).is_file():
            print(f"error: {name} is missing from {stage}")
            return 1

    files = sorted(p for p in stage.rglob("*") if p.is_file())
    if not files:
        print(f"error: no files to package in {stage}")
        return 1

    out.parent.mkdir(parents=True, exist_ok=True)
    out.unlink(missing_ok=True)

    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path in files:
            archive.write(path, path.relative_to(stage).as_posix())

    # Verify what was actually written, not what was intended.
    with zipfile.ZipFile(out) as archive:
        names = archive.namelist()

    if not names:
        print("error: the archive is empty")
        return 1
    if "KeePassXC.exe" not in names:
        print("error: KeePassXC.exe is not at the archive root (wrapped in a directory?)")
        return 1
    if any(n.lower().endswith((".zip", ".7z", ".tar", ".gz")) for n in names):
        print("error: the archive contains another archive")
        return 1

    wrapper_dirs = {n.split("/", 1)[0] for n in names if "/" in n}
    print(f"packed {len(files)} files, {len(names)} entries")
    print(f"archive: {out} ({out.stat().st_size} bytes)")
    print(f"plugin directories: {', '.join(sorted(wrapper_dirs)) or 'none'}")
    print("layout OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
