// RUN: %split-file %s %t
// RUN: sed 's/!simulation.logic<1>/i1/g' %t/chunks.mlir > %t/two-state.mlir
// RUN: obelisk-opt %t/two-state.mlir --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true primitive-only=true max-straight-line-members=2},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=TWO-STATE
// RUN: obelisk-opt %t/chunks.mlir --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true primitive-only=true max-straight-line-members=2},obelisk-sim-materialize-compute-fusion,obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=CHUNK
// RUN: not obelisk-opt %t/chunks.mlir --pass-pipeline='builtin.module(simulation.design(obelisk-sim-fuse-compute-fragments{body-fusion=true primitive-only=true max-straight-line-members=65}))' 2>&1 | FileCheck %s --check-prefix=LIMIT
// RUN: obelisk-opt %t/different-scopes.mlir --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true primitive-only=true max-straight-line-members=64},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=SCOPE
// RUN: obelisk-opt %t/cyclic.mlir --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true primitive-only=true max-straight-line-members=64},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=CYCLE

// Runtime behavior is checked in ../Runtime/simulation-fuse-primitive-cohorts.test.

// Five same-sensitivity primitive actors exercise the collision between the
// general sensitivity planner and the straight-line planner.  They must form
// exactly two kernels plus a one-member tail.  Re-running ordinary O3-style
// body fusion must not fuse or churn the generated kernels.
//--- chunks.mlir
module {
  simulation.design @chunks {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 continuous hierarchy "chunks.p0"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "chunks.p1"
    simulation.code_unit.decl 3 in 0 continuous hierarchy "chunks.p2"
    simulation.code_unit.decl 4 in 0 continuous hierarchy "chunks.p3"
    simulation.code_unit.decl 5 in 0 continuous hierarchy "chunks.p4"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 1 in 0 : !simulation.logic<1> design
    simulation.net.decl 2 in 0 : !simulation.logic<1> design
    simulation.net.decl 3 in 0 : !simulation.logic<1> design
    simulation.net.decl 4 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design
    simulation.driver.decl 1 in 0 drives 1 : !simulation.logic<1> design
    simulation.driver.decl 2 in 0 drives 2 : !simulation.logic<1> design
    simulation.driver.decl 3 in 0 drives 3 : !simulation.logic<1> design
    simulation.driver.decl 4 in 0 drives 4 : !simulation.logic<1> design

    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %d0 = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<1>>
      %d1 = simulation.context.driver %ctx[1] : !simulation.driver<!simulation.logic<1>>
      %d2 = simulation.context.driver %ctx[2] : !simulation.driver<!simulation.logic<1>>
      %d3 = simulation.context.driver %ctx[3] : !simulation.driver<!simulation.logic<1>>
      %d4 = simulation.context.driver %ctx[4] : !simulation.driver<!simulation.logic<1>>
      %p0 = simulation.spawn @p0(%ctx, %input, %d0) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.driver<!simulation.logic<1>> -> !simulation.process
      %p1 = simulation.spawn @p1(%ctx, %input, %d1) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.driver<!simulation.logic<1>> -> !simulation.process
      %p2 = simulation.spawn @p2(%ctx, %input, %d2) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.driver<!simulation.logic<1>> -> !simulation.process
      %p3 = simulation.spawn @p3(%ctx, %input, %d3) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.driver<!simulation.logic<1>> -> !simulation.process
      %p4 = simulation.spawn @p4(%ctx, %input, %d4) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.driver<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }

    simulation.func private @p0(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %driver: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64, schedule.primitive_name = "buf"} {
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.suspend.change %input to ^body : !simulation.ref<!simulation.logic<1>>
    }
    simulation.func private @p1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %driver: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64, schedule.primitive_name = "buf"} {
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.suspend.change %input to ^body : !simulation.ref<!simulation.logic<1>>
    }
    simulation.func private @p2(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %driver: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 3 : i64, schedule.primitive_name = "buf"} {
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.suspend.change %input to ^body : !simulation.ref<!simulation.logic<1>>
    }
    simulation.func private @p3(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %driver: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 4 : i64, schedule.primitive_name = "buf"} {
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.suspend.change %input to ^body : !simulation.ref<!simulation.logic<1>>
    }
    simulation.func private @p4(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %driver: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 5 : i64, schedule.primitive_name = "buf"} {
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.suspend.change %input to ^body : !simulation.ref<!simulation.logic<1>>
    }
  }
}

