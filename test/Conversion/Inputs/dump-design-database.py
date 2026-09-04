#!/usr/bin/env python3

"""Print the serialized Obelisk design database from MLIR input."""

import re
import struct
import sys


text = sys.stdin.read()
match = re.search(r"obelisk\.design\.database\s*=\s*array<i8:\s*([^>]*)>", text)
if not match:
    raise SystemExit("missing obelisk.design.database attribute")

image = bytes(int(value) & 0xFF for value in re.findall(r"-?\d+", match.group(1)))
if len(image) < 176 or image[:8] != b"OBDSGN1\0":
    raise SystemExit("invalid Obelisk design-database header")

scope_offset, scope_count = struct.unpack_from("<QQ", image, 48)
object_offset, object_count = struct.unpack_from("<QQ", image, 64)
type_offset, type_count = struct.unpack_from("<QQ", image, 80)
string_offset, string_size = struct.unpack_from("<QQ", image, 96)
statement_offset, statement_count = struct.unpack_from("<QQ", image, 128)
statement_site_offset, statement_site_count = struct.unpack_from("<QQ", image, 144)
relation_offset, relation_count = struct.unpack_from("<QQ", image, 160)


def checked_range(offset, count, size, description):
    if offset > len(image) or count > (len(image) - offset) // size:
        raise SystemExit(f"invalid design-database {description} range")


checked_range(scope_offset, scope_count, 64, "scope")
checked_range(object_offset, object_count, 96, "object")
checked_range(type_offset, type_count, 80, "type")
checked_range(string_offset, string_size, 1, "string")
checked_range(statement_offset, statement_count, 40, "statement")
checked_range(statement_site_offset, statement_site_count, 16, "statement site")
checked_range(relation_offset, relation_count, 16, "relation")


def string_at(offset):
    if offset < string_offset or offset >= string_offset + string_size:
        raise SystemExit("invalid design-database string offset")
    end = image.find(b"\0", offset, string_offset + string_size)
    if end < 0:
        raise SystemExit("unterminated design-database string")
    return image[offset:end].decode("utf-8")


scope_names = {}
scope_index_names = []
for index in range(scope_count):
    offset = scope_offset + index * 64
    packed_kind, capabilities, stable_id = struct.unpack_from("<IIQ", image, offset)
    kind = packed_kind & 0xFFFF
    vpi_kind = packed_kind >> 16
    name = string_at(struct.unpack_from("<Q", image, offset + 40)[0])
    scope_names[offset] = name
    scope_index_names.append(name)
    print(
        f"scope name={name} kind={kind} vpi_kind={vpi_kind} "
        f"caps=0x{capabilities:x} id={stable_id}"
    )

object_index_names = []
for index in range(object_count):
    offset = object_offset + index * 96
    packed_kind, capabilities, stable_id = struct.unpack_from("<IIQ", image, offset)
    kind = packed_kind & 0xFFFF
    vpi_kind = packed_kind >> 16
    scope = struct.unpack_from("<Q", image, offset + 16)[0]
    name = string_at(struct.unpack_from("<Q", image, offset + 40)[0])
    object_index_names.append(name)
    type_record = struct.unpack_from("<Q", image, offset + 48)[0]
    width, left, right, state = struct.unpack_from("<QqqQ", image, offset + 56)
    type_kind = 0
    type_flags = 0
    element_kind = 0
    element_flags = 0
    element_width = 0
    if type_record:
        if type_record < type_offset or type_record + 80 > type_offset + type_count * 80:
            raise SystemExit("invalid design-database type offset")
        packed_type = struct.unpack_from("<I", image, type_record + 4)[0]
        type_kind = packed_type & 0xFF
        type_flags = packed_type >> 8
        element_record = struct.unpack_from("<Q", image, type_record + 32)[0]
        if element_record:
            if element_record < type_offset or element_record + 80 > type_offset + type_count * 80:
                raise SystemExit("invalid design-database element type offset")
            packed_element = struct.unpack_from("<I", image, element_record + 4)[0]
            element_kind = packed_element & 0xFF
            element_flags = packed_element >> 8
            element_width = struct.unpack_from("<Q", image, element_record + 8)[0]
    ordinal = (capabilities >> 8) & 0xFFFFFF
    print(
        f"object name={name} kind={kind} vpi_kind={vpi_kind} "
        f"caps=0x{capabilities:x} "
        f"id={stable_id} scope={scope_names.get(scope, '?')} width={width} "
        f"range=[{left}:{right}] state={state} type_kind={type_kind} "
        f"type_flags=0x{type_flags:x} port_ordinal={ordinal} "
        f"element_kind={element_kind} element_flags=0x{element_flags:x} "
        f"element_width={element_width}"
    )

statement_index_names = []
for index in range(statement_count):
    offset = statement_offset + index * 40
    stable_id = struct.unpack_from("<Q", image, offset)[0]
    owner, scope, parent, source_file, name = struct.unpack_from(
        "<IIIII", image, offset + 8
    )
    line, column, vpi_kind, flags = struct.unpack_from("<IIHH", image, offset + 28)
    source = string_at(string_offset + source_file) if source_file else ""
    statement_name = string_at(string_offset + name) if name else ""
    statement_index_names.append(statement_name or f"statement#{stable_id}")
    print(
        f"statement id={stable_id} owner={owner} scope={scope} parent={parent} "
        f"type={vpi_kind} flags=0x{flags:x} source={source}:{line}:{column} "
        f"name={statement_name}"
    )

for index in range(statement_site_count):
    offset = statement_site_offset + index * 16
    stable_id, statement, phase, flags = struct.unpack_from("<QIHH", image, offset)
    print(
        f"statement_site id={stable_id} statement={statement} phase={phase} "
        f"flags=0x{flags:x}"
    )

for index in range(relation_count):
    offset = relation_offset + index * 16
    source, packed_target, ordinal, selector, packed_source = struct.unpack_from(
        "<IIIHH", image, offset
    )
    source_table = packed_source >> 14
    source_mode = "iterate" if packed_source & 0x2000 else "handle"
    source_kind = packed_source & 0x1FFF
    target_table = packed_target >> 30
    target = packed_target & 0x3FFFFFFF
    table_names = (scope_index_names, object_index_names, statement_index_names)
    source_name = table_names[source_table][source]
    target_name = table_names[target_table][target]
    print(
        f"relation source_table={source_table} source={source} "
        f"source_type={source_kind} mode={source_mode} selector={selector} ordinal={ordinal} "
        f"target_table={target_table} target={target} "
        f"source_name={source_name} target_name={target_name}"
    )
