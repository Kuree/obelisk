#!/usr/bin/env python3

"""Make one structurally valid corruption of a typed functional batch."""

import argparse
import re
import sys


parser = argparse.ArgumentParser()
parser.add_argument(
    "mode",
    choices=(
        "add-layout",
        "constructor-expression-reorder",
        "constructor-expression-subset",
        "constructor-expression-string-type-mismatch",
        "constructor-formal-string-type-mismatch",
        "formal-reorder",
        "insert-string-formal-read",
        "instance-query-nonexistent",
        "instance-query-wrong-owner",
        "sample-reorder",
        "sample-subset",
        "start-control-nonexistent",
        "stop-control-wrong-owner",
        "type-query-nonexistent",
        "type-query-wrong-owner",
        "create-null",
        "formal-read-zero",
    ),
)
args = parser.parse_args()
text = sys.stdin.read()

# Backend lowering intentionally requires an explicit target contract.  The
# frontend-oriented lowering pipeline used by these tests does not otherwise
# need one, so make it explicit before exercising pre-backend verification.
if "llvm.data_layout" not in text:
    text, count = re.subn(
        r"module attributes \{",
        'module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", '
        'llvm.target_triple = "x86_64-unknown-linux-gnu", ',
        text,
        count=1,
    )
    if count != 1:
        raise SystemExit("could not add backend target contract")


def fields(value):
    return [entry.strip() for entry in value.split(",") if entry.strip()]


if (
    args.mode.startswith("instance-query-")
    or args.mode.startswith("type-query-")
    or args.mode.startswith("start-control-")
    or args.mode.startswith("stop-control-")
):
    instance_pattern = re.compile(
        r"(?m)^.*?obelisk_sim\.covergroup\.instance_query\b.*?\bitem\s+"
        r"(-?[0-9]+).*?!obelisk_sim\.covergroup_handle<@([^>]+)>.*$"
    )
    type_pattern = re.compile(
        r"(?m)^.*?obelisk_sim\.covergroup\.type_query\b.*?\bfrom\s+@([^ ]+)"
        r"\s+item\s+(-?[0-9]+).*$"
    )
    control_pattern = re.compile(
        r"(?m)^.*?obelisk_sim\.covergroup\.(start|stop)\b.*?\bitem\s+"
        r"(-?[0-9]+).*?!obelisk_sim\.covergroup_handle<@([^>]+)>.*$"
    )
    records = []
    for match in instance_pattern.finditer(text):
        records.append(("instance", match.group(2), match.group(1),
                        match.start(1), match.end(1)))
    for match in type_pattern.finditer(text):
        records.append(("type", match.group(1), match.group(2),
                        match.start(2), match.end(2)))
    for match in control_pattern.finditer(text):
        records.append((match.group(1), match.group(3), match.group(2),
                        match.start(2), match.end(2)))
    if args.mode.startswith("instance-"):
        selected_kind = "instance"
    elif args.mode.startswith("type-"):
        selected_kind = "type"
    elif args.mode.startswith("start-"):
        selected_kind = "start"
    else:
        selected_kind = "stop"
    selected = next((record for record in records if record[0] == selected_kind), None)
    if selected is None:
        raise SystemExit(f"no {selected_kind} query found")
    if args.mode.endswith("wrong-owner"):
        replacement = next(
            (record[2] for record in records if record[1] != selected[1]), None
        )
        if replacement is None:
            raise SystemExit("no query for a different functional type found")
    else:
        used = {int(record[2]) for record in records}
        candidate = (1 << 63) - 1
        while candidate in used:
            candidate -= 1
        replacement = str(candidate)
    text = text[:selected[3]] + replacement + text[selected[4]:]
    sys.stdout.write(text)
    raise SystemExit(0)


if args.mode == "add-layout":
    sys.stdout.write(text)
    raise SystemExit(0)
elif args.mode == "create-null":
    pattern = re.compile(
        r"(?m)^(\s*)(%\S+) = obelisk_sim\.covergroup\.create[^\n]*"
        r"->\s*(!obelisk_sim\.covergroup_handle<[^>]+>)[ \t]*$"
    )

    def mutate(match):
        return (
            match.group(1)
            + match.group(2)
            + " = obelisk_sim.covergroup.null : "
            + match.group(3)
        )

elif args.mode == "formal-read-zero":
    pattern = re.compile(
        r"(?m)^(\s*)(%\S+) = obelisk_sim\.covergroup\.formal_read[^\n]*"
        r"->\s*(i[1-9][0-9]*)[ \t]*$"
    )

    def mutate(match):
        return (
            match.group(1)
            + match.group(2)
            + " = arith.constant 0 : "
            + match.group(3)
        )

elif args.mode == "insert-string-formal-read":
    pattern = re.compile(
        r"(?m)^(\s*)((%\S+) = obelisk_sim\.covergroup\.create\s+(%\S+)\s+"
        r"from\s+\S+[^\n]*formal_ids\s*\[([1-9][0-9]*)[^]]*\][^\n]*"
        r"->\s*(!obelisk_sim\.covergroup_handle<[^>]+>)[ \t]*)$"
    )

    def mutate(match):
        return (
            match.group(1)
            + match.group(2)
            + "\n"
            + match.group(1)
            + "%__bad_functional_string_read = "
            + "obelisk_sim.covergroup.formal_read "
            + match.group(4)
            + ", "
            + match.group(3)
            + "["
            + match.group(5)
            + "] : !obelisk_sim.context, "
            + match.group(6)
            + " -> !obelisk_sim.string"
        )

