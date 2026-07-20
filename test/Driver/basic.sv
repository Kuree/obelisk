// RUN: obelisk -emit-moore --std=1800-2023 -I%S \
// RUN:   -DDRIVER_WIDTH=8 -DDRIVER_VALUE=165 \
// RUN:   %S/driver_helper.svh %s | FileCheck %s --check-prefix=MOORE
// RUN: obelisk -emit-obelisk --std=1800-2017 -I %S \
// RUN:   -DDRIVER_WIDTH=8 -DDRIVER_VALUE=165 \
// RUN:   %S/driver_helper.svh %s | obelisk-opt \
// RUN:   | FileCheck %s --check-prefix=OBELISK
// RUN: obelisk --help | FileCheck %s --check-prefix=HELP
// RUN: obelisk --version | FileCheck %s --check-prefix=VERSION
// RUN: obelisk -emit-moore -f %S/driver_flags.f -I%S \
// RUN:   %s | FileCheck %s --check-prefix=FILELIST
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

// MOORE: moore.module{{.*}}@driver_helper
// MOORE: moore.module @driver_top
// MOORE: moore.variable : <l8>

// OBELISK: obelisk.semantic.graph_symbol @driver_helper module
// OBELISK: obelisk.semantic.graph_symbol @driver_top module
// OBELISK: !obelisk.logic<8>
// OBELISK-NOT: moore.

// HELP: OVERVIEW: Obelisk ahead-of-time SystemVerilog compiler
// HELP: -emit-moore
// HELP: -emit-obelisk
// HELP: -I <dir>
// HELP: --std=<1800-2017|1800-2023>

// VERSION: obelisk version
// VERSION: slang version

// FILELIST-DAG: moore.module{{.*}}@driver_helper
// FILELIST-DAG: moore.module @driver_top

// BAD-STD: obelisk: error: unsupported SystemVerilog revision 'invalid'
// NO-INPUT: obelisk: error: no input files
