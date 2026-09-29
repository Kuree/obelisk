# -*- Python -*-

import os
import shutil
import sys

import lit.formats
from lit.llvm import llvm_config

config.name = "OBELISK"
config.test_format = lit.formats.ShTest()
config.suffixes = [".mlir", ".sv", ".test", ".sdf"]
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = os.path.join(config.obelisk_obj_root, "test")

config.excludes = [
    "CMakeLists.txt",
    "Inputs",
    "lit.cfg.py",
    "lit.site.cfg.py",
    "lit.site.cfg.py.in",
]

llvm_config.with_system_environment(["HOME", "TMP", "TEMP"])
config.substitutions.append(("%python", '"{}"'.format(sys.executable)))
config.substitutions.append(("%obelisk", config.obelisk_driver_executable))
config.substitutions.append(
    ("%protect-obelisk", config.obelisk_protect_test_executable)
)
config.substitutions.append(
    ("%protect-inline-wipe-test", config.obelisk_protect_inline_wipe_test_executable)
)
config.substitutions.append(
    ("%host-c-runtime-test", config.obelisk_host_c_runtime_test_executable)
)
config.substitutions.append(
    ("%coverage-fixture", config.obelisk_coverage_fixture_executable)
)
config.substitutions.append(
    ("%source-tokens-test", config.obelisk_source_tokens_test_executable)
)
config.substitutions.append(
    ("%resource_dir", '"{}"'.format(config.obelisk_resource_dir))
)
config.substitutions.append(
    ("%native_support", config.obelisk_native_support_dir)
)
config.substitutions.append(("%source_root", config.obelisk_source_root))
config.substitutions.append(
    ("%slang_source_root", config.obelisk_slang_source_root)
)
config.substitutions.append(("%obj_root", config.obelisk_obj_root))
config.substitutions.append(("%llvm_dist", config.obelisk_llvm_dist))
config.substitutions.append(("%target_triple", config.obelisk_target_triple))
target_llc_options = ["-mtriple={}".format(config.obelisk_target_triple)]
if config.obelisk_target_triple.startswith("aarch64-"):
    # Long fused evaluation groups can form linear chains of hundreds of
    # dependent selects. AArch64 SelectionDAG is pathologically slow on that
    # shape; GlobalISel handles it in linear time and falls back safely for
    # operations it does not support.
    target_llc_options.extend(["-global-isel=true", "-global-isel-abort=0"])
config.substitutions.append(
    (
        "%llc",
        '"{}/bin/llc" {}'.format(
            config.obelisk_llvm_dist, " ".join(target_llc_options)
        ),
    )
)
config.substitutions.append(
    (
        "%target_clang",
        '"{}/bin/clang" --target={}'.format(
            config.obelisk_llvm_dist, config.obelisk_target_triple
        ),
    )
)
config.substitutions.append(
    ("%runtime_archive", config.obelisk_runtime_archive)
)
config.substitutions.append(("%cmake", config.cmake_executable))
node = shutil.which("node")
if node:
    config.available_features.add("node")
    config.substitutions.append(("%node", node))
lcov = shutil.which("lcov")
if lcov:
    config.available_features.add("lcov")
    config.substitutions.append(("%lcov", lcov))
split_file = next(
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
    config.obelisk_driver_dir,
    config.obelisk_filecheck_dir,
    config.obelisk_opt_dir,
    config.obelisk_translate_dir,
    config.obelisk_cov_dir,
    config.llvm_tools_dir,
]
llvm_config.add_tool_substitutions(
    ["obelisk", "obelisk-cov", "obelisk-opt", "obelisk-translate", "obelisk-sim-standard-api-test", "FileCheck",
     "llvm-readelf", "llvm-strings", "mlir-opt", "mlir-runner",
     "mlir-translate", "not", "opt"],
    tool_dirs,
)

if config.enable_real_uvm_tests:
    config.available_features.add("real-uvm")
    config.substitutions.append(("%uvm", config.real_uvm_dir))

if config.enable_z3:
    config.available_features.add("z3")
