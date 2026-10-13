"""Check resolved ODS formats and keep test inputs in custom assembly."""

import json
from pathlib import Path
import re
import subprocess
import sys


sdk, source, build = map(Path, sys.argv[1:])
dialects = {"slang", "obelisk", "simulation", "schedule", "runtime", "obelisk_sdf"}
inputs = [
    build / "include/obelisk/Dialect/Slang/SlangOps.td",
    build / "include/obelisk/Dialect/Obelisk/ObeliskASTOps.td",
    *[
        source / f"include/obelisk/Dialect/{name}/{name}Ops.td"
        for name in ["Obelisk", "Simulation", "Schedule", "Runtime", "SDF"]
    ],
]
seen = set()
counts = dict.fromkeys(dialects, 0)
errors = []
for td in inputs:
    records = json.loads(
        subprocess.check_output(
            [
                str(sdk / "bin/llvm-tblgen"),
                "--dump-json",
                "-I", str(source / "include"),
                "-I", str(build / "include"),
                "-I", str(sdk / "include"),
                str(td),
            ],
            text=True,
        )
    )
    for kind, dialect_field in [
        ("Op", "opDialect"),
        ("AttrDef", "dialect"),
        ("TypeDef", "dialect"),
    ]:
        for name in records["!instanceof"].get(kind, []):
            record = records[name]
            dialect = records[record[dialect_field]["def"]]["name"]
            if dialect not in dialects or (kind, name) in seen:
                continue
            seen.add((kind, name))
            if kind == "Op":
                counts[dialect] += 1
            # Parameterless types and attributes use MLIR's mnemonic-only form.
            elif not record["parameters"]["args"]:
                continue
            if not record["assemblyFormat"] and not record["hasCustomAssemblyFormat"]:
                errors.append(f"{dialect}: {name} has no custom assembly format")
            if kind == "Op":
                values = {
                    value: records[constraint["def"]]
                    for constraint, value in (
                        record["arguments"]["args"] + record["results"]["args"]
                    )
                }
                for directive in re.finditer(
                    r"(?<!qualified\()type\(\$(\w+)\)", record["assemblyFormat"] or ""
                ):
                    constraint = values[directive[1]]
                    if constraint.get("mnemonic") and constraint.get("parameters", {}).get("args"):
                        errors.append(
                            f"{dialect}: {name} must qualify {directive[0]} "
                            "to preserve the dialect type mnemonic"
                        )

# These deliberately malformed operations cannot be expressed by their custom
# parsers. Keep the exceptions narrow so other verifier fixtures also exercise
# the custom syntax, including malformed metadata that attr-dict can represent.
exceptions = {
    ("IR/slang-invalid.mlir", "slang.type.string_type"),  # Missing symbol name.
    ("IR/simulation-invalid.mlir", "simulation.design"),  # Zero-block region.
    ("IR/simulation-invalid.mlir", "simulation.suspend.observe"),  # Empty type list.
    ("IR/runtime-invalid.mlir", "runtime.status.is"),  # Wrong enum attribute type.
}
generic_op = re.compile(
    r'^\s*(?:%[^=\n]+\s*=\s*)?"('
    + "|".join(sorted(dialects))
    + r')\.[\w.]+"\s*\(',
    re.MULTILINE,
)
for path in sorted((source / "test").rglob("*.mlir")):
    text = path.read_text()
    relative = path.relative_to(source / "test").as_posix()
    for match in generic_op.finditer(text):
        op = re.search(r'"([\w.]+)"', match[0])[1]
        if (relative, op) not in exceptions:
            line = text.count("\n", 0, match.start()) + 1
            errors.append(f"{relative}:{line}: use custom syntax for {op}")

for dialect, count in sorted(counts.items()):
    if not count:
        errors.append(f"{dialect}: no operations checked")
    print(f"{dialect}: {count} operations have custom assembly")
if errors:
    sys.exit("\n".join(errors))
