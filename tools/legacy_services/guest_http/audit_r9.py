"""Conservative ARM-text audit for the original OS1 pthread register.

Requires Capstone. Decode every aligned word, including literal pools, so this
may conservatively reject data; no executable word is skipped as presumed data.
"""
import hashlib
from pathlib import Path
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
from capstone.arm import ARM_REG_R9
from macho_audit import parse


def audit_words(data, address=0):
    if len(data) % 4 or address % 4:
        raise ValueError("aligned ARM text required")
    decoder = Cs(CS_ARCH_ARM, CS_MODE_ARM)
    decoder.detail = True
    for offset in range(0, len(data), 4):
        instructions = list(decoder.disasm(data[offset:offset + 4], address + offset))
        if len(instructions) != 1 or instructions[0].size != 4:
            raise ValueError(f"undecodable ARM word at {address + offset:#x}")
        instruction = instructions[0]
        _, written = instruction.regs_access()
        if ARM_REG_R9 in written:
            raise ValueError(f"r9 write at {instruction.address:#x}: {instruction.mnemonic} {instruction.op_str}")
    return len(data) // 4


def audit(path):
    data, _, _, symbols, sections, *_ = parse(Path(path))
    text = next(section for section in sections if section["name"] == "__text")
    if any(symbol["section"] == sections.index(text) + 1 and symbol["description"] & 8 for symbol in symbols):
        raise ValueError("Thumb text needs a separate audit")
    count = audit_words(data[text["fileOffset"]:text["fileOffset"] + text["size"]], text["address"])
    return {"passed": True, "sha256": hashlib.sha256(data).hexdigest(),
            "armWords": count, "r9Writes": 0, "text": text,
            "scope": "All aligned text words including literal pools; explicit and implicit register writes, every condition. Preserves incoming r9; does not initialize TLS."}
