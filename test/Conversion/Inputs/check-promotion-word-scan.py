"""Execute the pass-produced scanner against a scalar bit-range oracle."""

from pathlib import Path
import subprocess
import sys

source, prefix, translate, llvm_bin, support = sys.argv[1:]
llvm_bin, support = Path(llvm_bin), Path(support)
text = Path(source).read_text()
# Export the private proof-state inspection points and keep the original model
# main available under another name. The scanner itself remains unchanged.
for symbol in ("__obelisk_state_unknown", "__obelisk_eval_function_route_v1_0",
               "__obelisk_eval_function_route_v1_1",
               "__obelisk_eval_kernel_promotion_latched_v1",
               "__obelisk_eval_promotion_pending_mask_v1"):
    old = "llvm.mlir.global internal @" + symbol + "("
    assert text.count(old) == 1, symbol
    text = text.replace(old, "llvm.mlir.global @" + symbol + "(")
assert text.count("llvm.func @main(") == 1
text = text.replace("llvm.func @main(", "llvm.func @model_main(")
exposed = Path(prefix + ".exposed.mlir")
exposed.write_text(text)
ir = subprocess.check_output([translate, "--mlir-to-llvmir", str(exposed)])
for level in ("O0", "O3"):
    lowered = Path(prefix + "." + level + ".ll")
    with lowered.open("wb") as output:
        subprocess.run(
            [str(llvm_bin / "opt"),
             "-passes=coro-early,coro-split<reuse-storage>,coro-cleanup,default<" + level + ">"],
            input=ir, stdout=output, check=True,
        )
    executable = prefix + "." + level + ".exe"
    subprocess.run(
        [str(llvm_bin / "clang++"), "-" + level, str(lowered),
         str(Path(__file__).with_suffix(".cpp")),
         *[str(support / lib) for lib in
           ("libobelisk_rt.a", "libc++.a", "libc++abi.a", "libunwind.a")],
         "-nostdlib++", "-lpthread", "-ldl", "-o", executable],
        check=True,
    )
    result = subprocess.run([executable], capture_output=True, check=True)
    assert result.stdout == b"promotion word scans passed\n", result.stdout
