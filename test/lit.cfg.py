# -*- Python -*-

import os
import shutil
import sys

import lit.formats
from lit.llvm import llvm_config

config.name = "OBELISK"
config.test_format = lit.formats.ShTest(not llvm_config.use_lit_shell)
config.suffixes = [".mlir", ".sv", ".test"]
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
    ("%native_support", config.obelisk_native_support_dir)
)
config.substitutions.append(
    ("%target_package_cache", config.obelisk_target_package_cache)
)
config.substitutions.append(("%target_sysroot", config.obelisk_target_sysroot))
config.substitutions.append(
    ("%target_provision_script", config.obelisk_target_provision_script)
)
config.substitutions.append(("%source_root", config.obelisk_source_root))
config.substitutions.append(("%llvm_dist", config.obelisk_llvm_dist))
config.substitutions.append(
    ("%runtime_archive", config.obelisk_runtime_archive)
)
config.substitutions.append(("%cmake", config.cmake_executable))
node = shutil.which("node")
if node:
    config.available_features.add("node")
    config.substitutions.append(("%node", node))
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
    config.llvm_tools_dir,
]
llvm_config.add_tool_substitutions(
    ["obelisk", "obelisk-opt", "obelisk-sim-standard-api-test", "FileCheck",
     "llvm-readelf", "llvm-strings", "mlir-opt", "mlir-runner",
     "mlir-translate", "not", "opt"],
    tool_dirs,
)

if config.enable_real_uvm_tests:
    config.available_features.add("real-uvm")
    config.substitutions.append(("%uvm", config.real_uvm_dir))

if config.enable_z3:
    config.available_features.add("z3")
