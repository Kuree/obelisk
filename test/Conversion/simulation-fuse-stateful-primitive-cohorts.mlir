// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true primitive-only=true max-straight-line-members=2},obelisk-sim-materialize-compute-fusion,obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s
// RUN: sed 's/to \^body([%]previous :/to ^body(%value :/g' %s > %t.same.mlir
// RUN: obelisk-opt %t.same.mlir --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true primitive-only=true max-straight-line-members=2},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=REUSE

// Two primitive declarations with different previous-input widths must form
// separate homogeneous kernels. Each kernel carries independent state lanes
// and calls one shared, noinline member evaluator. The ordinary inertial
// actors have no primitive marker and remain outside both fusion surfaces.
module {
  simulation.design @stateful_primitive_cohorts {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 continuous hierarchy "top.one0"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "top.one1"
    simulation.code_unit.decl 3 in 0 continuous hierarchy "top.two0"
    simulation.code_unit.decl 4 in 0 continuous hierarchy "top.two1"
    simulation.code_unit.decl 5 in 0 continuous hierarchy "top.ordinary0"
    simulation.code_unit.decl 6 in 0 continuous hierarchy "top.ordinary1"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design
    simulation.storage.decl 2 in 0 : !simulation.logic<2> design
    simulation.storage.decl 3 in 0 : !simulation.logic<2> design
    simulation.storage.decl 4 in 0 : !simulation.logic<1> design
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 1 in 0 : !simulation.logic<1> design
    simulation.net.decl 2 in 0 : !simulation.logic<1> design
    simulation.net.decl 3 in 0 : !simulation.logic<1> design
    simulation.net.decl 4 in 0 : !simulation.logic<1> design
    simulation.net.decl 5 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design
    simulation.driver.decl 1 in 0 drives 1 : !simulation.logic<1> design
    simulation.driver.decl 2 in 0 drives 2 : !simulation.logic<1> design
    simulation.driver.decl 3 in 0 drives 3 : !simulation.logic<1> design
    simulation.driver.decl 4 in 0 drives 4 : !simulation.logic<1> design
    simulation.driver.decl 5 in 0 drives 5 : !simulation.logic<1> design

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %one0 = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %one1 = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<1>>
      %two0 = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<2>>
      %two1 = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<2>>
      %ordinary = simulation.context.storage %ctx[4] : !simulation.ref<!simulation.logic<1>>
      %d0 = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<1>>
      %d1 = simulation.context.driver %ctx[1] : !simulation.driver<!simulation.logic<1>>
      %d2 = simulation.context.driver %ctx[2] : !simulation.driver<!simulation.logic<1>>
      %d3 = simulation.context.driver %ctx[3] : !simulation.driver<!simulation.logic<1>>
      %d4 = simulation.context.driver %ctx[4] : !simulation.driver<!simulation.logic<1>>
      %d5 = simulation.context.driver %ctx[5] : !simulation.driver<!simulation.logic<1>>
      %p0 = simulation.spawn @one0(%ctx, %one0, %d0) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.driver<!simulation.logic<1>> -> !simulation.process
      %p1 = simulation.spawn @one1(%ctx, %one1, %d1) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.driver<!simulation.logic<1>> -> !simulation.process
      %p2 = simulation.spawn @two0(%ctx, %two0, %d2) : !simulation.context, !simulation.ref<!simulation.logic<2>>, !simulation.driver<!simulation.logic<1>> -> !simulation.process
      %p3 = simulation.spawn @two1(%ctx, %two1, %d3) : !simulation.context, !simulation.ref<!simulation.logic<2>>, !simulation.driver<!simulation.logic<1>> -> !simulation.process
      %p4 = simulation.spawn @ordinary0(%ctx, %ordinary, %d4) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.driver<!simulation.logic<1>> -> !simulation.process
      %p5 = simulation.spawn @ordinary1(%ctx, %ordinary, %d5) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.driver<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }

    simulation.func private @one0(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %driver: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64, schedule.primitive_name = "one"} {
      %initial = simulation.logic.constant false, true : !simulation.logic<1>
      cf.br ^body(%initial : !simulation.logic<1>)
    ^body(%previous: !simulation.logic<1>):
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %changed = simulation.logic.compare case_ne %previous, %value : (!simulation.logic<1>, !simulation.logic<1>) -> i1
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.suspend.change %input to ^body(%value : !simulation.logic<1>) : !simulation.ref<!simulation.logic<1>>
    }
    simulation.func private @one1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %driver: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64, schedule.primitive_name = "one"} {
      %initial = simulation.logic.constant false, true : !simulation.logic<1>
      cf.br ^body(%initial : !simulation.logic<1>)
    ^body(%previous: !simulation.logic<1>):
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %changed = simulation.logic.compare case_ne %previous, %value : (!simulation.logic<1>, !simulation.logic<1>) -> i1
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.suspend.change %input to ^body(%value : !simulation.logic<1>) : !simulation.ref<!simulation.logic<1>>
    }

    simulation.func private @two0(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!simulation.logic<2>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %driver: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 3 : i64, schedule.primitive_name = "two"} {
      %initial = simulation.logic.constant 0 : i2, 3 : i2 : !simulation.logic<2>
      cf.br ^body(%initial : !simulation.logic<2>)
    ^body(%previous: !simulation.logic<2>):
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<2>> -> !simulation.logic<2>
      %changed = simulation.logic.compare case_ne %previous, %value : (!simulation.logic<2>, !simulation.logic<2>) -> i1
      %zero = simulation.logic.constant false, false : !simulation.logic<1>
      simulation.driver.drive %driver = %zero : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.suspend.change %input to ^body(%value : !simulation.logic<2>) : !simulation.ref<!simulation.logic<2>>
    }
    simulation.func private @two1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!simulation.logic<2>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}, %driver: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 4 : i64, schedule.primitive_name = "two"} {
      %initial = simulation.logic.constant 0 : i2, 3 : i2 : !simulation.logic<2>
      cf.br ^body(%initial : !simulation.logic<2>)
    ^body(%previous: !simulation.logic<2>):
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<2>> -> !simulation.logic<2>
      %changed = simulation.logic.compare case_ne %previous, %value : (!simulation.logic<2>, !simulation.logic<2>) -> i1
      %zero = simulation.logic.constant false, false : !simulation.logic<1>
      simulation.driver.drive %driver = %zero : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.suspend.change %input to ^body(%value : !simulation.logic<2>) : !simulation.ref<!simulation.logic<2>>
    }

    simulation.func private @ordinary0(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}, %driver: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 5 : i64} {
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %delay = simulation.time.constant 1
      simulation.driver.drive_inertial %driver = %value after[%delay, %delay, %delay] site 5 : 0 vector = false : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.suspend.change %input to ^body : !simulation.ref<!simulation.logic<1>>
    }
    simulation.func private @ordinary1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}, %driver: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 5 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 6 : i64} {
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %delay = simulation.time.constant 1
      simulation.driver.drive_inertial %driver = %value after[%delay, %delay, %delay] site 6 : 0 vector = false : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.suspend.change %input to ^body : !simulation.ref<!simulation.logic<1>>
    }
  }

  // The op lists and helper types are identical across both chunks, but the
  // second pair carries %previous instead of %value. Exact return identities
  // in the reuse fingerprint must retain two distinct helpers.
  simulation.design @return_mapping {
    simulation.scope.decl 0 hierarchy "return_mapping"
    simulation.code_unit.decl 20 in 0 continuous hierarchy "return_mapping.r0"
    simulation.code_unit.decl 21 in 0 continuous hierarchy "return_mapping.r1"
    simulation.code_unit.decl 22 in 0 continuous hierarchy "return_mapping.r2"
    simulation.code_unit.decl 23 in 0 continuous hierarchy "return_mapping.r3"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design
    simulation.storage.decl 2 in 0 : !simulation.logic<1> design
    simulation.storage.decl 3 in 0 : !simulation.logic<1> design
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 1 in 0 : !simulation.logic<1> design
    simulation.net.decl 2 in 0 : !simulation.logic<1> design
    simulation.net.decl 3 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design
    simulation.driver.decl 1 in 0 drives 1 : !simulation.logic<1> design
    simulation.driver.decl 2 in 0 drives 2 : !simulation.logic<1> design
    simulation.driver.decl 3 in 0 drives 3 : !simulation.logic<1> design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %i0 = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %i1 = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<1>>
      %i2 = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<1>>
      %i3 = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<1>>
      %d0 = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<1>>
      %d1 = simulation.context.driver %ctx[1] : !simulation.driver<!simulation.logic<1>>
      %d2 = simulation.context.driver %ctx[2] : !simulation.driver<!simulation.logic<1>>
      %d3 = simulation.context.driver %ctx[3] : !simulation.driver<!simulation.logic<1>>
      %p0 = simulation.spawn @r0(%ctx, %i0, %d0) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.driver<!simulation.logic<1>> -> !simulation.process
      %p1 = simulation.spawn @r1(%ctx, %i1, %d1) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.driver<!simulation.logic<1>> -> !simulation.process
      %p2 = simulation.spawn @r2(%ctx, %i2, %d2) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.driver<!simulation.logic<1>> -> !simulation.process
      %p3 = simulation.spawn @r3(%ctx, %i3, %d3) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.driver<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }
    simulation.func private @r0(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %driver: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 20 : i64, schedule.primitive_name = "same"} {
      %initial = simulation.logic.constant false, true : !simulation.logic<1>
      cf.br ^body(%initial : !simulation.logic<1>)
    ^body(%previous: !simulation.logic<1>):
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %changed = simulation.logic.compare case_ne %previous, %value : (!simulation.logic<1>, !simulation.logic<1>) -> i1
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.suspend.change %input to ^body(%value : !simulation.logic<1>) : !simulation.ref<!simulation.logic<1>>
    }
    simulation.func private @r1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %driver: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 21 : i64, schedule.primitive_name = "same"} {
      %initial = simulation.logic.constant false, true : !simulation.logic<1>
      cf.br ^body(%initial : !simulation.logic<1>)
    ^body(%previous: !simulation.logic<1>):
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %changed = simulation.logic.compare case_ne %previous, %value : (!simulation.logic<1>, !simulation.logic<1>) -> i1
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.suspend.change %input to ^body(%value : !simulation.logic<1>) : !simulation.ref<!simulation.logic<1>>
    }
    simulation.func private @r2(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %driver: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 22 : i64, schedule.primitive_name = "same"} {
      %initial = simulation.logic.constant false, true : !simulation.logic<1>
      cf.br ^body(%initial : !simulation.logic<1>)
    ^body(%previous: !simulation.logic<1>):
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %changed = simulation.logic.compare case_ne %previous, %value : (!simulation.logic<1>, !simulation.logic<1>) -> i1
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.suspend.change %input to ^body(%previous : !simulation.logic<1>) : !simulation.ref<!simulation.logic<1>>
    }
    simulation.func private @r3(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}, %driver: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 23 : i64, schedule.primitive_name = "same"} {
      %initial = simulation.logic.constant false, true : !simulation.logic<1>
      cf.br ^body(%initial : !simulation.logic<1>)
    ^body(%previous: !simulation.logic<1>):
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %changed = simulation.logic.compare case_ne %previous, %value : (!simulation.logic<1>, !simulation.logic<1>) -> i1
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.suspend.change %input to ^body(%previous : !simulation.logic<1>) : !simulation.ref<!simulation.logic<1>>
    }
  }
}

