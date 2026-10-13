// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true primitive-only=true max-straight-line-members=2},obelisk-sim-materialize-compute-fusion))' | FileCheck %s

// With identical continuation returns (each member resumes with its current
// value), both cohorts share one helper. The base test keeps separate
// helpers for different return values.
// CHECK-LABEL: simulation.design @return_mapping
// CHECK-COUNT-2: simulation.spawn @__obelisk_region_kernel_
// CHECK: simulation.call @[[MEMBER:__obelisk_region_kernel_[A-Za-z0-9_]+\.__member]](
// CHECK: simulation.call @[[MEMBER]](
// CHECK: simulation.func private @[[MEMBER]](
// CHECK-NOT: simulation.func private @{{.*}}.__member
// CHECK: simulation.call @[[MEMBER]](
// CHECK-NOT: simulation.func private @{{.*}}.__member
// CHECK: simulation.call @[[MEMBER]](
// CHECK-NOT: simulation.func private @{{.*}}.__member

!logic = !simulation.logic<1>
!ref = !simulation.ref<!logic>
!driver = !simulation.driver<!logic>
module {
  simulation.design @return_mapping {
    simulation.scope.decl 0 hierarchy "return_mapping"
    simulation.code_unit.decl 20 in 0 continuous hierarchy "return_mapping.r0"
    simulation.code_unit.decl 21 in 0 continuous hierarchy "return_mapping.r1"
    simulation.code_unit.decl 22 in 0 continuous hierarchy "return_mapping.r2"
    simulation.code_unit.decl 23 in 0 continuous hierarchy "return_mapping.r3"
    simulation.storage.decl 0 in 0 : !logic design
    simulation.storage.decl 1 in 0 : !logic design
    simulation.storage.decl 2 in 0 : !logic design
    simulation.storage.decl 3 in 0 : !logic design
    simulation.net.decl 0 in 0 : !logic design
    simulation.net.decl 1 in 0 : !logic design
    simulation.net.decl 2 in 0 : !logic design
    simulation.net.decl 3 in 0 : !logic design
    simulation.driver.decl 0 in 0 drives 0 : !logic design
    simulation.driver.decl 1 in 0 drives 1 : !logic design
    simulation.driver.decl 2 in 0 drives 2 : !logic design
    simulation.driver.decl 3 in 0 drives 3 : !logic design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %i0 = simulation.context.storage %ctx[0] : !ref
      %i1 = simulation.context.storage %ctx[1] : !ref
      %i2 = simulation.context.storage %ctx[2] : !ref
      %i3 = simulation.context.storage %ctx[3] : !ref
      %d0 = simulation.context.driver %ctx[0] : !driver
      %d1 = simulation.context.driver %ctx[1] : !driver
      %d2 = simulation.context.driver %ctx[2] : !driver
      %d3 = simulation.context.driver %ctx[3] : !driver
      %p0 = simulation.spawn @r0(%ctx, %i0, %d0) : !simulation.context, !ref, !driver -> !simulation.process
      %p1 = simulation.spawn @r1(%ctx, %i1, %d1) : !simulation.context, !ref, !driver -> !simulation.process
      %p2 = simulation.spawn @r2(%ctx, %i2, %d2) : !simulation.context, !ref, !driver -> !simulation.process
      %p3 = simulation.spawn @r3(%ctx, %i3, %d3) : !simulation.context, !ref, !driver -> !simulation.process
      simulation.return
    }
    simulation.func private @r0(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %driver: !driver {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 20 : i64, schedule.primitive_name = "same"} {
      %initial = simulation.logic.constant false, true : !logic
      cf.br ^body(%initial : !logic)
    ^body(%previous: !logic):
      %value = simulation.ref.load %input : !ref -> !logic
      %changed = simulation.logic.compare case_ne %previous, %value : (!logic, !logic) -> i1
      simulation.driver.drive %driver = %value : !driver, !logic
      simulation.suspend.change %input to ^body(%value : !logic) : !ref
    }
    simulation.func private @r1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %driver: !driver {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 21 : i64, schedule.primitive_name = "same"} {
      %initial = simulation.logic.constant false, true : !logic
      cf.br ^body(%initial : !logic)
    ^body(%previous: !logic):
      %value = simulation.ref.load %input : !ref -> !logic
      %changed = simulation.logic.compare case_ne %previous, %value : (!logic, !logic) -> i1
      simulation.driver.drive %driver = %value : !driver, !logic
      simulation.suspend.change %input to ^body(%value : !logic) : !ref
    }
    simulation.func private @r2(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %driver: !driver {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 22 : i64, schedule.primitive_name = "same"} {
      %initial = simulation.logic.constant false, true : !logic
      cf.br ^body(%initial : !logic)
    ^body(%previous: !logic):
      %value = simulation.ref.load %input : !ref -> !logic
      %changed = simulation.logic.compare case_ne %previous, %value : (!logic, !logic) -> i1
      simulation.driver.drive %driver = %value : !driver, !logic
      simulation.suspend.change %input to ^body(%value : !logic) : !ref
    }
    simulation.func private @r3(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}, %driver: !driver {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 23 : i64, schedule.primitive_name = "same"} {
      %initial = simulation.logic.constant false, true : !logic
      cf.br ^body(%initial : !logic)
    ^body(%previous: !logic):
      %value = simulation.ref.load %input : !ref -> !logic
      %changed = simulation.logic.compare case_ne %previous, %value : (!logic, !logic) -> i1
      simulation.driver.drive %driver = %value : !driver, !logic
      simulation.suspend.change %input to ^body(%value : !logic) : !ref
    }
  }
}
