# -*- Python -*-

import os
import shutil
import sys

from lit.llvm import llvm_config

sys.path.insert(
    0, os.path.join(os.path.dirname(__file__), os.pardir, "utils", "lit")
)
from talos_lit_format import TalosShTest

config.name = "TALOS"
config.test_format = TalosShTest()
config.suffixes = [".mlir", ".sv", ".test", ".sdf", ".ll"]
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = os.path.join(config.talos_obj_root, "test")

config.excludes = [
    "CMakeLists.txt",
    "Inputs",
    "lit.cfg.py",
    "lit.site.cfg.py",
    "lit.site.cfg.py.in",
]

llvm_config.with_system_environment(["HOME", "TMP", "TEMP"])
llvm_config.with_environment(
    "PATH", os.path.join(config.talos_llvm_dist, "bin"), append_path=True
)
if sys.platform == "win32":
    llvm_config.with_system_environment(
        ["SYSTEMROOT", "COMSPEC", "PATHEXT", "INCLUDE", "LIB", "LIBPATH",
         "VCINSTALLDIR", "VCToolsInstallDir", "WindowsSdkDir", "WindowsSDKVersion"]
    )
    config.available_features.add("windows")
elif "-linux-" in config.talos_target_triple:
    config.available_features.add("native-elf")
elif sys.platform == "darwin":
    # Upstream Clang does not ask xcrun for the SDK; it reads SDKROOT, as
    # Xcode's tools do, for headers and link stubs.
    llvm_config.with_environment("SDKROOT", config.talos_macos_sysroot)
    config.available_features.add("native-macho")
if sys.platform != "win32":
    config.available_features.add("llvm-pass-plugin")
config.substitutions.append(("%talos", config.talos_driver_executable))
# The test driver lowers resource limits so tests can reach them with small
# inputs (see test/Support/DriverTestMain.cpp).
config.substitutions.append(
    ("%test-talos", config.talos_test_driver_executable)
)
config.substitutions.append(
    ("%host-c-runtime-test", config.talos_host_c_runtime_test_executable)
)
config.substitutions.append(
    ("%coverage-fixture", config.talos_coverage_fixture_executable)
)
config.substitutions.append(
    ("%coverage-scaling-fixture", config.talos_coverage_scaling_fixture_executable)
)
config.substitutions.append(
    ("%source-tokens-test", config.talos_source_tokens_test_executable)
)
config.substitutions.append(
    ("%resource_dir", '"{}"'.format(config.talos_resource_dir))
)
config.substitutions.append(
    ("%native_support", config.talos_native_support_dir)
)
config.substitutions.append(("%dlltool_machine", config.talos_dlltool_machine))
config.substitutions.append(("%coff_machine", config.talos_coff_machine))
config.substitutions.append(("%source_root", config.talos_source_root))
config.substitutions.append(
    ("%slang_source_root", config.talos_slang_source_root)
)
config.substitutions.append(("%obj_root", config.talos_obj_root))
config.substitutions.append(("%llvm_dist", config.talos_llvm_dist))
config.substitutions.append(
    ("%pass_plugin", config.talos_llvm_pass_plugin)
)
config.substitutions.append(("%target_triple", config.talos_target_triple))
config.substitutions.append(("%target_pic", "" if sys.platform == "win32" else "-fPIC"))
config.substitutions.append(("%exeext", ".exe" if sys.platform == "win32" else ""))
config.substitutions.append(("%target_link_flags",
                             "-fuse-ld=lld -nostartfiles" if sys.platform == "win32" else ""))
target_llc_options = ["-mtriple={}".format(config.talos_target_triple)]
if config.talos_target_triple.startswith("aarch64-"):
    # Long fused evaluation groups can form linear chains of hundreds of
    # dependent selects. AArch64 SelectionDAG is pathologically slow on that
    # shape; GlobalISel handles it in linear time and falls back safely for
    # operations it does not support.
    target_llc_options.extend(["-global-isel=true", "-global-isel-abort=0"])
config.substitutions.append(
    (
        "%llc",
        '"{}/bin/llc" {}'.format(
            config.talos_llvm_dist, " ".join(target_llc_options)
        ),
    )
)
target_compile_flags = ""
target_cxx_flags = ""
if sys.platform == "win32":
    target_compile_flags = (
        "-fms-runtime-lib=dll -DDPI_DLLISPEC= -DPLI_DLLISPEC= -DPLI_DLLESPEC="
    )
