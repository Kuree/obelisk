// RUN: obelisk-opt %s --obelisk-sim-materialize-clocked-control | FileCheck %s
// Do not share continuation storage across instances, conflate different
// clocks, or lift references/managed values into scalar design slots.
// CHECK-NOT: schedule.clocked_control
// CHECK-NOT: simulation.storage.decl 2
// CHECK-LABEL: simulation.func @multiple
// CHECK: simulation.suspend.edge
// CHECK-LABEL: simulation.func @mixed_clocks
// CHECK: simulation.suspend.edge posedge %arg1
// CHECK: simulation.suspend.edge posedge %arg2
// CHECK-LABEL: simulation.func @live_ref
// CHECK: simulation.suspend.edge
// CHECK-NOT: schedule.clocked_control

module {
  simulation.design @reject {
    simulation.scope.decl 0 hierarchy "reject"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "reject.root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "reject.multiple"
    simulation.code_unit.decl 3 in 0 initial hierarchy "reject.mixed_clocks"
    simulation.code_unit.decl 4 in 0 initial hierarchy "reject.live_ref"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %a = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %b = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<1>>
      %p = simulation.spawn @multiple(%ctx, %a) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %q = simulation.spawn @multiple(%ctx, %b) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %r = simulation.spawn @mixed_clocks(%ctx, %a, %b) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %s = simulation.spawn @live_ref(%ctx, %a) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }
    simulation.func @multiple(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
                              %clock: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %n = arith.constant 10 : i64
      simulation.suspend.edge posedge %clock to ^resume(%n : i64) : !simulation.ref<!simulation.logic<1>>
    ^resume(%left: i64):
      simulation.return
    }
    simulation.func @mixed_clocks(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
                                  %a: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
                                  %b: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      simulation.suspend.edge posedge %a to ^resume : !simulation.ref<!simulation.logic<1>>
    ^resume:
      simulation.suspend.edge posedge %b to ^done : !simulation.ref<!simulation.logic<1>>
    ^done:
      simulation.return
    }
    simulation.func @live_ref(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
                              %clock: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      simulation.suspend.edge posedge %clock to ^resume(%clock : !simulation.ref<!simulation.logic<1>>) : !simulation.ref<!simulation.logic<1>>
    ^resume(%reference: !simulation.ref<!simulation.logic<1>>):
      simulation.return
    }
  }
}
