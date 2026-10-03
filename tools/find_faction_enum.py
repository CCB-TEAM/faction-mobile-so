#!/usr/bin/env python3
"""Locate KARDS' EFactionEnum reflection structures in an Android arm64 libUnreal.so.

The enum's name-pointer array lives in .data.rel.ro, whose pointer slots are
ZERO in the file -- the real values exist only as R_AARCH64_RELATIVE entries in
.rela.dyn. Scanning the file for pointers therefore finds nothing; this tool
reconstructs the array through the relocation table.

Outputs the Enumerator[13] array, the FEnumParams that owns it, and every
string address, so the profile JSON can be regenerated after a game update.

Pure stdlib -- no capstone / pyelftools required.

Usage:
    python3 find_faction_enum.py libUnreal.so [--json out.json] [--prefix EFactionEnum]
"""
from __future__ import annotations

import argparse
import json
import struct
import sys

ENUM_PREFIX_DEFAULT = "EFactionEnum"
PT_LOAD = 1
SHT_RELA = 4
R_AARCH64_RELATIVE = 1027


class Elf:
    def __init__(self, path: str):
        with open(path, "rb") as handle:
            self.data = handle.read()
        if self.data[:4] != b"\x7fELF":
            raise ValueError("not an ELF file")
        if self.data[4] != 2 or self.data[5] != 1:
            raise ValueError("expected ELF64 little-endian")
        self._parse_program_headers()
        self._parse_section_headers()

    def _u(self, fmt: str, offset: int):
        return struct.unpack_from(fmt, self.data, offset)

    def _parse_program_headers(self) -> None:
        e_phoff, = self._u("<Q", 0x20)
        e_phentsize, e_phnum = self._u("<HH", 0x36)
        self.loads: list[tuple[int, int, int, int]] = []  # fileoff, vaddr, filesz, flags
        for i in range(e_phnum):
            base = e_phoff + i * e_phentsize
            p_type, p_flags = self._u("<II", base)
            if p_type != PT_LOAD:
                continue
            p_offset, p_vaddr, _p_paddr, p_filesz, _p_memsz = self._u("<QQQQQ", base + 8)
            self.loads.append((p_offset, p_vaddr, p_filesz, p_flags))

    def _parse_section_headers(self) -> None:
        e_shoff, = self._u("<Q", 0x28)
        e_shentsize, e_shnum, e_shstrndx = self._u("<HHH", 0x3A)
        raw = []
        for i in range(e_shnum):
            base = e_shoff + i * e_shentsize
            name, s_type, flags, addr, offset, size, link, info, align, entsize = self._u(
                "<IIQQQQIIQQ", base
            )
            raw.append(dict(name_off=name, type=s_type, flags=flags, addr=addr,
                            offset=offset, size=size, link=link, info=info,
                            align=align, entsize=entsize))
        strtab = raw[e_shstrndx]
        self.sections: dict[str, dict] = {}
        for section in raw:
            start = strtab["offset"] + section["name_off"]
            end = self.data.find(b"\x00", start)
            name = self.data[start:end].decode("ascii", "replace")
            self.sections[name] = section

    def off_to_vaddr(self, offset: int) -> int | None:
        for file_off, vaddr, filesz, _flags in self.loads:
            if file_off <= offset < file_off + filesz:
                return vaddr + (offset - file_off)
        return None

    def vaddr_to_off(self, vaddr: int) -> int | None:
        for file_off, seg_vaddr, filesz, _flags in self.loads:
            if seg_vaddr <= vaddr < seg_vaddr + filesz:
                return file_off + (vaddr - seg_vaddr)
        return None

    def read_cstr(self, vaddr: int, limit: int = 128) -> str | None:
        offset = self.vaddr_to_off(vaddr)
        if offset is None:
            return None
        end = self.data.find(b"\x00", offset, offset + limit)
        if end < 0:
            return None
        try:
            return self.data[offset:end].decode("ascii")
        except UnicodeDecodeError:
            return None

    def relocations(self) -> list[tuple[int, int, int]]:
        """All Elf64_Rela entries as (r_offset, r_type, r_addend)."""
        section = self.sections.get(".rela.dyn")
        if section is None:
            return []
        base, size = section["offset"], section["size"]
        out = []
        for r_offset, r_info, r_addend in struct.iter_unpack("<QQq", self.data[base:base + size]):
            out.append((r_offset, r_info & 0xFFFFFFFF, r_addend))
        return out


def find_strings(elf: Elf, prefix: str) -> dict[int, str]:
    """Scan readable segments for '<prefix>::Name' strings -> {vaddr: string}."""
    needle = prefix.encode()
    found: dict[int, str] = {}
    for file_off, vaddr, filesz, flags in elf.loads:
        if not flags & 4:  # R
            continue
        blob = elf.data[file_off:file_off + filesz]
        cursor = 0
        while True:
            cursor = blob.find(needle, cursor)
            if cursor < 0:
                break
            end = blob.find(b"\x00", cursor, cursor + 96)
            if end > 0:
                try:
                    text = blob[cursor:end].decode("ascii")
                except UnicodeDecodeError:
                    text = ""
                suffix = text[len(prefix) + 2:]
                if text.startswith(prefix + "::") and suffix.isalnum():
                    found[vaddr + cursor] = text
            cursor += 1
    return found


