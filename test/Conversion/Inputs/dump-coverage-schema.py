#!/usr/bin/env python3

"""Dump coverage type dimensions embedded in textual MLIR.

This deliberately understands only the prototype v1 physical format. It is a
pass-test adapter, not a general database reader; production readers must use
the generated native codec and its full validation.
"""

import re
import struct
import sys


KINDS = {
    1: "Root",
    2: "Scalar",
    3: "Enum",
    4: "PackedArray",
    5: "UnpackedArray",
    6: "PackedStruct",
    7: "UnpackedStruct",
    8: "PackedUnion",
    9: "UnpackedUnion",
    10: "Field",
    11: "Tag",
}


def unpack_from(fmt, data, offset):
    size = struct.calcsize(fmt)
    if offset < 0 or offset + size > len(data):
        raise ValueError("truncated coverage schema")
    return struct.unpack_from(fmt, data, offset)


text = sys.stdin.read()
sample_calls = []
for match in re.finditer(
    r"simulation\.covergroup\.sample .*? ids \[([^]]*)\]"
    r"\s*: \(([^)]*)\) -> \(\)",
    text,
):
    ids = [int(value.strip()) for value in match.group(1).split(",") if value.strip()]
    operand_types = [value.strip() for value in match.group(2).split(",")]
    sample_calls.append((ids, operand_types[2:]))
images = re.findall(
    r"obelisk\.execution\.coverage_schema_blob = array<i8: ([^>]*)>", text
)
if not images:
    raise SystemExit("no embedded coverage schema found")

