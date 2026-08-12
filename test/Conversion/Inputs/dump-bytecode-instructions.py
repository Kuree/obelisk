#!/usr/bin/env python3

"""Print serialized Obelisk design-bytecode instructions from MLIR input."""

import re
import struct
import sys


text = sys.stdin.read()
match = re.search(r"obelisk\.bytecode\.image\s*=\s*array<i8:\s*([^>]*)>", text)
if not match:
    raise SystemExit("missing obelisk.bytecode.image attribute")

values = [int(value) & 0xFF for value in re.findall(r"-?\d+", match.group(1))]
image = bytes(values)
if len(image) < 208 or image[:8] != b"OBBCDS1\0":
    raise SystemExit("invalid Obelisk design-bytecode header")

code_offset = struct.unpack_from("<Q", image, 72)[0]
instruction_count = struct.unpack_from("<Q", image, 80)[0]
instruction_size = 32
if code_offset > len(image) or instruction_count > (len(image) - code_offset) // instruction_size:
    raise SystemExit("invalid Obelisk design-bytecode instruction range")

for index in range(instruction_count):
    offset = code_offset + index * instruction_size
    opcode, flags, destination, source0, source1, source2, auxiliary, immediate = (
        struct.unpack_from("<HHIIIIIQ", image, offset)
    )
    print(
        f"{index}: opcode={opcode} flags={flags} dst={destination} "
        f"src0={source0} src1={source1} src2={source2} "
        f"aux={auxiliary} imm={immediate}"
    )
