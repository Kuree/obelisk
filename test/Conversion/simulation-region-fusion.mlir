// RUN: obelisk-opt %S/../Runtime/Inputs/region-fusion.mlir --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,schedule-plan-combinational-statements,obelisk-sim-materialize-compute-fusion))' -o %t.fused
// RUN: FileCheck %s < %t.fused
// RUN: FileCheck %s --check-prefix=OWNERS < %t.fused
// RUN: obelisk-opt %t.fused --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' -o /dev/null
// Retain induction loops in one source-ordered DAG. The bank transform is
// between address production and result assembly, so only the external input
// remains in the union wait. Clock/NBA sampling stays a separate activation.
// CHECK: simulation.spawn @__obelisk_region_kernel_
// CHECK: simulation.spawn @sample
// CHECK-NOT: simulation.func @producer_and_consumer
// CHECK-NOT: simulation.func @transform
// CHECK: simulation.func private @__obelisk_region_kernel_
// CHECK: simulation.context.storage %arg0[0]
// CHECK: arith.cmpi slt
// CHECK: simulation.ref.dyn_extract
// CHECK: simulation.ref.store
// CHECK: simulation.logic.binary add
// CHECK: arith.cmpi slt
// CHECK: simulation.ref.dyn_extract
// CHECK: simulation.ref.store
// CHECK: simulation.suspend.any %{{[0-9]+}} edges [0]
// CHECK-NOT: scf.
// Original ownership survives nested loop cloning and CFG lowering. A later
// scheduling query can recover process order without treating this kernel as
// one original process. The helper's aggregate inventory remains available.
// OWNERS: simulation.func private @__obelisk_region_kernel_
// OWNERS-SAME: schedule.eval.source_owners = [#schedule.source_owner<codeUnit = 1 : i64, continuation = [[A:[0-9]+]] : i32>, #schedule.source_owner<codeUnit = 2 : i64, continuation = [[B:[0-9]+]] : i32>]
// OWNERS: simulation.ref.store {{.*}} {schedule.eval.source_owner = #schedule.source_owner<codeUnit = 1 : i64, continuation = [[A]] : i32>}
// OWNERS: simulation.ref.store {{.*}} {schedule.eval.source_owner = #schedule.source_owner<codeUnit = 2 : i64, continuation = [[B]] : i32>}
// OWNERS: simulation.ref.store {{.*}} {schedule.eval.source_owner = #schedule.source_owner<codeUnit = 1 : i64, continuation = [[A]] : i32>}
