# -*- Python -*-

import lit.formats

# A stub suite: it declares no tests of its own. The runtime unit tests are
# gtest binaries in the build tree, so this config points lit at that directory
# and lets the GoogleTest format expand each binary into its individual cases.
#
# Living under test/ is what matters. The regression suite's lit invocation
# walks into this directory, finds the generated lit.site.cfg.py and picks the
# suite up as a nested one, so a single lit process schedules the unit tests
# and the ShTest regression tests in one worker pool.
config.name = "OBELISK-UNIT"
config.suffixes = []
config.test_format = lit.formats.GoogleTest(".", "Tests")
config.test_exec_root = config.obelisk_runtime_unit_dir
config.test_source_root = config.test_exec_root
