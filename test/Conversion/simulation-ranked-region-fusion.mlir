// RUN: sed 's/schedule.native_scheduler = 1 : i32/schedule.native_scheduler = 3 : i32, obelisk.debug.native_timing/' %S/../Runtime/Inputs/region-fusion.mlir > %t.region-fusion.mlir
// RUN: obelisk-opt %t.region-fusion.mlir --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,schedule-plan-combinational-statements,obelisk-sim-materialize-compute-fusion,simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off prune-native=true},convert-obelisk-sim-processes-to-llvm-coroutines)' -o /dev/null 2> %t.region-fusion.trace
// RUN: FileCheck %s < %t.region-fusion.trace
// RUN: sed 's/schedule.native_scheduler = 1 : i32/schedule.native_scheduler = 3 : i32, obelisk.debug.native_timing/' %S/../Runtime/Inputs/snapshot-region-fusion.mlir > %t.snapshot-region-fusion.mlir
// RUN: obelisk-opt %t.snapshot-region-fusion.mlir --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,schedule-plan-combinational-statements,obelisk-sim-materialize-compute-fusion,simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off prune-native=true},convert-obelisk-sim-processes-to-llvm-coroutines)' -o /dev/null 2> %t.snapshot-region-fusion.trace
// RUN: FileCheck %s < %t.snapshot-region-fusion.trace

// Bounded dynamic accesses and internal blocking reads do not turn a proved
// finite activation DAG into feedback. The source proof survives native
// lowering in the preserved pipeline analysis and the same graph inventory.
// CHECK: obelisk ranked activation: __obelisk_region_kernel_0_0 valid=1 complete=1
// CHECK: obelisk ranked groups: candidates=2 groups=2 segments=0 members=2 clocked_members=1
// CHECK: obelisk eval dependency: owner=0 island=0 body=__obelisk_region_kernel_0_0.__obelisk_eval_body_0 successors=
// CHECK: obelisk eval dependency: owner=1 island=1 body=sample.__obelisk_eval_body_0 successors=
