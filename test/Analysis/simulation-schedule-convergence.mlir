// RUN: obelisk-opt %S/../Conversion/simulation-ranked-group-routing.mlir -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-simulation-schedule-analysis)' \
// RUN:   2>&1 | FileCheck %s

// Use the executable configured-routing fixture, which also compares native
// and required-bytecode behavior at O0/O3. Its potential SCC keeps every edge,
// but its two resumed bodies now have distinct dependency-ordered ranks. The
// wait backedges are not interpreted as combinational execution dependencies.
// This is the shared semantic schedule, not a native helper's private order.
// CHECK: schedule @forward_chain
// CHECK: func @step2 entry=0
// CHECK-NEXT: bb0 rank=0
// CHECK-NEXT: bb1 rank=19
// CHECK-NEXT: bb2 rank=2
// CHECK-NEXT: func @step3 entry=1
// CHECK-NEXT: bb0 rank=1
// CHECK-NEXT: bb1 rank=20
// CHECK-NEXT: bb2 rank=3
