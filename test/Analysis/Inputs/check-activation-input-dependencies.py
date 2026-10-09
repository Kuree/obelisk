import pathlib
import subprocess
import sys

executable, directory = sys.argv[1:]
fixtures = [
    "activation-final-values.mlir",
    "activation-private-state.mlir",
    "activation-mixed-local-reference.mlir",
    "activation-packed-induction.mlir",
    "activation-range-joins.mlir",
    "activation-net-drives.mlir",
    "activation-nested-bounds.mlir",
    "activation-nested-breaks.mlir",
    "activation-slice-loop-values.mlir",
]


def analyze(path, mode):
    result = subprocess.run(
        [executable, str(path), "--split-input-file",
         f"--test-obelisk-activation-regions={mode}=true inputs=true", "-o", "/dev/null"],
        check=True, capture_output=True, text=True,
    )
    cases = []
    case = None
    for line in result.stderr.splitlines():
        if line.startswith("regions @"):
            case = {"name": line, "valid": False}
            cases.append(case)
        elif case is not None:
            if line == ("  values valid=1" if mode == "reaching-values" else "  inputs valid=1"):
                case["valid"] = True
            if line.startswith("  value-input"):
                case["inputs"] = line
    return cases


checked = 0
for name in fixtures:
    path = pathlib.Path(directory) / name
    full = analyze(path, "reaching-values")
    inputs = analyze(path, "input-dependencies")
    assert len(full) == len(inputs), name
    for a, b in zip(full, inputs):
        assert a["name"] == b["name"], (name, a, b)
        if a["valid"]:
            assert b["valid"] and a["inputs"] == b["inputs"], (name, a, b)
            checked += 1
assert checked >= 10, checked
print(f"Compared {checked} activation input proofs with full publication analysis")
