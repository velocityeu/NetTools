import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "build_native_resources.py"


def load_generator():
    spec = importlib.util.spec_from_file_location("build_native_resources", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def png_fixture(width=64, height=64, tail=b"exact-source-payload"):
    return (
        b"\x89PNG\r\n\x1a\n"
        + struct.pack(">I", 13)
        + b"IHDR"
        + struct.pack(">II", width, height)
        + b"\x08\x06\x00\x00\x00"
        + b"\x00\x00\x00\x00"
        + tail
    )


def unpack_index(payload):
    if payload[:8] != b"VEUHLP1\x00":
        raise AssertionError("bad index signature")
    position = 8
    count, = struct.unpack_from("<I", payload, position)
    position += 4
    records = []
    for _ in range(count):
        resource_id, *lengths = struct.unpack_from("<IIIII", payload, position)
        position += 20
        fields = []
        for length in lengths:
            fields.append(payload[position:position + length].decode("utf-8"))
            position += length
        records.append((resource_id, *fields))
    if position != len(payload):
        raise AssertionError("trailing index data")
    return records


class NativeResourceTests(unittest.TestCase):
    def test_rtf_escape_handles_control_characters_and_utf16_surrogates(self):
        generator = load_generator()

        escaped = generator.rtf_escape("A\\{B}\tline\nEuro € rocket \U0001f680")

        self.assertEqual(
            escaped,
            r"A\\\{B\}\tab line\par" + "\n" + r"Euro \u8364? rocket \u-10179?\u-8576?",
        )

    def test_png_icon_wraps_the_exact_official_64px_png(self):
        generator = load_generator()
        source = png_fixture()

        icon = generator.package_png_as_ico(source)

        reserved, image_type, count = struct.unpack_from("<HHH", icon)
        width, height, _, _, planes, bits, size, offset = struct.unpack_from("<BBBBHHII", icon, 6)
        self.assertEqual((reserved, image_type, count), (0, 1, 1))
        self.assertEqual((width, height, planes, bits), (64, 64, 1, 32))
        self.assertEqual(size, len(source))
        self.assertEqual(icon[offset:offset + size], source)

    def test_png_icon_rejects_an_asset_with_the_wrong_dimensions(self):
        generator = load_generator()

        with self.assertRaisesRegex(ValueError, "64 x 64"):
            generator.package_png_as_ico(png_fixture(width=32, height=64))

    def test_generate_emits_searchable_topics_and_preserves_artwork(self):
        generator = load_generator()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "docs/help").mkdir(parents=True)
            (root / "docs/design").mkdir(parents=True)
            (root / "resources").mkdir()
            favicon = png_fixture(tail=b"official-favicon-bytes")
            logo = png_fixture(width=1432, height=307, tail=b"official-logo-bytes")
            (root / "docs/design/velocity-corporate-favicon.png").write_bytes(favicon)
            (root / "docs/design/velocity-corporate-logo.png").write_bytes(logo)
            (root / "LICENSE").write_text("MIT licence text\n", encoding="utf-8")
            topics = {
                "schemaVersion": 1,
                "topics": [{
                    "id": "getting-started",
                    "title": "Start & learn",
                    "keywords": "start rocket",
                    "blocks": [
                        {"type": "heading", "children": ["First {steps}"]},
                        {"type": "paragraph", "children": ["Launch \U0001f680 safely."]},
                        {"type": "link", "href": "https://www.velocity-eu.com/", "children": ["Velocity EU"]},
                    ],
                }],
            }
            (root / "docs/help/topics.json").write_text(
                json.dumps(topics, ensure_ascii=False), encoding="utf-8"
            )

            generated = generator.generate(root)

            generated_dir = root / "resources/generated"
            self.assertEqual(
                set(generated),
                {
                    generated_dir / "help-index.bin",
                    generated_dir / "topics/getting-started.rtf",
                    generated_dir / "license.txt",
                    generated_dir / "velocity-corporate-logo.png",
                    generated_dir / "velocity-nettools.ico",
                    generated_dir / "help_resources.rcinc",
                },
            )
            self.assertEqual((generated_dir / "velocity-corporate-logo.png").read_bytes(), logo)
            icon = (generated_dir / "velocity-nettools.ico").read_bytes()
            size, offset = struct.unpack_from("<II", icon, 14)
            self.assertEqual(icon[offset:offset + size], favicon)
            self.assertEqual((generated_dir / "license.txt").read_text(encoding="utf-8"), "MIT licence text\n")

            records = unpack_index((generated_dir / "help-index.bin").read_bytes())
            self.assertEqual(records, [(1000, "getting-started", "Start & learn", "start rocket", "First {steps}\nLaunch \U0001f680 safely.\nVelocity EU (online: https://www.velocity-eu.com/)")])
            rtf = (generated_dir / "topics/getting-started.rtf").read_text(encoding="ascii")
            self.assertIn(r"First \{steps\}", rtf)
            self.assertIn(r"\u-10179?\u-8576?", rtf)
            self.assertIn("https://www.velocity-eu.com/", rtf)
            rcinc = (generated_dir / "help_resources.rcinc").read_text(encoding="ascii")
            self.assertIn('IDR_HELP_INDEX RCDATA "generated/help-index.bin"', rcinc)
            self.assertIn('1000 RCDATA "generated/topics/getting-started.rtf"', rcinc)


if __name__ == "__main__":
    unittest.main()
