// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true primitive-only=true max-straight-line-members=2},obelisk-sim-materialize-compute-fusion,obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s

// Two primitive declarations with different previous-input widths must form
// separate homogeneous kernels. Each kernel carries independent state lanes
// and calls one shared, noinline member evaluator. The ordinary inertial
// actors have no primitive marker and remain outside both fusion surfaces.
module {
  obelisk_sim.design @stateful_primitive_cohorts {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 continuous hierarchy "top.one0"
    obelisk_sim.code_unit.decl 2 in 0 continuous hierarchy "top.one1"
    obelisk_sim.code_unit.decl 3 in 0 continuous hierarchy "top.two0"
    obelisk_sim.code_unit.decl 4 in 0 continuous hierarchy "top.two1"
    obelisk_sim.code_unit.decl 5 in 0 continuous hierarchy "top.ordinary0"
    obelisk_sim.code_unit.decl 6 in 0 continuous hierarchy "top.ordinary1"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<2> design
    obelisk_sim.storage.decl 3 in 0 : !obelisk_sim.logic<2> design
    obelisk_sim.storage.decl 4 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 2 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 3 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 4 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 5 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 1 in 0 drives 1 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 2 in 0 drives 2 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 3 in 0 drives 3 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 4 in 0 drives 4 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 5 in 0 drives 5 : !obelisk_sim.logic<1> design

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %one0 = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %one1 = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %two0 = obelisk_sim.context.storage %ctx[2] : !obelisk_sim.ref<!obelisk_sim.logic<2>>
      %two1 = obelisk_sim.context.storage %ctx[3] : !obelisk_sim.ref<!obelisk_sim.logic<2>>
      %ordinary = obelisk_sim.context.storage %ctx[4] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %d0 = obelisk_sim.context.driver %ctx[0] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d1 = obelisk_sim.context.driver %ctx[1] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d2 = obelisk_sim.context.driver %ctx[2] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d3 = obelisk_sim.context.driver %ctx[3] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d4 = obelisk_sim.context.driver %ctx[4] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d5 = obelisk_sim.context.driver %ctx[5] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %p0 = obelisk_sim.spawn @one0(%ctx, %one0, %d0) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.driver<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %p1 = obelisk_sim.spawn @one1(%ctx, %one1, %d1) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.driver<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %p2 = obelisk_sim.spawn @two0(%ctx, %two0, %d2) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<2>>, !obelisk_sim.driver<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %p3 = obelisk_sim.spawn @two1(%ctx, %two1, %d3) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<2>>, !obelisk_sim.driver<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %p4 = obelisk_sim.spawn @ordinary0(%ctx, %ordinary, %d4) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.driver<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %p5 = obelisk_sim.spawn @ordinary1(%ctx, %ordinary, %d5) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.driver<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @one0(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64, schedule.primitive_name = "one"} {
      %initial = obelisk_sim.logic.constant false, true : !obelisk_sim.logic<1>
      cf.br ^body(%initial : !obelisk_sim.logic<1>)
    ^body(%previous: !obelisk_sim.logic<1>):
      %value = obelisk_sim.ref.load %input : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %changed = obelisk_sim.logic.compare case_ne %previous, %value : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
      obelisk_sim.driver.drive %driver = %value : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.suspend.change %input to ^body(%value : !obelisk_sim.logic<1>) : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    }
    obelisk_sim.func private @one1(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}, %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 1 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64, schedule.primitive_name = "one"} {
      %initial = obelisk_sim.logic.constant false, true : !obelisk_sim.logic<1>
      cf.br ^body(%initial : !obelisk_sim.logic<1>)
    ^body(%previous: !obelisk_sim.logic<1>):
      %value = obelisk_sim.ref.load %input : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %changed = obelisk_sim.logic.compare case_ne %previous, %value : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
      obelisk_sim.driver.drive %driver = %value : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.suspend.change %input to ^body(%value : !obelisk_sim.logic<1>) : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    }

    obelisk_sim.func private @two0(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !obelisk_sim.ref<!obelisk_sim.logic<2>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64}, %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 2 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 3 : i64, schedule.primitive_name = "two"} {
      %initial = obelisk_sim.logic.constant 0 : i2, 3 : i2 : !obelisk_sim.logic<2>
      cf.br ^body(%initial : !obelisk_sim.logic<2>)
    ^body(%previous: !obelisk_sim.logic<2>):
      %value = obelisk_sim.ref.load %input : !obelisk_sim.ref<!obelisk_sim.logic<2>> -> !obelisk_sim.logic<2>
      %changed = obelisk_sim.logic.compare case_ne %previous, %value : (!obelisk_sim.logic<2>, !obelisk_sim.logic<2>) -> i1
      %zero = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %driver = %zero : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.suspend.change %input to ^body(%value : !obelisk_sim.logic<2>) : !obelisk_sim.ref<!obelisk_sim.logic<2>>
    }
    obelisk_sim.func private @two1(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !obelisk_sim.ref<!obelisk_sim.logic<2>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64}, %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 3 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 4 : i64, schedule.primitive_name = "two"} {
      %initial = obelisk_sim.logic.constant 0 : i2, 3 : i2 : !obelisk_sim.logic<2>
      cf.br ^body(%initial : !obelisk_sim.logic<2>)
    ^body(%previous: !obelisk_sim.logic<2>):
      %value = obelisk_sim.ref.load %input : !obelisk_sim.ref<!obelisk_sim.logic<2>> -> !obelisk_sim.logic<2>
      %changed = obelisk_sim.logic.compare case_ne %previous, %value : (!obelisk_sim.logic<2>, !obelisk_sim.logic<2>) -> i1
      %zero = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %driver = %zero : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.suspend.change %input to ^body(%value : !obelisk_sim.logic<2>) : !obelisk_sim.ref<!obelisk_sim.logic<2>>
    }

    obelisk_sim.func private @ordinary0(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 4 : i64}, %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 4 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 5 : i64} {
      cf.br ^body
    ^body:
      %value = obelisk_sim.ref.load %input : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.driver.drive_inertial %driver = %value after[%delay, %delay, %delay] site 5 : 0 vector = false : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.suspend.change %input to ^body : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    }
    obelisk_sim.func private @ordinary1(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 4 : i64}, %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 5 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 6 : i64} {
      cf.br ^body
    ^body:
      %value = obelisk_sim.ref.load %input : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.driver.drive_inertial %driver = %value after[%delay, %delay, %delay] site 6 : 0 vector = false : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.suspend.change %input to ^body : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    }
  }

  // The op lists and helper types are identical across both chunks, but the
  // second pair carries %previous instead of %value. Exact return identities
  // in the reuse fingerprint must retain two distinct helpers.
  obelisk_sim.design @return_mapping {
    obelisk_sim.scope.decl 0 hierarchy "return_mapping"
    obelisk_sim.code_unit.decl 20 in 0 continuous hierarchy "return_mapping.r0"
    obelisk_sim.code_unit.decl 21 in 0 continuous hierarchy "return_mapping.r1"
    obelisk_sim.code_unit.decl 22 in 0 continuous hierarchy "return_mapping.r2"
    obelisk_sim.code_unit.decl 23 in 0 continuous hierarchy "return_mapping.r3"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 3 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 2 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 3 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 1 in 0 drives 1 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 2 in 0 drives 2 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 3 in 0 drives 3 : !obelisk_sim.logic<1> design
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %i0 = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %i1 = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %i2 = obelisk_sim.context.storage %ctx[2] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %i3 = obelisk_sim.context.storage %ctx[3] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %d0 = obelisk_sim.context.driver %ctx[0] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d1 = obelisk_sim.context.driver %ctx[1] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d2 = obelisk_sim.context.driver %ctx[2] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d3 = obelisk_sim.context.driver %ctx[3] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %p0 = obelisk_sim.spawn @r0(%ctx, %i0, %d0) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.driver<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %p1 = obelisk_sim.spawn @r1(%ctx, %i1, %d1) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.driver<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %p2 = obelisk_sim.spawn @r2(%ctx, %i2, %d2) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.driver<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %p3 = obelisk_sim.spawn @r3(%ctx, %i3, %d3) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.driver<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func private @r0(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 20 : i64, schedule.primitive_name = "same"} {
      %initial = obelisk_sim.logic.constant false, true : !obelisk_sim.logic<1>
      cf.br ^body(%initial : !obelisk_sim.logic<1>)
    ^body(%previous: !obelisk_sim.logic<1>):
      %value = obelisk_sim.ref.load %input : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %changed = obelisk_sim.logic.compare case_ne %previous, %value : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
      obelisk_sim.driver.drive %driver = %value : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.suspend.change %input to ^body(%value : !obelisk_sim.logic<1>) : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    }
    obelisk_sim.func private @r1(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}, %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 1 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 21 : i64, schedule.primitive_name = "same"} {
      %initial = obelisk_sim.logic.constant false, true : !obelisk_sim.logic<1>
      cf.br ^body(%initial : !obelisk_sim.logic<1>)
    ^body(%previous: !obelisk_sim.logic<1>):
      %value = obelisk_sim.ref.load %input : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %changed = obelisk_sim.logic.compare case_ne %previous, %value : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
      obelisk_sim.driver.drive %driver = %value : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.suspend.change %input to ^body(%value : !obelisk_sim.logic<1>) : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    }
    obelisk_sim.func private @r2(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64}, %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 2 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 22 : i64, schedule.primitive_name = "same"} {
      %initial = obelisk_sim.logic.constant false, true : !obelisk_sim.logic<1>
      cf.br ^body(%initial : !obelisk_sim.logic<1>)
    ^body(%previous: !obelisk_sim.logic<1>):
      %value = obelisk_sim.ref.load %input : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %changed = obelisk_sim.logic.compare case_ne %previous, %value : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
      obelisk_sim.driver.drive %driver = %value : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.suspend.change %input to ^body(%previous : !obelisk_sim.logic<1>) : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    }
    obelisk_sim.func private @r3(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64}, %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 3 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 23 : i64, schedule.primitive_name = "same"} {
      %initial = obelisk_sim.logic.constant false, true : !obelisk_sim.logic<1>
      cf.br ^body(%initial : !obelisk_sim.logic<1>)
    ^body(%previous: !obelisk_sim.logic<1>):
      %value = obelisk_sim.ref.load %input : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %changed = obelisk_sim.logic.compare case_ne %previous, %value : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
      obelisk_sim.driver.drive %driver = %value : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.suspend.change %input to ^body(%previous : !obelisk_sim.logic<1>) : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    }
  }
}

