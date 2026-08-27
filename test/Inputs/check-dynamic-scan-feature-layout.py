import pathlib
import re
import subprocess
import sys


if len(sys.argv) != 9:
    raise SystemExit(
        "usage: check-dynamic-scan-feature-layout.py SOURCE ARCHIVE LLVM_DIST "
        "NO_GENERIC NO_BYTECODE YES_BYTECODE NO_BYTECODE_LTO "
        "YES_BYTECODE_LTO"
    )

source = pathlib.Path(sys.argv[1])
archive = pathlib.Path(sys.argv[2])
llvm = pathlib.Path(sys.argv[3]) / "bin"
no_feature_binaries = [pathlib.Path(sys.argv[index]) for index in (4, 5, 7)]
feature_binaries = [pathlib.Path(sys.argv[index]) for index in (6, 8)]

# ABI.cpp is compiled independently for native and wasm32 target runtimes.
# These width-independent assertions lock the wasm32 rule that uint64_t keeps
# eight-byte alignment in the v2 prefix and export tail.
abi_source = (source / "runtime/lib/ABI.cpp").read_text()
for assertion in (
    "ABI_SIZE_ALIGN(obelisk_rt_export_descriptor_v1, 64, 8);",
    "ABI_OFFSET(obelisk_rt_export_descriptor_v1, bytecode_function, 40);",
    "ABI_OFFSET(obelisk_rt_export_descriptor_v1, native_entry, 48);",
    "ABI_OFFSET(obelisk_rt_export_descriptor_v1, reserved_tail, ABI_PTR(56, 56));",
    "ABI_SIZE_ALIGN(obelisk_rt_execution_extension_v2, 40, 8);",
    "ABI_OFFSET(obelisk_rt_execution_extension_v2, sampled_range_count, 16);",
    "ABI_OFFSET(obelisk_rt_execution_extension_v2, exports, 24);",
    "ABI_OFFSET(obelisk_rt_execution_extension_v2, export_count, 32);",
):
    if assertion not in abi_source:
        raise SystemExit(f"missing native/wasm32 DPI export ABI assertion: {assertion}")


