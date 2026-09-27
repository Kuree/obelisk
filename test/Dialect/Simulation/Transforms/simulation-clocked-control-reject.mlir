// RUN: obelisk-opt %s --obelisk-sim-materialize-clocked-control | FileCheck %s
// Do not share continuation storage across instances, conflate different
// clocks, or lift references/managed values into scalar design slots.
// CHECK-NOT: schedule.clocked_control
// CHECK-NOT: obelisk_sim.storage.decl 2
// CHECK-LABEL: obelisk_sim.func @multiple
// CHECK: obelisk_sim.suspend.edge
// CHECK-LABEL: obelisk_sim.func @mixed_clocks
// CHECK: obelisk_sim.suspend.edge posedge %arg1
// CHECK: obelisk_sim.suspend.edge posedge %arg2
// CHECK-LABEL: obelisk_sim.func @live_ref
// CHECK: obelisk_sim.suspend.edge
// CHECK-NOT: schedule.clocked_control

module {
  obelisk_sim.design @reject {
    obelisk_sim.scope.decl 0 hierarchy "reject"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "reject.root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "reject.multiple"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "reject.mixed_clocks"
    obelisk_sim.code_unit.decl 4 in 0 initial hierarchy "reject.live_ref"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %a = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %b = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %p = obelisk_sim.spawn @multiple(%ctx, %a) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %q = obelisk_sim.spawn @multiple(%ctx, %b) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %r = obelisk_sim.spawn @mixed_clocks(%ctx, %a, %b) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %s = obelisk_sim.spawn @live_ref(%ctx, %a) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @multiple(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
                              %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %n = arith.constant 10 : i64
      obelisk_sim.suspend.edge posedge %clock to ^resume(%n : i64) : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^resume(%left: i64):
      obelisk_sim.return
    }
    obelisk_sim.func @mixed_clocks(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
                                  %a: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
                                  %b: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      obelisk_sim.suspend.edge posedge %a to ^resume : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^resume:
      obelisk_sim.suspend.edge posedge %b to ^done : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^done:
      obelisk_sim.return
    }
    obelisk_sim.func @live_ref(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
                              %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      obelisk_sim.suspend.edge posedge %clock to ^resume(%clock : !obelisk_sim.ref<!obelisk_sim.logic<1>>) : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^resume(%reference: !obelisk_sim.ref<!obelisk_sim.logic<1>>):
      obelisk_sim.return
    }
  }
}
