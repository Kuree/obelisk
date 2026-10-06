// RUN: obelisk-opt %S/../Runtime/Inputs/aggregate-snapshot-region-fusion.mlir --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,schedule-plan-combinational-statements,obelisk-sim-materialize-compute-fusion))' -o %t.fused
// RUN: FileCheck %s < %t.fused
// RUN: obelisk-opt %t.fused --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' -o /dev/null
// All snapshot lanes are overwritten by a bounded SSA aggregate loop. Its
// old output is not an incoming value and does not need a sensitivity edge.
// CHECK: simulation.spawn @__obelisk_region_kernel_
// CHECK: simulation.spawn @sample
// CHECK-NOT: simulation.func @producer_and_consumer
// CHECK-NOT: simulation.func @transform
// CHECK: simulation.func private @__obelisk_region_kernel_
// CHECK: arith.cmpi slt
// CHECK: simulation.array.insert_dynamic
// CHECK: simulation.logic.binary add
// CHECK: simulation.suspend.any %{{[0-9]+}} edges [0]
// CHECK-NOT: scf.
