// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' > %t.off
// RUN: FileCheck %s --check-prefix=CHECK < %t.off
// RUN: FileCheck %s --check-prefix=OFF < %t.off
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph{vpi=read},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=READ

// Generated eval bodies retain a stable continuation in the normalized
// internal continuation namespace. Source diagnostic id 42 is intentionally
// remapped to dense scheduler continuation 1; the scheduler must not recover
// that identity from an operation pointer after fusion or CFG cleanup, and
// eligibility must not depend on a unit_N symbol spelling.
module attributes {schedule.native_scheduler = 3 : i32} {
  simulation.design @eval_continuation {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 continuous hierarchy "test.project_clock"
    simulation.code_unit.decl 2 in 0 function hierarchy "test.forward_static"
    simulation.code_unit.decl 3 in 0 function hierarchy "test.forward_coverage"
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i1 design
    simulation.storage.decl 2 in 0 : i1 static
    simulation.storage.decl 3 in 0 : i1 static {obelisk.coverage.toggle_observable}

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %output = simulation.context.storage %ctx[1] : !simulation.ref<i1>
      %process = simulation.spawn @project_clock(%ctx, %input, %output) :
          !simulation.context, !simulation.ref<i1>, !simulation.ref<i1>
          -> !simulation.process
      simulation.return
    }

    simulation.func private @project_clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !simulation.ref<i1>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64},
        %output: !simulation.ref<i1>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64} {
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !simulation.ref<i1> -> i1
      %forwarded = simulation.call @forward_static(%ctx, %value) :
          (!simulation.context, i1) -> i1
      %covered = simulation.call @forward_coverage(%ctx, %forwarded) :
          (!simulation.context, i1) -> i1
      simulation.ref.store %covered to %output : i1, !simulation.ref<i1>
      simulation.suspend.change %input to ^body
          {site = #schedule.continuation<id = 42>} : !simulation.ref<i1>
    }

    simulation.func private @forward_static(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i1 {simulation.capture_kind = 1 : i32}) -> i1
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %temporary = simulation.context.storage %ctx[2] : !simulation.ref<i1>
      simulation.ref.store %value to %temporary : i1, !simulation.ref<i1>
      %forwarded = simulation.ref.load %temporary : !simulation.ref<i1> -> i1
      simulation.return %forwarded : i1
    }

    simulation.func private @forward_coverage(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i1 {simulation.capture_kind = 1 : i32}) -> i1
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %temporary = simulation.context.storage %ctx[3] : !simulation.ref<i1>
      simulation.ref.store %value to %temporary : i1, !simulation.ref<i1>
      %forwarded = simulation.ref.load %temporary : !simulation.ref<i1> -> i1
      simulation.return %forwarded : i1
    }
  }

  // The root initializer is the one permitted non-load/store user of an
  // actor's captured storage reference.  Keep this direct-context form as a
  // regression for comparing the spawn's FlatSymbolRef callee with the
  // function's symbol name while proving the temporary private.
  simulation.design @spawn_context_promotion {
    simulation.scope.decl 0
    simulation.code_unit.decl 3 in 0 always hierarchy "test.context_actor"
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i1 static

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %clock = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %temporary = simulation.context.storage %ctx[1] : !simulation.ref<i1>
      %process = simulation.spawn @context_actor(%ctx, %clock, %temporary) :
          !simulation.context, !simulation.ref<i1>, !simulation.ref<i1>
          -> !simulation.process
      simulation.return
    }

    simulation.func private @context_actor(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64},
        %temporary: !simulation.ref<i1>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %value = arith.constant true
      simulation.ref.store %value to %temporary : i1, !simulation.ref<i1>
      %forwarded = simulation.ref.load %temporary :
          !simulation.ref<i1> -> i1
      %used = arith.xori %forwarded, %value : i1
      simulation.suspend.edge posedge %clock to ^body :
          !simulation.ref<i1>
    }
  }
}

// CHECK-LABEL: simulation.func private @project_clock.__obelisk_eval_body_0
// CHECK-SAME: schedule.eval.borrowed_captures
// CHECK-SAME: schedule.eval.continuation = 1 : i32
// CHECK-SAME: schedule.eval.raw_captures
// CHECK: simulation.ref.load
// CHECK: simulation.ref.store
// CHECK: simulation.return
// OFF-LABEL: simulation.func private @forward_static(
// OFF-NOT: simulation.ref.store
// OFF-NOT: simulation.ref.load
// OFF: simulation.return
// OFF-NOT: schedule.eval.discardable_store
// OFF-LABEL: simulation.func private @forward_coverage(
// OFF: simulation.ref.store
// OFF: simulation.ref.load
// OFF: simulation.return
// OFF-NOT: schedule.eval.discardable_store

// READ-LABEL: simulation.func private @project_clock(
// READ-SAME: schedule.eval.body = @[[READ_BODY:[^, }]+]]
// READ-COUNT-1: simulation.ref.store
// READ-NOT: schedule.eval.discardable_store
// READ-LABEL: simulation.func private @forward_static(
// READ-COUNT-1: simulation.ref.store
// READ-SAME: schedule.eval.discardable_store
// READ-NOT: simulation.ref.load
// READ: simulation.return
// READ-LABEL: simulation.func private @forward_coverage(
// READ: simulation.ref.store
// READ: simulation.ref.load
// READ: simulation.return
// READ-NOT: schedule.eval.discardable_store
// READ-LABEL: simulation.func private @project_clock.__obelisk_eval_body_{{[0-9]+}}(
// READ-NOT: simulation.context.storage %{{.*}}[2]
// READ-COUNT-1: simulation.ref.store
// READ-NOT: simulation.ref.subelement
// READ-NOT: schedule.eval.discardable_store

// OFF-LABEL: simulation.design @spawn_context_promotion
// OFF-LABEL: simulation.func private @context_actor(
// OFF-NOT: simulation.ref.store
// OFF-NOT: simulation.ref.load
// OFF: arith.xori

// READ-LABEL: simulation.design @spawn_context_promotion
// READ-LABEL: simulation.func private @context_actor(
// READ-COUNT-1: simulation.ref.store
// READ-NOT: simulation.ref.load
// READ-NOT: schedule.eval.discardable_store
