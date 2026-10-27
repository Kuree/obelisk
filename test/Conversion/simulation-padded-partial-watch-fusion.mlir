// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,schedule-plan-combinational-statements,talos-sim-materialize-compute-fusion))' -o %t
// RUN: FileCheck %s < %t
// RUN: talos-opt %t --pass-pipeline='builtin.module(simulation.design(simulation.func(talos-sim-thread-suspension),talos-sim-build-compute-graph,talos-sim-verify-compute-graph))' -o /dev/null

// Padded descending unpacked arrays: only the first outer element is internal.
// The padding after each i3 lane is not a dependency or an external watch.
// The remaining two elements must still wake the kernel on external writes.
module {
  simulation.design @mixed_watch {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i3 design
    simulation.storage.decl 1 in 0 : i3 design
    simulation.storage.decl 2 in 0 : !simulation.unpacked_array<2 : 0 x !simulation.unpacked_array<-1 : -1 x i3, #simulation.storage_layout<size = 1, alignment = 1, stride = 1, offsets = []>>, #simulation.storage_layout<size = 3, alignment = 1, stride = 1, offsets = []>> design
    simulation.storage.decl 3 in 0 : i3 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "producer_consumer"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "bank"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %a = simulation.spawn @producer_consumer(%ctx) : !simulation.context -> !simulation.process
      %b = simulation.spawn @bank(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @producer_consumer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i3>
      %address = simulation.context.storage %ctx[1] : !simulation.ref<i3>
      %data = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.unpacked_array<2 : 0 x !simulation.unpacked_array<-1 : -1 x i3, #simulation.storage_layout<size = 1, alignment = 1, stride = 1, offsets = []>>, #simulation.storage_layout<size = 3, alignment = 1, stride = 1, offsets = []>>>
      %output = simulation.context.storage %ctx[3] : !simulation.ref<i3>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %input : !simulation.ref<i3> -> i3
      simulation.ref.store %a to %address : i3, !simulation.ref<i3>
      %first = simulation.ref.subelement %data[[0, 0]] : !simulation.ref<!simulation.unpacked_array<2 : 0 x !simulation.unpacked_array<-1 : -1 x i3, #simulation.storage_layout<size = 1, alignment = 1, stride = 1, offsets = []>>, #simulation.storage_layout<size = 3, alignment = 1, stride = 1, offsets = []>>> -> !simulation.ref<i3>
      %last = simulation.ref.subelement %data[[2, 0]] : !simulation.ref<!simulation.unpacked_array<2 : 0 x !simulation.unpacked_array<-1 : -1 x i3, #simulation.storage_layout<size = 1, alignment = 1, stride = 1, offsets = []>>, #simulation.storage_layout<size = 3, alignment = 1, stride = 1, offsets = []>>> -> !simulation.ref<i3>
      %x = simulation.ref.load %first : !simulation.ref<i3> -> i3
      %y = simulation.ref.load %last : !simulation.ref<i3> -> i3
      %r = arith.xori %x, %y : i3
      simulation.ref.store %r to %output : i3, !simulation.ref<i3>
      simulation.suspend.any %input, %data edges [0, 0] to ^body : !simulation.ref<i3>, !simulation.ref<!simulation.unpacked_array<2 : 0 x !simulation.unpacked_array<-1 : -1 x i3, #simulation.storage_layout<size = 1, alignment = 1, stride = 1, offsets = []>>, #simulation.storage_layout<size = 3, alignment = 1, stride = 1, offsets = []>>>
    }
    simulation.func @bank(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %address = simulation.context.storage %ctx[1] : !simulation.ref<i3>
      %data = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.unpacked_array<2 : 0 x !simulation.unpacked_array<-1 : -1 x i3, #simulation.storage_layout<size = 1, alignment = 1, stride = 1, offsets = []>>, #simulation.storage_layout<size = 3, alignment = 1, stride = 1, offsets = []>>>
      %first = simulation.ref.subelement %data[[0, 0]] : !simulation.ref<!simulation.unpacked_array<2 : 0 x !simulation.unpacked_array<-1 : -1 x i3, #simulation.storage_layout<size = 1, alignment = 1, stride = 1, offsets = []>>, #simulation.storage_layout<size = 3, alignment = 1, stride = 1, offsets = []>>> -> !simulation.ref<i3>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %address : !simulation.ref<i3> -> i3
      simulation.ref.store %a to %first : i3, !simulation.ref<i3>
      simulation.suspend.change %address to ^body : !simulation.ref<i3>
    }
  }
}
// CHECK: simulation.spawn @__talos_region_kernel_
// CHECK-NOT: simulation.func @producer_consumer
// CHECK-NOT: simulation.func @bank
// CHECK: simulation.func private @__talos_region_kernel_
// CHECK: %[[INPUT:.*]] = simulation.context.storage %arg0[0]
// CHECK: %[[DATA:.*]] = simulation.context.storage %arg0[2]
// CHECK: %[[MIDDLE:.*]] = simulation.ref.subelement %[[DATA]]{{\[\[1\]\]}}
// CHECK: %[[LAST:.*]] = simulation.ref.subelement %[[DATA]]{{\[\[2\]\]}}
// CHECK: simulation.suspend.any %[[INPUT]], %[[MIDDLE]], %[[LAST]] edges [0, 0, 0]
