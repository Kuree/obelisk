#!/usr/bin/env python3

import argparse
import subprocess
import sys
import os
import tempfile
import shutil
from pathlib import Path


def _find_circt_dir():
    env = os.environ.get("CIRCT_DIR")
    if env:
        return Path(env)

    try:
        import pycde
        pkg = Path(pycde.__path__[0]).resolve()
        # Walk up to find a dir with bin/arcilator
        for parent in [pkg] + list(pkg.parents):
            cand = parent / "bin" / "arcilator"
            if cand.is_file() and os.access(cand, os.X_OK):
                return parent
    except ImportError:
        pass

    for cmd in ("arcilator", "circt-verilog"):
        found = shutil.which(cmd)
        if found:
            return Path(found).resolve().parent.parent

    print("CIRCT not found. Set CIRCT_DIR or ensure arcilator/circt-verilog are on PATH.", file=sys.stderr)
    sys.exit(1)


def _find_tool(name):
    env = os.environ.get("CIRCT_DIR")
    if env:
        cand = Path(env) / "bin" / name
        if cand.is_file():
            return cand
    return shutil.which(name) or _find_circt_dir() / "bin" / name


CIRCT_DIR = _find_circt_dir()
CIRCT_VERILOG = _find_tool("circt-verilog")
ARCILATOR = _find_tool("arcilator")
LLC = _find_tool("llc")
LIBS_DIR = CIRCT_DIR / "lib"

RUNTIME_LIBS = [
    "CIRCTArcRuntime",
    "CIRCTSupport",
    "LLVMSupport",
    "LLVMDemangle",
    "LLVMTargetParser",
    "fmt",
]

SYS_LIBS = ["pthread", "rt", "dl", "m"]


def run(cmd, **kwargs):
    res = subprocess.run(cmd, capture_output=True, text=True, **kwargs)
    if res.returncode != 0:
        print(f"Error running: {' '.join(str(a) for a in cmd)}", file=sys.stderr)
        print(res.stderr[:2000], file=sys.stderr)
        sys.exit(1)
    return res


def sv_to_mlir(sv_files, output_dir):
    mlir_files = []
    for sv in sv_files:
        sv_path = Path(sv)
        stem = sv_path.stem
        mlir_out = output_dir / f"{stem}.mlir"
        run([str(CIRCT_VERILOG), str(sv_path), "--ir-hw", "-o", str(mlir_out)])
        mlir_files.append(mlir_out)
    return mlir_files


def merge_mlir(mlir_files, output_path):
    bodies = []
    for f in mlir_files:
        content = f.read_text().strip()
        content = content.removeprefix("module {").removesuffix("}").strip()
        bodies.append(content)
    merged = "\n\n".join(bodies)
    output_path.write_text(merged)
    return output_path


def build():
    parser = argparse.ArgumentParser(description="Compile SV files to standalone ELF via CIRCT/arcilator")
    parser.add_argument("sv_files", nargs="+", help="SystemVerilog RTL files")
    parser.add_argument("-t", "--testbench", help="arc.sim testbench MLIR file (optional; without it, only models are compiled)")
    parser.add_argument("-o", "--output", default="a.elf", help="Output path")
    parser.add_argument("--circt-dir", help="Override CIRCT installation directory")
    args = parser.parse_args()

    if args.circt_dir:
        global CIRCT_DIR, CIRCT_VERILOG, ARCILATOR, LLC, LIBS_DIR
        CIRCT_DIR = Path(args.circt_dir)
        CIRCT_VERILOG = CIRCT_DIR / "bin" / "circt-verilog"
        ARCILATOR = CIRCT_DIR / "bin" / "arcilator"
        LLC = CIRCT_DIR / "bin" / "llc"
        LIBS_DIR = CIRCT_DIR / "lib"

    workdir = Path(tempfile.mkdtemp(prefix="circt_"))

    print(f"[1/5] Converting {len(args.sv_files)} SV file(s) to MLIR via circt-verilog...")
    mlir_files = sv_to_mlir(args.sv_files, workdir)

    merged_mlir = workdir / "design.mlir"
    merge_mlir(mlir_files, merged_mlir)
    print(f"      Merged -> {merged_mlir}")

    sim_mlir = workdir / "sim.mlir"
    entry = None
    if args.testbench:
        tb = Path(args.testbench)
        combined = merged_mlir.read_text() + "\n" + tb.read_text()
        sim_mlir.write_text(combined)
        entry = "main"
        print(f"      + testbench {tb}")
    else:
        sim_mlir.write_text(merged_mlir.read_text())
        print("      (no testbench, emitting model object only)")

    print(f"[2/5] Running arcilator AOT compilation...")
    llvm_ir = workdir / "model.ll"
    obj_file = workdir / "model.o"

    arcilator_cmd = [str(ARCILATOR), str(sim_mlir), "--emit-llvm", "-o", str(llvm_ir)]
    if entry:
        arcilator_cmd += ["--run", f"--jit-entry={entry}", "--jit-object-file=" + str(obj_file)]
    run(arcilator_cmd)

    if not obj_file.exists():
        print(f"[3/5] Compiling LLVM IR to object file...")
        run([str(LLC), "-filetype=obj", str(llvm_ir), "-o", str(obj_file)])
    else:
        print(f"[3/5] Object file already produced by arcilator.")

    output_path = Path(args.output)
    if entry:
        print(f"[4/5] Linking ELF...")
        link_cmd = ["clang++", "-no-pie", "-o", str(output_path), str(obj_file)]
        link_cmd += ["-L" + str(LIBS_DIR)]
        for lib in RUNTIME_LIBS:
            link_cmd += ["-l" + lib]
        for lib in SYS_LIBS:
            link_cmd += ["-l" + lib]
        run(link_cmd)
        if output_path.exists() and output_path.stat().st_size > 0:
            print(f"[5/5] Done: {output_path} ({output_path.stat().st_size} bytes)")
            print(f"      Run with: {output_path}")
        else:
            print("ERROR: ELF not produced", file=sys.stderr)
            sys.exit(1)
    else:
        obj_file.rename(output_path)
        print(f"[4/4] Done: {output_path} ({output_path.stat().st_size} bytes, model object only)")
        print(f"      Link with -lCIRCTArcRuntime and a testbench to create an executable")


if __name__ == "__main__":
    build()