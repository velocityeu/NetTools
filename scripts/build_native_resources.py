"""Generate the resources embedded in the native Velocity NetTools executable.

The canonical lesson source remains docs/help/topics.json. Generated files are
build inputs only; the installed application remains one executable.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import struct
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parents[1]
TOPIC_RESOURCE_BASE = 1000
INDEX_SIGNATURE = b"VEUHLP1\0"
_ALLOWED_TYPES = {
    "paragraph", "heading", "strong", "inline-code", "link", "list",
    "ordered-list", "item", "group", "span", "break",
}
_ALLOWED_VARIANTS = {
    "calculation", "reference", "lesson-meta", "note", "lesson-step",
    "step-number", "answer-strip",
}


def _signed_word(value: int) -> int:
    return value if value < 0x8000 else value - 0x10000


def rtf_escape(text: str) -> str:
    """Escape Unicode text for an ASCII RTF stream using UTF-16 code units."""
    result: list[str] = []
    for character in text:
        codepoint = ord(character)
        if character == "\\":
            result.append(r"\\")
        elif character == "{":
            result.append(r"\{")
        elif character == "}":
            result.append(r"\}")
        elif character == "\t":
            result.append(r"\tab ")
        elif character in {"\r", "\n"}:
            if character == "\n":
                result.append("\\par\n")
        elif 0x20 <= codepoint <= 0x7E:
            result.append(character)
        elif codepoint <= 0xFFFF:
            result.append(f"\\u{_signed_word(codepoint)}?")
        else:
            scalar = codepoint - 0x10000
            high = 0xD800 + (scalar >> 10)
            low = 0xDC00 + (scalar & 0x3FF)
            result.append(f"\\u{_signed_word(high)}?\\u{_signed_word(low)}?")
    return "".join(result)


def package_png_as_ico(png: bytes) -> bytes:
    """Wrap the exact official 64px PNG bytes in a one-image ICO container."""
    if len(png) < 29 or png[:8] != b"\x89PNG\r\n\x1a\n" or png[12:16] != b"IHDR":
        raise ValueError("Favicon is not a valid PNG")
    width, height = struct.unpack_from(">II", png, 16)
    if (width, height) != (64, 64):
        raise ValueError("Official favicon must be 64 x 64 pixels")
    header = struct.pack("<HHH", 0, 1, 1)
    entry = struct.pack("<BBBBHHII", 64, 64, 0, 0, 1, 32, len(png), 22)
    return header + entry + png


def _validate_node(node: object) -> None:
    if isinstance(node, str):
        return
    if not isinstance(node, dict) or node.get("type") not in _ALLOWED_TYPES:
        raise ValueError(f"Unsupported content node: {node!r}")
    if set(node) - {"type", "variant", "href", "children"}:
        raise ValueError("Unexpected content attributes")
    if "variant" in node and node["variant"] not in _ALLOWED_VARIANTS:
        raise ValueError("Unsupported content variant")
    if node["type"] == "link":
        url = urlparse(node.get("href", ""))
        if url.scheme != "https" or not url.hostname or url.username or url.password:
            raise ValueError("Reference links must be plain HTTPS URLs")
    elif "href" in node:
        raise ValueError("Only links may carry URLs")
    if not isinstance(node.get("children"), list):
        raise ValueError("Every content node needs a children list")
    for child in node["children"]:
        _validate_node(child)


def _plain(node: object) -> str:
    if isinstance(node, str):
        return node
    kind = node["type"]
    children = node["children"]
    body = "".join(_plain(child) for child in children)
    if kind == "link":
        return f'{body} (online: {node["href"]})'
    if kind == "break":
        return "\n"
    if kind in {"paragraph", "heading", "group", "item"}:
        return body.strip() + "\n"
    if kind in {"list", "ordered-list"}:
        lines = []
        for index, child in enumerate(children):
            marker = f"{index + 1}. " if kind == "ordered-list" else "• "
            lines.append(marker + _plain(child).strip())
        return "\n".join(lines) + "\n"
    return body


def _rtf_inline(node: object) -> str:
    if isinstance(node, str):
        return rtf_escape(node)
    kind = node["type"]
    body = "".join(_rtf_inline(child) for child in node["children"])
    if kind == "strong":
        return r"\b " + body + r"\b0 "
    if kind == "inline-code":
        return r"\f1 " + body + r"\f0 "
    if kind == "link":
        url = rtf_escape(node["href"])
        return r"\cf2\ul " + body + r"\ul0\cf1  (online: \cf2\ul " + url + r"\ul0\cf1 )"
    if kind == "break":
        return "\\line "
    return body


def _rtf_block(node: object) -> str:
    if isinstance(node, str):
        return r"\pard\sa120 " + rtf_escape(node) + "\\par\n"
    kind = node["type"]
    if kind == "heading":
        return r"\pard\sa180\sb120\b\fs28 " + _rtf_inline(node) + r"\b0\fs22\par" + "\n"
    if kind in {"list", "ordered-list"}:
        lines = []
        for index, child in enumerate(node["children"]):
            marker = f"{index + 1}. " if kind == "ordered-list" else "• "
            lines.append(r"\pard\li360\fi-240\sa80 " + rtf_escape(marker) + _rtf_inline(child) + r"\par")
        return "\n".join(lines) + "\n"
    prefix = r"\pard\sa120 "
    if node.get("variant") == "calculation":
        prefix = r"\pard\li240\ri240\sa120\f1 "
    suffix = r"\f0\par" if node.get("variant") == "calculation" else r"\par"
    return prefix + _rtf_inline(node) + suffix + "\n"


def _topic_rtf(title: str, blocks: list[object]) -> bytes:
    header = (
        r"{\rtf1\ansi\ansicpg1252\deff0\uc1"
        r"{\fonttbl{\f0 Segoe UI;}{\f1 Consolas;}}"
        r"{\colortbl;\red0\green0\blue0;\red0\green102\blue180;}"
        "\n"
        r"\viewkind4\pard\cf1\fs32\b "
        + rtf_escape(title)
        + r"\b0\fs22\par\par"
        + "\n"
    )
    body = "".join(_rtf_block(block) for block in blocks)
    return (header + body + "}\n").encode("ascii")


def _validated_topics(data: object) -> list[dict]:
    if not isinstance(data, dict) or data.get("schemaVersion") != 1 or not isinstance(data.get("topics"), list):
        raise ValueError("Unsupported topic schema")
    result = []
    identifiers: set[str] = set()
    for topic in data["topics"]:
        if not isinstance(topic, dict) or set(topic) != {"id", "title", "keywords", "blocks"}:
            raise ValueError("A topic needs exactly id, title, keywords and blocks")
        if any(not isinstance(topic.get(key), str) or not topic[key].strip() for key in ("id", "title", "keywords")):
            raise ValueError("Topic identifiers, titles and keywords must be nonempty text")
        if not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", topic["id"]) or topic["id"] in identifiers:
            raise ValueError("Topic IDs must be unique stable slugs")
        if not isinstance(topic["blocks"], list) or not topic["blocks"]:
            raise ValueError("A topic needs content blocks")
        identifiers.add(topic["id"])
        for block in topic["blocks"]:
            _validate_node(block)
        result.append(topic)
    return result


def _index_payload(topics: list[dict]) -> bytes:
    payload = bytearray(INDEX_SIGNATURE)
    payload.extend(struct.pack("<I", len(topics)))
    for offset, topic in enumerate(topics):
        fields = [
            topic["id"],
            topic["title"],
            topic["keywords"],
            _plain({"type": "group", "children": topic["blocks"]}).strip(),
        ]
        encoded = [field.encode("utf-8") for field in fields]
        payload.extend(struct.pack("<IIIII", TOPIC_RESOURCE_BASE + offset, *(len(field) for field in encoded)))
        for field in encoded:
            payload.extend(field)
    return bytes(payload)


def _expected_outputs(root: Path) -> dict[Path, bytes]:
    source = json.loads((root / "docs/help/topics.json").read_text(encoding="utf-8"))
    topics = _validated_topics(source)
    generated = root / "resources/generated"
    favicon = (root / "docs/design/velocity-corporate-favicon.png").read_bytes()
    logo = (root / "docs/design/velocity-corporate-logo.png").read_bytes()
    licence = (root / "LICENSE").read_bytes()
    results: dict[Path, bytes] = {
        generated / "help-index.bin": _index_payload(topics),
        generated / "license.txt": licence,
        generated / "velocity-corporate-logo.png": logo,
        generated / "velocity-nettools.ico": package_png_as_ico(favicon),
    }
    rc_lines = [
        "// Generated by scripts/build_native_resources.py. Edit docs/help/topics.json.",
        'IDR_HELP_INDEX RCDATA "generated/help-index.bin"',
    ]
    for offset, topic in enumerate(topics):
        resource_id = TOPIC_RESOURCE_BASE + offset
        topic_path = generated / "topics" / f'{topic["id"]}.rtf'
        results[topic_path] = _topic_rtf(topic["title"], topic["blocks"])
        rc_lines.append(f'{resource_id} RCDATA "generated/topics/{topic["id"]}.rtf"')
    results[generated / "help_resources.rcinc"] = ("\n".join(rc_lines) + "\n").encode("ascii")
    return results


def generate(root: Path = ROOT, check: bool = False) -> list[Path]:
    """Generate all native resource inputs, or verify them with check=True."""
    root = Path(root)
    expected = _expected_outputs(root)
    stale = [path for path, content in expected.items() if not path.exists() or path.read_bytes() != content]
    if check:
        if stale:
            relative = ", ".join(str(path.relative_to(root)) for path in stale)
            raise RuntimeError("Generated native resources are stale: " + relative)
        return list(expected)

    for path, content in expected.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)

    topics_dir = root / "resources/generated/topics"
    expected_topic_paths = {path for path in expected if path.parent == topics_dir}
    if topics_dir.exists():
        for path in topics_dir.glob("*.rtf"):
            if path not in expected_topic_paths:
                path.unlink()
    return list(expected)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail when generated resources are stale")
    args = parser.parse_args()
    try:
        generated = generate(ROOT, check=args.check)
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as error:
        raise SystemExit(str(error)) from error
    verb = "Verified" if args.check else "Generated"
    print(f"{verb} {len(generated)} native resource files.")


if __name__ == "__main__":
    main()
