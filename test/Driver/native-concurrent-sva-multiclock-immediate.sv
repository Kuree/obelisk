// RUN: talos --std=1800-2023 -emit-talos %s | FileCheck %s

// Exact source-import conformance for an immediate source-clock term followed
// by a cross-clock ##1 handoff. Execution semantics are covered by the
// hand-authored Simulation MLIR test.
module native_concurrent_sva_multiclock_immediate;
  logic source_clk, destination_clk;
  logic first, second;

  assert property (@(posedge source_clk)
                   first ##1 @(posedge destination_clk) second);
endmodule

// CHECK: talos.sv.assertion.clocking
// CHECK: talos.sv.assertion.sequence_concat attributes
// CHECK-SAME: delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64
// CHECK: talos.sv.assertion.clocking