// CHECK-LABEL: obelisk_sim.design @stateful_primitive_cohorts
// CHECK-COUNT-2: obelisk_sim.spawn @__obelisk_region_kernel_
// CHECK: obelisk_sim.spawn @ordinary0
// CHECK: obelisk_sim.spawn @ordinary1
// CHECK-NOT: obelisk_sim.spawn @one0
// CHECK-NOT: obelisk_sim.spawn @one1
// CHECK-NOT: obelisk_sim.spawn @two0
// CHECK-NOT: obelisk_sim.spawn @two1
// CHECK: obelisk_sim.func private @ordinary0
// CHECK: obelisk_sim.driver.drive_inertial
// CHECK: obelisk_sim.func private @ordinary1
// CHECK: obelisk_sim.driver.drive_inertial
// CHECK-COUNT-2: obelisk_sim.call @__obelisk_region_kernel_{{.*}}.__member
// CHECK: obelisk_sim.func private @__obelisk_region_kernel_{{.*}}.__member(%[[CTX0:.*]]: !obelisk_sim.context{{.*}}, %{{.*}}: !obelisk_sim.ref<!obelisk_sim.logic<1>>{{.*}}, %{{.*}}: !obelisk_sim.logic<1>{{.*}}, %{{.*}}: !obelisk_sim.driver<!obelisk_sim.logic<1>>{{.*}}) -> (!obelisk_sim.logic<1>, i1) attributes
// CHECK-SAME: passthrough = ["noinline"]
// CHECK-SAME: schedule.outlined_primitive_member
// CHECK: obelisk_sim.func private @__obelisk_region_kernel_{{.*}}.__member(%[[CTX1:.*]]: !obelisk_sim.context{{.*}}, %{{.*}}: !obelisk_sim.ref<!obelisk_sim.logic<2>>{{.*}}, %{{.*}}: !obelisk_sim.logic<2>{{.*}}, %{{.*}}: !obelisk_sim.driver<!obelisk_sim.logic<1>>{{.*}}) -> (!obelisk_sim.logic<2>, i1) attributes
// CHECK-SAME: passthrough = ["noinline"]
// CHECK-SAME: schedule.outlined_primitive_member

// CHECK-LABEL: obelisk_sim.design @return_mapping
// CHECK-COUNT-2: obelisk_sim.spawn @__obelisk_region_kernel_
// CHECK-DAG: obelisk_sim.func private @__obelisk_region_kernel_{{.*}}.__member
// CHECK-DAG: obelisk_sim.func private @__obelisk_region_kernel_{{.*}}.__member
