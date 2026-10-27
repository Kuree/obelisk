// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,talos-sim-verify-compute-graph,talos-sim-materialize-compute-fusion))' | FileCheck %s

// IEEE 1800-2023 9.2.2.2: the original always_comb actor executes its
// time-zero path during bootstrap. The eval body executes only the resumed
// activation. Different entry CFGs, initial loop values and initial effects
// therefore do not prevent extraction or get repeated on subsequent events.
module attributes {schedule.native_scheduler = 3 : i32} {
  simulation.design @forwarded {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "folded"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "different"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "effectful"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %a = simulation.spawn @folded(%ctx, %in, %out) : !simulation.context, !simulation.ref<i8>, !simulation.ref<i8> -> !simulation.process
      %b = simulation.spawn @different(%ctx, %in, %out) : !simulation.context, !simulation.ref<i8>, !simulation.ref<i8> -> !simulation.process
      %c = simulation.spawn @effectful(%ctx, %in, %out) : !simulation.context, !simulation.ref<i8>, !simulation.ref<i8> -> !simulation.process
      simulation.return
    }

    // CHECK-LABEL: simulation.func @folded(
    // CHECK-SAME: schedule.eval.body = @[[FOLDED:folded.__talos_eval_body_[0-9]+]]
    simulation.func @folded(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %in: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %out: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^activation:
      %restart = arith.constant 0 : i32
      cf.br ^head(%restart : i32)
    ^head(%i: i32):
      %limit = arith.constant 2 : i32
      %test = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %test, ^body, ^wait
    ^body:
      %value = simulation.ref.load %in : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %out : i8, !simulation.ref<i8>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^wait:
      simulation.suspend.any %in edges [0] to ^activation : !simulation.ref<i8>
    }

    // CHECK-LABEL: simulation.func @different(
    // CHECK-SAME: schedule.eval.body = @[[DIFFERENT:different.__talos_eval_body_[0-9]+]]
    simulation.func @different(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %in: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %out: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^activation:
      %restart = arith.constant 1 : i32
      cf.br ^head(%restart : i32)
    ^head(%i: i32):
      %limit = arith.constant 2 : i32
      %test = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %test, ^body, ^wait
    ^body:
      %value = simulation.ref.load %in : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %out : i8, !simulation.ref<i8>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^wait:
      simulation.suspend.any %in edges [0] to ^activation : !simulation.ref<i8>
    }

    // CHECK-LABEL: simulation.func @effectful(
    // CHECK-SAME: schedule.eval.body = @[[EFFECTFUL:effectful.__talos_eval_body_[0-9]+]]
    simulation.func @effectful(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %in: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %out: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      %zero = arith.constant 0 : i32
      %reset = arith.constant 0 : i8
      simulation.ref.store %reset to %out : i8, !simulation.ref<i8>
      cf.br ^head(%zero : i32)
    ^activation:
      %restart = arith.constant 0 : i32
      cf.br ^head(%restart : i32)
    ^head(%i: i32):
      %limit = arith.constant 2 : i32
      %test = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %test, ^body, ^wait
    ^body:
      %value = simulation.ref.load %in : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %out : i8, !simulation.ref<i8>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^wait:
      simulation.suspend.any %in edges [0] to ^activation : !simulation.ref<i8>
    }

    // The folded body enters through its activation, which restarts the loop
    // header at the time-zero induction value, and returns where the source
    // waits.
    // CHECK: simulation.func private @[[FOLDED]](
    // CHECK: cf.br ^[[ACTIVATION:bb[0-9]+]]
    // CHECK: ^[[ACTIVATION]]:
    // CHECK-NEXT: %[[ZERO:.*]] = arith.constant 0 : i32
    // CHECK-NEXT: cf.br ^{{bb[0-9]+}}(%[[ZERO]] : i32)
    // CHECK: arith.cmpi slt
    // CHECK: simulation.return
    // CHECK: simulation.func private @[[DIFFERENT]](
    // CHECK: %[[ONE:.*]] = arith.constant 1 : i32
    // CHECK-NEXT: cf.br ^{{bb[0-9]+}}(%[[ONE]] : i32)
    // CHECK: simulation.return
    // CHECK: simulation.func private @[[EFFECTFUL]](
    // CHECK-NOT: simulation.ref.store
    // CHECK: %[[INPUT:.*]] = simulation.ref.load
    // CHECK-NEXT: simulation.ref.store %[[INPUT]]
    // CHECK-NOT: simulation.ref.store
    // CHECK: cf.br
  }
}