all_expressions = {}
for image_index, values in enumerate(images):
    data = bytes(int(value.strip()) & 0xFF for value in values.split(","))
    if data[:8] != b"OBCOV\r\n\x1a":
        raise SystemExit("bad coverage schema magic")
    version, = unpack_from("<I", data, 8)
    if version != 1:
        raise SystemExit(f"unsupported coverage schema version {version}")
    directory_offset, = unpack_from("<Q", data, 24)
    section_count, = unpack_from("<I", data, 32)
    sections = {}
    for section_index in range(section_count):
        entry = unpack_from("<IIQQQ", data, directory_offset + 32 * section_index)
        kind, _, offset, size, count = entry
        if offset + size > len(data):
            raise SystemExit("coverage schema section exceeds image")
        sections[kind] = (offset, size, count)
    dimensions = sections.get(6)
    if dimensions is None:
        raise SystemExit("coverage schema has no toggle dimension section")
    offset, size, count = dimensions
    if size != count * 64:
        raise SystemExit("invalid toggle dimension section size")
    print(f"schema {image_index}")
    for record_index in range(count):
        record = unpack_from("<QIIIIqqQQII", data, offset + record_index * 64)
        kind = KINDS.get(record[2], f"Unknown({record[2]})")
        print(f"dimension kind={kind} offset={record[7]} width={record[8]}")
    string_offset, string_size, string_count = sections[1]
    strings = []
    cursor = string_offset
    for _ in range(string_count):
        length, = unpack_from("<I", data, cursor)
        cursor += 4
        if cursor + length > string_offset + string_size:
            raise SystemExit("invalid coverage string table")
        strings.append(data[cursor:cursor + length].decode("utf-8"))
        cursor = (cursor + length + 3) & ~3
    offset, size, count = sections[3]
    if size != count * 32:
        raise SystemExit("invalid scope section size")
    for record_index in range(count):
        record = unpack_from("<QQIIII", data, offset + record_index * 32)
        print(
            f"scope id={record[0]} parent={record[1]} "
            f"name={strings[record[2]]} kind={record[3]} "
            f"definition={strings[record[4]]}"
        )
    source_files = {}
    offset, size, count = sections[2]
    if size != count * 48:
        raise SystemExit("invalid source file section size")
    for record_index in range(count):
        record = unpack_from("<QII32s", data, offset + record_index * 48)
        source_files[record[0]] = strings[record[1]]
    offset, size, count = sections[4]
    if size != count * 64:
        raise SystemExit("invalid line point section size")
    for record_index in range(count):
        record = unpack_from("<QQQQIIIIIIII", data, offset + record_index * 64)
        print(
            f"line_point id={record[0]} file={source_files[record[1]]} "
            f"end_file={source_files[record[2]]} scope={record[3]} "
            f"macro={strings[record[4]]} "
            f"range={record[5]}:{record[6]}-{record[7]}:{record[8]} "
            f"phase={record[9]} flags={record[10]}"
        )
    for kind, size_expected, fmt, label in [
        (7, 32, "<QQIIII", "functional_type"),
        (8, 48, "<QQIIIIIIII", "functional_item"),
        (9, 48, "<QQIIIIQII", "functional_bin"),
    ]:
        offset, size, count = sections[kind]
        if size != count * size_expected:
            raise SystemExit(f"invalid {label} section size")
        for record_index in range(count):
            record = unpack_from(fmt, data, offset + record_index * size_expected)
            if label == "functional_type":
                print(f"{label} id={record[0]} name={strings[record[2]]} language={record[4]} hierarchy={strings[record[5]]}")
            elif label == "functional_item":
                flags = f" flags={record[4]}" if record[4] else ""
                print(f"{label} id={record[0]} type={record[1]} name={strings[record[2]]} kind={record[3]}{flags} ordinal={record[7]} hierarchy={strings[record[8]]}")
            else:
                print(f"{label} id={record[0]} item={record[1]} name={strings[record[2]]} kind={record[3]} flags={record[4]} ordinal={record[5]} at_least={record[6]} hierarchy={strings[record[7]]}")
    offset, size, count = sections[10]
    if size != count * 32:
        raise SystemExit("invalid transition program section size")
    for record_index in range(count):
        record = unpack_from("<QQIIII", data, offset + record_index * 32)
        print(
            f"transition_program bin={record[0]} item={record[1]} "
            f"first_alternative={record[2]} alternative_count={record[3]} "
            f"flags={record[4]}"
        )
    offset, size, count = sections[20]
    if size != count * 32:
        raise SystemExit("invalid transition alternative section size")
    for record_index in range(count):
        record = unpack_from("<QQIIII", data, offset + record_index * 32)
        print(
            f"transition_alternative bin={record[0]} "
            f"terminal_value_set={record[1]} first_step={record[2]} "
            f"step_count={record[3]} ordinal={record[4]} flags={record[5]}"
        )
    offset, size, count = sections[21]
    if size != count * 64:
        raise SystemExit("invalid transition step section size")
    for record_index in range(count):
        record = unpack_from("<QQQQQQIIII", data, offset + record_index * 64)
        print(
            f"transition_step bin={record[0]} value_set={record[1]} "
            f"lower_expression={record[2]} upper_expression={record[3]} "
            f"lower_bound={record[4]} upper_bound={record[5]} "
            f"alternative_ordinal={record[6]} ordinal={record[7]} "
            f"repetition={record[8]} flags={record[9]}"
        )
    offset, size, count = sections[11]
    if size != count * 64:
        raise SystemExit("invalid cross plan section size")
    for record_index in range(count):
        record = unpack_from("<QIIIIIIQQQII", data, offset + record_index * 64)
        print(
            f"cross_plan item={record[0]} first_target={record[1]} "
            f"target_count={record[2]} first_bin={record[3]} "
            f"bin_count={record[4]} retain={record[5]} "
            f"iff_expression={record[7]} tuple_element_type={record[8]} "
            f"tuple_provenance_span={record[9]} tuple_flags={record[10]}"
        )
    offset, size, count = sections[22]
    if size != count * 48:
        raise SystemExit("invalid cross target section size")
    for record_index in range(count):
        record = unpack_from("<QQIIQIIII", data, offset + record_index * 48)
        print(
            f"cross_target cross={record[0]} target={record[1]} "
            f"ordinal={record[2]} tuple_bit_offset={record[4]} "
            f"tuple_bit_width={record[5]} tuple_result_kind={record[6]} "
            f"tuple_signedness={record[7]} tuple_flags={record[8]}"
        )
    offset, size, count = sections[23]
    if size != count * 32:
        raise SystemExit("invalid cross bin section size")
    for record_index in range(count):
        record = unpack_from("<QQQII", data, offset + record_index * 32)
        print(
            f"cross_bin bin={record[0]} cross={record[1]} "
            f"root_selector={record[2]} flags={record[3]}"
        )
    offset, size, count = sections[24]
    if size != count * 112:
        raise SystemExit("invalid cross selector node section size")
    for record_index in range(count):
        record = unpack_from(
            "<QQQQQQQQQIIIIIIQQ", data, offset + record_index * 112
        )
        print(
            f"cross_selector id={record[0]} cross={record[1]} "
            f"target={record[2]} bin={record[3]} value_set={record[4]} "
            f"kind={record[11]} ordinal={record[12]} "
            f"first_operand={record[9]} operand_count={record[10]} "
            f"with_expression={record[5]} "
            f"construction_expression={record[6]} tuple_set={record[7]} "
            f"matches_expression={record[8]} "
            f"matches_policy={record[14]} matches_count={record[15]}"
        )
    offset, size, count = sections[25]
    if size != count * 24:
        raise SystemExit("invalid cross selector operand section size")
    for record_index in range(count):
        record = unpack_from("<QQII", data, offset + record_index * 24)
        print(
            f"cross_selector_operand node={record[0]} operand={record[1]} "
            f"ordinal={record[2]}"
        )
    if 56 in sections:
        offset, size, count = sections[56]
        if size != count * 64:
            raise SystemExit("invalid functional formal section size")
        for record_index in range(count):
            record = unpack_from("<QQIIIIIIIIQQ", data, offset + record_index * 64)
            print(
                f"functional_formal id={record[0]} type={record[1]} "
                f"name={strings[record[2]]} kind={record[3]} "
                f"direction={record[4]} result_kind={record[5]} "
                f"flags={record[6]} width={record[7]} signedness={record[8]} "
                f"ordinal={record[9]} default_expression={record[10]}"
            )
    offset, size, count = sections[27]
    if size != count * 56:
        raise SystemExit("invalid functional value-set section size")
    for record_index in range(count):
        record = unpack_from("<QQIIIIIIQQ", data, offset + record_index * 56)
        print(
            f"functional_value_set id={record[0]} item={record[1]} "
            f"atoms={record[3]} width={record[4]} kind={record[5]} "
            f"flags={record[6]} signedness={record[7]} set_expression={record[9]}"
        )
    offset, size, count = sections[28]
    if size != count * 64:
        raise SystemExit("invalid functional value-atom section size")
    for record_index in range(count):
        record = unpack_from("<QQQIIIIIIQQ", data, offset + record_index * 64)
        print(
            f"functional_value_atom set={record[0]} ordinal={record[5]} "
            f"kind={record[6]} flags={record[7]} "
            f"lower_expression={record[9]} upper_expression={record[10]}"
        )
    offset, size, count = sections[30]
    if size != count * 88:
        raise SystemExit("invalid functional_expression section size")
    for record_index in range(count):
        record = unpack_from(
            "<QQIIII32sIIIIII", data, offset + record_index * 88
        )
        expression_id = record[0]
        if expression_id in all_expressions:
            raise SystemExit("duplicate FunctionalExpression ID across schemas")
        all_expressions[expression_id] = record
        print(
            f"functional_expression id={expression_id} owner={record[1]} "
            f"owner_kind={record[2]} role={record[3]} result_kind={record[4]} "
            f"width={record[7]} signedness={record[8]} "
            f"owner_ordinal={record[9]} owner_subordinal={record[10]} "
            f"phase={record[11]} result_ordinal={record[12]} flags={record[5]}"
        )
    source_roles = {1: "expanded", 2: "original", 3: "definition", 4: "invocation"}
    offset, size, count = sections[19]
    if size != count * 64:
        raise SystemExit("invalid functional source section size")
    for record_index in range(count):
        record = unpack_from("<QIIQQIIIIIIII", data, offset + record_index * 64)
        print(
            f"functional_source bin={record[0]} role={source_roles.get(record[1], record[1])} "
            f"ordinal={record[2]} file={source_files[record[3]]} end_file={source_files[record[4]]} "
            f"macro={strings[record[5]]} range={record[6]}:{record[7]}-{record[8]}:{record[9]}"
        )
    offset, size, count = sections[12]
    if size != count * 24:
        raise SystemExit("invalid exclusion section size")
    for record_index in range(count):
        record = unpack_from("<QIIII", data, offset + record_index * 24)
        print(f"exclusion entity={record[0]} metric={record[1]} reason={strings[record[2]]}")
    offset, size, count = sections[40]
    if size != count * 56:
        raise SystemExit("invalid functional bin-plan section size")
    for record_index in range(count):
        record = unpack_from("<QQQQQIIII", data, offset + record_index * 56)
        print(
            f"functional_bin_plan bin={record[0]} value_set={record[1]} "
            f"iff_expression={record[2]} cardinality_expression={record[3]} "
            f"array_cardinality={record[4]} array_mode={record[5]} "
            f"distribution={record[6]} flags={record[7]}"
        )
    offset, size, count = sections[44]
    if size != count * 40:
        raise SystemExit("invalid functional option-plan section size")
    for record_index in range(count):
        record = unpack_from("<QQIIIIII", data, offset + record_index * 40)
        print(
            f"functional_option_plan owner={record[0]} expression={record[1]} "
            f"owner_kind={record[2]} scope={record[3]} option={record[4]} "
            f"ordinal={record[5]} flags={record[6]}"
        )

