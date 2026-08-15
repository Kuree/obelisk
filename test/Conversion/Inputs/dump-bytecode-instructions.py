#!/usr/bin/env python3

"""Print serialized Obelisk design-bytecode instructions from MLIR input."""

import argparse
import re
import struct
import sys


parser = argparse.ArgumentParser()
parser.add_argument(
    "--metadata",
    action="store_true",
    help="print function, register-layout, and operand-map metadata",
)
args = parser.parse_args()


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

operand_offset = struct.unpack_from("<Q", image, 88)[0]
operand_count = struct.unpack_from("<Q", image, 96)[0]
operand_size = 8
if operand_offset > len(image) or operand_count > (
    len(image) - operand_offset
) // operand_size:
    raise SystemExit("invalid Obelisk design-bytecode operand range")
operands = [
    struct.unpack_from("<II", image, operand_offset + index * operand_size)
    for index in range(operand_count)
]

if args.metadata:
    function_offset = struct.unpack_from("<Q", image, 40)[0]
    function_count = struct.unpack_from("<Q", image, 48)[0]
    function_size = 96
    layout_offset = struct.unpack_from("<Q", image, 56)[0]
    layout_count = struct.unpack_from("<Q", image, 64)[0]
    layout_size = 40
    if function_offset > len(image) or function_count > (
        len(image) - function_offset
    ) // function_size:
        raise SystemExit("invalid Obelisk design-bytecode function range")
    if layout_offset > len(image) or layout_count > (
        len(image) - layout_offset
    ) // layout_size:
        raise SystemExit("invalid Obelisk design-bytecode layout range")
    for index in range(function_count):
        offset = function_offset + index * function_size
        (
            stable_id,
            schedule_rank,
            first_instruction,
            function_instruction_count,
            first_layout,
            function_layout_count,
            argument_count,
            result_count,
        ) = struct.unpack_from("<QQQQQQII", image, offset)
        if first_layout > layout_count or function_layout_count > (
            layout_count - first_layout
        ):
            raise SystemExit(
                f"function {index} has an invalid register-layout range"
            )
        print(
            f"function {index}: id={stable_id} rank={schedule_rank} "
            f"first_instruction={first_instruction} "
            f"instruction_count={function_instruction_count} "
            f"arguments={argument_count} results={result_count}"
        )
        for register in range(function_layout_count):
            layout = layout_offset + (first_layout + register) * layout_size
            kind, flags, width, storage_offset, size, auxiliary = (
                struct.unpack_from("<BB2xIQQQ", image, layout)
            )
            print(
                f"layout function={index} register={register} kind={kind} "
                f"flags={flags} width={width} offset={storage_offset} "
                f"size={size} auxiliary={auxiliary}"
            )
    for index, (destination, source) in enumerate(operands):
        print(f"operand {index}: dst={destination} src={source}")

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

continuation_offset = struct.unpack_from("<Q", image, 120)[0]
continuation_count = struct.unpack_from("<Q", image, 128)[0]
continuation_size = 24
if continuation_offset > len(image) or continuation_count > (
    len(image) - continuation_offset
) // continuation_size:
    raise SystemExit("invalid Obelisk design-bytecode continuation range")

for index in range(continuation_count):
    offset = continuation_offset + index * continuation_size
    function, continuation, instruction, rank, reserved = struct.unpack_from(
        "<IIQII", image, offset
    )
    print(
        f"continuation {index}: function={function} id={continuation} "
        f"instruction={instruction} rank={rank} reserved={reserved}"
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

site_offset = struct.unpack_from("<Q", image, 152)[0]
site_count = struct.unpack_from("<Q", image, 160)[0]
site_size = 16
if site_offset > len(image) or site_count > (len(image) - site_offset) // site_size:
    raise SystemExit("invalid Obelisk design-bytecode intrinsic-site range")

for index in range(site_count):
    offset = site_offset + index * site_size
    signature, first_operand, input_count, output_count = struct.unpack_from(
        "<IIII", image, offset
    )
    if signature >= intrinsic_count or first_operand > operand_count or (
        input_count + output_count > operand_count - first_operand
    ):
        raise SystemExit("invalid Obelisk design-bytecode intrinsic site")
    site_operands = operands[
        first_operand : first_operand + input_count + output_count
    ]
    inputs = [source for _, source in site_operands[:input_count]]
    outputs = [destination for destination, _ in site_operands[input_count:]]
    intrinsic = struct.unpack_from(
        "<I", image, intrinsic_offset + signature * intrinsic_size
    )[0]
    print(
        f"site {index}: signature={signature} id=0x{intrinsic:08x} "
        f"inputs={inputs} outputs={outputs}"
    )
