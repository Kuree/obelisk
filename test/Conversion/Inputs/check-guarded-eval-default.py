"""Check that an uncovered ready owner requests recoverable hybrid dispatch."""

import re
import sys

text = sys.stdin.read()
function = re.search(
    r"  llvm.func @__obelisk_eval_steady_two_state_coordinator_v1\((.*?)\).*?\n  }",
    text,
    re.S,
)
assert function, "missing guarded coordinator"
arguments = re.findall(r"(%[\w]+): !llvm.ptr", function[1])
assert len(arguments) == 4, arguments
default = re.search(r"llvm.switch .*? : i64, \^(\w+) \[", function[0])
assert default, "missing ready-owner switch"
blocks = dict(re.findall(
    r"^  \^(\w+)[^\n]*\n(.*?)(?=^  \^|^  })", function[0], re.M | re.S
))
rejected = blocks[default[1]]
flag = re.search(r"(%\w+) = llvm.mlir.constant\(true\) : i1", rejected)
assert flag, "uncovered owner must set the recoverable guard-result flag"
assert re.search(r"llvm.store " + flag[1] + r", " + arguments[-1] + r"\b", rejected)
assert "llvm.mlir.constant(13 : i32)" in rejected, rejected
assert "llvm.return" in rejected, rejected
assert "__obelisk_aot_model_ingress_v1" not in rejected, "rejection must retain work"
print("guarded eval default: PASS")
