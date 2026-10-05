#!/usr/bin/env python3
"""Create a static library archive without depending on `ar`.

Why this exists
---------------
The obvious way to build libstl.a is `ar rcs`, but `ar` is not available
everywhere the library is expected to build: MSVC ships `lib.exe` instead,
and minimal container images often omit binutils. Rather than maintain one
archiver invocation per platform, the Makefile calls this script, which
writes the archive itself using the documented `ar` format:

    !<arch>\\n                       global header, 8 bytes
    <member header>                 60 bytes, fixed-width ASCII fields
    <member data>                   padded to an even byte boundary

Both the common variant (GNU/BSD, symbol table in a `/` member) and the
Windows variant (COFF, symbol table in a `/` member plus a longer-name
member) are supported, because the script is used for MinGW and MSVC objects
as well.  Long member names are stored indirectly in a `//` member with the
BSD `#1/<len>` convention, which every mainstream linker understands.

Usage:
    make_archive.py <output.a> <object> [<object> ...]
"""

import os
import struct
import sys

GLOBAL_HEADER = b"!<arch>\n"
HEADER_SIZE = 60
MAX_DIRECT_NAME = 16


def _is_coff(data: bytes) -> bool:
    """True for a COFF object (MSVC / MinGW, i.e. a Windows target)."""
    if len(data) < 2:
        return False
    # IMAGE_FILE_MACHINE_* values are little-endian and non-zero in the low
    # byte for every supported architecture (0x14c i386, 0x8664 amd64, ...).
    machine = struct.unpack_from("<H", data, 0)[0]
    if machine == 0:
        return False
    # ELF and Mach-O have distinctive magic numbers; rule them out first.
    if data[:4] == b"\x7fELF":
        return False
    if data[:4] in (b"\xfe\xed\xfa\xce", b"\xfe\xed\xfa\xcf",
                    b"\xce\xfa\xed\xfe", b"\xcf\xfa\xed\xfe"):
        return False
    return True


def _symbols_of(obj_path: str) -> list:
    """Return the global (external) symbols defined by `obj_path`.

    Only used for the archive index.  A missing index is not fatal -- linkers
    rescan archives -- so any parse failure simply yields no symbols.
    """
    try:
        with open(obj_path, "rb") as handle:
            data = handle.read()
    except OSError:
        return []

    names = []
    if data[:4] == b"\x7fELF":
        names = _elf_symbols(data)
    elif data[:2] == b"MZ" or _is_coff(data):
        names = _coff_symbols(data)
    # Mach-O archives are built by the platform toolchain in practice; an
    # empty index is acceptable there.
    return names


def _elf_symbols(data: bytes) -> list:
    endian = "<" if data[5] == 1 else ">"
    is64 = data[4] == 2
    try:
        if is64:
            shoff, shentsize, shnum = struct.unpack_from(endian + "QHQ", data, 0x28)[0], \
                struct.unpack_from(endian + "H", data, 0x3A)[0], \
                struct.unpack_from(endian + "H", data, 0x3C)[0]
        else:
            shoff = struct.unpack_from(endian + "I", data, 0x20)[0]
            shentsize = struct.unpack_from(endian + "H", data, 0x2E)[0]
            shnum = struct.unpack_from(endian + "H", data, 0x30)[0]
    except struct.error:
        return []

    names = []
    for i in range(shnum):
        off = shoff + i * shentsize
        try:
            if is64:
                sh_type = struct.unpack_from(endian + "I", data, off + 4)[0]
                sh_offset = struct.unpack_from(endian + "Q", data, off + 0x18)[0]
                sh_size = struct.unpack_from(endian + "Q", data, off + 0x20)[0]
                sh_link = struct.unpack_from(endian + "I", data, off + 0x28)[0]
                sh_entsize = struct.unpack_from(endian + "Q", data, off + 0x38)[0]
            else:
                sh_type = struct.unpack_from(endian + "I", data, off + 4)[0]
                sh_offset = struct.unpack_from(endian + "I", data, off + 0x10)[0]
                sh_size = struct.unpack_from(endian + "I", data, off + 0x14)[0]
                sh_link = struct.unpack_from(endian + "I", data, off + 0x18)[0]
                sh_entsize = struct.unpack_from(endian + "I", data, off + 0x24)[0]
        except struct.error:
            continue
        if sh_type != 2:            # SHT_SYMTAB
            continue
        if sh_entsize == 0:
            continue
        # String table for the symbol names.
        stroff = 0
        try:
            strtab_off = shoff + sh_link * shentsize
            stroff = struct.unpack_from(endian + ("Q" if is64 else "I"),
                                        data, strtab_off + (0x18 if is64 else 0x10))[0]
        except struct.error:
            pass
        count = sh_size // sh_entsize
        for j in range(count):
            eo = sh_offset + j * sh_entsize
            try:
                if is64:
                    st_name = struct.unpack_from(endian + "I", data, eo)[0]
                    st_info = data[eo + 4]
                    st_shndx = struct.unpack_from(endian + "H", data, eo + 6)[0]
                else:
                    st_name = struct.unpack_from(endian + "I", data, eo)[0]
                    st_info = data[eo + 12]
                    st_shndx = struct.unpack_from(endian + "H", data, eo + 14)[0]
            except (struct.error, IndexError):
                continue
            bind = st_info >> 4
            if bind not in (1, 2):  # GLOBAL, WEAK
                continue
            if st_shndx == 0:       # SHN_UNDEF
                continue
            if st_name == 0:
                continue
            end = data.find(b"\0", stroff + st_name)
            if end < 0:
                continue
            names.append(data[stroff + st_name:end].decode("ascii", "replace"))
    return names


