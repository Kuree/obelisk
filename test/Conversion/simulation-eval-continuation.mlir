// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' > %t.off
// RUN: FileCheck %s --check-prefix=CHECK < %t.off
// RUN: FileCheck %s --check-prefix=OFF < %t.off
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph{vpi=read},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=READ

// Generated eval bodies retain a stable continuation in the normalized
// internal continuation namespace. Source diagnostic id 42 is intentionally
// remapped to dense scheduler continuation 1; the scheduler must not recover
// that identity from an operation pointer after fusion or CFG cleanup, and
// eligibility must not depend on a unit_N symbol spelling.
module attributes {obelisk.native_scheduler = 3 : i32} {
  obelisk_sim.design @eval_continuation {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 continuous hierarchy "test.project_clock"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "test.forward_static"
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.storage.decl 1 in 0 : i1 design
    obelisk_sim.storage.decl 2 in 0 : i1 static

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %input = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i1>
      %output = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<i1>
      %process = obelisk_sim.spawn @project_clock(%ctx, %input, %output) :
          !obelisk_sim.context, !obelisk_sim.ref<i1>, !obelisk_sim.ref<i1>
          -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @project_clock(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %input: !obelisk_sim.ref<i1>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %output: !obelisk_sim.ref<i1>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64} {
      cf.br ^body
    ^body:
      %value = obelisk_sim.ref.load %input : !obelisk_sim.ref<i1> -> i1
      %forwarded = obelisk_sim.call @forward_static(%ctx, %value) :
          (!obelisk_sim.context, i1) -> i1
      obelisk_sim.ref.store %forwarded to %output : i1, !obelisk_sim.ref<i1>
      obelisk_sim.suspend.change %input to ^body
          {site = #obelisk_sim.continuation<id = 42>} : !obelisk_sim.ref<i1>
    }

    obelisk_sim.func private @forward_static(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i1 {obelisk_sim.capture_kind = 1 : i32}) -> i1
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %temporary = obelisk_sim.context.storage %ctx[2] : !obelisk_sim.ref<i1>
      obelisk_sim.ref.store %value to %temporary : i1, !obelisk_sim.ref<i1>
      %forwarded = obelisk_sim.ref.load %temporary : !obelisk_sim.ref<i1> -> i1
      obelisk_sim.return %forwarded : i1
    }
  }

  // The root initializer is the one permitted non-load/store user of an
  // actor's captured storage reference.  Keep this direct-context form as a
  // regression for comparing the spawn's FlatSymbolRef callee with the
  // function's symbol name while proving the temporary private.
  obelisk_sim.design @spawn_context_promotion {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "test.context_actor"
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.storage.decl 1 in 0 : i1 static

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %clock = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i1>
      %temporary = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<i1>
      %process = obelisk_sim.spawn @context_actor(%ctx, %clock, %temporary) :
          !obelisk_sim.context, !obelisk_sim.ref<i1>, !obelisk_sim.ref<i1>
          -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @context_actor(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<i1>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %temporary: !obelisk_sim.ref<i1>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %value = arith.constant true
      obelisk_sim.ref.store %value to %temporary : i1, !obelisk_sim.ref<i1>
      %forwarded = obelisk_sim.ref.load %temporary :
          !obelisk_sim.ref<i1> -> i1
      %used = arith.xori %forwarded, %value : i1
      obelisk_sim.suspend.edge posedge %clock to ^body :
          !obelisk_sim.ref<i1>
    }
  }
}

// CHECK-LABEL: obelisk_sim.func private @project_clock.__obelisk_eval_body_0
// CHECK-SAME: obelisk.eval.borrowed_captures
// CHECK-SAME: obelisk.eval.continuation = 1 : i32
// CHECK-SAME: obelisk.eval.raw_captures
// CHECK: obelisk_sim.ref.load
// CHECK: obelisk_sim.ref.store
// CHECK: obelisk_sim.return
// OFF-LABEL: obelisk_sim.func private @forward_static(
// OFF-NOT: obelisk_sim.ref.store
// OFF-NOT: obelisk_sim.ref.load
// OFF: obelisk_sim.return
// OFF-NOT: obelisk.eval.discardable_store

// READ-LABEL: obelisk_sim.func private @project_clock(
// READ-SAME: obelisk.eval.body = @[[READ_BODY:[^, }]+]]
// READ-COUNT-1: obelisk_sim.ref.store
// READ-NOT: obelisk.eval.discardable_store
// READ-LABEL: obelisk_sim.func private @forward_static(
// READ-COUNT-1: obelisk_sim.ref.store
// READ-SAME: obelisk.eval.discardable_store
// READ-NOT: obelisk_sim.ref.load
// READ: obelisk_sim.return
// READ-LABEL: obelisk_sim.func private @project_clock.__obelisk_eval_body_{{[0-9]+}}(
// READ-NOT: obelisk_sim.context.storage %{{.*}}[2]
// READ-COUNT-1: obelisk_sim.ref.store
// READ-NOT: obelisk_sim.ref.subelement
// READ-NOT: obelisk.eval.discardable_store

// OFF-LABEL: obelisk_sim.design @spawn_context_promotion
// OFF-LABEL: obelisk_sim.func private @context_actor(
// OFF-NOT: obelisk_sim.ref.store
// OFF-NOT: obelisk_sim.ref.load
// OFF: arith.xori

// READ-LABEL: obelisk_sim.design @spawn_context_promotion
// READ-LABEL: obelisk_sim.func private @context_actor(
// READ-COUNT-1: obelisk_sim.ref.store
// READ-NOT: obelisk_sim.ref.load
// READ-NOT: obelisk.eval.discardable_store
