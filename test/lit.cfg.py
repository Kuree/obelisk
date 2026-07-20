# -*- Python -*-

import os

import lit.formats
from lit.llvm import llvm_config

config.name = "OBELISK"
config.test_format = lit.formats.ShTest(not llvm_config.use_lit_shell)
config.suffixes = [".mlir", ".sv"]
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = os.path.join(config.obelisk_obj_root, "test")

config.excludes = [
    "CMakeLists.txt",
    "lit.cfg.py",
    "lit.site.cfg.py",
    "lit.site.cfg.py.in",
]

llvm_config.with_system_environment(["HOME", "TMP", "TEMP"])
llvm_config.use_default_substitutions()

tool_dirs = [
    config.obelisk_opt_dir,
    config.obelisk_translate_dir,
    config.circt_tools_dir,
]
llvm_config.add_tool_substitutions(
    ["obelisk-opt", "obelisk-translate", "FileCheck", "not"], tool_dirs
)
