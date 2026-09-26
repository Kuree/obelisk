// RUN: %split-file %s %t
// RUN: sed 's/!obelisk_sim.logic<1>/i1/g' %t/chunks.mlir > %t/two-state.mlir
// RUN: obelisk-opt %t/two-state.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true primitive-only=true max-straight-line-members=2},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=TWO-STATE
// RUN: obelisk-opt %t/chunks.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true primitive-only=true max-straight-line-members=2},obelisk-sim-materialize-compute-fusion,obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=CHUNK
// RUN: not obelisk-opt %t/chunks.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-fuse-compute-fragments{body-fusion=true primitive-only=true max-straight-line-members=65}))' 2>&1 | FileCheck %s --check-prefix=LIMIT
// RUN: obelisk-opt %t/different-scopes.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true primitive-only=true max-straight-line-members=64},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=SCOPE
// RUN: obelisk-opt %t/cyclic.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true primitive-only=true max-straight-line-members=64},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=CYCLE

// Runtime behavior is checked in ../Runtime/simulation-fuse-primitive-cohorts.test.

// Five same-sensitivity primitive actors exercise the collision between the
// general sensitivity planner and the straight-line planner.  They must form
// exactly two kernels plus a one-member tail.  Re-running ordinary O3-style
// body fusion must not fuse or churn the generated kernels.
//--- chunks.mlir
module {
  obelisk_sim.design @chunks {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 continuous hierarchy "chunks.p0"
    obelisk_sim.code_unit.decl 2 in 0 continuous hierarchy "chunks.p1"
    obelisk_sim.code_unit.decl 3 in 0 continuous hierarchy "chunks.p2"
    obelisk_sim.code_unit.decl 4 in 0 continuous hierarchy "chunks.p3"
    obelisk_sim.code_unit.decl 5 in 0 continuous hierarchy "chunks.p4"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 2 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 3 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 4 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 1 in 0 drives 1 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 2 in 0 drives 2 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 3 in 0 drives 3 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 4 in 0 drives 4 : !obelisk_sim.logic<1> design

    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %input = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %d0 = obelisk_sim.context.driver %ctx[0] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d1 = obelisk_sim.context.driver %ctx[1] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d2 = obelisk_sim.context.driver %ctx[2] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d3 = obelisk_sim.context.driver %ctx[3] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d4 = obelisk_sim.context.driver %ctx[4] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %p0 = obelisk_sim.spawn @p0(%ctx, %input, %d0) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.driver<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %p1 = obelisk_sim.spawn @p1(%ctx, %input, %d1) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.driver<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %p2 = obelisk_sim.spawn @p2(%ctx, %input, %d2) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.driver<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %p3 = obelisk_sim.spawn @p3(%ctx, %input, %d3) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.driver<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %p4 = obelisk_sim.spawn @p4(%ctx, %input, %d4) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.driver<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @p0(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64, obelisk_sim.primitive_name = "buf"} {
      cf.br ^body
    ^body:
      %value = obelisk_sim.ref.load %input : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %driver = %value : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.suspend.change %input to ^body : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    }
    obelisk_sim.func private @p1(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 1 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64, obelisk_sim.primitive_name = "buf"} {
      cf.br ^body
    ^body:
      %value = obelisk_sim.ref.load %input : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %driver = %value : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.suspend.change %input to ^body : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    }
    obelisk_sim.func private @p2(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 2 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 3 : i64, obelisk_sim.primitive_name = "buf"} {
      cf.br ^body
    ^body:
      %value = obelisk_sim.ref.load %input : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %driver = %value : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.suspend.change %input to ^body : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    }
    obelisk_sim.func private @p3(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 3 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 4 : i64, obelisk_sim.primitive_name = "buf"} {
      cf.br ^body
    ^body:
      %value = obelisk_sim.ref.load %input : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %driver = %value : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.suspend.change %input to ^body : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    }
    obelisk_sim.func private @p4(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 4 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 5 : i64, obelisk_sim.primitive_name = "buf"} {
      cf.br ^body
    ^body:
      %value = obelisk_sim.ref.load %input : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %driver = %value : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.suspend.change %input to ^body : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    }
  }
}

