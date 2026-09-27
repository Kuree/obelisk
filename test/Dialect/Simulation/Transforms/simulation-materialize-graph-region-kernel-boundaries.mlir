// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s

// A ref.store publishes an immediate storage transition, but the current
// generated region ABI has no changed-range result for it. Do not erase the
// original actors and silently lose the internal producer-to-consumer wake.
module {
  simulation.design @store_boundary {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 continuous hierarchy "store_boundary.first"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "store_boundary.second"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design
    simulation.storage.decl 2 in 0 : !simulation.logic<1> design

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %input = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      %middle = simulation.context.storage %ctx[1] :
          !simulation.ref<!simulation.logic<1>>
      %output = simulation.context.storage %ctx[2] :
          !simulation.ref<!simulation.logic<1>>
      %first = simulation.spawn @store_first(%ctx, %input, %middle) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>,
          !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %second = simulation.spawn @store_second(%ctx, %middle, %output) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>,
          !simulation.ref<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }

    simulation.func private @store_first(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %middle: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64} {
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.ref.store %value to %middle :
          !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.suspend.change %input to ^body :
          !simulation.ref<!simulation.logic<1>>
    }

    simulation.func private @store_second(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %middle: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %output: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %value = simulation.ref.load %middle :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.ref.store %value to %output :
          !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.suspend.change %middle to ^body :
          !simulation.ref<!simulation.logic<1>>
    }
  }
}

// CHECK-LABEL: simulation.design @store_boundary
// CHECK: simulation.spawn @store_first
// CHECK: simulation.spawn @store_second
// CHECK: simulation.func private @store_first
// CHECK: simulation.func private @store_second
// CHECK-NOT: simulation.func private @__obelisk_region_kernel_
