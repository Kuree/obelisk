// RUN: %python %S/../../../Conversion/Inputs/gen-startup-phases.py 2 --feedback > %t.mlir
// RUN: obelisk-opt %t.mlir --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-simulation-schedule-analysis)' > %t.graph 2> %t.ranks
// RUN: FileCheck %s --check-prefix=GRAPH < %t.graph
// RUN: FileCheck %s --check-prefix=RANK < %t.ranks

// Initial writes activate the startup waits. Together with the implicit
// startup phase this is a cycle: dropping the phase would split the SCC.
// Both groups and ranks match the original explicit Cartesian-product graph.
// GRAPH: regions = [#schedule.region<kind = active, groups = [#schedule.group<fragments = [0, 2, 4, 5], schedule = convergence
// GRAPH-SAME: #schedule.group<fragments = [1], schedule = acyclic
// GRAPH-SAME: #schedule.group<fragments = [3], schedule = acyclic
// GRAPH-SAME: #schedule.group<fragments = [6], schedule = acyclic

// RANK: schedule @startup_phases
// RANK-NEXT: func @always0 entry=0
// RANK-NEXT: bb0 rank=0
// RANK-NEXT: bb1 rank=4
// RANK-NEXT: func @always1 entry=2
// RANK-NEXT: bb0 rank=2
// RANK-NEXT: bb1 rank=5
// RANK-NEXT: func @initial0 entry=1
// RANK-NEXT: bb0 rank=1
// RANK-NEXT: func @initial1 entry=3
// RANK-NEXT: bb0 rank=3
// RANK-NEXT: func @root entry=6
// RANK-NEXT: bb0 rank=6
