"""Verify portable archive contents and both website download formats."""
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import prepare_site as site

EXE = "VelocityNetTools-x64.exe"
ZIP = "VelocityNetTools-x64.zip"
FILES = {EXE, "LICENSE.txt", "THIRD-PARTY-NOTICES.txt", "START-HERE.txt", "SHA256SUMS.txt"}

class DistributionTests(unittest.TestCase):
    def package(self, folder):
        (folder / EXE).write_bytes(b"MZ exact executable fixture \x00\xff")
        (folder / "LICENSE.txt").write_text("MIT licence fixture\n", encoding="utf-8")
        (folder / "THIRD-PARTY-NOTICES.txt").write_text("Windows system APIs\n", encoding="utf-8")
        result = subprocess.run([sys.executable, str(ROOT / "scripts/build_portable_zip.py"),
                                 "--directory", str(folder), "--version", "0.1.0-dev.5+1234567",
                                 "--signing", "unsigned"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return (folder / ZIP).read_bytes()

    def test_zip_extracts_identical_exe_and_verifiable_documentation(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            data = self.package(folder)
            with zipfile.ZipFile(io.BytesIO(data)) as archive:
                self.assertEqual(set(archive.namelist()), FILES)
                self.assertEqual(len(archive.namelist()), len(FILES))
                self.assertEqual(archive.read(EXE), (folder / EXE).read_bytes())
                guide = archive.read("START-HERE.txt").decode("utf-8")
                self.assertIn("Extract all", guide)
                self.assertIn("unsigned", guide)
                self.assertIn("F1", guide)
                checks = archive.read("SHA256SUMS.txt").decode("ascii").splitlines()
                self.assertEqual(len(checks), len(FILES)-1)
                for line in checks:
                    digest, name = line.split(" *")
                    self.assertEqual(hashlib.sha256(archive.read(name)).hexdigest(), digest)
                archive.extractall(folder / "extracted")
                self.assertEqual((folder / "extracted" / EXE).read_bytes(), (folder / EXE).read_bytes())

    def release(self, zip_bytes=None):
        tag = "v0.1.0-dev.5+1234567"
        prefix = site.RELEASES_URL + "/download/v0.1.0-dev.5%2B1234567/"
        payloads = {prefix + EXE: b"MZ exact executable fixture \x00\xff"}
        if zip_bytes is not None:
            payloads[prefix + ZIP] = zip_bytes
        assets = [{"name":url.rsplit("/",1)[1], "browser_download_url":url,
                   "state":"uploaded", "size":len(data),
                   "digest":"sha256:"+hashlib.sha256(data).hexdigest()}
                  for url,data in payloads.items()]
        return {"draft":False,"immutable":True,"prerelease":True,"published_at":"2026-09-13T00:00:00Z",
                "tag_name":tag,"html_url":site.RELEASES_URL+"/tag/v0.1.0-dev.5%2B1234567",
                "assets":assets}, payloads

    def test_legacy_exe_only_release_still_renders(self):
        release, payloads = self.release()
        with patch.object(site, "urlopen", side_effect=lambda req, **kw:io.BytesIO(payloads[req.full_url])):
            verified = site.verify_release(release)
        card = site.release_card(verified, True)
        self.assertIn("EXE", card)
        self.assertNotIn("Download ZIP", card)

    def test_both_downloads_render_with_separate_hashes(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = self.package(Path(tmp))
        release, payloads = self.release(data)
        with patch.object(site, "urlopen", side_effect=lambda req, **kw:io.BytesIO(payloads[req.full_url])):
            verified = site.verify_release(release)
        self.assertEqual(verified["archive"]["sha256"], hashlib.sha256(data).hexdigest())
        card = site.release_card(verified, True)
        self.assertIn("Download ZIP", card)
        self.assertIn("Download EXE", card)
        self.assertIn(hashlib.sha256(data).hexdigest(), card)
        self.assertIn(verified["sha256"], card)
        self.assertIn("Extract", card)

    def test_bad_zip_download_fails_instead_of_linking_unverified_bytes(self):
        release, payloads = self.release(b"expected ZIP bytes")
        zip_url = next(url for url in payloads if url.endswith(".zip"))
        payloads[zip_url] = b"corrupt ZIP bytes"
        with patch.object(site, "urlopen", side_effect=lambda req, **kw:io.BytesIO(payloads[req.full_url])):
            with self.assertRaises(ValueError):
                site.verify_release(release)

    def test_duplicate_zip_asset_rejected(self):
        release, payloads = self.release(b"zip")
        release["assets"].append(dict(release["assets"][-1]))
        with patch.object(site, "urlopen", side_effect=lambda req, **kw:io.BytesIO(payloads[req.full_url])):
            with self.assertRaises(ValueError):
                site.verify_release(release)

if __name__ == "__main__":
    unittest.main()
