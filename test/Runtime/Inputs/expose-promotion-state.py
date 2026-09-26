"""Expose private promotion state for the C++ runtime oracle."""

from pathlib import Path
import sys

multiword = sys.argv[-1] == "--multiword"
arguments = sys.argv[1:-1] if multiword else sys.argv[1:]
source, output = arguments
text = Path(source).read_text()
# Export the private proof-state inspection points and keep the original model
# main available under another name. The scanner itself remains unchanged.
symbols = ("__obelisk_state_unknown", "__obelisk_eval_function_route_v1_0",
               "__obelisk_eval_function_route_v1_1",
               "__obelisk_eval_kernel_promotion_latched_v1",
               "__obelisk_eval_promotion_pending_mask_v1",
               "__obelisk_eval_route_promotion_pending_v1",
               "__obelisk_eval_promotion_latched_v1",
               "__obelisk_eval_step_four_state_fallback_v1",
               "__obelisk_eval_fast_nba_roots_v1")
if multiword:
    symbols = ("__obelisk_state_unknown",
               "__obelisk_eval_function_route_v1_0",
               "__obelisk_eval_function_route_v1_63",
               "__obelisk_eval_function_route_v1_64",
               "__obelisk_eval_route_promotion_pending_v1",
               "__obelisk_eval_route_promotion_dirty_v1")
for symbol in symbols:
    old = "llvm.mlir.global internal @" + symbol + "("
    assert text.count(old) == 1, symbol
    text = text.replace(old, "llvm.mlir.global @" + symbol + "(")
assert text.count("llvm.func @main(") == 1
text = text.replace("llvm.func @main(", "llvm.func @model_main(")
Path(output).write_text(text)