// CHUNK-LABEL: obelisk_sim.design @chunks
// CHUNK-COUNT-2: obelisk_sim.spawn @__obelisk_region_kernel_
// CHUNK: obelisk_sim.spawn @p4
// CHUNK: obelisk_sim.func private @p4
// CHUNK-COUNT-2: obelisk_sim.func private @__obelisk_region_kernel_
// CHUNK-NOT: obelisk_sim.func private @p0
// CHUNK-NOT: obelisk_sim.func private @p1
// CHUNK-NOT: obelisk_sim.func private @p2
// CHUNK-NOT: obelisk_sim.func private @p3
// CHUNK-NOT: .__member
// LIMIT: error: 'obelisk_sim.design' op straight-line fusion member limit must be between 2 and 64
// TWO-STATE-COUNT-2: obelisk_sim.spawn @__obelisk_region_kernel_
// TWO-STATE: arith.cmpi eq, {{.*}} : i1
// TWO-STATE-NOT: obelisk_sim.logic.compare

// SCOPE-LABEL: obelisk_sim.design @different_scopes

//--- different-scopes.mlir
module {
  obelisk_sim.design @different_scopes {
    obelisk_sim.scope.decl 0
    obelisk_sim.scope.decl 1 parent 0 hierarchy "different_scopes.child"
    obelisk_sim.code_unit.decl 1 in 0 continuous hierarchy "different_scopes.parent"
    obelisk_sim.code_unit.decl 2 in 1 continuous hierarchy "different_scopes.child"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 1 in 1 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 1 in 1 drives 1 : !obelisk_sim.logic<1> design
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %input = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %d0 = obelisk_sim.context.driver %ctx[0] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d1 = obelisk_sim.context.driver %ctx[1] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %p0 = obelisk_sim.spawn @parent(%ctx, %input, %d0) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.driver<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %p1 = obelisk_sim.spawn @child(%ctx, %input, %d1) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.driver<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func private @parent(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64, obelisk_sim.primitive_name = "buf"} {
      cf.br ^body
    ^body:
      %value = obelisk_sim.ref.load %input : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %driver = %value : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.suspend.change %input to ^body : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    }
    obelisk_sim.func private @child(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 1 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64, obelisk_sim.primitive_name = "buf"} {
      cf.br ^body
    ^body:
      %value = obelisk_sim.ref.load %input : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %driver = %value : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.suspend.change %input to ^body : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    }
  }
}

// SCOPE: obelisk_sim.spawn @parent
// SCOPE: obelisk_sim.spawn @child
// SCOPE-NOT: __obelisk_region_kernel_

//--- cyclic.mlir
// A real sensitivity cycle must keep the original convergence-scheduled
// actors. The straight-line kernel rejects the backward edge rather than
// hiding oscillation or changing the scheduler's nonconvergence behavior.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @cyclic {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 3 in 0 root_initializer hierarchy "cyclic.root"
    obelisk_sim.code_unit.decl 1 in 0 continuous hierarchy "cyclic.first"
    obelisk_sim.code_unit.decl 2 in 0 continuous hierarchy "cyclic.second"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 1 in 0 drives 1 : !obelisk_sim.logic<1> design
    obelisk_sim.func @__obelisk_root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 3 : i64} {
      %n0 = obelisk_sim.context.net %ctx[0] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %n1 = obelisk_sim.context.net %ctx[1] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %d0 = obelisk_sim.context.driver %ctx[0] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d1 = obelisk_sim.context.driver %ctx[1] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %p0 = obelisk_sim.spawn @first(%ctx, %n1, %d0) : !obelisk_sim.context, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.driver<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %p1 = obelisk_sim.spawn @second(%ctx, %n0, %d1) : !obelisk_sim.context, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.driver<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func private @first(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 1 : i64}, %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64, obelisk_sim.primitive_name = "not"} {
      cf.br ^body
    ^body:
      %value = obelisk_sim.net.read %input : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %inverted = obelisk_sim.logic.unary bit_not %value : (!obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %driver = %inverted : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.suspend.change %input to ^body : !obelisk_sim.net<!obelisk_sim.logic<1>>
    }
    obelisk_sim.func private @second(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 0 : i64}, %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 1 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64, obelisk_sim.primitive_name = "not"} {
      cf.br ^body
    ^body:
      %value = obelisk_sim.net.read %input : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %driver = %value : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.suspend.change %input to ^body : !obelisk_sim.net<!obelisk_sim.logic<1>>
    }
  }
}

// CYCLE-LABEL: obelisk_sim.design @cyclic
// CYCLE: obelisk_sim.spawn @first
// CYCLE: obelisk_sim.spawn @second
// CYCLE: obelisk_sim.func private @first
// CYCLE: obelisk_sim.func private @second
// CYCLE-NOT: __obelisk_region_kernel_
