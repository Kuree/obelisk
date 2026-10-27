#!/usr/bin/env python3
"""Generate reference Markdown from the built tools and their TableGen sources."""

import argparse
from collections import defaultdict
from html import escape
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile


TOOLS = ("talos", "talos-opt", "talos-translate", "talos-cov", "talos-tblgen")

DIALECTS = {
    "slang": ["Slang/SlangOps.td"],
    "talos": ["Talos/TalosOps.td", "Talos/TalosASTOps.td"],
    "simulation": ["Simulation/SimulationOps.td"],
    "schedule": ["Schedule/ScheduleOps.td"],
    "runtime": ["Runtime/RuntimeOps.td"],
    "sdf": ["SDF/SDFOps.td"],
    "coverage": ["Coverage/CoverageAttrs.td"],
    "dpi": ["DPI/DPIAttrs.td"],
    "native": ["Native/NativeAttrs.td"],
}


def driver_options(tablegen, source, sdk):
    """Render parsed TableGen records, preserving each option's argument syntax.

    LLVM's -gen-opt-rst adds '=' before metavariables even for Separate and
    Joined options whose names already end in '='. The JSON backend retains
    the option kinds needed to render the driver's actual accepted syntax.
    """
    records = json.loads(run([str(tablegen), "--dump-json", "-I", str(sdk / "include"),
                              str(source / "tools/driver/Options.td")]))
    groups = defaultdict(list)

    def target(option):
        return records[option["Alias"]["def"]] if option["Alias"] else option

    def spellings(option):
        kind = option["Kind"]["def"]
        argument = option["MetaVarName"] or target(option)["MetaVarName"] or "<arg>"
        names = [prefix + option["Name"] for prefix in option["Prefixes"]]
        if kind == "KIND_FLAG":
            return names
        if kind in {"KIND_JOINED", "KIND_COMMAJOINED"}:
            return [name + argument for name in names]
        if kind == "KIND_JOINED_OR_SEPARATE":
            return [spelling for name in names for spelling in (name + argument, name + " " + argument)]
        if kind == "KIND_SEPARATE":
            return [name + " " + argument for name in names]
        if kind == "KIND_REMAINING_ARGS":
            return [name + " " + argument + "…" for name in names]
        raise ValueError(f"Unsupported driver option kind: {kind}")

    count = 0
    for key in records["!instanceof"]["Option"]:
        option = records[key]
        if option["Kind"]["def"] in {"KIND_INPUT", "KIND_UNKNOWN"}:
            continue
        count += 1
        group = option["Group"] or target(option)["Group"]
        group_name = records[group["def"]]["Name"] if group else "General options"
        syntax = spellings(option)
        lines = ["### " + ", ".join(f"`{value}`" for value in syntax), ""]
        help_text = option["HelpText"] or target(option)["HelpText"]
        if help_text:
            lines.extend([escape(help_text, quote=False), ""])
        if option["Alias"]:
            alias = target(option)
            lines.extend([f"Alias of `{alias['Prefixes'][0] + alias['Name']}`.", ""])
        values = option["Values"] or target(option)["Values"]
        if values:
            lines.extend(["Allowed values: " + ", ".join(f"`{value}`" for value in values.split(",")) + ".", ""])
        if any(flag["def"] == "HelpHidden" for flag in option["Flags"]):
            lines.extend(["Hidden from `--help`.", ""])
        groups[group_name].append((syntax[0].casefold(), "\n".join(lines)))
    text = ["# `talos`", "", "<!-- Generated from tools/driver/Options.td by llvm-tblgen --dump-json. -->", "",
            "```text", "talos [options] <input files>", "```", ""]
    for group, entries in sorted(groups.items()):
        text.extend([f"## {group}", ""])
        text.extend(entry for _, entry in sorted(entries))
    return "\n".join(text), count