def run(arguments, *, input=None):
    result = subprocess.run(
        [str(argument) for argument in arguments],
        input=input,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if result.returncode:
        sys.stdout.buffer.write(result.stdout)
        raise SystemExit(f"command failed: {' '.join(map(str, arguments))}")
    return result.stdout


members = run([llvm / "llvm-ar", "t", archive]).decode().splitlines()


def cmake_list(path, variable):
    text = path.read_text()
    match = re.search(r"set\(" + re.escape(variable) + r"\s+(.*?)\)", text,
                      re.DOTALL)
    if not match:
        raise SystemExit(f"missing {variable} in {path}")
    body = re.sub(r"#.*", "", match.group(1))
    return re.findall(r"(?:lib/)?[A-Za-z][A-Za-z0-9]*(?:\.cpp)?", body)


runtime_cmake = source / "runtime/CMakeLists.txt"
host_common = cmake_list(runtime_cmake, "_obelisk_runtime_common_sources")
host_cold = cmake_list(runtime_cmake, "_obelisk_runtime_cold_tail_sources")
expected_members = [pathlib.Path(item).name + ".o"
                    for item in host_common + host_cold]
if members != expected_members:
    raise SystemExit("built runtime archive does not preserve its declared "
                     "common/cold-tail grouping")
if host_cold[:7] != [
    "lib/ScanFormat.cpp",
    "lib/DynamicScanBytecode.cpp",
    "lib/ContainerBitstream.cpp",
    "lib/RecursiveBitstream.cpp",
    "lib/ContainerBitstreamBytecode.cpp",
    "lib/DPIExport.cpp",
    "lib/DPIExportBytecode.cpp",
]:
    raise SystemExit("feature services are not in the cold-tail group")


def member_layout(member):
    contents = run([llvm / "llvm-ar", "p", archive, member])
    return run([llvm / "llvm-readobj", "--sections", "--symbols", "-"],
               input=contents).decode()


def require_feature_symbol(layout, symbol):
    expression = (
        r"Symbol \{(?:(?!Symbol \{).)*Name: [^\n]*"
        + re.escape(symbol)
        + r"[^\n]*\n(?:(?!Symbol \{).)*Section: \.obelisk\.feature\.text"
    )
    if not re.search(expression, layout, re.DOTALL):
        raise SystemExit(f"{symbol} is not in .obelisk.feature.text")


def require_feature_or_inlined(layout, symbol):
    if symbol in layout:
        require_feature_symbol(layout, symbol)


def require_recursive_feature_symbol(layout, symbol):
    expression = (
        r"Symbol \{(?:(?!Symbol \{).)*Name: [^\n]*"
        + re.escape(symbol)
        + r"[^\n]*\n(?:(?!Symbol \{).)*Section: "
        r"\.obelisk\.feature\.recursive_bitstream\.text"
    )
    if not re.search(expression, layout, re.DOTALL):
        raise SystemExit(
            f"{symbol} is not in recursive bit-stream feature text"
        )


def require_recursive_feature_or_inlined(layout, symbol):
    if symbol in layout:
        require_recursive_feature_symbol(layout, symbol)


scan_layout = member_layout("ScanFormat.cpp.o")
for symbol in (
    "obelisk_rt_dynamic_scan_plan",
    "obelisk_rt_v1_scan_dynamic_validate",
):
    require_feature_symbol(scan_layout, symbol)
require_feature_or_inlined(scan_layout, "parsePlan")

bytecode_layout = member_layout("DynamicScanBytecode.cpp.o")
require_feature_symbol(bytecode_layout, "invokeDynamicScanIntrinsic")
require_feature_symbol(bytecode_layout, "obelisk_rt_v1_dynamic_scan_link_anchor")
for symbol in (
    "readScalar",
    "writeScalar",
    "readString",
    "writeString",
):
    require_feature_or_inlined(bytecode_layout, symbol)

bitstream_layout = member_layout("ContainerBitstream.cpp.o")
require_feature_symbol(
    bitstream_layout, "obelisk_rt_v1_container_export_bitstream"
)
require_feature_symbol(
    bitstream_layout, "obelisk_rt_v1_aggregate_export_bitstream"
)
for symbol in (
    "copyBits",
    "packAssocBuffer",
    "packAssocOrder",
    "packContainer",
    "packBuffer",
    "readPlan64",
    "readRecord",
    "checkedRange",
    "overlaps",
    "validatePlan",
    "executePlan",
):
    require_feature_or_inlined(bitstream_layout, symbol)

recursive_bitstream_layout = member_layout("RecursiveBitstream.cpp.o")
require_recursive_feature_symbol(
    recursive_bitstream_layout, "obelisk_rt_v1_recursive_export_bitstream"
)
require_recursive_feature_symbol(
    recursive_bitstream_layout, "obelisk_rt_v1_recursive_bitstream_link_anchor"
)
require_recursive_feature_symbol(
    recursive_bitstream_layout, "obelisk_rt_expand_recursive_watch_group"
)
for symbol in (
    "readRecursive64",
    "validateRecursiveBody",
    "walkRecursiveBody",
    "acquireRecursiveContainer",
    "createRecursiveWatchGroup",
):
    require_recursive_feature_or_inlined(recursive_bitstream_layout, symbol)

managed_heap_layout = member_layout("ManagedHeap.cpp.o")
for symbol in (
    "obelisk_rt_managed_allocate_without_safepoint",
    "obelisk_rt_managed_object_acquire",
    "obelisk_rt_managed_object_release",
):
    require_feature_symbol(managed_heap_layout, symbol)
require_feature_or_inlined(
    managed_heap_layout, "allocateManagedWithoutSafepoint"
)

containers_layout = member_layout("Containers.cpp.o")
require_feature_or_inlined(containers_layout, "allocateBufferWithoutSafepoint")

bitstream_bytecode_layout = member_layout("ContainerBitstreamBytecode.cpp.o")
for symbol in (
    "invokeContainerBitstreamIntrinsic",
    "obelisk_rt_v1_container_bitstream_link_anchor",
):
    require_feature_symbol(bitstream_bytecode_layout, symbol)
for symbol in ("readScalar", "readManaged", "readBytes"):
    require_feature_or_inlined(bitstream_bytecode_layout, symbol)
weak_recursive_bitstream = re.search(
    r"Symbol \{(?:(?!Symbol \{).)*Name: [^\n]*"
    r"obelisk_rt_v1_recursive_export_bitstream[^\n]*\n"
    r"(?:(?!Symbol \{).)*Binding: Weak\s*"
    r"(?:(?!Symbol \{).)*Section: Undefined",
    bitstream_bytecode_layout,
    re.DOTALL,
)
if not weak_recursive_bitstream:
    raise SystemExit("legacy bit-stream bytecode has a strong recursive edge")

dpi_export_layout = member_layout("DPIExport.cpp.o")
for symbol in (
    "obelisk_rt_validate_dpi_exports",
    "obelisk_rt_v1_export_call",
    "obelisk_rt_v1_export_string",
    "obelisk_rt_v1_dpi_export_unpack_vector",
    "obelisk_rt_v1_dpi_export_pack_vector",
):
    require_feature_symbol(dpi_export_layout, symbol)
weak_export_bytecode_handler = re.search(
    r"Symbol \{(?:(?!Symbol \{).)*Name: [^\n]*"
    r"obelisk_rt_execute_dpi_export_bytecode[^\n]*\n"
    r"(?:(?!Symbol \{).)*Binding: Weak\s*"
    r"(?:(?!Symbol \{).)*Section: Undefined",
    dpi_export_layout,
    re.DOTALL,
)
if not weak_export_bytecode_handler:
    raise SystemExit("native DPI export has a strong bytecode handler edge")

dpi_export_bytecode_layout = member_layout("DPIExportBytecode.cpp.o")
require_feature_symbol(
    dpi_export_bytecode_layout,
    "obelisk_rt_v1_dpi_export_bytecode_link_anchor",
)
require_feature_symbol(
    dpi_export_bytecode_layout, "obelisk_rt_execute_dpi_export_bytecode"
)

for member, symbol in (
    ("Containers.cpp.o", "obelisk_rt_v1_string_scan_dynamic"),
    ("FileIO.cpp.o", "obelisk_rt_v1_file_scan_dynamic"),
):
    require_feature_symbol(member_layout(member), symbol)

# These are the two common-object edges that previously extracted both cold
# archive members even for a no-scan design. Teardown is now type-erased, and
# the dispatcher handler must be undefined weak so archive search ignores it.
runtime_layout = member_layout("Runtime.cpp.o")
if "obelisk_rt_dynamic_scan_destroy" in runtime_layout:
    raise SystemExit("common runtime teardown strongly references scan format")
weak_export_validator = re.search(
    r"Symbol \{(?:(?!Symbol \{).)*Name: [^\n]*obelisk_rt_validate_dpi_exports"
    r"[^\n]*\n(?:(?!Symbol \{).)*Binding: Weak\s*"
    r"(?:(?!Symbol \{).)*Section: Undefined",
    runtime_layout,
    re.DOTALL,
)
if not weak_export_validator:
    raise SystemExit("common runtime has a strong DPI export validator edge")
dispatcher_layout = member_layout("DesignBytecodeIntrinsics.cpp.o")
weak_handler = re.search(
    r"Symbol \{(?:(?!Symbol \{).)*Name: [^\n]*invokeDynamicScanIntrinsic"
    r"[^\n]*\n(?:(?!Symbol \{).)*Binding: Weak\s*"
    r"(?:(?!Symbol \{).)*Section: Undefined",
    dispatcher_layout,
    re.DOTALL,
)
weak_bitstream_handler = re.search(
    r"Symbol \{(?:(?!Symbol \{).)*Name: [^\n]*invokeContainerBitstreamIntrinsic"
    r"[^\n]*\n(?:(?!Symbol \{).)*Binding: Weak\s*"
    r"(?:(?!Symbol \{).)*Section: Undefined",
    dispatcher_layout,
    re.DOTALL,
)
if not weak_handler or not weak_bitstream_handler:
    raise SystemExit("common bytecode dispatcher has a strong feature edge")


for path in (source / "cmake/TargetNativeSupport.cmake",
             source / "cmake/TargetWasmSupport.cmake"):
    common = cmake_list(path, "_obelisk_target_runtime_common_sources")
    cold = cmake_list(path, "_obelisk_target_runtime_cold_tail_sources")
    if cold[:7] != [
        "ScanFormat",
        "DynamicScanBytecode",
        "ContainerBitstream",
        "RecursiveBitstream",
        "ContainerBitstreamBytecode",
        "DPIExport",
        "DPIExportBytecode",
    ]:
        raise SystemExit(f"feature services are not cold-tail sources in {path}")
    if set(common) & set(cold):
        raise SystemExit(f"common and cold-tail sources overlap in {path}")
    text = path.read_text()
    if not re.search(
        r"foreach\(source IN LISTS _obelisk_target_runtime_common_sources\s+"
        r"_obelisk_target_runtime_cold_tail_sources\)", text
    ):
        raise SystemExit(f"{path} does not append the declared cold-tail group")

# WebAssembly has one code section.  Its portable guarantee is therefore a
# noinline+cold backend hint plus feature objects at the archive tail, rather
# than the ELF-only named section checked above.
internal = (source / "runtime/lib/RuntimeInternal.h").read_text()
if not re.search(
    r"#elif defined\(__clang__\) \|\| defined\(__GNUC__\).*?"
    r"OBELISK_RT_FEATURE_TEXT __attribute__\(\(noinline, cold\)\)",
    internal,
    re.DOTALL,
):
    raise SystemExit("wasm dynamic scan services lack noinline+cold placement")

# Keep the unavoidable common bytecode-dispatch change to adjacent tail labels
# and one call into each separate cold object. In particular, this rejects a
# range check or feature implementation added to the ordinary dispatch path.
dispatcher = (source / "runtime/lib/DesignBytecodeIntrinsics.cpp").read_text()
if dispatcher.count("invokeDynamicScanIntrinsic") != 2:
    raise SystemExit("dynamic scan bytecode service leaked into the dispatcher")
if dispatcher.count("invokeContainerBitstreamIntrinsic") != 2:
    raise SystemExit("container bit-stream service leaked into the dispatcher")
tail = re.search(
    r"case OBELISK_RT_INTRINSIC_V1_STRING_SCAN_DYNAMIC:\s*"
    r"case OBELISK_RT_INTRINSIC_V1_FILE_SCAN_DYNAMIC:\s*"
    r"case OBELISK_RT_INTRINSIC_V1_SCAN_DYNAMIC_VALIDATE:\s*"
    r"if \(!invokeDynamicScanIntrinsic\)\s*"
    r"return OBELISK_RT_INVALID_BYTECODE;\s*"
    r"return invokeDynamicScanIntrinsic\(image, frame, context, site,\s*"
    r"signature\.id\);\s*"
    r"case OBELISK_RT_INTRINSIC_V1_CONTAINER_EXPORT_BITSTREAM:\s*"
    r"case OBELISK_RT_INTRINSIC_V1_AGGREGATE_EXPORT_BITSTREAM:\s*"
    r"if \(!invokeContainerBitstreamIntrinsic\)\s*"
    r"return OBELISK_RT_INVALID_BYTECODE;\s*"
    r"return invokeContainerBitstreamIntrinsic\(image, frame, context, site,\s*"
    r"signature\.id\);\s*default:",
    dispatcher,
)
if not tail:
    raise SystemExit("dynamic scan bytecode forwarding is not the switch tail")

image_validator = (source / "runtime/lib/DesignBytecodeImage.cpp").read_text()
recursive_image_contract = re.search(
    r"case OBELISK_RT_INTRINSIC_V1_CONTAINER_EXPORT_BITSTREAM:\s*"
    r"if \(signature\.flags == 1 \|\| signature\.flags == 2\)\s*"
    r"return site\.inputCount == 2 && site\.outputCount == 3 &&\s*"
    r"\(numeric\(input\(0\)\) \|\| managed\(input\(0\)\) \|\|\s*"
    r"string\(input\(0\)\)\) &&\s*"
    r"bytes\(input\(1\)\) &&\s*numeric\(output\(0\)\) &&\s*"
    r"twoStateBits\(output\(1\), 1\) &&\s*"
    r"twoStateBits\(output\(2\), 64\);",
    image_validator,
)
if not recursive_image_contract:
    raise SystemExit("recursive bit-stream bytecode layout is not rejected early")


def linked_layout(path):
    sections = run([llvm / "llvm-readobj", "--sections", path]).decode()
    symbols = run([llvm / "llvm-nm", "--defined-only", path]).decode()
    return sections, symbols


feature_symbols = (
    "obelisk_rt_v1_dynamic_scan_link_anchor",
    "invokeDynamicScanIntrinsic",
    "obelisk_rt_dynamic_scan_plan",
    "obelisk_rt_v1_scan_dynamic_validate",
    "obelisk_rt_v1_string_scan_dynamic",
    "obelisk_rt_v1_file_scan_dynamic",
)
bitstream_symbols = (
    "obelisk_rt_v1_container_bitstream_link_anchor",
    "invokeContainerBitstreamIntrinsic",
    "obelisk_rt_v1_container_export_bitstream",
    "obelisk_rt_v1_aggregate_export_bitstream",
    "obelisk_rt_v1_recursive_export_bitstream",
    "obelisk_rt_v1_recursive_bitstream_link_anchor",
    "obelisk_rt_expand_recursive_watch_group",
    "obelisk_rt_managed_allocate_without_safepoint",
    "obelisk_rt_managed_object_acquire",
    "obelisk_rt_managed_object_release",
)
dpi_export_symbols = (
    "obelisk_rt_validate_dpi_exports",
    "obelisk_rt_execute_dpi_export_bytecode",
    "obelisk_rt_v1_export_call",
    "obelisk_rt_v1_export_string",
    "obelisk_rt_v1_dpi_export_unpack_vector",
    "obelisk_rt_v1_dpi_export_pack_vector",
    "obelisk_rt_v1_dpi_export_bytecode_link_anchor",
)
for binary in no_feature_binaries:
    sections, symbols = linked_layout(binary)
    # The shared feature section may contain unrelated cold services (for
    # example, the bytecode scheduler's large ready-cohort accelerator).
    # Dynamic-scan pay-for-play is therefore identified by its complete symbol
    # set rather than by requiring the process-wide feature section to be
    # absent.
    for symbol in feature_symbols + bitstream_symbols + dpi_export_symbols:
        if symbol in symbols:
            raise SystemExit(f"no-feature binary retained {symbol}: {binary}")

for binary in feature_binaries:
    sections, symbols = linked_layout(binary)
    if ".obelisk.feature.text" not in sections:
        raise SystemExit(f"dynamic bytecode binary has no feature text: {binary}")
    for symbol in feature_symbols:
        if symbol not in symbols:
            raise SystemExit(f"dynamic bytecode binary omitted {symbol}: {binary}")
