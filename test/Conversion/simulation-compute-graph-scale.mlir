// Generated process CFGs reach tens of thousands of blocks. Every graph
// traversal must therefore be iterative: a per-block recursion exhausts the
// stack long before it exhausts memory. Running under a deliberately small
// stack makes that a hard requirement rather than a machine-dependent one.
//
// RUN: %python %S/Inputs/gen-deep-cfg.py 20000 > %t.mlir
// RUN: ulimit -s 1024 || true; \
// RUN:   obelisk-opt %t.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' \
// RUN:   | FileCheck %s

// The chain is one long acyclic run, and the loop back to the top of the body
// is the only cycle in it.
// CHECK: #obelisk_sim.region<kind = active
// CHECK-SAME: schedule = control_loop
// CHECK: #obelisk_sim.region<kind = postponed
