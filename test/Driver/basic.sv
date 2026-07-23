// RUN: obelisk -emit-slang --std=1800-2023 -I%S \
// RUN:   -DDRIVER_WIDTH=8 -DDRIVER_VALUE=165 \
// RUN:   %S/driver_helper.svh %s | FileCheck %s --check-prefix=SLANG
// RUN: obelisk -emit-obelisk --std=1800-2017 -I %S \
// RUN:   -DDRIVER_WIDTH=8 -DDRIVER_VALUE=165 \
// RUN:   %S/driver_helper.svh %s | obelisk-opt \
// RUN:   | FileCheck %s --check-prefix=OBELISK
// RUN: obelisk --help | FileCheck %s --check-prefix=HELP
// RUN: obelisk --version | FileCheck %s --check-prefix=VERSION
// RUN: (cd %S && obelisk -emit-slang -f driver_flags.f -I%S \
// RUN:   basic.sv) | FileCheck %s --check-prefix=FILELIST
// RUN: not obelisk --std=invalid %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BAD-STD
// RUN: not obelisk 2>&1 | FileCheck %s --check-prefix=NO-INPUT

`include "driver_defs.svh"

module driver_top;
  logic ready;
  logic [`DRIVER_WIDTH-1:0] value;

  driver_helper helper(ready);

  initial value = `DRIVER_VALUE;
endmodule

// SLANG-DAG: slang.symbol.instance attributes {{.*}}hierarchical_name = "driver_top.helper"
// SLANG-DAG: slang.symbol.instance attributes {{.*}}hierarchical_name = "driver_top"
// SLANG-DAG: slang.symbol.variable attributes {{.*}}semantic_type = !slang.packed_array<7 : 0 x
// SLANG: macro_expansion_stack = [{{.*}}name = "DRIVER_VALUE"
// SLANG-SAME: original_source_range = !slang.source_range<"<command-line>"
// SLANG-SAME: source_range = !slang.source_range<"{{.*}}basic.sv"

// OBELISK-DAG: obelisk.sv.symbol.instance attributes {{.*}}hierarchical_name = "driver_top.helper"
// OBELISK-DAG: obelisk.sv.symbol.instance attributes {{.*}}hierarchical_name = "driver_top"
// OBELISK-DAG: !obelisk.ranged_packed_array<7 : 0 x
// OBELISK-NOT: slang.

// HELP: OVERVIEW: Obelisk ahead-of-time SystemVerilog compiler
// HELP-DAG: -emit-obelisk
// HELP-DAG: -emit-sim
// HELP-DAG: -emit-slang
// HELP-DAG: -emit-llvm
// HELP-DAG: --emit-dpi-header
// HELP-DAG: --execution-tier=<native|bytecode>
// HELP-DAG: --dpi-link=<path>
// HELP-DAG: --print-resource-dir
// HELP-DAG: -c
// HELP-DAG: --sysroot=<dir>
// HELP-DAG: -I <dir>
// HELP-DAG: --std=<1800-2017|1800-2023>

// VERSION: obelisk version
// VERSION: slang version

// FILELIST-DAG: slang.symbol.instance attributes {{.*}}hierarchical_name = "driver_top.helper"
// FILELIST-DAG: slang.symbol.instance attributes {{.*}}hierarchical_name = "driver_top"

// BAD-STD: obelisk: error: unsupported SystemVerilog revision 'invalid'
// NO-INPUT: obelisk: error: no input files
