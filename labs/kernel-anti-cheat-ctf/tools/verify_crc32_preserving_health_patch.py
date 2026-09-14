"""Prove a CRC32-preserving TBM.exe health patch without writing a binary.

The repository's execution policy forbids running samples.  This verifier only
reads the disabled image, applies the candidate edits to an in-memory bytearray,
and checks the resulting CRC32 and PE layout.
"""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import struct
import zlib


EXPECTED_SIZE = 463_872
EXPECTED_SHA256 = "de2a5b567b6f53ed6625c428e0ebe3fbec0797f5e6b3a3d1bcee5bb3fc403a42"
EXPECTED_CRC32 = 0x688FFE38

# RVA 0x51F70 (file offset 0x51370) is sub_51F70, the guarded health
# decrementer.  Both call sites ignore its return value.  RET leaves the
# coherently encoded health record unchanged rather than corrupting a replica.
HEALTH_PATCH_OFFSET = 0x51370
HEALTH_PATCH_EXPECTED = bytes.fromhex("48")
HEALTH_PATCH_REPLACEMENT = bytes.fromhex("C3")

# The final four bytes of .reloc's raw allocation lie beyond its VirtualSize
# and the relocation directory.  They are zero padding in this snapshot.
FIXUP_OFFSET = 0x713FC
FIXUP_EXPECTED = bytes(4)


def crc32(data: bytes | bytearray) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def target_crc32(data: bytes | bytearray) -> int:
    """Bitwise reflected IEEE CRC32 used by all three target modules."""

    value = 0xFFFFFFFF
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0xEDB88320 if value & 1 else 0)
    return (~value) & 0xFFFFFFFF


def solve_four_byte_fixup(image: bytearray, offset: int, target: int) -> bytes:
    """Solve the 32 GF(2) variables at ``offset`` for the requested CRC32."""

    image[offset : offset + 4] = bytes(4)
    base = crc32(image)
    columns: list[int] = []
    for bit_index in range(32):
        candidate = bytearray(image)
        candidate[offset + bit_index // 8] ^= 1 << (bit_index % 8)
        columns.append(crc32(candidate) ^ base)

    # Each basis entry carries both its CRC-space vector and the combination of
    # input bits that produces it.
    basis: list[tuple[int, int] | None] = [None] * 32
    for bit_index, vector in enumerate(columns):
        combination = 1 << bit_index
        while vector:
            pivot = vector.bit_length() - 1
            if basis[pivot] is None:
                basis[pivot] = (vector, combination)
                break
            vector ^= basis[pivot][0]
            combination ^= basis[pivot][1]

    remaining = target ^ base
    solution = 0
    while remaining:
        pivot = remaining.bit_length() - 1
        entry = basis[pivot]
        assert entry is not None, "four-byte CRC32 fixup matrix is singular"
        remaining ^= entry[0]
        solution ^= entry[1]
    return solution.to_bytes(4, "little")


def read_pe_geometry(image: bytes) -> tuple[tuple[int, int, int], tuple[int, int, int]]:
    pe_offset = struct.unpack_from("<I", image, 0x3C)[0]
    section_count = struct.unpack_from("<H", image, pe_offset + 6)[0]
    optional_size = struct.unpack_from("<H", image, pe_offset + 20)[0]
    optional = pe_offset + 24
    assert struct.unpack_from("<H", image, optional)[0] == 0x20B
    assert struct.unpack_from("<I", image, optional + 64)[0] == 0  # CheckSum
    directory = optional + 112
    security_offset, security_size = struct.unpack_from("<II", image, directory + 8 * 4)
    reloc_rva, reloc_size = struct.unpack_from("<II", image, directory + 8 * 5)
    assert (security_offset, security_size) == (0, 0)
    section_table = pe_offset + 24 + optional_size
    text_geometry: tuple[int, int, int] | None = None
    reloc_geometry: tuple[int, int, int] | None = None
    for index in range(section_count):
        entry = section_table + 40 * index
        name = image[entry : entry + 8].rstrip(b"\0")
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
            "<IIII", image, entry + 8
        )
        if name == b".text":
            text_geometry = (virtual_address, raw_size, raw_offset)
        if name == b".reloc":
            reloc_geometry = (virtual_size, raw_size, raw_offset)
            assert reloc_rva == virtual_address
            assert reloc_size == virtual_size
    assert text_geometry is not None, ".text section not found"
    assert reloc_geometry is not None, ".reloc section not found"
    return text_geometry, reloc_geometry


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Verify a CRC32-preserving TBM.exe health patch in memory."
    )
    parser.add_argument("target", type=pathlib.Path, help="path to the original TBM.exe")
    args = parser.parse_args()

    original = args.target.read_bytes()
    assert len(original) == EXPECTED_SIZE
    assert hashlib.sha256(original).hexdigest() == EXPECTED_SHA256
    assert crc32(original) == EXPECTED_CRC32
    assert target_crc32(original) == EXPECTED_CRC32
    assert original[HEALTH_PATCH_OFFSET : HEALTH_PATCH_OFFSET + 1] == HEALTH_PATCH_EXPECTED
    assert original[FIXUP_OFFSET : FIXUP_OFFSET + 4] == FIXUP_EXPECTED

    text_geometry, reloc_geometry = read_pe_geometry(original)
    text_rva, text_raw_size, text_raw_offset = text_geometry
    assert text_rva + HEALTH_PATCH_OFFSET - text_raw_offset == 0x51F70
    assert HEALTH_PATCH_OFFSET < text_raw_offset + text_raw_size

    virtual_size, raw_size, raw_offset = reloc_geometry
    assert raw_offset == 0x71200
    assert virtual_size == 0x94
    assert raw_size == 0x200
    assert FIXUP_OFFSET >= raw_offset + virtual_size
    assert FIXUP_OFFSET + 4 <= raw_offset + raw_size

    candidate = bytearray(original)
    candidate[HEALTH_PATCH_OFFSET : HEALTH_PATCH_OFFSET + 1] = HEALTH_PATCH_REPLACEMENT
    fixup = solve_four_byte_fixup(candidate, FIXUP_OFFSET, EXPECTED_CRC32)
    candidate[FIXUP_OFFSET : FIXUP_OFFSET + 4] = fixup

    assert crc32(candidate) == EXPECTED_CRC32
    assert target_crc32(candidate) == EXPECTED_CRC32
    changed = [index for index, (old, new) in enumerate(zip(original, candidate)) if old != new]
    assert changed[0] == HEALTH_PATCH_OFFSET
    assert all(index in {HEALTH_PATCH_OFFSET, *range(FIXUP_OFFSET, FIXUP_OFFSET + 4)} for index in changed)

    print(f"input_sha256={EXPECTED_SHA256}")
    print(f"input_and_candidate_crc32=0x{EXPECTED_CRC32:08X}")
    print(f"health_patch=0x{HEALTH_PATCH_OFFSET:X}:48->C3")
    print(f"reloc_padding_fixup=0x{FIXUP_OFFSET:X}:{fixup.hex().upper()}")
    print("candidate_written=no")


if __name__ == "__main__":
    main()
