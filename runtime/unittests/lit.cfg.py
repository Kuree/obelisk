# -*- Python -*-

import lit.formats

config.name = "OBELISK-RUNTIME-UNIT"
config.test_format = lit.formats.GoogleTest(".", "Tests")
config.test_source_root = config.obelisk_runtime_unit_dir
config.test_exec_root = config.obelisk_runtime_unit_dir
