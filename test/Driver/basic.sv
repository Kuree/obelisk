// RUN: talos -emit-slang --std=1800-2023 -I%S \
// RUN:   -DDRIVER_WIDTH=8 -DDRIVER_VALUE=165 \
// RUN:   %S/driver_helper.svh %s | FileCheck %s --check-prefix=SLANG
// RUN: talos -emit-talos --std=1800-2017 -I %S \
// RUN:   -DDRIVER_WIDTH=8 -DDRIVER_VALUE=165 \
// RUN:   %S/driver_helper.svh %s | talos-opt \
// RUN:   | FileCheck %s --check-prefix=TALOS
// RUN: talos --help | FileCheck %s --check-prefix=HELP
// RUN: talos --version | FileCheck %s --check-prefix=VERSION
// RUN: cd %S && talos -emit-slang -f driver_flags.f -I%S \
// RUN:   basic.sv | FileCheck %s --check-prefix=FILELIST
// RUN: not talos --std=invalid %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BAD-STD
// RUN: not talos 2>&1 | FileCheck %s --check-prefix=NO-INPUT

`include "driver_defs.svh"

module driver_top;
  logic ready;
  logic [`DRIVER_WIDTH-1:0] value;

  driver_helper helper(ready);

  initial value = `DRIVER_VALUE;
endmodule

// SLANG-DAG: slang.symbol.instance @{{[^ ]+}} attributes {{.*}}hierarchical_name = "driver_top.helper"
// SLANG-DAG: slang.symbol.instance @{{[^ ]+}} attributes {{.*}}hierarchical_name = "driver_top"
// SLANG-DAG: slang.symbol.variable @{{[^ ]+}} attributes {{.*}}semantic_type = !slang.packed_array<7 : 0 x
// SLANG: macro_expansion_stack = [{{.*}}name = "DRIVER_VALUE"
// SLANG-SAME: original_source_range = !slang.source_range<"<command-line>"
// SLANG-SAME: source_range = !slang.source_range<"{{.*}}basic.sv"

// TALOS-DAG: talos.sv.symbol.instance @{{[^ ]+}} attributes {{.*}}hierarchical_name = "driver_top.helper"
// TALOS-DAG: talos.sv.symbol.instance @{{[^ ]+}} attributes {{.*}}hierarchical_name = "driver_top"
// TALOS-DAG: !talos.ranged_packed_array<7 : 0 x
// TALOS-NOT: slang.

// HELP: OVERVIEW: Talos ahead-of-time SystemVerilog compiler
// HELP-DAG: -emit-talos
// HELP-DAG: -emit-sim
// HELP-DAG: -emit-slang
// HELP-DAG: -emit-llvm
// HELP-DAG: --emit-dpi-header
// HELP-DAG: --execution-tier=<native|bytecode>
// HELP-DAG: --native-scheduler=<auto|generic|aot|eval>
// HELP-DAG: --print-resource-dir
// HELP-DAG: -c
// HELP-DAG: --sysroot=<dir>{{ *}}Use <dir> for wasm32 C and C++ link inputs
// HELP-DAG: -I <dir>
// HELP-DAG: --std=<1800-2017|1800-2023>

// VERSION: talos version
// VERSION: slang version {{[0-9]+\.[0-9]+\.0\+0$}}

// FILELIST-DAG: slang.symbol.instance @{{[^ ]+}} attributes {{.*}}hierarchical_name = "driver_top.helper"
// FILELIST-DAG: slang.symbol.instance @{{[^ ]+}} attributes {{.*}}hierarchical_name = "driver_top"

// BAD-STD: talos: error: unsupported SystemVerilog revision 'invalid'
// NO-INPUT: talos: error: no input files
