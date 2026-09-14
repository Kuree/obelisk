"""Exercise native-only and bytecode-backed path storage at O0 and O3."""

from pathlib import Path
import subprocess
import sys

source, prefix, llvm, support, filecheck = sys.argv[1:]
llvm = Path(llvm) / "bin"
support = Path(support)
for image in ("native", "bytecode"):
    ir = subprocess.check_output([
        llvm / "mlir-translate", "--mlir-to-llvmir", f"{prefix}.{image}.mlir"])
    for level in (0, 3):
        optimized = subprocess.check_output([
            llvm / "opt", "-passes=coro-early,coro-split<reuse-storage>,"
            f"coro-cleanup,default<O{level}>"], input=ir)
        obj = f"{prefix}.{image}.o{level}.o"
        exe = f"{prefix}.{image}.o{level}.exe"
        subprocess.run([llvm / "llc", "-filetype=obj", "-relocation-model=pic",
                        "-o", obj], input=optimized, check=True)
        subprocess.run([llvm / "clang++", obj,
                        *[support / name for name in (
                            "libobelisk_rt.a", "libc++.a", "libc++abi.a",
                            "libunwind.a")],
                        "-nostdlib++", "-lpthread", "-ldl", "-o", exe], check=True)
        for tier in (("native",) if image == "native" else ("native", "bytecode")):
            result = subprocess.run([exe, f"--execution-tier={tier}"],
                                    capture_output=True, check=True, timeout=15)
            subprocess.run([filecheck, source], input=result.stdout, check=True)
            print(f"{image} O{level} {tier}: PASS", flush=True)
