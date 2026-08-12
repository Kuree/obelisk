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

constant_offset = struct.unpack_from("<Q", image, 104)[0]
constant_size = struct.unpack_from("<Q", image, 112)[0]
if constant_offset > len(image) or constant_size > len(image) - constant_offset:
    raise SystemExit("invalid Obelisk design-bytecode constant range")
constant_data = image[constant_offset : constant_offset + constant_size]
print(f"constants: {constant_data.hex()}")

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

intrinsic_offset = struct.unpack_from("<Q", image, 136)[0]
intrinsic_count = struct.unpack_from("<Q", image, 144)[0]
intrinsic_size = 16
if intrinsic_offset > len(image) or intrinsic_count > (
    len(image) - intrinsic_offset
) // intrinsic_size:
    raise SystemExit("invalid Obelisk design-bytecode intrinsic range")

for index in range(intrinsic_count):
    offset = intrinsic_offset + index * intrinsic_size
    intrinsic, inputs, outputs, flags = struct.unpack_from("<IIII", image, offset)
    print(
        f"intrinsic {index}: id=0x{intrinsic:08x} "
        f"inputs={inputs} outputs={outputs} flags={flags}"
    )
