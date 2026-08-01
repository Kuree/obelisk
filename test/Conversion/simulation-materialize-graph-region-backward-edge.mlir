// A hand-written fusion that lists the members against the schedule order
// puts an internal sensitivity edge backwards. One pass over the members can
// no longer settle the region, so the fusion must be rejected outright rather
// than dropping the activation of the earlier member.
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' | FileCheck %s

module {
  obelisk_sim.design @region_kernel attributes {
      obelisk_sim.static_body_fusion =
        [#obelisk_sim.fusion<id = 0, fragments = [4, 1]>]} {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 continuous hierarchy "region_kernel.first"
    obelisk_sim.code_unit.decl 2 in 0 continuous hierarchy "region_kernel.second"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 1 in 0 drives 1 : !obelisk_sim.logic<1> design

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %input = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %first_driver = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %first_net = obelisk_sim.context.net %ctx[0] :
          !obelisk_sim.net<!obelisk_sim.logic<1>>
      %second_driver = obelisk_sim.context.driver %ctx[1] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %first = obelisk_sim.spawn @first(%ctx, %input, %first_driver) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.driver<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %second = obelisk_sim.spawn @second(%ctx, %first_net, %second_driver) :
          !obelisk_sim.context, !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.driver<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @first(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %input: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 3 : i32,
           obelisk_sim.descriptor_id = 0 : i64},
        %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 5 : i32,
           obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64} {
      cf.br ^body
    ^body:
      %value = obelisk_sim.ref.load %input :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %driver = %value :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>
      obelisk_sim.suspend.change %input to ^body :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
    }

    obelisk_sim.func private @second(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %input: !obelisk_sim.net<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 4 : i32,
           obelisk_sim.descriptor_id = 0 : i64},
        %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 5 : i32,
           obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %value = obelisk_sim.net.read %input :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %driver = %value :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>
      obelisk_sim.suspend.change %input to ^body :
          !obelisk_sim.net<!obelisk_sim.logic<1>>
    }
  }
}

// The two original activations must survive untouched, and no partially
// populated kernel may be left behind by the rejected materialization.
// CHECK-NOT: __obelisk_region_kernel_
// CHECK: obelisk_sim.func private @first
// CHECK: obelisk_sim.func private @second
// CHECK-NOT: __obelisk_region_kernel_
