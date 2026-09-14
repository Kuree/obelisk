"""Exercise the periodic-control ABI with a 32-bit-pointer target layout."""

from pathlib import Path
import subprocess
import sys

planned, prefix, optimizer, filecheck, checks = sys.argv[1:]
source = Path(planned).read_text()
assert 'llvm.target_triple = "x86_64-unknown-linux-gnu"' in source
assert "e-m:e-p:64:64-i64:64-n8:16:32:64-S128" in source
source = source.replace("x86_64-unknown-linux-gnu", "wasm32-unknown-emscripten")
source = source.replace("e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
                        "e-m:e-p:32:32-i64:64-n32:64-S128")
wasm = Path(prefix + ".wasm32.planned.mlir")
wasm.write_text(source)
result = subprocess.run(
    [optimizer, str(wasm), "--convert-obelisk-sim-processes-to-llvm-coroutines"],
    capture_output=True, check=True,
)
assert b'"e-m:e-p:32:32-i64:64-n32:64-S128"' in result.stdout
assert b'"wasm32-unknown-emscripten"' in result.stdout
subprocess.run([filecheck, checks], input=result.stdout, check=True)
