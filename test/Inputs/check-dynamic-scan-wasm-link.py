import pathlib
import re
import subprocess
import sys


if len(sys.argv) != 6:
    raise SystemExit(
        "usage: check-dynamic-scan-wasm-link.py LLVM_DIST NO_WASM NO_MAP "
        "YES_WASM YES_MAP"
    )

llvm = pathlib.Path(sys.argv[1]) / "bin"
no_wasm = pathlib.Path(sys.argv[2])
no_map = pathlib.Path(sys.argv[3]).read_text()
yes_wasm = pathlib.Path(sys.argv[4])
yes_map = pathlib.Path(sys.argv[5]).read_text()


def code_size(path):
    result = subprocess.run(
        [str(llvm / "llvm-readobj"), "--sections", str(path)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if result.returncode:
        sys.stdout.buffer.write(result.stdout)
        raise SystemExit(f"could not inspect {path}")
    match = re.search(r"Type: CODE .*?Size: ([0-9]+)",
                      result.stdout.decode(), re.DOTALL)
    if not match:
        raise SystemExit(f"{path} has no WebAssembly CODE section")
    return int(match.group(1))


feature_members = ("ScanFormat.o", "DynamicScanBytecode.o")
for member in feature_members:
    if member in no_map:
        raise SystemExit(f"no-feature wasm extracted {member}")
    if member not in yes_map:
        raise SystemExit(f"dynamic-scan wasm did not extract {member}")
bitstream_members = ("ContainerBitstream.o", "ContainerBitstreamBytecode.o")
for member in bitstream_members:
    if member in no_map:
        raise SystemExit(f"no-feature wasm extracted {member}")
    if member in yes_map:
        raise SystemExit(f"scan-only wasm extracted {member}")
dpi_export_members = ("DPIExport.o", "DPIExportBytecode.o")
for member in dpi_export_members:
    if member in no_map:
        raise SystemExit(f"no-feature wasm extracted {member}")
    if member in yes_map:
        raise SystemExit(f"scan-only wasm extracted {member}")
feature_body_symbols = (
    "obelisk_rt_v1_dynamic_scan_link_anchor",
    "obelisk_rt_dynamic_scan_plan",
)
for symbol in feature_body_symbols:
    if symbol in no_map:
        raise SystemExit(f"no-feature wasm retained {symbol}")
for symbol in (*feature_body_symbols, "invokeDynamicScanIntrinsic"):
    if symbol not in yes_map:
        raise SystemExit(f"dynamic-scan wasm did not retain {symbol}")
bitstream_symbols = (
    "obelisk_rt_v1_container_export_bitstream",
    "obelisk_rt_v1_aggregate_export_bitstream",
    "obelisk_rt_v1_container_bitstream_link_anchor",
    "invokeContainerBitstreamIntrinsic",
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
for symbol in bitstream_symbols:
    if symbol in no_map:
        raise SystemExit(f"no-feature wasm retained {symbol}")
    if symbol in yes_map:
        raise SystemExit(f"scan-only wasm retained {symbol}")
for symbol in dpi_export_symbols:
    if symbol in no_map:
        raise SystemExit(f"no-feature wasm retained {symbol}")
    if symbol in yes_map:
        raise SystemExit(f"scan-only wasm retained {symbol}")
if code_size(yes_wasm) <= code_size(no_wasm):
    raise SystemExit("dynamic-scan wasm did not retain additional feature code")
