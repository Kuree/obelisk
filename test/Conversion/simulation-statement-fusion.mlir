// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,schedule-plan-combinational-statements,obelisk-sim-materialize-compute-fusion))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,schedule-plan-combinational-statements,obelisk-sim-materialize-compute-fusion))' -o %t.fused.mlir
// RUN: FileCheck %s < %t.fused.mlir
// RUN: obelisk-opt %t.fused.mlir --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' -o /dev/null
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph{vpi=full},schedule-plan-combinational-statements,obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=VPI --implicit-check-not=__obelisk_region_kernel_
// VPI: simulation.spawn @producer_and_consumer
// VPI: simulation.spawn @transform

// Statement ordering breaks an artificial actor SCC while a clocked observer
// keeps its original independent activation. Publications remain source ordered.
module {
  simulation.design @address_then_result {
    simulation.scope.decl 0
    simulation.scope.decl 1 parent 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.storage.decl 4 in 0 : i1 design
    simulation.storage.decl 5 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "producer_and_consumer"
    simulation.code_unit.decl 2 in 1 always_comb hierarchy "transform"
    simulation.code_unit.decl 3 in 0 always hierarchy "sample"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %a = simulation.spawn @producer_and_consumer(%ctx) : !simulation.context -> !simulation.process
      %b = simulation.spawn @transform(%ctx) : !simulation.context -> !simulation.process
      %s = simulation.spawn @sample(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @producer_and_consumer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %address = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %bank_address = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %bank_result = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %result = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %address : !simulation.ref<i8> -> i8
      simulation.ref.store %a to %bank_address : i8, !simulation.ref<i8>
      %r = simulation.ref.load %bank_result : !simulation.ref<i8> -> i8
      simulation.ref.store %r to %result : i8, !simulation.ref<i8>
      simulation.suspend.any %address, %bank_result edges [0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i8>
    }
    simulation.func @transform(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %address = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %result = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %address : !simulation.ref<i8> -> i8
      %one = arith.constant 1 : i8
      %r = arith.addi %a, %one : i8
      simulation.ref.store %r to %result : i8, !simulation.ref<i8>
      simulation.suspend.change %address to ^body : !simulation.ref<i8>
    }
    simulation.func @sample(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      %clock = simulation.context.storage %ctx[4] : !simulation.ref<i1>
      %result = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %sample = simulation.context.storage %ctx[5] : !simulation.ref<i8>
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clock to ^body : !simulation.ref<i1>
    ^body:
      %r = simulation.ref.load %result : !simulation.ref<i8> -> i8
      simulation.nba.enqueue %r to %sample : (i8, !simulation.ref<i8>) -> ()
      cf.br ^wait
    }
  }
}

// CHECK: simulation.spawn @__obelisk_region_kernel_
// CHECK: simulation.spawn @sample
// CHECK-NOT: simulation.func @producer_and_consumer
// CHECK-NOT: simulation.func @transform
// CHECK: simulation.func private @__obelisk_region_kernel_
// CHECK: %[[INPUT:.*]] = simulation.context.storage %arg0[0]
// CHECK: %[[ADDR:.*]] = simulation.context.storage %arg0[1]
// CHECK: %[[DATA:.*]] = simulation.context.storage %arg0[2]
// CHECK: %[[OUTPUT:.*]] = simulation.context.storage %arg0[3]
// CHECK: simulation.ref.load %[[INPUT]]
// CHECK: simulation.ref.store {{.*}} to %[[ADDR]]
// CHECK: arith.addi
// CHECK: simulation.ref.store
// CHECK: simulation.ref.load %[[DATA]]
// CHECK: simulation.ref.store {{.*}} to %[[OUTPUT]]
// CHECK: simulation.suspend.any %[[INPUT]] edges [0]