for ids, value_types in sample_calls:
    if len(ids) != len(value_types):
        raise SystemExit("sample ID/type cardinality mismatch")
    for ordinal, (expression_id, value_type) in enumerate(zip(ids, value_types)):
        record = all_expressions.get(expression_id)
        if record is None:
            raise SystemExit(
                f"sample references absent FunctionalExpression {expression_id}"
            )
        result_kind, bit_width = record[4], record[7]
        if record[11] != 2 or record[12] != ordinal:
            raise SystemExit(
                f"sample FunctionalExpression {expression_id} has wrong phase/ordinal"
            )
        integral = re.fullmatch(r"i([1-9][0-9]*)", value_type)
        logic = re.fullmatch(r"!simulation\.logic<([1-9][0-9]*)>", value_type)
        if result_kind == 1 and value_type != "i1":
            raise SystemExit(
                f"boolean FunctionalExpression {expression_id} is {value_type}"
            )
        if result_kind == 2 and not (
            (integral and int(integral.group(1)) == bit_width)
            or (logic and int(logic.group(1)) == bit_width)
        ):
            raise SystemExit(
                f"integral FunctionalExpression {expression_id} is {value_type}"
            )
        if result_kind == 3 and value_type != "f64":
            raise SystemExit(
                f"real FunctionalExpression {expression_id} is {value_type}"
            )
