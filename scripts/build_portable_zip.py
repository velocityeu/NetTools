"""Create a portable ZIP around the already verified release executable."""
import argparse
import hashlib
from pathlib import Path
import zipfile

def build_archive(directory, version, signing):
    directory = Path(directory)
    names = ["VelocityNetTools-x64.exe", "LICENSE.txt", "THIRD-PARTY-NOTICES.txt"]
    files = {name: (directory / name).read_bytes() for name in names}
    archive_path = directory / "VelocityNetTools-x64.zip"
    if archive_path.exists():
        raise ValueError("Refusing to overwrite an existing release archive")
    guide = f"""Velocity NetTools {version}
By Velocity EU Inc - https://www.velocity-eu.com/
Signature status: {signing}

GETTING STARTED
1. In Windows, right-click the ZIP and choose Extract all.
2. Open the extracted folder and run VelocityNetTools-x64.exe.
3. Press F1 for the built-in searchable help, examples and networking lessons.

No installer or additional application runtime is required. The EXE can also
be downloaded separately; it is byte-for-byte identical to the EXE in this ZIP.
The documentation files are optional for running the application.

Target: Windows 10/11 x64 and Windows Server 2016 or later with Desktop
Experience. The oldest supported targets still require clean-machine testing.
Settings and history stay in the current session unless you explicitly save.
Network probes and the external-IP check run only when requested.

DOWNLOAD VERIFICATION
SHA256SUMS.txt inside this ZIP lists SHA-256 hashes of its executable and
documentation. Compare the archive itself with the separate release checksum.
ZIP packaging does not remove Windows SmartScreen checks. Unsigned previews
can show an unknown-publisher or reputation warning. Signing is a separate step.

Website and training: https://velocityeu.github.io/NetTools/
Online help: https://velocityeu.github.io/NetTools/help/
Releases: https://github.com/velocityeu/NetTools/releases
Report a bug: https://github.com/velocityeu/NetTools/issues
Include the app version, Windows version and reproducible steps.
"""
    files["START-HERE.txt"] = guide.encode("utf-8")
    (directory / "START-HERE.txt").write_bytes(files["START-HERE.txt"])
    checks = "".join(f"{hashlib.sha256(data).hexdigest()} *{name}\n" for name, data in files.items())
    files["SHA256SUMS.txt"] = checks.encode("ascii")
    with zipfile.ZipFile(archive_path, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in files.items():
            archive.writestr(name, data)
    with zipfile.ZipFile(archive_path) as archive:
        if len(archive.infolist()) != len(files) or set(archive.namelist()) != set(files):
            raise ValueError("Portable archive entry set is incorrect")
        for name, data in files.items():
            if archive.read(name) != data:
                raise ValueError(f"Portable archive bytes differ: {name}")
    return archive_path

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--signing", choices=["unsigned", "authenticode"], required=True)
    args = parser.parse_args()
    result = build_archive(args.directory, args.version, args.signing)
    print(f"Verified portable archive: {result.name}")
