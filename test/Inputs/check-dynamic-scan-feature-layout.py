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
if host_cold[:2] != ["lib/ScanFormat.cpp", "lib/DynamicScanBytecode.cpp"]:
    raise SystemExit("dynamic scan services are not in the cold-tail group")


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
dispatcher_layout = member_layout("DesignBytecodeIntrinsics.cpp.o")
weak_handler = re.search(
    r"Symbol \{(?:(?!Symbol \{).)*Name: [^\n]*invokeDynamicScanIntrinsic"
    r"[^\n]*\n(?:(?!Symbol \{).)*Binding: Weak\s*"
    r"(?:(?!Symbol \{).)*Section: Undefined",
    dispatcher_layout,
    re.DOTALL,
)
if not weak_handler:
    raise SystemExit("common bytecode dispatcher has a strong feature edge")


for path in (source / "cmake/TargetNativeSupport.cmake",
             source / "cmake/TargetWasmSupport.cmake"):
    common = cmake_list(path, "_obelisk_target_runtime_common_sources")
    cold = cmake_list(path, "_obelisk_target_runtime_cold_tail_sources")
    if cold[:2] != ["ScanFormat", "DynamicScanBytecode"]:
        raise SystemExit(f"dynamic scan services are not cold-tail sources in {path}")
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

# Keep the unavoidable common bytecode-dispatch change to three tail labels
# and one call into the separate cold object.  In particular, this rejects a
# range check or feature implementation added to the ordinary dispatch path.
dispatcher = (source / "runtime/lib/DesignBytecodeIntrinsics.cpp").read_text()
if dispatcher.count("invokeDynamicScanIntrinsic") != 2:
    raise SystemExit("dynamic scan bytecode service leaked into the dispatcher")
tail = re.search(
    r"case OBELISK_RT_INTRINSIC_V1_STRING_SCAN_DYNAMIC:\s*"
    r"case OBELISK_RT_INTRINSIC_V1_FILE_SCAN_DYNAMIC:\s*"
    r"case OBELISK_RT_INTRINSIC_V1_SCAN_DYNAMIC_VALIDATE:\s*"
    r"if \(!invokeDynamicScanIntrinsic\)\s*"
    r"return OBELISK_RT_INVALID_BYTECODE;\s*"
    r"return invokeDynamicScanIntrinsic\(image, frame, context, site,\s*"
    r"signature\.id\);\s*default:",
    dispatcher,
)
if not tail:
    raise SystemExit("dynamic scan bytecode forwarding is not the switch tail")


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
for binary in no_feature_binaries:
    sections, symbols = linked_layout(binary)
    if ".obelisk.feature.text" in sections:
        raise SystemExit(f"no-feature binary retained feature text: {binary}")
    for symbol in feature_symbols:
        if symbol in symbols:
            raise SystemExit(f"no-feature binary retained {symbol}: {binary}")

for binary in feature_binaries:
    sections, symbols = linked_layout(binary)
    if ".obelisk.feature.text" not in sections:
        raise SystemExit(f"dynamic bytecode binary has no feature text: {binary}")
    for symbol in feature_symbols:
        if symbol not in symbols:
            raise SystemExit(f"dynamic bytecode binary omitted {symbol}: {binary}")