// CHUNK-LABEL: simulation.design @chunks
// CHUNK-COUNT-2: simulation.spawn @__obelisk_region_kernel_
// CHUNK: simulation.spawn @p4
// CHUNK: simulation.func private @p4
// CHUNK-COUNT-2: simulation.func private @__obelisk_region_kernel_
// CHUNK-NOT: simulation.func private @p0
// CHUNK-NOT: simulation.func private @p1
// CHUNK-NOT: simulation.func private @p2
// CHUNK-NOT: simulation.func private @p3
// CHUNK-NOT: .__member
// LIMIT: error: 'simulation.design' op straight-line fusion member limit must be between 2 and 64
// TWO-STATE-COUNT-2: simulation.spawn @__obelisk_region_kernel_
// TWO-STATE: arith.cmpi eq, {{.*}} : i1
// TWO-STATE-NOT: simulation.logic.compare

// SCOPE-LABEL: simulation.design @different_scopes

//--- different-scopes.mlir
module {
  simulation.design @different_scopes {
    simulation.scope.decl 0
    simulation.scope.decl 1 parent 0 hierarchy "different_scopes.child"
    simulation.code_unit.decl 1 in 0 continuous hierarchy "different_scopes.parent"
    simulation.code_unit.decl 2 in 1 continuous hierarchy "different_scopes.child"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 1 in 1 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design
    simulation.driver.decl 1 in 1 drives 1 : !simulation.logic<1> design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %d0 = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<1>>
      %d1 = simulation.context.driver %ctx[1] : !simulation.driver<!simulation.logic<1>>
      %p0 = simulation.spawn @parent(%ctx, %input, %d0) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.driver<!simulation.logic<1>> -> !simulation.process
      %p1 = simulation.spawn @child(%ctx, %input, %d1) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.driver<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }
    simulation.func private @parent(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %driver: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64, schedule.primitive_name = "buf"} {
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.suspend.change %input to ^body : !simulation.ref<!simulation.logic<1>>
    }
    simulation.func private @child(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %driver: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64, schedule.primitive_name = "buf"} {
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.suspend.change %input to ^body : !simulation.ref<!simulation.logic<1>>
    }
  }
}

// SCOPE: simulation.spawn @parent
// SCOPE: simulation.spawn @child
// SCOPE-NOT: __obelisk_region_kernel_

//--- cyclic.mlir
// A real sensitivity cycle must keep the original convergence-scheduled
// actors. The straight-line kernel rejects the backward edge rather than
// hiding oscillation or changing the scheduler's nonconvergence behavior.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @cyclic {
    simulation.scope.decl 0
    simulation.code_unit.decl 3 in 0 root_initializer hierarchy "cyclic.root"
    simulation.code_unit.decl 1 in 0 continuous hierarchy "cyclic.first"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "cyclic.second"
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 1 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design
    simulation.driver.decl 1 in 0 drives 1 : !simulation.logic<1> design
    simulation.func @__obelisk_root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 3 : i64} {
      %n0 = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<1>>
      %n1 = simulation.context.net %ctx[1] : !simulation.net<!simulation.logic<1>>
      %d0 = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<1>>
      %d1 = simulation.context.driver %ctx[1] : !simulation.driver<!simulation.logic<1>>
      %p0 = simulation.spawn @first(%ctx, %n1, %d0) : !simulation.context, !simulation.net<!simulation.logic<1>>, !simulation.driver<!simulation.logic<1>> -> !simulation.process
      %p1 = simulation.spawn @second(%ctx, %n0, %d1) : !simulation.context, !simulation.net<!simulation.logic<1>>, !simulation.driver<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }
    simulation.func private @first(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 1 : i64}, %driver: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64, schedule.primitive_name = "not"} {
      cf.br ^body
    ^body:
      %value = simulation.net.read %input : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %inverted = simulation.logic.unary bit_not %value : (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.driver.drive %driver = %inverted : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.suspend.change %input to ^body : !simulation.net<!simulation.logic<1>>
    }
    simulation.func private @second(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64}, %driver: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64, schedule.primitive_name = "not"} {
      cf.br ^body
    ^body:
      %value = simulation.net.read %input : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.suspend.change %input to ^body : !simulation.net<!simulation.logic<1>>
    }
  }
}

// CYCLE-LABEL: simulation.design @cyclic
// CYCLE: simulation.spawn @first
// CYCLE: simulation.spawn @second
// CYCLE: simulation.func private @first
// CYCLE: simulation.func private @second
// CYCLE-NOT: __obelisk_region_kernel_
