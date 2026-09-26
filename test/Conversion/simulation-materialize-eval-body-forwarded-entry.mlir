// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' | FileCheck %s

// IEEE 1800-2023 9.2.2.2: an always_comb procedure runs its activation once
// at time zero, then after each change of its inputs. Canonicalization folds
// the argument-free activation block that only restarts a for-loop out of the
// time-zero path, so the entry branches to the loop header with the same
// constant. That entry still reaches the activation and the process keeps its
// standalone eval body. A forwarding block that passes a different value is
// a different activation and gets no body, and so does an entry that stores
// before it branches: the eval body would repeat that time-zero store on every
// activation.
module attributes {obelisk.native_scheduler = 3 : i32} {
  obelisk_sim.design @forwarded {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i8 design
    obelisk_sim.storage.decl 1 in 0 : i8 design
    obelisk_sim.code_unit.decl 1 in 0 always_comb hierarchy "folded"
    obelisk_sim.code_unit.decl 2 in 0 always_comb hierarchy "different"
    obelisk_sim.code_unit.decl 3 in 0 always_comb hierarchy "effectful"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %in = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i8>
      %out = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<i8>
      %a = obelisk_sim.spawn @folded(%ctx, %in, %out) : !obelisk_sim.context, !obelisk_sim.ref<i8>, !obelisk_sim.ref<i8> -> !obelisk_sim.process
      %b = obelisk_sim.spawn @different(%ctx, %in, %out) : !obelisk_sim.context, !obelisk_sim.ref<i8>, !obelisk_sim.ref<i8> -> !obelisk_sim.process
      %c = obelisk_sim.spawn @effectful(%ctx, %in, %out) : !obelisk_sim.context, !obelisk_sim.ref<i8>, !obelisk_sim.ref<i8> -> !obelisk_sim.process
      obelisk_sim.return
    }

    // CHECK-LABEL: obelisk_sim.func @folded(
    // CHECK-SAME: obelisk.eval.body = @[[FOLDED:folded.__obelisk_eval_body_[0-9]+]]
    obelisk_sim.func @folded(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %in: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %out: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
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
      %value = obelisk_sim.ref.load %in : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.ref.store %value to %out : i8, !obelisk_sim.ref<i8>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^wait:
      obelisk_sim.suspend.any %in edges [0] to ^activation : !obelisk_sim.ref<i8>
    }

    // CHECK-LABEL: obelisk_sim.func @different(
    // CHECK-NOT: obelisk.eval.body
    // CHECK: obelisk_sim.suspend.any
    obelisk_sim.func @different(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %in: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %out: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
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
      %value = obelisk_sim.ref.load %in : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.ref.store %value to %out : i8, !obelisk_sim.ref<i8>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^wait:
      obelisk_sim.suspend.any %in edges [0] to ^activation : !obelisk_sim.ref<i8>
    }

    // CHECK-LABEL: obelisk_sim.func @effectful(
    // CHECK-NOT: obelisk.eval.body
    // CHECK: obelisk_sim.suspend.any
    obelisk_sim.func @effectful(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %in: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %out: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      %zero = arith.constant 0 : i32
      %reset = arith.constant 0 : i8
      obelisk_sim.ref.store %reset to %out : i8, !obelisk_sim.ref<i8>
      cf.br ^head(%zero : i32)
    ^activation:
      %restart = arith.constant 0 : i32
      cf.br ^head(%restart : i32)
    ^head(%i: i32):
      %limit = arith.constant 2 : i32
      %test = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %test, ^body, ^wait
    ^body:
      %value = obelisk_sim.ref.load %in : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.ref.store %value to %out : i8, !obelisk_sim.ref<i8>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^wait:
      obelisk_sim.suspend.any %in edges [0] to ^activation : !obelisk_sim.ref<i8>
    }

    // The folded body enters through its activation, which restarts the loop
    // header at the time-zero induction value, and returns where the source
    // waits.
    // CHECK: obelisk_sim.func private @[[FOLDED]](
    // CHECK: cf.br ^[[ACTIVATION:bb[0-9]+]]
    // CHECK: ^[[ACTIVATION]]:
    // CHECK-NEXT: %[[ZERO:.*]] = arith.constant 0 : i32
    // CHECK-NEXT: cf.br ^{{bb[0-9]+}}(%[[ZERO]] : i32)
    // CHECK: arith.cmpi slt
    // CHECK: obelisk_sim.return
    // CHECK-NOT: different.__obelisk_eval_body
    // CHECK-NOT: effectful.__obelisk_eval_body
  }
}