elif sys.platform == "darwin":
    target_compile_flags = '-isysroot "{}"'.format(config.talos_macos_sysroot)
    target_cxx_flags = '{} -nostdinc++ -isystem "{}"'.format(
        target_compile_flags, config.talos_macos_cxx_headers
    )
config.substitutions.append(
    (
        "%target_clang",
        '"{}/bin/clang" --target={} {}'.format(
            config.talos_llvm_dist, config.talos_target_triple,
            target_compile_flags,
        ),
    )
)
config.substitutions.append(
    ("%target_cxx", '"{}/bin/clang++" --target={} {}'.format(
        config.talos_llvm_dist, config.talos_target_triple,
        target_cxx_flags))
)
if sys.platform == "win32":
    runtime_link_flags = (
        '-fuse-ld=lld -nostartfiles -Wl,/debug:symtab -Wl,/stack:8388608 -Wl,/opt:noicf '
        '-fms-runtime-lib=dll -DDPI_DLLISPEC= '
        '-DPLI_DLLISPEC= -DPLI_DLLESPEC= '
        '"{0}/libtalos_rt.a"'
    ).format(config.talos_native_support_dir)
    read_exports = '"{}/bin/llvm-readobj" --coff-exports'.format(config.talos_llvm_dist)
elif sys.platform == "darwin":
    runtime_link_flags = '"{0}/libtalos_rt.a" "@{0}/weak-imports.rsp" -lc++'.format(
        config.talos_native_support_dir
    )
    read_exports = '"{}/bin/llvm-nm" --extern-only --defined-only'.format(
        config.talos_llvm_dist
    )
else:
    runtime_link_flags = (
        '"{0}/libtalos_rt.a" "{0}/libc++.a" "{0}/libc++abi.a" '
        '"{0}/libunwind.a" -nostdlib++ -lpthread -ldl'
    ).format(config.talos_native_support_dir)
    read_exports = '"{}/bin/llvm-readelf" --dyn-syms'.format(config.talos_llvm_dist)
config.substitutions.append(("%runtime_link_flags", runtime_link_flags))
config.substitutions.append(("%read_exports", read_exports))
config.substitutions.append(("%feature_section_prefix",
                             "COFF-SECTION" if sys.platform == "win32"
                             else "MACHO-SECTION" if sys.platform == "darwin"
                             else "SECTION"))
config.substitutions.append(
    ("%runtime_archive", config.talos_runtime_archive)
)
config.substitutions.append(("%cmake", config.cmake_executable))
node = shutil.which("node") if sys.platform != "win32" else None
if node:
    config.available_features.add("node")
    config.substitutions.append(("%node", node))
lcov = shutil.which("lcov")
if lcov:
    config.available_features.add("lcov")
    config.substitutions.append(("%lcov", lcov))
split_file = shutil.which(
    "split-file", path=os.path.join(config.talos_llvm_dist, "libexec", "llvm")
) or next(
    (
        path
        for name in [
            "split-file",
            "split-file-22",
            "split-file-21",
            "split-file-20",
            "split-file-19",
            "split-file-18",
            "split-file-17",
            "split-file-16",
            "split-file-15",
        ]
        if (path := shutil.which(name))
    ),
    None,
)
if not split_file:
    lit_config.fatal("unable to find LLVM split-file")
config.substitutions.append(("%split-file", split_file))
llvm_config.add_err_msg_substitutions()

tool_dirs = [
    config.test_exec_root,
    config.talos_driver_dir,
    config.talos_filecheck_dir,
    config.talos_opt_dir,
    config.talos_translate_dir,
    config.talos_cov_dir,
    config.llvm_tools_dir,
]
llvm_config.add_tool_substitutions(
    ["talos", "talos-cov", "talos-opt", "talos-translate", "talos-sim-standard-api-test", "FileCheck",
     "llvm-readelf", "llvm-readobj", "llvm-strings", "mlir-opt", "mlir-runner",
     "mlir-translate", "not", "opt"],
    tool_dirs,
)

if config.enable_real_uvm_tests:
    config.available_features.add("real-uvm")
    config.substitutions.append(("%uvm", config.real_uvm_dir))

if config.enable_z3:
    config.available_features.add("z3")
    config.available_features.add("compiled-sampler")