def write_if_changed(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


def run(command):
    environment = dict(os.environ, LC_ALL="C", NO_COLOR="1", TERM="dumb")
    result = subprocess.run(command, capture_output=True, text=True, env=environment)
    if result.returncode:
        raise RuntimeError(f"{Path(command[0]).name} failed:\n{result.stderr or result.stdout}")
    return result.stdout


def generate(args):
    source, build, sdk, output = [Path(value).resolve() for value in
                                  (args.source, args.build, args.sdk, args.output)]
    previous_manifest = output / "manifest.json"
    previous_pages = json.loads(previous_manifest.read_text())["pages"] if previous_manifest.is_file() else []
    tools = dict(value.split("=", 1) for value in args.tool)
    if set(tools) != set(TOOLS):
        raise ValueError("Provide executable paths for: " + ", ".join(TOOLS))
    index = ["# Talos reference", "", "```{toctree}", ":maxdepth: 1", ":caption: Command-line tools", ""]
    pages = []
    llvm_tablegen = sdk / "bin" / ("llvm-tblgen.exe" if os.name == "nt" else "llvm-tblgen")
    for name in TOOLS:
        relative = f"tools/{name}.md"
        if name == "talos":
            text, option_count = driver_options(llvm_tablegen, source, sdk)
            print(f"Generated {option_count} driver options from TableGen records")
        else:
            help_args = ["report", "--help"] if name == "talos-cov" else ["--help"]
            help_text = run([tools[name], *help_args])
            # Runtime discovery must not leak a particular checkout into the docs.
            help_text = help_text.replace(str(build), "<build>").replace(str(source), "<source>")
            help_text = help_text.replace(tools[name], name)
            command = " ".join([name, *help_args])
            text = (f"# `{name}`\n\n"
                    f"## Command-line reference\n\nGenerated from `{command}`.\n\n"
                    f"```text\n{help_text.rstrip()}\n```\n")
        write_if_changed(output / relative, text)
        index.append(f"{name} <{Path(relative).with_suffix('').as_posix()}>")
        pages.append({"path": relative, "kind": "tool", "name": name})

    tablegen = sdk / "bin" / ("mlir-tblgen.exe" if os.name == "nt" else "mlir-tblgen")
    includes = ["-I", str(source / "include"), "-I", str(build / "include"), "-I", str(sdk / "include")]
    index.extend(["```", "", "```{toctree}", ":maxdepth: 1", ":caption: Dialects", ""])
    with tempfile.TemporaryDirectory(prefix="talos-docs-") as temporary:
        for name, definitions in DIALECTS.items():
            wrapper = Path(temporary) / f"{name}.td"
            wrapper.write_text('include "mlir/IR/OpBase.td"\n' + "\n".join(f'include "talos/Dialect/{item}"' for item in definitions) + "\n")
            text = run([str(tablegen), "-gen-dialect-doc", f"-dialect={name}", *includes, str(wrapper)])
            relative = f"dialects/{name}.md"
            write_if_changed(output / relative, text.replace("\n[TOC]\n", "\n"))
            index.append(f"{name} <{Path(relative).with_suffix('').as_posix()}>")
            pages.append({"path": relative, "kind": "dialect", "name": name})

    index.extend(["```", "", "```{toctree}", ":maxdepth: 1", ":caption: Passes", ""])
    for definition in sorted((source / "include/talos").rglob("Passes.td")):
        parent = definition.parent
        name = (parent.parent.name if parent.name == "Transforms" else parent.name).lower()
        title = name.capitalize() + " passes"
        text = run([str(tablegen), "-gen-pass-doc", *includes, str(definition)])
        relative = f"passes/{name}.md"
        text = re.sub(r"(?m)^(#{3,}) ", lambda match: match[1][1:] + " ", text)
        write_if_changed(output / relative, f"# {title}\n\n{text}".replace("\n[TOC]\n", "\n"))
        index.append(f"{title} <{Path(relative).with_suffix('').as_posix()}>")
        pages.append({"path": relative, "kind": "pass", "name": title})

    index.extend(["```", ""])
    write_if_changed(output / "index.md", "\n".join(index))
    current_paths = {page["path"] for page in pages}
    for page in previous_pages:
        stale = (output / page["path"]).resolve()
        if page["path"] not in current_paths and stale.is_relative_to(output) and stale.is_file():
            stale.unlink()
    write_if_changed(output / "manifest.json", json.dumps({"pages": pages}, indent=2) + "\n")
    print(f"Generated {len(TOOLS)} tool references, {len(DIALECTS)} dialect references, "
          f"and {sum(item['kind'] == 'pass' for item in pages)} pass references in {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ["source", "build", "sdk", "output"]:
        parser.add_argument(f"--{name}", required=True)
    parser.add_argument("--tool", action="append", default=[], metavar="NAME=EXECUTABLE")
    generate(parser.parse_args())


if __name__ == "__main__":
    main()
