#!/usr/bin/env python3
from __future__ import annotations

import argparse
import pathlib
import re
from typing import Iterable

from fontTools import subset


ROOT = pathlib.Path(__file__).resolve().parents[1]
PROJECT_DIR = ROOT / "Borderlands4"
DEFAULT_MAIN_FONT = PROJECT_DIR / "Fonts" / "MapleMonoNormalNL-Regular.ttf"
DEFAULT_FALLBACK_FONT = PROJECT_DIR / "Fonts" / "MapleMono-NF-CN-Regular.ttf"
DEFAULT_OUTPUT_DIR = PROJECT_DIR / "Fonts" / "Subset"
DEFAULT_EMBED_DIR = PROJECT_DIR / "Fonts" / "Embedded"

SCAN_SUFFIXES = {".h", ".hpp", ".cpp", ".cxx", ".inl", ".md", ".txt"}
SKIP_PARTS = {"ImGui", "imgui", "Borderlands4_SDK", "x64", ".git", "build"}


STRING_RE = re.compile(r'(?:u8)?\"((?:[^\"\\]|\\.)*)\"')
CPP_ESCAPE_REPLACEMENTS = {
    r"\\n": "\n",
    r"\\r": "\r",
    r"\\t": "\t",
    r'\\\"': '"',
    r"\\\\": "\\",
}


def decode_cpp_string(value: str) -> str:
    for src, dst in CPP_ESCAPE_REPLACEMENTS.items():
        value = value.replace(src, dst)
    return value


def iter_project_texts(project_dir: pathlib.Path) -> Iterable[str]:
    for path in project_dir.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in SCAN_SUFFIXES:
            continue
        if any(part in SKIP_PARTS for part in path.parts):
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        for match in STRING_RE.finditer(text):
            decoded = decode_cpp_string(match.group(1))
            if decoded:
                yield decoded


def classify_chars(strings: Iterable[str]) -> tuple[str, str]:
    main_chars = set(chr(c) for c in range(0x20, 0x7F))
    main_chars.update("\n\r\t")
    fallback_chars = set()

    for text in strings:
        for ch in text:
            code = ord(ch)
            if code < 0x20:
                continue
            if (
                0x4E00 <= code <= 0x9FFF
                or 0x3400 <= code <= 0x4DBF
                or 0x3000 <= code <= 0x303F
                or 0xFF00 <= code <= 0xFFEF
            ):
                fallback_chars.add(ch)
            else:
                main_chars.add(ch)

    main_chars.update("▲▼◀▶•●○◆◇★☆→←↑↓×✓✕")
    fallback_chars.update("，。！？：【】（）《》、“”‘’")
    return "".join(sorted(main_chars)), "".join(sorted(fallback_chars))


def subset_font(input_path: pathlib.Path, output_path: pathlib.Path, text: str, keep_layout: bool = False) -> None:
    options = subset.Options()
    options.flavor = None
    options.layout_features = ["*"] if keep_layout else ["liga", "calt", "kern", "locl"]
    options.name_IDs = ["*"]
    options.name_legacy = True
    options.name_languages = ["*"]
    options.notdef_glyph = True
    options.notdef_outline = True
    options.recommended_glyphs = True
    options.hinting = True
    options.legacy_cmap = True
    options.symbol_cmap = True
    options.desubroutinize = False
    options.drop_tables = []
    options.ignore_missing_glyphs = True
    options.ignore_missing_unicodes = True

    font = subset.load_font(str(input_path), options)
    subsetter = subset.Subsetter(options=options)
    subsetter.populate(text=text)
    subsetter.subset(font)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    subset.save_font(font, str(output_path), options)


def bytes_to_cpp_array(data: bytes, array_name: str) -> str:
    lines = []
    for i in range(0, len(data), 12):
        chunk = data[i:i + 12]
        lines.append(", ".join(f"0x{b:02X}" for b in chunk))
    body = ",\n    ".join(lines)
    return (
        f"const unsigned char {array_name}[] = {{\n"
        f"    {body}\n"
        f"}};\n"
        f"const unsigned int {array_name}_size = {len(data)};\n"
    )


def emit_embedded_pair(input_font: pathlib.Path, header_path: pathlib.Path, cpp_path: pathlib.Path, symbol_name: str) -> None:
    data = input_font.read_bytes()
    header_path.parent.mkdir(parents=True, exist_ok=True)
    header_path.write_text(
        "#pragma once\n\n"
        f"extern const unsigned char {symbol_name}[];\n"
        f"extern const unsigned int {symbol_name}_size;\n",
        encoding="utf-8",
    )
    cpp_path.write_text(
        '#include "pch.h"\n'
        f'#include "Fonts/Embedded/{header_path.name}"\n\n'
        + bytes_to_cpp_array(data, symbol_name),
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Subset project fonts and emit embeddable C++ resources.")
    parser.add_argument("--main-font", type=pathlib.Path, default=DEFAULT_MAIN_FONT)
    parser.add_argument("--fallback-font", type=pathlib.Path, default=DEFAULT_FALLBACK_FONT)
    parser.add_argument("--output-dir", type=pathlib.Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--embed-dir", type=pathlib.Path, default=DEFAULT_EMBED_DIR)
    args = parser.parse_args()

    strings = list(iter_project_texts(PROJECT_DIR))
    main_chars, fallback_chars = classify_chars(strings)

    main_subset = args.output_dir / "MapleMonoMain-Subset.ttf"
    fallback_subset = args.output_dir / "MapleMonoCN-Subset.ttf"
    subset_font(args.main_font, main_subset, main_chars, keep_layout=False)
    subset_font(args.fallback_font, fallback_subset, fallback_chars, keep_layout=False)

    emit_embedded_pair(
        main_subset,
        args.embed_dir / "EmbeddedFontMain.h",
        args.embed_dir / "EmbeddedFontMain.cpp",
        "g_EmbeddedFontMain",
    )
    emit_embedded_pair(
        fallback_subset,
        args.embed_dir / "EmbeddedFontFallback.h",
        args.embed_dir / "EmbeddedFontFallback.cpp",
        "g_EmbeddedFontFallback",
    )

    manifest = args.output_dir / "font_subset_manifest.txt"
    manifest.write_text(
        "\n".join([
            f"main_font={args.main_font}",
            f"fallback_font={args.fallback_font}",
            f"main_subset={main_subset}",
            f"fallback_subset={fallback_subset}",
            f"main_char_count={len(main_chars)}",
            f"fallback_char_count={len(fallback_chars)}",
        ]) + "\n",
        encoding="utf-8",
    )

    print(f"Wrote {main_subset}")
    print(f"Wrote {fallback_subset}")
    print(f"Wrote {args.embed_dir / 'EmbeddedFontMain.h'}")
    print(f"Wrote {args.embed_dir / 'EmbeddedFontMain.cpp'}")
    print(f"Wrote {args.embed_dir / 'EmbeddedFontFallback.h'}")
    print(f"Wrote {args.embed_dir / 'EmbeddedFontFallback.cpp'}")
    print(f"Wrote {manifest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
