// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' | FileCheck %s --implicit-check-not='simulation.func private @first' --implicit-check-not='simulation.func private @second'

// First reject a backwards cohort after creating its temporary kernel, then
// accept the ordered cohort with the same generated base name. Finally revisit
// the erased sources. The mutable symbol index must release rejected names and
// erased operations while retaining the newly materialized kernel.

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @region_kernel attributes {
      schedule.static_body_fusion = [
        #schedule.fusion<id = 0, fragments = [4, 1]>,
        #schedule.fusion<id = 0, fragments = [1, 4]>,
        #schedule.fusion<id = 0, fragments = [1, 4]>]} {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 continuous hierarchy "region_kernel.first"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "region_kernel.second"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 1 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design
    simulation.driver.decl 1 in 0 drives 1 : !simulation.logic<1> design

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %input = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      %first_driver = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<1>>
      %first_net = simulation.context.net %ctx[0] :
          !simulation.net<!simulation.logic<1>>
      %second_driver = simulation.context.driver %ctx[1] :
          !simulation.driver<!simulation.logic<1>>
      %first = simulation.spawn @first(%ctx, %input, %first_driver) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>,
          !simulation.driver<!simulation.logic<1>> -> !simulation.process
      %second = simulation.spawn @second(%ctx, %first_net, %second_driver) :
          !simulation.context, !simulation.net<!simulation.logic<1>>,
          !simulation.driver<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }

    simulation.func private @first(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 3 : i32,
           simulation.descriptor_id = 0 : i64},
        %driver: !simulation.driver<!simulation.logic<1>>
          {simulation.capture_kind = 5 : i32,
           simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64} {
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.driver.drive %driver = %value {
        schedule.defer_net_resolution
      } :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>
      simulation.suspend.change %input to ^body :
          !simulation.ref<!simulation.logic<1>>
    }

    simulation.func private @second(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !simulation.net<!simulation.logic<1>>
          {simulation.capture_kind = 4 : i32,
           simulation.descriptor_id = 0 : i64},
        %driver: !simulation.driver<!simulation.logic<1>>
          {simulation.capture_kind = 5 : i32,
           simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %value = simulation.net.read %input :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.driver.drive %driver = %value :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>
      simulation.suspend.change %input to ^body :
          !simulation.net<!simulation.logic<1>>
    }
  }
}

// CHECK: simulation.spawn @__obelisk_region_kernel_0_0(
// CHECK: simulation.func private @__obelisk_region_kernel_0_0(
// CHECK-SAME: entry_kind = 7 : i32
// CHECK: ^bb{{[0-9]+}}(%[[INITIAL:.*]]: i1, %[[PREV0:.*]]: !simulation.logic<1>, %[[PREV1:.*]]: !simulation.logic<1>):
// CHECK: simulation.logic.compare case_eq
// CHECK: arith.select %[[INITIAL]]
// CHECK: arith.andi
// CHECK: cf.cond_br
// CHECK: simulation.suspend.any
// CHECK-SAME: edges [0, 0]
// CHECK: simulation.driver.drive_changed
// CHECK-SAME: schedule.defer_net_resolution
// CHECK: simulation.driver.drive_changed
// CHECK-NOT: schedule.defer_net_resolution
// CHECK-NOT: simulation.func private @first
// CHECK-NOT: simulation.func private @second

// CHECK-NOT: simulation.func private @__obelisk_region_kernel_
