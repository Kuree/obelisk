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
if len(image) < 128 or image[:8] != b"OBDSGN1\0":
    raise SystemExit("invalid Obelisk design-database header")

scope_offset, scope_count = struct.unpack_from("<QQ", image, 48)
object_offset, object_count = struct.unpack_from("<QQ", image, 64)
type_offset, type_count = struct.unpack_from("<QQ", image, 80)
string_offset, string_size = struct.unpack_from("<QQ", image, 96)


def checked_range(offset, count, size, description):
    if offset > len(image) or count > (len(image) - offset) // size:
        raise SystemExit(f"invalid design-database {description} range")


checked_range(scope_offset, scope_count, 64, "scope")
checked_range(object_offset, object_count, 96, "object")
checked_range(type_offset, type_count, 80, "type")
checked_range(string_offset, string_size, 1, "string")


def string_at(offset):
    if offset < string_offset or offset >= string_offset + string_size:
        raise SystemExit("invalid design-database string offset")
    end = image.find(b"\0", offset, string_offset + string_size)
    if end < 0:
        raise SystemExit("unterminated design-database string")
    return image[offset:end].decode("utf-8")


scope_names = {}
for index in range(scope_count):
    offset = scope_offset + index * 64
    kind, capabilities, stable_id = struct.unpack_from("<IIQ", image, offset)
    name = string_at(struct.unpack_from("<Q", image, offset + 40)[0])
    scope_names[offset] = name
    print(
        f"scope name={name} kind={kind} caps=0x{capabilities:x} id={stable_id}"
    )

for index in range(object_count):
    offset = object_offset + index * 96
    kind, capabilities, stable_id = struct.unpack_from("<IIQ", image, offset)
    scope = struct.unpack_from("<Q", image, offset + 16)[0]
    name = string_at(struct.unpack_from("<Q", image, offset + 40)[0])
    type_record = struct.unpack_from("<Q", image, offset + 48)[0]
    width, left, right, state = struct.unpack_from("<QqqQ", image, offset + 56)
    type_kind = 0
    type_flags = 0
    if type_record:
        if type_record < type_offset or type_record + 80 > type_offset + type_count * 80:
            raise SystemExit("invalid design-database type offset")
        packed_type = struct.unpack_from("<I", image, type_record + 4)[0]
        type_kind = packed_type & 0xFF
        type_flags = packed_type >> 8
    ordinal = (capabilities >> 8) & 0xFFFFFF
    print(
        f"object name={name} kind={kind} caps=0x{capabilities:x} "
        f"id={stable_id} scope={scope_names.get(scope, '?')} width={width} "
        f"range=[{left}:{right}] state={state} type_kind={type_kind} "
        f"type_flags=0x{type_flags:x} port_ordinal={ordinal}"
    )