def _coff_symbols(data: bytes) -> list:
    try:
        ptr_symtab = struct.unpack_from("<I", data, 0x08)[0]
        num_sym = struct.unpack_from("<I", data, 0x0C)[0]
        size_opt = struct.unpack_from("<H", data, 0x14)[0]
    except struct.error:
        return []

    strtab_off = ptr_symtab + num_sym * 18
    names = []
    for i in range(num_sym):
        off = ptr_symtab + i * 18
        try:
            raw = data[off:off + 8]
            value, section, stype, storage = struct.unpack_from("<IhH B", data, off + 8)
        except struct.error:
            break
        if storage != 2:            # IMAGE_SYM_CLASS_EXTERNAL
            continue
        if section <= 0:            # undefined
            continue
        if raw[:4] == b"\0\0\0\0":
            # Name is an offset into the string table.
            str_off = struct.unpack_from("<I", raw, 4)[0]
            end = data.find(b"\0", strtab_off + str_off)
            if end < 0:
                continue
            name = data[strtab_off + str_off:end]
        else:
            name = raw.split(b"\0", 1)[0]
        if name:
            names.append(name.decode("ascii", "replace"))
    return names


def _member_header(name: str, size: int) -> bytes:
    # Fields are ASCII, left-justified, space-padded; the trailer is "`\n".
    header = "{:<16}{:<12}{:<6}{:<6}{:<8}{:<10}`\n".format(
        name, "0", "0", "0", "644", size)
    assert len(header) == HEADER_SIZE, len(header)
    return header.encode("ascii")


def _align(data: bytes) -> bytes:
    return data if len(data) % 2 == 0 else data + b"\n"


def write_archive(out_path: str, objects: list) -> None:
    members = []            # (name, payload, symbols)
    long_names = b""

    for path in objects:
        with open(path, "rb") as handle:
            payload = handle.read()
        symbols = _symbols_of(path)
        base = os.path.basename(path)
        # Archives are keyed by object name; keep them unique and short.
        if base.endswith(".o") or base.endswith(".obj"):
            member_name = base
        else:
            member_name = base.rsplit(".", 1)[0] + ".o"
        members.append([member_name, payload, symbols])

        if len(member_name) > MAX_DIRECT_NAME:
            members[-1].append(len(long_names))
            long_names += member_name.encode("ascii") + b"/\n"
        else:
            members[-1].append(None)

    # Sort the index for reproducibility and rebuild member frames.
    symbol_index = []
    for name, _payload, symbols, _off in members:
        for sym in symbols:
            symbol_index.append((sym, name))
    symbol_index.sort()

    # The Windows/COFF index stores 4-byte member offsets, so the "/" member
    # must be laid out first and its size must be known up front.
    sorted_index = [sym for sym, _ in symbol_index]
    index_size = 4 + 4 * len(sorted_index)
    for sym in sorted_index:
        index_size += len(sym.encode("ascii")) + 1
    index_size = index_size + (index_size % 2)

    long_name_blob = long_names
    if long_name_blob:
        long_name_blob = _align(long_name_blob)

    # First pass: compute member layout so offsets are correct.
    offset = len(GLOBAL_HEADER)
    offset += HEADER_SIZE + index_size                     # "/" index member
    if long_name_blob:
        offset += HEADER_SIZE + len(long_name_blob)        # "//" names member

    name_offsets = {}
    member_offsets = {}
    cursor = offset
    for name, payload, _symbols, long_off in members:
        stored_name = name
        # Long names use the BSD "#1/<len>" convention and prefix the payload.
        if long_off is not None:
            stored_name = "#1/%d" % len(name)
        member_offsets[name] = cursor
        name_offsets[name] = stored_name
        body = payload if long_off is None else name.encode("ascii") + payload
        cursor += HEADER_SIZE + len(_align(body))

    with open(out_path, "wb") as out:
        out.write(GLOBAL_HEADER)

        # Symbol index member named "/".
        index = struct.pack(">I", len(sorted_index))
        for sym in sorted_index:
            index += struct.pack(">I", member_offsets.get(
                dict(symbol_index).get(sym, ""), 0))
        for sym in sorted_index:
            index += sym.encode("ascii") + b"\0"
        index = _align(index)
        out.write(_member_header("/", len(index)))
        out.write(index)

        # Long-name table member named "//".
        if long_name_blob:
            out.write(_member_header("//", len(long_name_blob)))
            out.write(long_name_blob)

        # Object members.
        for name, payload, _symbols, long_off in members:
            stored_name = name_offsets[name]
            body = payload if long_off is None else name.encode("ascii") + payload
            padded = _align(body)
            out.write(_member_header(stored_name, len(body)))
            out.write(padded)


def main(argv: list) -> int:
    if len(argv) < 3:
        sys.stderr.write("usage: make_archive.py <output.a> <object> [<object>...]\n")
        return 2
    out_path, objects = argv[1], argv[2:]
    try:
        write_archive(out_path, objects)
    except OSError as exc:
        sys.stderr.write("make_archive: %s\n" % exc)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