elif args.mode in (
    "constructor-expression-string-type-mismatch",
    "constructor-formal-string-type-mismatch",
):
    pattern = re.compile(
        r"(?m)^(\s*)([^\n]*\bobelisk_sim\.covergroup\.create\b[^\n]*?"
        r"\bpayloads\[)([^]]+)(\]\s*argument_count\s+([0-9]+)\s+"
        r"formal_ids\s*\[[^]]*\]\s*expression_ids\s*\[[^]]*\]\s*:\s*\()"
        r"([^)]*)(\)\s*->[^\n]+)$"
    )

    def mutate(match):
        payloads = fields(match.group(3))
        argument_count = int(match.group(5))
        types = fields(match.group(6))
        if len(payloads) != len(types):
            raise SystemExit("unexpected typed constructor assembly")
        if args.mode == "constructor-formal-string-type-mismatch":
            candidates = range(argument_count)
            name = "%__bad_functional_string_formal"
        else:
            candidates = range(argument_count, len(types))
            name = "%__bad_functional_string_expression"
        index = next(
            (candidate for candidate in candidates
             if types[candidate] == "!obelisk_sim.string"),
            None,
        )
        if index is None:
            raise SystemExit("selected constructor batch has no String value")
        payloads[index] = name
        types[index] = "i32"
        return (
            match.group(1)
            + name
            + " = arith.constant 0 : i32\n"
            + match.group(1)
            + match.group(2)
            + ", ".join(payloads)
            + match.group(4)
            + ", ".join(types)
            + match.group(7)
        )

elif args.mode == "constructor-expression-reorder":
    pattern = re.compile(
        r"(obelisk_sim\.covergroup\.create\b[^\n]*?\bexpression_ids\s*\[)"
        r"([^]]+)(\])"
    )

    def mutate(match):
        entries = fields(match.group(2))
        if len(entries) < 2:
            raise SystemExit("constructor batch has fewer than two expressions")
        entries[0], entries[1] = entries[1], entries[0]
        return match.group(1) + ", ".join(entries) + match.group(3)

elif args.mode == "constructor-expression-subset":
    pattern = re.compile(
        r"(obelisk_sim\.covergroup\.create\b[^\n]*?\bpayloads\s*\[)"
        r"([^]]+)(\]\s*argument_count\s+([0-9]+)\s+formal_ids\s*\[[^]]*\]"
        r"\s*expression_ids\s*\[)([^]]+)(\]\s*:\s*\()([^)]*)(\)\s*->)"
    )

    def mutate(match):
        payloads = fields(match.group(2))
        argument_count = int(match.group(4))
        ids = fields(match.group(5))
        types = fields(match.group(7))
        if (
            len(ids) < 2
            or len(payloads) != argument_count + len(ids)
            or len(types) != len(payloads)
        ):
            raise SystemExit("unexpected typed constructor assembly")
        return (
            match.group(1)
            + ", ".join(payloads[:-1])
            + match.group(3)
            + ", ".join(ids[:-1])
            + match.group(6)
            + ", ".join(types[:-1])
            + match.group(8)
        )

elif args.mode == "formal-reorder":
    pattern = re.compile(r"(formal_ids\s*\[)([^]]+)(\])")

    def mutate(match):
        entries = fields(match.group(2))
        if len(entries) < 2:
            raise SystemExit("constructor batch has fewer than two formals")
        entries[0], entries[1] = entries[1], entries[0]
        return match.group(1) + ", ".join(entries) + match.group(3)

elif args.mode == "sample-reorder":
    pattern = re.compile(
        r"(obelisk_sim\.covergroup\.sample\b[^\n]*?\bids\s*\[)([^]]+)(\])"
    )

    def mutate(match):
        entries = fields(match.group(2))
        if len(entries) < 2:
            raise SystemExit("sample batch has fewer than two expressions")
        entries[0], entries[1] = entries[1], entries[0]
        return match.group(1) + ", ".join(entries) + match.group(3)

else:
    pattern = re.compile(
        r"(obelisk_sim\.covergroup\.sample\b[^\n]*?\bvalues\s*\[)"
        r"([^]]+)(\]\s*ids\s*\[)([^]]+)(\][^\n]*?:\s*\()([^)]*)(\)\s*->\s*\(\))"
    )

    def mutate(match):
        values = fields(match.group(2))
        ids = fields(match.group(4))
        types = fields(match.group(6))
        if len(values) < 2 or len(ids) != len(values) or len(types) != len(values) + 2:
            raise SystemExit("unexpected typed sample assembly")
        return (
            match.group(1)
            + ", ".join(values[:-1])
            + match.group(3)
            + ", ".join(ids[:-1])
            + match.group(5)
            + ", ".join(types[:-1])
            + match.group(7)
        )


replace_count = 0 if args.mode == "formal-read-zero" else 1
text, count = pattern.subn(mutate, text, count=replace_count)
if count < 1:
    raise SystemExit(f"could not apply {args.mode} mutation")
sys.stdout.write(text)
