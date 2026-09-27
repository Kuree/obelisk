// RUN: %python %S/../../../Conversion/Inputs/gen-startup-phases.py 3 --boundaries > %t.mlir
// RUN: obelisk-opt %t.mlir -o /dev/null --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-simulation-schedule-analysis)' 2>&1 | FileCheck %s

// Keep distinct root blocks and event regions separate. An always process
// marked starts_without_waiting does not precede initial0; always1 is spawned
// in another block and orders only initial1. Unspawned always2 adds no phase.
// These are the ranks produced by the original explicit-edge representation.
// CHECK: schedule @startup_phases
// CHECK-NEXT: func @a_initial0 entry=0
// CHECK-NEXT: bb0 rank=0
// CHECK-NEXT: func @a_initial1 entry=5
// CHECK-NEXT: bb0 rank=5
// CHECK-NEXT: func @a_initial2 entry=7
// CHECK-NEXT: bb0 rank=7
// CHECK-NEXT: func @root entry=1
// CHECK-NEXT: bb0 rank=1
// CHECK-NEXT: bb1 rank=2
// CHECK-NEXT: func @z_always0 entry=3
// CHECK-NEXT: bb0 rank=3
// CHECK-NEXT: func @z_always1 entry=4
// CHECK-NEXT: bb0 rank=4
// CHECK-NEXT: func @z_always2 entry=6
// CHECK-NEXT: bb0 rank=6
