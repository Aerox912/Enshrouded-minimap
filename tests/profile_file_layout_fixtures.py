#!/usr/bin/env python3
"""Build small, clearly synthetic PE fixtures from Minimap's current source guards."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

PAGE = 0x1000
FILE_ALIGNMENT = 0x200
SECTION_ALIGNMENT = 0x1000
PE_OFFSET = 0x80
OPTIONAL_HEADER_SIZE = 0xF0
MAX_SECTIONS = 96
STARTUP_CALL_RVA = 0xFBF573
STARTUP_TARGET_RVA = 0xFE0BE0


@dataclass(frozen=True)
class Signature:
    source: str
    group: str
    name: str
    rva: int
    data: bytes
    executable: bool


def block_after(text: str, marker: str) -> str:
    start = text.index(marker)
    brace = text.index("{", start)
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[brace + 1 : index]
    raise ValueError(f"unclosed source block: {marker}")


def parse_local_arrays(body: str) -> dict[str, bytes]:
    arrays: dict[str, bytes] = {}
    array_pattern = re.compile(
        r"const\s+std::uint8_t\s+(\w+)\s*\[\s*\]\s*=\s*\{(.*?)\};",
        re.DOTALL,
    )
    for match in array_pattern.finditer(body):
        values = re.findall(r"0x([0-9a-fA-F]{1,2})", match.group(2))
        arrays[match.group(1)] = bytes(int(value, 16) for value in values)
    return arrays


def parse_byte_match_calls(body: str) -> list[tuple[int, str]]:
    call_pattern = re.compile(
        r"BytesMatchRva\s*\(\s*(0x[0-9a-fA-F]+)\s*,\s*(\w+)\s*,"
        r"\s*sizeof\s*\(\s*\w+\s*\)\s*\)"
    )
    return [(int(match.group(1), 16), match.group(2)) for match in call_pattern.finditer(body)]


def guard_signatures(source_root: Path, build: str) -> list[Signature]:
    path = source_root / "src" / "client_1_0_guards.h"
    text = path.read_text(encoding="utf-8-sig")
    function_pattern = re.compile(
        rf"\bbool\s+(Match\w+{build})\s*\(\s*\)\s*\{{"
    )
    signatures: list[Signature] = []
    for match in function_pattern.finditer(text):
        function = match.group(1)
        body = block_after(text, match.group(0))
        arrays = parse_local_arrays(body)
        calls = parse_byte_match_calls(body)
        if not calls:
            raise ValueError(f"{function} has no fixed-RVA signature calls")
        for rva, name in calls:
            if name not in arrays or not arrays[name]:
                raise ValueError(f"{function} references missing or empty array {name}")
            is_data = name == "b6" and function.startswith("MatchFog")
            signatures.append(
                Signature(
                    path.name,
                    function.removeprefix("Match").removesuffix(build),
                    f"{function}.{name}",
                    rva,
                    arrays[name],
                    not is_data,
                )
            )
    return signatures


def menu_signatures(source_root: Path, table_name: str) -> list[Signature]:
    path = source_root / "src" / "menu_visibility_1_0.h"
    text = path.read_text(encoding="utf-8-sig")
    body = block_after(text, f"constexpr Guard {table_name}[]")
    entry_pattern = re.compile(
        r'\{\s*(0x[0-9a-fA-F]+)\s*,\s*"([^"]*)"\s*sv\s*\}'
    )
    signatures: list[Signature] = []
    for index, match in enumerate(entry_pattern.finditer(body), start=1):
        raw = bytes(
            int(value, 16)
            for value in re.findall(r"\\x([0-9a-fA-F]{2})", match.group(2))
        )
        if not raw:
            raise ValueError(f"{table_name} contains an empty string signature")
        signatures.append(
            Signature(path.name, "Hud", f"{table_name}[{index}]", int(match.group(1), 16), raw, True)
        )
    return signatures


def vulkan_signatures(source_root: Path) -> list[Signature]:
    path = source_root / "src" / "vulkan_startup.h"
    text = path.read_text(encoding="utf-8-sig")
    marker = "bool MatchVulkanStartupCallsite()"
    body = block_after(text, marker)
    arrays = parse_local_arrays(body)
    signatures: list[Signature] = []
    for rva, name in parse_byte_match_calls(body):
        if name not in arrays or not arrays[name]:
            raise ValueError(f"Vulkan startup references missing or empty array {name}")
        signatures.append(Signature(path.name, "VulkanStartup", name, rva, arrays[name], True))
    if len(signatures) != 3:
        raise ValueError(f"expected three Vulkan startup signatures, got {len(signatures)}")
    return signatures


def collect_signatures(source_root: Path, build: str) -> list[Signature]:
    signatures = guard_signatures(source_root, build)
    menu_table = "currentGuards" if build == "26082781" else "oldGuards"
    signatures += menu_signatures(source_root, menu_table)
    if build == "26082781":
        signatures += vulkan_signatures(source_root)
    return signatures


def signature_metrics(signatures: list[Signature]) -> dict[str, object]:
    bytes_by_rva: dict[int, int] = {}
    for signature in signatures:
        for offset, byte in enumerate(signature.data):
            rva = signature.rva + offset
            previous = bytes_by_rva.get(rva)
            if previous is not None and previous != byte:
                raise ValueError(f"conflicting source signatures at RVA 0x{rva:x}")
            bytes_by_rva[rva] = byte

    spans = 0
    previous_rva: int | None = None
    for rva in sorted(bytes_by_rva):
        if previous_rva is None or rva != previous_rva + 1:
            spans += 1
        previous_rva = rva

    groups: dict[str, int] = {}
    for signature in signatures:
        groups[signature.group] = groups.get(signature.group, 0) + 1
    return {
        "rangeCount": len(signatures),
        "bytesBeforeOverlap": sum(len(signature.data) for signature in signatures),
        "uniqueBytes": len(bytes_by_rva),
        "mergedSpans": spans,
        "groups": groups,
    }


def validate_latest_inventory(signatures: list[Signature]) -> dict[str, object]:
    metrics = signature_metrics(signatures)
    expected_groups = {
        "Camera": 5,
        "Renderer": 5,
        "Markers": 6,
        "Clock": 4,
        "Weather": 4,
        "Fog": 7,
        "World": 4,
        "Death": 1,
        "Visibility": 3,
        "Hud": 9,
        "VulkanStartup": 3,
    }
    if metrics != {
        "rangeCount": 51,
        "bytesBeforeOverlap": 2577,
        "uniqueBytes": 2513,
        "mergedSpans": 46,
        "groups": expected_groups,
    }:
        raise ValueError(f"latest signature inventory differs from design: {metrics}")
    if not any(not signature.executable and signature.rva == 0x13B9EDC for signature in signatures):
        raise ValueError("the read-only fog signature is missing from the inventory")
    return metrics


def render_signature_header(signatures: list[Signature]) -> str:
    metrics = validate_latest_inventory(signatures)
    byte_offset = 0
    rows: list[str] = []
    flattened: list[int] = []
    for signature in signatures:
        rows.append(
            f"    {{ 0x{signature.rva:08x}u, {byte_offset}u, "
            f"{len(signature.data)}u, {'true' if signature.executable else 'false'} }},"
        )
        byte_offset += len(signature.data)
        flattened.extend(signature.data)

    byte_rows = []
    for start in range(0, len(flattened), 16):
        byte_rows.append("    " + ", ".join(f"0x{value:02x}" for value in flattened[start : start + 16]) + ",")

    return "\n".join(
        [
            "#pragma once",
            "#include <cstdint>",
            "",
            "// Generated from the Build26082781 source predicates. Verify with",
            "// tests/profile_file_layout_fixtures.py --check-signature-header.",
            "namespace MinimapClient10 { namespace FileLayout {",
            "struct Signature {",
            "    std::uint32_t rva;",
            "    std::uint16_t byteOffset;",
            "    std::uint16_t byteCount;",
            "    bool executable;",
            "};",
            "inline constexpr std::uint8_t CurrentSignatureBytes[] = {",
            *byte_rows,
            "};",
            "inline constexpr Signature CurrentSignatures[] = {",
            *rows,
            "};",
            "inline constexpr std::uint32_t CurrentSignatureRangeCount =",
            "    static_cast<std::uint32_t>(sizeof(CurrentSignatures) / sizeof(CurrentSignatures[0]));",
            "static_assert(CurrentSignatureRangeCount == 51, \"current profile signature range drift\");",
            "static_assert(sizeof(CurrentSignatureBytes) == 2577, \"current profile signature byte drift\");",
            "} } // namespace MinimapClient10::FileLayout",
            "",
        ]
    )


def check_signature_header(source_root: Path, header_path: Path, *, write: bool = False) -> None:
    signatures = collect_signatures(source_root, "26082781")
    expected = render_signature_header(signatures)
    if write:
        header_path.write_text(expected, encoding="utf-8", newline="\n")
        return
    actual = header_path.read_text(encoding="utf-8")
    if actual != expected:
        raise ValueError(
            "generated current file-signature catalogue differs from client guards; "
            "regenerate it only after reviewing the accepted source predicates"
        )


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def write_u16(buffer: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<H", buffer, offset, value)


def write_u32(buffer: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", buffer, offset, value)


def page_runs(signatures: list[Signature], extra_rvas: tuple[int, ...] = ()) -> list[tuple[int, int]]:
    pages: set[int] = set()
    for signature in signatures:
        first = signature.rva & ~(PAGE - 1)
        end = align_up(signature.rva + len(signature.data), PAGE)
        pages.update(range(first, end, PAGE))
    for rva in extra_rvas:
        pages.add(rva & ~(PAGE - 1))
    if not pages:
        pages.add(PAGE)
    runs: list[tuple[int, int]] = []
    for page in sorted(pages):
        if runs and page == runs[-1][0] + runs[-1][1]:
            start, size = runs[-1]
            runs[-1] = (start, size + PAGE)
        else:
            runs.append((page, PAGE))
    return runs


def build_pe(
    signatures: list[Signature], include_startup_call: bool
) -> tuple[bytearray, list[dict[str, int]], dict[str, int]]:
    extra_rvas = (STARTUP_CALL_RVA,) if include_startup_call else ()
    runs = page_runs(signatures, extra_rvas)
    if len(runs) > MAX_SECTIONS:
        raise ValueError(f"synthetic PE needs too many sections: {len(runs)}")

    section_table = PE_OFFSET + 4 + 20 + OPTIONAL_HEADER_SIZE
    headers_size = align_up(section_table + len(runs) * 40, PAGE)
    image_end = max(start + size for start, size in runs)
    size_of_image = align_up(image_end + 0x10000, SECTION_ALIGNMENT)
    sections: list[dict[str, int]] = []
    raw_cursor = headers_size
    for index, (virtual_address, size) in enumerate(runs):
        has_code = any(
            signature.executable
            and signature.rva < virtual_address + size
            and signature.rva + len(signature.data) > virtual_address
            for signature in signatures
        )
        characteristics = 0x40000040 | (0x20000000 if has_code else 0)
        header = section_table + index * 40
        sections.append(
            {
                "header": header,
                "virtual_address": virtual_address,
                "virtual_size": size,
                "raw_size": size,
                "raw_offset": raw_cursor,
                "characteristics": characteristics,
            }
        )
        raw_cursor += size

    image = bytearray(raw_cursor)
    image[0:2] = b"MZ"
    write_u32(image, 0x3C, PE_OFFSET)
    image[PE_OFFSET : PE_OFFSET + 4] = b"PE\0\0"
    coff = PE_OFFSET + 4
    write_u16(image, coff, 0x8664)
    write_u16(image, coff + 2, len(sections))
    write_u32(image, coff + 4, 0x12345678)
    write_u16(image, coff + 16, OPTIONAL_HEADER_SIZE)
    write_u16(image, coff + 18, 0x0022)

    optional = PE_OFFSET + 24
    write_u16(image, optional, 0x020B)
    image[optional + 2] = 14
    write_u32(image, optional + 16, runs[0][0])
    struct.pack_into("<Q", image, optional + 24, 0x140000000)
    write_u32(image, optional + 32, SECTION_ALIGNMENT)
    write_u32(image, optional + 36, FILE_ALIGNMENT)
    write_u16(image, optional + 40, 6)
    write_u16(image, optional + 48, 6)
    write_u32(image, optional + 56, size_of_image)
    write_u32(image, optional + 60, headers_size)
    write_u16(image, optional + 68, 3)
    write_u32(image, optional + 108, 16)

    for index, section in enumerate(sections):
        header = section["header"]
        name = f".tx{index:04d}" if section["characteristics"] & 0x20000000 else f".rd{index:04d}"
        image[header : header + 8] = name.encode("ascii").ljust(8, b"\0")
        write_u32(image, header + 8, section["virtual_size"])
        write_u32(image, header + 12, section["virtual_address"])
        write_u32(image, header + 16, section["raw_size"])
        write_u32(image, header + 20, section["raw_offset"])
        write_u32(image, header + 36, section["characteristics"])

    def rva_to_file(rva: int, length: int) -> int:
        matches = [
            section
            for section in sections
            if rva >= section["virtual_address"]
            and rva + length <= section["virtual_address"] + section["virtual_size"]
            and rva + length <= section["virtual_address"] + section["raw_size"]
        ]
        if len(matches) != 1:
            raise ValueError(f"fixture RVA 0x{rva:x} has {len(matches)} sections")
        section = matches[0]
        return section["raw_offset"] + rva - section["virtual_address"]

    for signature in signatures:
        offset = rva_to_file(signature.rva, len(signature.data))
        image[offset : offset + len(signature.data)] = signature.data
    if include_startup_call:
        displacement = STARTUP_TARGET_RVA - (STARTUP_CALL_RVA + 5)
        offset = rva_to_file(STARTUP_CALL_RVA, 5)
        image[offset : offset + 5] = b"\xE8" + struct.pack("<i", displacement)

    offsets = {"pe": PE_OFFSET, "optional": optional, "section_table": section_table}
    return image, sections, offsets


def rva_to_file(sections: list[dict[str, int]], rva: int, length: int) -> int:
    matches = [
        section
        for section in sections
        if rva >= section["virtual_address"]
        and rva + length <= section["virtual_address"] + section["virtual_size"]
        and rva + length <= section["virtual_address"] + section["raw_size"]
    ]
    if len(matches) != 1:
        raise ValueError(f"fixture RVA 0x{rva:x} has {len(matches)} sections")
    section = matches[0]
    return section["raw_offset"] + rva - section["virtual_address"]


def add_case(
    output: Path,
    cases: list[tuple[str, str]],
    filename: str,
    data: bytes | bytearray,
    expected: str,
) -> None:
    (output / filename).write_bytes(data)
    cases.append((filename, expected))


def make_cases(source_root: Path, output: Path) -> dict[str, object]:
    output.mkdir(parents=True, exist_ok=True)
    if any(output.iterdir()):
        raise FileExistsError(f"refusing to overwrite non-empty fixture directory: {output}")

    latest = collect_signatures(source_root, "26082781")
    metrics = validate_latest_inventory(latest)
    legacy = collect_signatures(source_root, "25988036")
    base, sections, offsets = build_pe(latest, include_startup_call=True)
    cases: list[tuple[str, str]] = []

    add_case(output, cases, "synthetic-current-layout.pe", base, "current")

    patched = bytearray(base)
    write_u32(patched, offsets["pe"] + 8, 0xD1CEB00C)
    write_u32(patched, offsets["optional"] + 64, 0xA17E57ED)
    protected = {
        signature.rva + index
        for signature in latest
        for index in range(len(signature.data))
    }
    protected.update(range(STARTUP_CALL_RVA, STARTUP_CALL_RVA + 5))
    outside_offset = None
    for section in sections:
        if not section["characteristics"] & 0x20000000:
            continue
        for delta in range(section["raw_size"]):
            rva = section["virtual_address"] + delta
            if rva not in protected:
                outside_offset = section["raw_offset"] + delta
                break
        if outside_offset is not None:
            break
    if outside_offset is None:
        raise ValueError("synthetic code sections have no byte outside the interface ranges")
    patched[outside_offset] ^= 0x80
    add_case(output, cases, "synthetic-unrelated-patcher-edits.pe", patched, "current")

    for index, signature in enumerate(latest, start=1):
        changed = bytearray(base)
        rva = signature.rva + len(signature.data) // 2
        changed[rva_to_file(sections, rva, 1)] ^= 1
        add_case(
            output,
            cases,
            f"synthetic-protected-range-{index:02d}.pe",
            changed,
            "unsupported",
        )

    call_offset = rva_to_file(sections, STARTUP_CALL_RVA, 5)
    changed_call = bytearray(base)
    changed_call[call_offset + 4] ^= 1
    add_case(output, cases, "synthetic-startup-call-target-change.pe", changed_call, "unsupported")
    changed_call = bytearray(base)
    changed_call[call_offset] = 0xE9
    add_case(output, cases, "synthetic-startup-call-opcode-change.pe", changed_call, "unsupported")

    unsupported, _, _ = build_pe([], include_startup_call=False)
    add_case(output, cases, "synthetic-unsupported-profile.pe", unsupported, "unsupported")
    old_profile, _, _ = build_pe(legacy, include_startup_call=False)
    add_case(output, cases, "synthetic-legacy-signatures-only.pe", old_profile, "unsupported")

    shifted = bytearray(base)
    moved_section = next(
        section
        for section in sections
        if section["characteristics"] & 0x20000000
        and any(
            signature.executable
            and signature.rva >= section["virtual_address"]
            and signature.rva < section["virtual_address"] + section["virtual_size"]
            for signature in latest
        )
    )
    max_end = max(section["virtual_address"] + section["virtual_size"] for section in sections)
    moved_va = align_up(max_end, SECTION_ALIGNMENT)
    if moved_va + moved_section["virtual_size"] > struct.unpack_from(
        "<I", shifted, offsets["optional"] + 56
    )[0]:
        raise ValueError("synthetic image has no free space for a shifted-RVA control")
    write_u32(shifted, moved_section["header"] + 12, moved_va)
    add_case(output, cases, "synthetic-shifted-fixed-rva.pe", shifted, "unsupported")

    malformed = bytearray(base)
    malformed[0] = 0
    add_case(output, cases, "synthetic-invalid-dos-signature.pe", malformed, "unsupported")

    malformed = bytearray(base)
    write_u32(malformed, 0x3C, 0xFFFFFFF0)
    add_case(output, cases, "synthetic-overflow-dos-offset.pe", malformed, "unsupported")

    malformed = bytearray(base)
    malformed[offsets["pe"]] = 0
    add_case(output, cases, "synthetic-invalid-pe-signature.pe", malformed, "unsupported")

    malformed = bytearray(base)
    write_u16(malformed, offsets["pe"] + 4, 0x014C)
    add_case(output, cases, "synthetic-wrong-machine.pe", malformed, "unsupported")

    malformed = bytearray(base)
    write_u16(malformed, offsets["optional"], 0x010B)
    add_case(output, cases, "synthetic-pe32-not-pe32plus.pe", malformed, "unsupported")

    malformed = bytearray(base)
    write_u16(malformed, offsets["pe"] + 20, 0x0060)
    add_case(output, cases, "synthetic-truncated-optional-header.pe", malformed, "unsupported")

    malformed = bytearray(base)
    write_u16(malformed, offsets["pe"] + 6, 0)
    add_case(output, cases, "synthetic-no-sections.pe", malformed, "unsupported")

    malformed = bytearray(base)
    write_u16(malformed, offsets["pe"] + 6, MAX_SECTIONS + 1)
    add_case(output, cases, "synthetic-section-count-over-limit.pe", malformed, "unsupported")

    malformed = bytearray(base)
    write_u32(malformed, offsets["optional"] + 60, 0x200)
    add_case(output, cases, "synthetic-headers-do-not-cover-table.pe", malformed, "unsupported")

    malformed = bytearray(base)
    write_u32(malformed, offsets["optional"] + 56, PAGE)
    add_case(output, cases, "synthetic-image-size-too-small.pe", malformed, "unsupported")

    malformed = bytearray(base)
    write_u32(malformed, offsets["optional"] + 32, 0x3000)
    add_case(output, cases, "synthetic-invalid-section-alignment.pe", malformed, "unsupported")

    malformed = bytearray(base)
    write_u32(malformed, offsets["optional"] + 36, 0x300)
    add_case(output, cases, "synthetic-invalid-file-alignment.pe", malformed, "unsupported")

    malformed = bytearray(base)
    second = sections[1]
    write_u32(malformed, second["header"] + 12, sections[0]["virtual_address"])
    add_case(output, cases, "synthetic-overlapping-virtual-sections.pe", malformed, "unsupported")

    malformed = bytearray(base)
    write_u32(malformed, sections[1]["header"] + 20, sections[0]["raw_offset"])
    add_case(output, cases, "synthetic-overlapping-raw-sections.pe", malformed, "unsupported")

    malformed = bytearray(base)
    write_u32(malformed, sections[0]["header"] + 20, 0xFFFFFFF0)
    add_case(output, cases, "synthetic-overflowing-raw-range.pe", malformed, "unsupported")

    malformed = bytearray(base)
    write_u32(malformed, sections[0]["header"] + 12, 0xFFFFFFF0)
    add_case(output, cases, "synthetic-overflowing-virtual-range.pe", malformed, "unsupported")

    last_section = max(sections, key=lambda section: section["raw_offset"] + section["raw_size"])
    truncated = base[: last_section["raw_offset"] + last_section["raw_size"] - 1]
    add_case(output, cases, "synthetic-truncated-raw-section.pe", truncated, "unsupported")

    malformed = bytearray(base)
    code_signature = next(signature for signature in latest if signature.executable)
    code_section = next(
        section
        for section in sections
        if code_signature.rva >= section["virtual_address"]
        and code_signature.rva < section["virtual_address"] + section["virtual_size"]
    )
    write_u32(
        malformed,
        code_section["header"] + 36,
        code_section["characteristics"] & ~0x40000000,
    )
    add_case(output, cases, "synthetic-signature-section-not-readable.pe", malformed, "unsupported")

    malformed = bytearray(base)
    write_u32(
        malformed,
        code_section["header"] + 36,
        code_section["characteristics"] & ~0x20000000,
    )
    add_case(output, cases, "synthetic-code-signature-section-not-executable.pe", malformed, "unsupported")

    data_signature = next(signature for signature in latest if not signature.executable)
    data_section = next(
        section
        for section in sections
        if data_signature.rva >= section["virtual_address"]
        and data_signature.rva < section["virtual_address"] + section["virtual_size"]
    )
    malformed = bytearray(base)
    write_u32(
        malformed,
        data_section["header"] + 36,
        data_section["characteristics"] & ~0x40000000,
    )
    add_case(output, cases, "synthetic-data-signature-section-not-readable.pe", malformed, "unsupported")

    signature_section = next(
        section
        for section in sections
        if latest[0].rva >= section["virtual_address"]
        and latest[0].rva < section["virtual_address"] + section["virtual_size"]
    )
    delta = latest[0].rva - signature_section["virtual_address"]
    minimum_raw = align_up(delta + len(latest[0].data), FILE_ALIGNMENT) - FILE_ALIGNMENT
    if minimum_raw < delta + len(latest[0].data):
        minimum_raw -= FILE_ALIGNMENT
    malformed = bytearray(base)
    write_u32(malformed, signature_section["header"] + 16, max(FILE_ALIGNMENT, minimum_raw))
    add_case(output, cases, "synthetic-signature-not-raw-backed.pe", malformed, "unsupported")

    truncated_headers = base[: offsets["section_table"] + len(sections) * 40 - 1]
    add_case(output, cases, "synthetic-truncated-section-table.pe", truncated_headers, "unsupported")

    malformed = bytearray(base)
    write_u16(malformed, offsets["pe"] + 20, 0xFFFF)
    add_case(output, cases, "synthetic-optional-header-out-of-file.pe", malformed, "unsupported")

    cases_path = output / "cases.txt"
    cases_path.write_text(
        "".join(f"{filename}|{expected}\n" for filename, expected in cases),
        encoding="ascii",
        newline="\n",
    )
    coverage = {
        "fixtureType": "synthetic-only; not a game executable",
        "latest": metrics,
        "legacyRangeCount": len(legacy),
        "signatures": [
            {
                "source": signature.source,
                "group": signature.group,
                "name": signature.name,
                "rva": f"0x{signature.rva:08x}",
                "length": len(signature.data),
                "sha256": hashlib.sha256(signature.data).hexdigest(),
                "kind": "code" if signature.executable else "data",
            }
            for signature in latest
        ],
        "caseCount": len(cases),
        "currentExpected": sum(expected == "current" for _, expected in cases),
        "unsupportedExpected": sum(expected == "unsupported" for _, expected in cases),
    }
    (output / "signature-coverage.json").write_text(
        json.dumps(coverage, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    return coverage


def make_followup_cases(source_root: Path, output: Path) -> dict[str, object]:
    output.mkdir(parents=True, exist_ok=True)
    if any(output.iterdir()):
        raise FileExistsError(f"refusing to overwrite non-empty fixture directory: {output}")

    latest = collect_signatures(source_root, "26082781")
    metrics = validate_latest_inventory(latest)
    base, sections, offsets = build_pe(latest, include_startup_call=True)
    cases: list[tuple[str, str]] = []

    add_case(output, cases, "synthetic-current-layout.pe", base, "current")

    unrelated = bytearray(base)
    write_u32(unrelated, offsets["pe"] + 8, 0xD1CEB00C)
    write_u32(unrelated, offsets["optional"] + 64, 0xA17E57ED)
    protected = {
        signature.rva + index
        for signature in latest
        for index in range(len(signature.data))
    }
    protected.update(range(STARTUP_CALL_RVA, STARTUP_CALL_RVA + 5))
    unrelated_file_offset = None
    for section in sections:
        if not section["characteristics"] & 0x20000000:
            continue
        for delta in range(section["raw_size"]):
            if section["virtual_address"] + delta not in protected:
                unrelated_file_offset = section["raw_offset"] + delta
                break
        if unrelated_file_offset is not None:
            break
    if unrelated_file_offset is None:
        raise ValueError("synthetic code sections have no unprotected byte")
    unrelated[unrelated_file_offset] ^= 0x80
    add_case(output, cases, "synthetic-unrelated-layout-edit.pe", unrelated, "current")

    low_alignment_mismatch = bytearray(base)
    write_u32(low_alignment_mismatch, offsets["optional"] + 32, FILE_ALIGNMENT)
    add_case(output, cases, "synthetic-subpage-raw-rva-mismatch.pe", low_alignment_mismatch, "unsupported")

    # PE/COFF sub-page layouts keep each raw section at the same file offset as its RVA.
    identity_size = max(section["virtual_address"] + section["raw_size"] for section in sections)
    if identity_size > 128 * 1024 * 1024:
        raise ValueError("sub-page identity control would exceed the profile reader's file bound")
    low_alignment_identity = bytearray(identity_size)
    headers_size = sections[0]["raw_offset"]
    low_alignment_identity[:headers_size] = base[:headers_size]
    for section in sections:
        source_start = section["raw_offset"]
        source_end = source_start + section["raw_size"]
        target_start = section["virtual_address"]
        target_end = target_start + section["raw_size"]
        low_alignment_identity[target_start:target_end] = base[source_start:source_end]
        write_u32(low_alignment_identity, section["header"] + 20, section["virtual_address"])
    write_u32(low_alignment_identity, offsets["optional"] + 32, FILE_ALIGNMENT)
    add_case(output, cases, "synthetic-subpage-raw-rva-identity.pe", low_alignment_identity, "current")

    optional_size = int.from_bytes(base[offsets["pe"] + 20 : offsets["pe"] + 22], "little")
    directory_capacity = (optional_size - 112) // 8
    at_capacity = bytearray(base)
    write_u32(at_capacity, offsets["optional"] + 108, directory_capacity)
    add_case(output, cases, "synthetic-directory-count-at-capacity.pe", at_capacity, "current")

    over_capacity = bytearray(base)
    write_u32(over_capacity, offsets["optional"] + 108, directory_capacity + 1)
    add_case(output, cases, "synthetic-directory-count-over-capacity.pe", over_capacity, "unsupported")

    boundary_signature = None
    boundary_raw_size = 0
    for signature in latest:
        section_delta = signature.rva & (PAGE - 1)
        signature_end = section_delta + len(signature.data)
        candidate_raw_size = ((signature_end - 1) // FILE_ALIGNMENT) * FILE_ALIGNMENT
        if section_delta <= candidate_raw_size < signature_end:
            boundary_signature = signature
            boundary_raw_size = candidate_raw_size
            break
    if boundary_signature is None:
        raise ValueError("no current signature crosses a file-aligned raw boundary")
    boundary_section = next(
        section for section in sections
        if boundary_signature.rva >= section["virtual_address"]
        and boundary_signature.rva < section["virtual_address"] + section["virtual_size"]
    )
    raw_boundary = bytearray(base)
    write_u32(raw_boundary, boundary_section["header"] + 16, boundary_raw_size)
    add_case(output, cases, "synthetic-signature-crosses-raw-boundary.pe", raw_boundary, "unsupported")

    truncated_optional = bytearray(base)
    write_u16(truncated_optional, offsets["pe"] + 20, 0x0060)
    add_case(output, cases, "synthetic-truncated-optional-header.pe", truncated_optional, "unsupported")

    optional_out_of_file = bytearray(base)
    write_u16(optional_out_of_file, offsets["pe"] + 20, 0xFFFF)
    add_case(output, cases, "synthetic-optional-header-out-of-file.pe", optional_out_of_file, "unsupported")

    raw_overflow = bytearray(base)
    first_section = sections[0]
    write_u32(raw_overflow, first_section["header"] + 20, 0xFFFFFE00)
    write_u32(raw_overflow, first_section["header"] + 16, 0x0200)
    add_case(output, cases, "synthetic-aligned-raw-end-overflow.pe", raw_overflow, "unsupported")

    virtual_overflow = bytearray(base)
    write_u32(virtual_overflow, first_section["header"] + 12, 0xFFFFF000)
    write_u32(virtual_overflow, first_section["header"] + 8, PAGE)
    write_u32(virtual_overflow, first_section["header"] + 16, PAGE)
    write_u32(virtual_overflow, offsets["optional"] + 56, 0xFFFFF000)
    add_case(output, cases, "synthetic-aligned-virtual-end-overflow.pe", virtual_overflow, "unsupported")

    (output / "cases.txt").write_text(
        "".join(f"{filename}|{expected}\n" for filename, expected in cases),
        encoding="ascii",
        newline="\n",
    )
    coverage = {
        "fixtureType": "synthetic-only; not a game executable",
        "followupFixOnly": True,
        "latest": metrics,
        "directoryCapacity": directory_capacity,
        "caseCount": len(cases),
        "currentExpected": sum(expected == "current" for _, expected in cases),
        "unsupportedExpected": sum(expected == "unsupported" for _, expected in cases),
        "cases": [{"name": filename, "expected": expected} for filename, expected in cases],
    }
    (output / "followup-coverage.json").write_text(
        json.dumps(coverage, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    return coverage


def main() -> int:
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("source_root", type=Path)
    parser.add_argument("output_directory", type=Path)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check-signature-header", type=Path)
    mode.add_argument("--write-signature-header", type=Path)
    parser.add_argument("--followup-controls", action="store_true")
    args = parser.parse_args()
    source_root = args.source_root.resolve()
    output = args.output_directory.resolve()
    try:
        if args.check_signature_header:
            check_signature_header(source_root, args.check_signature_header.resolve())
        elif args.write_signature_header:
            check_signature_header(source_root, args.write_signature_header.resolve(), write=True)
        coverage = make_followup_cases(source_root, output) if args.followup_controls else make_cases(source_root, output)
    except (OSError, ValueError, struct.error) as error:
        print(f"fixture generation failed: {error}", file=sys.stderr)
        return 1
    print(
        "synthetic fixtures: "
        f"cases={coverage['caseCount']} signatures={coverage['latest']['rangeCount']} "
        f"bytes={coverage['latest']['bytesBeforeOverlap']} "
        f"unique={coverage['latest']['uniqueBytes']} spans={coverage['latest']['mergedSpans']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
