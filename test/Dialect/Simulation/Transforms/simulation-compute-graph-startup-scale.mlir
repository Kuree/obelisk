// RUN: %python %S/../../../Conversion/Inputs/gen-startup-phases.py 1000 > %t.mlir
// RUN: obelisk-opt %t.mlir --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s --implicit-check-not='kind = process_order'

// The old representation stored one million startup x initial edges. Only
// the 2000 spawn edges remain. Barriers never escape into graph node IDs or
// groups, and initial entries still follow all startup entries.
// CHECK: #schedule.fragment<id = 2000, function = @root
// CHECK-SAME: regions = [#schedule.region<kind = active, groups = [
// CHECK-SAME: #schedule.group<fragments = [0], schedule = acyclic
// CHECK-SAME: #schedule.group<fragments = [999], schedule = acyclic
// CHECK-SAME: #schedule.group<fragments = [1000], schedule = acyclic
// CHECK-SAME: #schedule.group<fragments = [1999], schedule = acyclic
// CHECK-SAME: #schedule.group<fragments = [2000], schedule = acyclic
// CHECK-SAME: #schedule.region<kind = nba
