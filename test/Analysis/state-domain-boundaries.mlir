// RUN: obelisk-opt %s -o /dev/null --pass-pipeline='builtin.module(test-obelisk-sim-state-domain)' 2> %t.threaded
// RUN: obelisk-opt %s -o /dev/null --mlir-disable-threading --pass-pipeline='builtin.module(test-obelisk-sim-state-domain)' 2> %t.single
// RUN: diff %t.threaded %t.single
// RUN: FileCheck %s < %t.threaded

module {
  obelisk_sim.design @boundaries {
    obelisk_sim.scope.decl 0

    obelisk_sim.func @callee(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.logic<8> {obelisk_sim.capture_kind = 1 : i32})
        -> !obelisk_sim.logic<8> attributes {entry_kind = 8 : i32} {
      %resized = obelisk_sim.logic.resize %value signed = false : !obelisk_sim.logic<8> -> !obelisk_sim.logic<8>
      obelisk_sim.return %resized : !obelisk_sim.logic<8>
    }

    obelisk_sim.func @caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32} {
      %bits = arith.constant 5 : i8
      %known = obelisk_sim.logic.from_bits %bits : i8 -> !obelisk_sim.logic<8>
      %called = obelisk_sim.call @callee(%ctx, %known) : (!obelisk_sim.context, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<8>
      %looped = obelisk_sim.call @loop(%ctx, %known) : (!obelisk_sim.context, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<8>
      %recursive = obelisk_sim.call @recursive_a(%ctx, %called) : (!obelisk_sim.context, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<8>
      obelisk_sim.return
    }

    obelisk_sim.func @external_caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32} {
      %bits = arith.constant 1 : i8
      %known = obelisk_sim.logic.from_bits %bits : i8 -> !obelisk_sim.logic<8>
      %external = obelisk_sim.call @external_fn(%ctx, %known) : (!obelisk_sim.context, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<8>
      obelisk_sim.return
    }

    obelisk_sim.func private @external_fn(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.logic<8> {obelisk_sim.capture_kind = 1 : i32})
        -> !obelisk_sim.logic<8> attributes {entry_kind = 8 : i32}

    obelisk_sim.func @joiner(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32} {
      %condition = arith.constant true
      %known = obelisk_sim.logic.constant 3 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %unknown = obelisk_sim.logic.constant 0 : i8, -1 : i8 : !obelisk_sim.logic<8>
      cf.cond_br %condition, ^join(%known : !obelisk_sim.logic<8>),
          ^join(%unknown : !obelisk_sim.logic<8>)
    ^join(%value: !obelisk_sim.logic<8>):
      obelisk_sim.return
    }

    obelisk_sim.func @loop(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.logic<8> {obelisk_sim.capture_kind = 1 : i32})
        -> !obelisk_sim.logic<8> attributes {entry_kind = 8 : i32} {
      cf.br ^header(%value : !obelisk_sim.logic<8>)
    ^header(%current: !obelisk_sim.logic<8>):
      %next = obelisk_sim.logic.unary bit_not %current : (!obelisk_sim.logic<8>) -> !obelisk_sim.logic<8>
      %done = arith.constant true
      cf.cond_br %done, ^exit(%current : !obelisk_sim.logic<8>), ^header(%next : !obelisk_sim.logic<8>)
    ^exit(%result: !obelisk_sim.logic<8>):
      obelisk_sim.return %result : !obelisk_sim.logic<8>
    }

    obelisk_sim.func @mixed_continuation(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32} {
      %condition = arith.constant true
      %known = obelisk_sim.logic.constant 6 : i8, 0 : i8 : !obelisk_sim.logic<8>
      cf.cond_br %condition, ^suspend, ^ordinary
    ^suspend:
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^join(%known : !obelisk_sim.logic<8>)
    ^ordinary:
      cf.br ^join(%known : !obelisk_sim.logic<8>)
    ^join(%value: !obelisk_sim.logic<8>):
      obelisk_sim.return
    }

    obelisk_sim.func @nested_caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32} {
      %bits = arith.constant 4 : i8
      %known = obelisk_sim.logic.from_bits %bits : i8 -> !obelisk_sim.logic<8>
      %direct = obelisk_sim.call @nested_target(%ctx, %known) : (!obelisk_sim.context, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<8>
      %region_result = scf.execute_region -> !obelisk_sim.logic<8> {
        %unknown = obelisk_sim.logic.constant 0 : i8, -1 : i8 : !obelisk_sim.logic<8>
        %nested = obelisk_sim.call @nested_target(%ctx, %unknown) : (!obelisk_sim.context, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<8>
        scf.yield %nested : !obelisk_sim.logic<8>
      }
      obelisk_sim.return
    }

    obelisk_sim.func @nested_target(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.logic<8> {obelisk_sim.capture_kind = 1 : i32})
        -> !obelisk_sim.logic<8> attributes {entry_kind = 8 : i32} {
      %resized = obelisk_sim.logic.resize %value signed = false : !obelisk_sim.logic<8> -> !obelisk_sim.logic<8>
      obelisk_sim.return %resized : !obelisk_sim.logic<8>
    }

    obelisk_sim.func @recursive_a(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.logic<8> {obelisk_sim.capture_kind = 1 : i32})
        -> !obelisk_sim.logic<8> attributes {entry_kind = 8 : i32} {
      %from_b = obelisk_sim.call @recursive_b(%ctx, %value) : (!obelisk_sim.context, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<8>
      %zero = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %known = obelisk_sim.logic.binary and %from_b, %zero : !obelisk_sim.logic<8>
      obelisk_sim.return %known : !obelisk_sim.logic<8>
    }

    obelisk_sim.func @recursive_b(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.logic<8> {obelisk_sim.capture_kind = 1 : i32})
        -> !obelisk_sim.logic<8> attributes {entry_kind = 8 : i32} {
      %from_a = obelisk_sim.call @recursive_a(%ctx, %value) : (!obelisk_sim.context, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<8>
      obelisk_sim.return %from_a : !obelisk_sim.logic<8>
    }

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %bits = arith.constant 9 : i8
      %known = obelisk_sim.logic.from_bits %bits : i8 -> !obelisk_sim.logic<8>
      %unknown = obelisk_sim.logic.constant 0 : i8, -1 : i8 : !obelisk_sim.logic<8>
      %worker = obelisk_sim.spawn @worker(%ctx, %known) : !obelisk_sim.context, !obelisk_sim.logic<8> -> !obelisk_sim.process
      %unknown_process = obelisk_sim.spawn @unknown_worker(%ctx, %unknown) : !obelisk_sim.context, !obelisk_sim.logic<8> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @unbound(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.logic<8> {obelisk_sim.capture_kind = 1 : i32})
        -> !obelisk_sim.logic<8> attributes {entry_kind = 8 : i32} {
      obelisk_sim.return %value : !obelisk_sim.logic<8>
    }

    obelisk_sim.func @unknown_worker(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.logic<8> {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32} {
      obelisk_sim.return
    }

    obelisk_sim.func @worker(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.logic<8> {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32} {
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^resume(%value : !obelisk_sim.logic<8>)
    ^resume(%continued: !obelisk_sim.logic<8>):
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: state-domain @boundaries
// CHECK-LABEL: func @callee
// CHECK-NEXT:   bb0.arg1: two-state (call-actual)
// CHECK-NEXT:   bb0.op0.result0: two-state (logic-resize)
// CHECK-LABEL: func @caller
// CHECK-NEXT:   bb0.op1.result0: two-state (logic-from-bits)
// CHECK-NEXT:   bb0.op2.result0: two-state (call-result)
// CHECK-NEXT:   bb0.op3.result0: two-state (call-result)
// CHECK-NEXT:   bb0.op4.result0: two-state (call-result)
// CHECK-LABEL: func @external_caller
// CHECK-NEXT:   bb0.op1.result0: two-state (logic-from-bits)
// CHECK-NEXT:   bb0.op2.result0: may-four-state (external-declaration)
// CHECK-LABEL: func @external_fn
// CHECK-LABEL: func @joiner
// CHECK-NEXT:   bb0.op1.result0: two-state (logic-constant)
// CHECK-NEXT:   bb0.op2.result0: may-four-state (unknown-constant)
// CHECK-NEXT:   bb1.arg0: may-four-state (cfg-join)
// CHECK-LABEL: func @loop
// CHECK-NEXT:   bb0.arg1: two-state (call-actual)
// CHECK-NEXT:   bb1.arg0: two-state (cfg-join)
// CHECK-NEXT:   bb1.op0.result0: two-state (logic-unary)
// CHECK-NEXT:   bb2.arg0: two-state (cfg-join)
// CHECK-LABEL: func @mixed_continuation
// CHECK-NEXT:   bb0.op1.result0: two-state (logic-constant)
// CHECK-NEXT:   bb3.arg0: two-state (cfg-join)
// CHECK-LABEL: func @nested_caller
// CHECK-NEXT:   bb0.op1.result0: two-state (logic-from-bits)
// CHECK-NEXT:   bb0.op2.result0: may-four-state (call-result)
// CHECK-NEXT:   bb0.op3.result0: may-four-state (unsupported-producer)
// CHECK-NEXT:   bb1.op0.result0: may-four-state (unknown-constant)
// CHECK-NEXT:   bb1.op1.result0: may-four-state (call-result)
// CHECK-LABEL: func @nested_target
// CHECK-NEXT:   bb0.arg1: may-four-state (call-actual)
// CHECK-NEXT:   bb0.op0.result0: may-four-state (logic-resize)
// CHECK-LABEL: func @recursive_a
// CHECK-NEXT:   bb0.arg1: two-state (call-actual)
// CHECK-NEXT:   bb0.op0.result0: two-state (call-result)
// CHECK-NEXT:   bb0.op1.result0: two-state (logic-constant)
// CHECK-NEXT:   bb0.op2.result0: two-state (absorbing-constant)
// CHECK-LABEL: func @recursive_b
// CHECK-NEXT:   bb0.arg1: two-state (call-actual)
// CHECK-NEXT:   bb0.op0.result0: two-state (call-result)
// CHECK-LABEL: func @root
// CHECK-NEXT:   bb0.op1.result0: two-state (logic-from-bits)
// CHECK-NEXT:   bb0.op2.result0: may-four-state (unknown-constant)
// CHECK-LABEL: func @unbound
// CHECK-NEXT:   bb0.arg1: may-four-state (function-entry)
// CHECK-LABEL: func @unknown_worker
// CHECK-NEXT:   bb0.arg1: may-four-state (spawn-actual)
// CHECK-LABEL: func @worker
// CHECK-NEXT:   bb0.arg1: two-state (spawn-actual)
// CHECK-NEXT:   bb1.arg0: two-state (continuation)
