import pathlib
import sys


expected = {"dynamic-linker", "crt1", "crti", "crtn", "libc", "libm"}
lines = pathlib.Path(sys.argv[1]).read_text().splitlines()
inputs = dict(line.split("=", 1) for line in lines)

if set(inputs) != expected:
    raise SystemExit(f"unexpected host C-runtime fields: {sorted(inputs)}")

for name, value in inputs.items():
    path = pathlib.Path(value)
    if not path.is_absolute():
        raise SystemExit(f"{name} is not absolute: {value}")
    if not path.exists():
        raise SystemExit(f"{name} does not exist: {value}")