// CHECK-LABEL: simulation.design @stateful_primitive_cohorts
// CHECK-COUNT-2: simulation.spawn @__obelisk_region_kernel_
// CHECK: simulation.spawn @ordinary0
// CHECK: simulation.spawn @ordinary1
// CHECK-NOT: simulation.spawn @one0
// CHECK-NOT: simulation.spawn @one1
// CHECK-NOT: simulation.spawn @two0
// CHECK-NOT: simulation.spawn @two1
// CHECK: simulation.func private @ordinary0
// CHECK: simulation.driver.drive_inertial
// CHECK: simulation.func private @ordinary1
// CHECK: simulation.driver.drive_inertial
// CHECK-COUNT-2: simulation.call @__obelisk_region_kernel_{{.*}}.__member
// CHECK: simulation.func private @__obelisk_region_kernel_{{.*}}.__member(%[[CTX0:.*]]: !simulation.context{{.*}}, %{{.*}}: !simulation.ref<!simulation.logic<1>>{{.*}}, %{{.*}}: !simulation.logic<1>{{.*}}, %{{.*}}: !simulation.driver<!simulation.logic<1>>{{.*}}) -> (!simulation.logic<1>, i1) attributes
// CHECK-SAME: passthrough = ["noinline"]
// CHECK-SAME: schedule.outlined_primitive_member
// CHECK: simulation.func private @__obelisk_region_kernel_{{.*}}.__member(%[[CTX1:.*]]: !simulation.context{{.*}}, %{{.*}}: !simulation.ref<!simulation.logic<2>>{{.*}}, %{{.*}}: !simulation.logic<2>{{.*}}, %{{.*}}: !simulation.driver<!simulation.logic<1>>{{.*}}) -> (!simulation.logic<2>, i1) attributes
// CHECK-SAME: passthrough = ["noinline"]
// CHECK-SAME: schedule.outlined_primitive_member

// CHECK-LABEL: simulation.design @return_mapping
// CHECK-COUNT-2: simulation.spawn @__obelisk_region_kernel_
// CHECK-DAG: simulation.func private @__obelisk_region_kernel_{{.*}}.__member
// CHECK-DAG: simulation.func private @__obelisk_region_kernel_{{.*}}.__member

// With identical continuation returns, both cohorts share one helper. The
// original checks above retain separate helpers for different return values.
// REUSE-LABEL: simulation.design @return_mapping
// REUSE-COUNT-2: simulation.spawn @__obelisk_region_kernel_
// REUSE: simulation.call @[[MEMBER:__obelisk_region_kernel_[A-Za-z0-9_]+\.__member]](
// REUSE: simulation.call @[[MEMBER]](
// REUSE: simulation.func private @[[MEMBER]](
// REUSE-NOT: simulation.func private @{{.*}}.__member
// REUSE: simulation.call @[[MEMBER]](
// REUSE-NOT: simulation.func private @{{.*}}.__member
// REUSE: simulation.call @[[MEMBER]](
// REUSE-NOT: simulation.func private @{{.*}}.__member
