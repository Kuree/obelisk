"""Translating Icarus `iverilog` arguments into Obelisk flags.

ivtest descriptors carry per-test `iverilog-args` (language generation, macro
defines, include/library paths, parameter overrides). Since the benchmark owns
its run loop it calls Obelisk directly, but it still has to honour those Icarus
arguments; this pure function performs that translation. Verilator's driver
constructs Obelisk flags directly and does not use this path.
"""

from __future__ import annotations


def translate_args(icarus_args: list[str]) -> tuple[list[str], str]:
    """Translate Icarus compile args to `(obelisk_flags, language_std)`.

    Only the subset ivtest actually uses is handled; output paths and source
    files are supplied by the caller and never appear here. Flags with no Obelisk
    analogue (`-Wall`, timing modes) are dropped.
    """
    flags: list[str] = []
    std = "1800-2017"
    index = 0
    while index < len(icarus_args):
        arg = icarus_args[index]
        if arg in ("-I", "-y", "-Y", "-l"):
            if index + 1 < len(icarus_args):
                flags.extend((arg, icarus_args[index + 1]))
            index += 2
            continue
        if arg.startswith("-I") and len(arg) > 2:
            flags.extend(("-I", arg[2:]))
            index += 1
            continue
        if arg.startswith("-y") and len(arg) > 2:
            flags.extend(("-y", arg[2:]))
            index += 1
            continue
        if arg.startswith("-D") and len(arg) > 2:
            flags.extend(("-D", arg[2:]))
            index += 1
            continue
        if arg == "-D" and index + 1 < len(icarus_args):
            flags.extend(("-D", icarus_args[index + 1]))
            index += 2
            continue
        if arg.startswith("-U") and len(arg) > 2:
            flags.extend(("-U", arg[2:]))
            index += 1
            continue
        if arg == "-U" and index + 1 < len(icarus_args):
            flags.extend(("-U", icarus_args[index + 1]))
            index += 2
            continue
        if arg.startswith("-P") and len(arg) > 2:
            # -P<instance.param>=value -> Obelisk -G<param>=value.
            override = arg[2:]
            if "." in override:
                override = override.split(".", 1)[1]
            flags.extend(("-G", override))
            index += 1
            continue
        if arg == "-g2023":
            std = "1800-2023"
            index += 1
            continue
        # Everything else (other -g generations, -Wall, -gstrict-expr-width,
        # timing modes) has no Obelisk analogue and is dropped.
        index += 1
    return flags, std
