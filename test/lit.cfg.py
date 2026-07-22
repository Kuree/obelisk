# -*- Python -*-

import os
import sys

import lit.formats
from lit.llvm import llvm_config

config.name = "OBELISK"
config.test_format = lit.formats.ShTest(not llvm_config.use_lit_shell)
config.suffixes = [".mlir", ".sv"]
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
     "mlir-opt", "mlir-runner", "not"],
    tool_dirs,
)

if config.enable_real_uvm_tests:
    config.available_features.add("real-uvm")
    config.substitutions.append(("%uvm", config.real_uvm_dir))
