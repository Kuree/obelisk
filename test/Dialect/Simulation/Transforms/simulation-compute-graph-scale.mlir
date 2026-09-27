// Generated process CFGs reach tens of thousands of blocks. Every graph
// traversal must therefore be iterative: a per-block recursion exhausts the
// stack long before it exhausts memory. Keep the graph deep enough that a
// recursive traversal is not viable with conventional thread stack sizes.
//
// RUN: %python %S/../../../Conversion/Inputs/gen-deep-cfg.py 20000 > %t.mlir
// RUN: obelisk-opt %t.mlir \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' \
// RUN:   | FileCheck %s

// The chain is one long acyclic run, and the loop back to the top of the body
// is the only cycle in it.
// CHECK: #schedule.region<kind = active
// CHECK-SAME: schedule = control_loop
// CHECK: #schedule.region<kind = postponed

// An acyclic chain has many singleton SCCs. Classifying each singleton by
// rescanning every edge is quadratic even though SCC traversal is iterative.
// Exercise the stable internal-edge index and verify full node coverage.
// RUN: %python %S/../../../Conversion/Inputs/gen-deep-cfg.py 20000 --acyclic > %t.acyclic.mlir
// RUN: obelisk-opt %t.acyclic.mlir \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' \
// RUN:   | FileCheck %s --check-prefix=ACYCLIC --implicit-check-not="schedule = convergence" --implicit-check-not="schedule = control_loop"
// ACYCLIC: #schedule.fragment<id = 20000
// ACYCLIC-SAME: #schedule.region<kind = active
// ACYCLIC-SAME: #schedule.group<fragments = [0], schedule = acyclic
// ACYCLIC-SAME: #schedule.group<fragments = [20000], schedule = acyclic
// ACYCLIC-SAME: #schedule.region<kind = nba
