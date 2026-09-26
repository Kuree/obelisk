import os
import pathlib
import shutil
import subprocess
import sys


def fingerprint(path):
    status = path.stat()
    return status.st_ino, status.st_size, status.st_mtime_ns


cmake = sys.argv[1]
scratch = pathlib.Path(sys.argv[2]).resolve()
source_root = pathlib.Path(sys.argv[3]).resolve()
llvm_dist = pathlib.Path(sys.argv[4]).resolve()
slang_source = pathlib.Path(sys.argv[5]).resolve()
reflection_include = pathlib.Path(sys.argv[6]).resolve()
shutil.rmtree(scratch, ignore_errors=True)
scratch.mkdir(parents=True)

target_candidates = [
    path.name
    for path in (llvm_dist / "lib/clang/22/lib").iterdir()
    if (path / "libclang_rt.builtins.a").is_file()
    and (llvm_dist / "lib" / path.name / "libc++.a").is_file()
]
if len(target_candidates) != 1:
    raise SystemExit(f"expected one LLVM native runtime: {target_candidates}")
target_triple = target_candidates[0]


def run_checked(command, message):
    result = subprocess.run(
        [str(item) for item in command],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if result.returncode:
        sys.stdout.buffer.write(result.stdout)
        raise SystemExit(message)
    return result


def configure_graph(source, build, runtime_source=None):
    command = [
        cmake,
        "-S",
        source,
        "-B",
        build,
        "-G",
        "Ninja",
    ]
    if runtime_source is not None:
        command.append(f"-DOBELISK_TARGET_RUNTIME_SOURCE_DIR={runtime_source}")
    run_checked(command, "isolated CMake graph configuration failed")


def build_graph(build, target, expect_success=True):
    result = subprocess.run(
        [cmake, "--build", str(build), "--target", target],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if expect_success and result.returncode:
        sys.stdout.buffer.write(result.stdout)
        raise SystemExit(f"isolated CMake graph target failed: {target}")
    if not expect_success and result.returncode == 0:
        raise SystemExit(f"isolated CMake graph unexpectedly succeeded: {target}")
    return result


# Exercise the production runtime and staging graph without configuring the
# compiler or its frontend dependencies.
graph_source = scratch / "graph-source"
graph_build = scratch / "graph-build"
graph_runtime = scratch / "graph-runtime"
graph_source.mkdir()
shutil.copytree(source_root / "runtime", graph_runtime)
# Test the production dependency/staging graph with tiny translation units.
# Compiling the runtime implementation again (including after a header edit)
# adds minutes without testing any additional build-graph behavior. The normal
# build and runtime tests cover the real implementation. Keep every source and
# header path so archive membership and dependency checks still use the real
# graph, compiler, archiver, and staging commands.
for source in (graph_runtime / "lib").glob("*.cpp"):
    includes = ""
    if source.stem == "Runtime":
        includes = '#include "obelisk/Runtime/Runtime.h"\n'
    elif source.stem == "Bytecode":
        includes = '#include "GraphIndirect.h"\n'
    source.write_text(
        includes + f'extern "C" int graph_probe_{source.stem}() {{ return 0; }}\n'
    )
(graph_runtime / "lib/GraphIndirect.h").write_text('#include "GraphPrivate.h"\n')
(graph_runtime / "lib/GraphPrivate.h").write_text('// transitive dependency\n')
(graph_source / "CMakeLists.txt").write_text(
    "\n".join(
        (
            "cmake_minimum_required(VERSION 3.20)",
            "project(ObeliskNativeGraphTest LANGUAGES NONE)",
            f'set(OBELISK_SOURCE_DIR [[{source_root}]])',
            f'set(OBELISK_LLVM_DIST_DIR [[{llvm_dist}]])',
            f'set(OBELISK_SLANG_SOURCE_DIR [[{slang_source}]])',
            f'set(OBELISK_TARGET_REFLECTION_INCLUDE_DIR [[{reflection_include}]])',
            'set(OBELISK_LLVM_VERSION "22.1.6")',
            'set(LLVM_VERSION_MAJOR "22")',
            f'include([[{source_root / "cmake/TargetNativeSupport.cmake"}]])',
            "",
        )
    )
)
configure_graph(graph_source, graph_build, graph_runtime)
build_graph(graph_build, "obelisk_native_support")
support_stamps = sorted(graph_build.glob("native-support-*.complete"))
runtime_archive = graph_build / "target-runtime/libobelisk_rt.a"
runtime_lto_archive = graph_build / "target-runtime/libobelisk_rt_lto.a"
support = graph_build / "lib/obelisk/targets" / target_triple
if len(support_stamps) != 1:
    raise SystemExit("first graph build did not create one support stamp")
first_runtime = fingerprint(runtime_archive)
first_runtime_lto = fingerprint(runtime_lto_archive)
first_support = fingerprint(support_stamps[0])

native_members = [
    "ABI.o",
    "Bytecode.o",
    "Containers.o",
    "Coverage.o",
    "CoverageBlockEvents.o",
    "CoverageDatabase.o",
    "DesignBytecode.o",
    "DesignBytecodeImage.o",
    "DesignBytecodeIntrinsics.o",
    "DesignBytecodeLogic.o",
    "DesignBytecodeNets.o",
    "DesignBytecodeObservers.o",
    "DesignBytecodeRoots.o",
    "DesignDatabase.o",
    "DPI.o",
    "FileIO.o",
    "Format.o",
    "ManagedHeap.o",
    "Plusargs.o",
    "Process.o",
    "ProcessAllocation.o",
    "ProcessAOT.o",
    "ProcessNativeState.o",
    "ProcessNBA.o",
    "ProcessObservers.o",
    "ProcessSignals.o",
    "ProcessState.o",
    "ProcessTransitions.o",
    "ProcessValidation.o",
    "Random.o",
    "RandSolve.o",
    "RandSolveWide.o",
    "Runtime.o",
    "Sampled.o",
    "StochasticQueue.o",
    "System.o",
    "VCD.o",
    "VPI.o",
    # Pay-for-play services are deliberately the archive tail. The wasm graph
    # uses the same order when it cannot provide an ELF feature text section.
    "ScanFormat.o",
    "DynamicScanBytecode.o",
    "ContainerBitstream.o",
    "RecursiveBitstream.o",
    "ContainerBitstreamBytecode.o",
    "ClassBitstream.o",
    "ClassBitstreamBytecode.o",
    "DPIExport.o",
    "DPIExportBytecode.o",
    "DPIOpenArray.o",
    "DPIAggregate.o",
]
lto_members = [
    pathlib.Path(member).with_suffix(".bc").name for member in native_members
]
archive_inspection = scratch / "archive-inspection"
archive_inspection.mkdir()


def inspect_archive(archive, expected_members, bitcode):
    members = run_checked(
        [llvm_dist / "bin/llvm-ar", "t", archive],
        f"could not inspect target runtime archive {archive.name}",
    ).stdout.decode().splitlines()
    if members != expected_members:
        raise SystemExit(
            f"target runtime archive has unstable members: {archive}: {members}"
        )
    for member in members:
        contents = run_checked(
            [llvm_dist / "bin/llvm-ar", "p", archive, member],
            f"could not extract {member} from {archive.name}",
        ).stdout
        if bitcode:
            output = archive_inspection / f"{archive.name}-{member}.ll"
            parser = [llvm_dist / "bin/llvm-dis", "-o", output, "-"]
        else:
            parser = [llvm_dist / "bin/llvm-readobj", "--file-headers", "-"]
        parsed = subprocess.run(
            [str(item) for item in parser],
            input=contents,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        if parsed.returncode:
            sys.stdout.buffer.write(parsed.stdout)
            kind = "LLVM bitcode" if bitcode else "ELF"
            raise SystemExit(f"{archive.name} member {member} is not {kind}")


def verify_staged_archives():
    for archive in (runtime_archive, runtime_lto_archive):
        staged = support / archive.name
        if not staged.is_file() or staged.read_bytes() != archive.read_bytes():
            raise SystemExit(f"native support did not stage {archive.name}")
    complete = (support / ".complete").read_text().splitlines()
    if len(complete) != 3 or len(complete[2]) != 64:
        raise SystemExit("native-support completion record has no content hash")


inspect_archive(runtime_archive, native_members, bitcode=False)
inspect_archive(runtime_lto_archive, lto_members, bitcode=True)
verify_staged_archives()

# An unchanged second build is a true graph no-op.
build_graph(graph_build, "obelisk_native_support")
if fingerprint(runtime_archive) != first_runtime:
    raise SystemExit("second graph build rebuilt the target runtime")
if fingerprint(runtime_lto_archive) != first_runtime_lto:
    raise SystemExit("second graph build rebuilt the Full-LTO target runtime")
if fingerprint(support_stamps[0]) != first_support:
    raise SystemExit("second graph build restaged native support")

# A runtime-only source edit rebuilds the archive and staged support.
# Recreating the archive must also retain deterministic members.
with (graph_runtime / "lib/Runtime.cpp").open("a") as stream:
    stream.write("\n// isolated build-graph rebuild probe\n")
build_graph(graph_build, "obelisk_native_support")
if fingerprint(runtime_archive) == first_runtime:
    raise SystemExit("runtime-only change did not rebuild the runtime archive")
if fingerprint(runtime_lto_archive) == first_runtime_lto:
    raise SystemExit(
        "runtime-only change did not rebuild the Full-LTO runtime archive"
    )
if fingerprint(support_stamps[0]) == first_support:
    raise SystemExit("runtime-only change did not restage native support")
inspect_archive(runtime_archive, native_members, bitcode=False)
inspect_archive(runtime_lto_archive, lto_members, bitcode=True)
verify_staged_archives()

# Public and internal runtime headers are also dependencies of both archive
# forms and of the staged content-hash command.
unrelated_object = graph_build / "target-runtime/Plusargs.o"
unrelated_lto = graph_build / "target-runtime/Plusargs.bc"
unrelated_fingerprints = (fingerprint(unrelated_object), fingerprint(unrelated_lto))
source_runtime = fingerprint(runtime_archive)
source_runtime_lto = fingerprint(runtime_lto_archive)
source_support = fingerprint(support_stamps[0])
with (graph_runtime / "include/obelisk/Runtime/Runtime.h").open("a") as stream:
    stream.write("\n// isolated build-graph header rebuild probe\n")
build_graph(graph_build, "obelisk_native_support")
if fingerprint(runtime_archive) == source_runtime:
    raise SystemExit("runtime-header change did not rebuild the runtime archive")
if fingerprint(runtime_lto_archive) == source_runtime_lto:
    raise SystemExit(
        "runtime-header change did not rebuild the Full-LTO runtime archive"
    )
if fingerprint(support_stamps[0]) == source_support:
    raise SystemExit("runtime-header change did not restage native support")
verify_staged_archives()
if (fingerprint(unrelated_object), fingerprint(unrelated_lto)) != unrelated_fingerprints:
    raise SystemExit("runtime-header change rebuilt an unrelated source")

# Track transitive private includes discovered by the compiler; neither
# variant may miss a header absent from the handwritten dependency list.
bytecode_objects = [
    graph_build / f"target-runtime/Bytecode.{ext}" for ext in ("o", "bc")
]
bytecode_before = [fingerprint(path) for path in bytecode_objects]
with (graph_runtime / "lib/GraphPrivate.h").open("a") as stream:
    stream.write("// transitive private-header edit\n")
build_graph(graph_build, "obelisk_native_support")
if any(
    fingerprint(path) == before
    for path, before in zip(bytecode_objects, bytecode_before)
):
    raise SystemExit("private-header change did not rebuild both runtime variants")
if (fingerprint(unrelated_object), fingerprint(unrelated_lto)) != unrelated_fingerprints:
    raise SystemExit("private-header change rebuilt an unrelated source")
verify_staged_archives()

# Recovering a missing native object must not recompile its intact LTO sibling.
bytecode_lto_before = fingerprint(bytecode_objects[1])
bytecode_objects[0].unlink()
build_graph(graph_build, "obelisk_native_support")
if not bytecode_objects[0].is_file():
    raise SystemExit("missing native object did not rebuild")
if fingerprint(bytecode_objects[1]) != bytecode_lto_before:
    raise SystemExit("missing native object unnecessarily rebuilt its LTO sibling")

# Missing declared byproducts cause the staging command to self-heal and
# atomically move the public relative link to a complete version.
(support / "README.txt").unlink()
build_graph(graph_build, "obelisk_native_support")
if not (support / "README.txt").is_file():
    raise SystemExit("staged native-support byproduct did not self-heal")
published = pathlib.Path(os.readlink(support))
if published.is_absolute():
    raise SystemExit("published native-support link is not relocatable")
