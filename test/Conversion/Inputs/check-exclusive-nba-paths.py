"""A branch join or backedge must not masquerade as exclusive NBA arms."""
import pathlib
import subprocess
import sys
import tempfile

source = pathlib.Path(sys.argv[1]).read_text()
pipeline = source.split("--pass-pipeline='", 1)[1].split("'", 1)[0]
first = "cf.br ^wait // first arm exits"
second = "cf.br ^wait // second arm exits"
assert source.count(first) == source.count(second) == 1
for name, text in {
    "exclusive": source,
    "sequential": source.replace(first, "cf.br ^second"),
    "backedge": source.replace(second, "cf.cond_br %take, ^first, ^wait"),
}.items():
    result = subprocess.run(
        [sys.argv[2], "--pass-pipeline=" + pipeline], input=text,
        text=True, capture_output=True, timeout=60,
    )
    assert result.returncode == 0, (name, result.stderr)
    slots = result.stdout.count("llvm.mlir.global internal @__obelisk_eval_nba_valid_")
    assert slots == (4 if name == "exclusive" else 0), (name, slots)
    fast = "llvm.func @__obelisk_eval_fast_coordinator_v1(" in result.stdout
    assert fast == (name == "exclusive"), name

# Width and executor choice are independent axes of the same contract. Cover
# an odd-width window, byte-aligned fields, and both sides of the 64-bit limit.
# In particular the initial canonical X bits must clear on a known write, and
# a subsequent four-state literal must retain its unknown plane even when all
# of the executor's INPUTS are known.
llvm_bin = pathlib.Path(sys.argv[3]) / "bin"
support = pathlib.Path(sys.argv[4])
with tempfile.TemporaryDirectory(prefix="obelisk-nba-domains-") as directory:
    obj = pathlib.Path(directory) / "test.o"
    exe = pathlib.Path(directory) / "test.exe"
    for width in (33, 40, 63, 64):
        digits = 9  # The fixture explicitly requests %09h, at every width.
        expected = (
            f"{'11':0>{digits}} {'22':0>{digits}}\n"
            f"{'5X':0>{digits}} {'0':0>{digits}}\n"
        )
        for scheduler in (0, 1, 2, 3):
            text = source.replace("logic<34>", f"logic<{width}>")
            text = text.replace("i34", f"i{width}").replace(
                "obelisk.native_scheduler = 3 : i32",
                f"obelisk.native_scheduler = {scheduler} : i32",
            )
            lowered = subprocess.run(
                [sys.argv[2], "--pass-pipeline=" + pipeline],
                input=text.encode(), capture_output=True, check=True,
            ).stdout
            translated = subprocess.run(
                [llvm_bin / "mlir-translate", "--mlir-to-llvmir"],
                input=lowered, capture_output=True, check=True,
            ).stdout
            optimized = subprocess.run(
                [llvm_bin / "opt", "-passes=coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>"],
                input=translated, capture_output=True, check=True,
            ).stdout
            subprocess.run(
                [llvm_bin / "llc", "-filetype=obj", "-relocation-model=pic", "-o", obj],
                input=optimized, capture_output=True, check=True,
            )
            subprocess.run(
                [llvm_bin / "clang++", obj, support / "libobelisk_rt.a",
                 support / "libc++.a", support / "libc++abi.a",
                 support / "libunwind.a", "-nostdlib++", "-lpthread", "-ldl", "-o", exe],
                capture_output=True, check=True,
            )
            for tier in ("native", "bytecode"):
                result = subprocess.run(
                    [exe, "--execution-tier=" + tier], text=True,
                    capture_output=True, timeout=10, check=True,
                )
                assert result.stdout == expected, (
                    width, scheduler, tier, result.stdout, expected,
                )
