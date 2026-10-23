// RUN: %obelisk -O1 %s --dump-ir-before=finalize-native-llvm --dump-ir-path=%t.mlirbc -o %t.exe 2>&1 | FileCheck %s --check-prefix=BEFORE
// RUN: obelisk-opt %t.mlirbc --finalize-native-llvm -o /dev/null
// RUN: %obelisk -O1 --compile-threads=1 %s --dump-ir-after=obelisk-sim-build-compute-graph --dump-ir-path=%t.mlir -o %t.exe 2>&1 | FileCheck %s --check-prefix=AFTER
// RUN: FileCheck %s --check-prefix=GRAPH < %t.mlir
// RUN: not %obelisk -O1 %s --dump-ir-before=obelisk-sim-build-compute-graph --dump-ir-path=%t.unused -o %t.exe 2>&1 | FileCheck %s --check-prefix=NESTED
// RUN: not %obelisk -O1 %s --dump-ir-path=%t.unused -o %t.exe 2>&1 | FileCheck %s --check-prefix=NOPASS

module top;
  logic clk = 0, q = 0;
  always #1 clk = ~clk;
  always_ff @(posedge clk) q <= ~q;
  initial #10 $finish;
endmodule

// BEFORE: dumped IR before finalize-native-llvm to '{{.*}}.mlirbc'
// AFTER: dumped IR after obelisk-sim-build-compute-graph to '{{.*}}.mlir'
// GRAPH: simulation.design @
// GRAPH-SAME: compute_graph
// NESTED: error: --dump-ir-before=obelisk-sim-build-compute-graph names a pass nested on 'simulation.design'; add --compile-threads=1
// NOPASS: error: --dump-ir-path needs --dump-ir-before or --dump-ir-after