def find_enumerator_array(elf: Elf, relocs, string_vaddrs: dict[int, str]):
    """Return (array_vaddr, [(name, value, slot_vaddr), ...]) or None."""
    slots = {r_offset: r_addend for r_offset, r_type, r_addend in relocs
             if r_type == R_AARCH64_RELATIVE and r_addend in string_vaddrs}
    if not slots:
        return None
    ordered = sorted(slots)
    run = [ordered[0]]
    runs = []
    for prev, nxt in zip(ordered, ordered[1:]):
        if nxt - prev == 16:
            run.append(nxt)
        else:
            runs.append(run)
            run = [nxt]
    runs.append(run)
    # Longest run is the enum; UE also emits other name arrays, so prefer the
    # one whose entries all decode as '<prefix>::Something'.
    best = max(runs, key=len)
    entries = []
    for slot in best:
        offset = elf.vaddr_to_off(slot + 8)
        value = struct.unpack_from("<q", elf.data, offset)[0]
        entries.append((string_vaddrs[slots[slot]], value, slot))
    return best[0], entries


def find_params(elf: Elf, relocs, array_vaddr: int):
    """Find the FEnumParams whose `entries` field points at array_vaddr."""
    for r_offset, r_type, r_addend in relocs:
        if r_type == R_AARCH64_RELATIVE and r_addend == array_vaddr:
            return r_offset - 32  # entries sits at struct offset 32
    return None


def read_params(elf: Elf, relocs, params_vaddr: int) -> dict:
    reloc_map = {r_offset: r_addend for r_offset, r_type, r_addend in relocs
                 if r_type == R_AARCH64_RELATIVE}
    offset = elf.vaddr_to_off(params_vaddr)
    raw = elf.data[offset:offset + 48]

    def ptr(field_offset: int) -> int:
        slot = params_vaddr + field_offset
        if slot in reloc_map:
            return reloc_map[slot]
        return struct.unpack_from("<Q", raw, field_offset)[0]

    flags, = struct.unpack_from("<I", raw, 40)
    count, = struct.unpack_from("<h", raw, 44)
    outer, display, name, cpp_type, entries = (ptr(o) for o in (0, 8, 16, 24, 32))
    return {
        "vaddr": params_vaddr,
        "outer": outer,
        "outer_str": elf.read_cstr(outer),
        "display": display,
        "name": name,
        "name_str": elf.read_cstr(name),
        "cpp_type": cpp_type,
        "cpp_type_str": elf.read_cstr(cpp_type),
        "entries": entries,
        "flags": flags,
        "count": count,
        "enum_flags": raw[46],
        "cpp_form": raw[47],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("binary", help="path to libUnreal.so")
    parser.add_argument("--prefix", default=ENUM_PREFIX_DEFAULT,
                        help=f"enum name prefix (default: {ENUM_PREFIX_DEFAULT})")
    parser.add_argument("--json", help="also write findings as JSON to this path")
    args = parser.parse_args()

    elf = Elf(args.binary)
    print(f"ELF64 aarch64, {len(elf.data)} bytes, {len(elf.loads)} PT_LOAD segments")

    strings = find_strings(elf, args.prefix)
    print(f"\n=== {args.prefix}:: strings ({len(strings)}) ===")
    for vaddr, text in sorted(strings.items(), key=lambda kv: kv[1]):
        print(f"  0x{vaddr:08x}  {text}")
    relocs = elf.relocations()
    print(f"\n.rela.dyn entries: {len(relocs)}")

    located = find_enumerator_array(elf, relocs, strings)
    if located is None:
        print("\nERROR: no enumerator array found. "
              "Is this a stripped build without .rela.dyn, or a different enum name?")
        return 1
    array_vaddr, entries = located
    print(f"\n=== Enumerator[{len(entries)}] @ 0x{array_vaddr:x} ===")
    for index, (name, value, slot) in enumerate(entries):
        print(f"  [{index:2}] slot=0x{slot:08x}  value={value:3}  {name}")

    params_vaddr = find_params(elf, relocs, array_vaddr)
    if params_vaddr is None:
        print("\nWARNING: no FEnumParams references this array")
        return 1
    params = read_params(elf, relocs, params_vaddr)
    print(f"\n=== FEnumParams @ 0x{params_vaddr:x} ===")
    for key in ("outer", "display", "name", "cpp_type", "entries", "flags",
                "count", "enum_flags", "cpp_form"):
        value = params[key]
        suffix = ""
        if key in ("name", "cpp_type", "outer"):
            text = params.get(f"{key}_str")
            suffix = f"  ({text})" if text else ""
        print(f"  {key:11} = {hex(value) if isinstance(value, int) and value > 9 else value}{suffix}")

    print("\n=== checks against the Windows-side validation ===")
    checks = [
        ("count == 13", params["count"] == 13),
        ("display is NULL", params["display"] == 0),
        ("flags == 0x45", params["flags"] == 0x45),
        ("enum_flags == 0", params["enum_flags"] == 0),
        ("cpp_form == 2", params["cpp_form"] == 2),
        ("cpp_type == name", params["cpp_type"] == params["name"]),
        (f'name == "{args.prefix}"', params["name_str"] == args.prefix),
        ("values are 0..n-1", [v for _, v, _ in entries] == list(range(len(entries)))),
    ]
    for label, ok in checks:
        print(f"  [{'OK' if ok else 'FAIL'}] {label}")

    if args.json:
        payload = {
            "enum": {
                "enumerators_vaddr": array_vaddr,
                "params_vaddr": params_vaddr,
                "params": params,
                "entries": [{"name": n, "value": v, "slot_vaddr": s} for n, v, s in entries],
            },
            "strings": {f"0x{k:x}": v for k, v in strings.items()},
        }
        with open(args.json, "w", encoding="utf-8") as handle:
            json.dump(payload, handle, indent=2)
            handle.write("\n")
        print(f"\nwrote {args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
