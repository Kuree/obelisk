// RUN: obelisk-opt %s -o /dev/null --pass-pipeline='builtin.module(test-obelisk-sim-state-domain)' 2> %t.threaded
// RUN: obelisk-opt %s -o /dev/null --mlir-disable-threading --pass-pipeline='builtin.module(test-obelisk-sim-state-domain)' 2> %t.single
// RUN: diff %t.threaded %t.single
// RUN: FileCheck %s < %t.threaded
// RUN: obelisk-opt %s -o /dev/null --pass-pipeline='builtin.module(test-obelisk-sim-state-domain{shared=true})' 2> %t.shared
// RUN: diff %t.threaded %t.shared

module {
  simulation.design @boundaries {
    simulation.code_unit.decl 9000001 in 0 function hierarchy "test.boundaries.callee.9000001"
    simulation.code_unit.decl 9000002 in 0 initial hierarchy "test.boundaries.caller.9000002"
    simulation.code_unit.decl 9000003 in 0 initial hierarchy "test.boundaries.external_caller.9000003"
    simulation.code_unit.decl 9000004 in 0 initial hierarchy "test.boundaries.joiner.9000004"
    simulation.code_unit.decl 9000005 in 0 function hierarchy "test.boundaries.loop.9000005"
    simulation.code_unit.decl 9000006 in 0 initial hierarchy "test.boundaries.mixed_continuation.9000006"
    simulation.code_unit.decl 9000007 in 0 initial hierarchy "test.boundaries.nested_caller.9000007"
    simulation.code_unit.decl 9000008 in 0 function hierarchy "test.boundaries.nested_target.9000008"
    simulation.code_unit.decl 9000009 in 0 function hierarchy "test.boundaries.recursive_a.9000009"
    simulation.code_unit.decl 9000010 in 0 function hierarchy "test.boundaries.recursive_b.9000010"
    simulation.code_unit.decl 9000011 in 0 function hierarchy "test.boundaries.unbound.9000011"
    simulation.code_unit.decl 9000012 in 0 initial hierarchy "test.boundaries.unknown_worker.9000012"
    simulation.code_unit.decl 9000013 in 0 initial hierarchy "test.boundaries.worker.9000013"
    simulation.code_unit.decl 9000014 in 0 function hierarchy "test.boundaries.public_known.9000014"
    simulation.code_unit.decl 9000015 in 0 function hierarchy "test.boundaries.nested_known.9000015"
    simulation.code_unit.decl 9000016 in 0 function hierarchy "test.boundaries.noncall_known.9000016"
    simulation.code_unit.decl 9000017 in 0 task hierarchy "test.boundaries.task_sink.9000017"
    simulation.code_unit.decl 9000018 in 0 initial hierarchy "test.boundaries.task_caller.9000018"
    simulation.scope.decl 0

    simulation.func private @callee(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<8> {simulation.capture_kind = 1 : i32})
        -> !simulation.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %resized = simulation.logic.resize %value signed = false : !simulation.logic<8> -> !simulation.logic<8>
      simulation.return %resized : !simulation.logic<8>
    }

    simulation.func @caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64,
                    symbol_anchor = @noncall_known} {
      %bits = arith.constant 5 : i8
      %known = simulation.logic.from_bits %bits : i8 -> !simulation.logic<8>
      %called = simulation.call @callee(%ctx, %known) : (!simulation.context, !simulation.logic<8>) -> !simulation.logic<8>
      %looped = simulation.call @loop(%ctx, %known) : (!simulation.context, !simulation.logic<8>) -> !simulation.logic<8>
      %recursive = simulation.call @recursive_a(%ctx, %called) : (!simulation.context, !simulation.logic<8>) -> !simulation.logic<8>
      %public = simulation.call @public_known(%ctx, %known) : (!simulation.context, !simulation.logic<8>) -> !simulation.logic<8>
      %nested_visibility = simulation.call @nested_known(%ctx, %known) : (!simulation.context, !simulation.logic<8>) -> !simulation.logic<8>
      %noncall = simulation.call @noncall_known(%ctx, %known) : (!simulation.context, !simulation.logic<8>) -> !simulation.logic<8>
      simulation.return
    }

    simulation.func @public_known(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<8> {simulation.capture_kind = 1 : i32})
        -> !simulation.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 9000014 : i64} {
      %resized = simulation.logic.resize %value signed = false : !simulation.logic<8> -> !simulation.logic<8>
      simulation.return %resized : !simulation.logic<8>
    }

    simulation.func nested @nested_known(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<8> {simulation.capture_kind = 1 : i32})
        -> !simulation.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 9000015 : i64} {
      %resized = simulation.logic.resize %value signed = false : !simulation.logic<8> -> !simulation.logic<8>
      simulation.return %resized : !simulation.logic<8>
    }

    simulation.func private @noncall_known(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<8> {simulation.capture_kind = 1 : i32})
        -> !simulation.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 9000016 : i64} {
      %resized = simulation.logic.resize %value signed = false : !simulation.logic<8> -> !simulation.logic<8>
      simulation.return %resized : !simulation.logic<8>
    }

    simulation.func @external_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000003 : i64} {
      %bits = arith.constant 1 : i8
      %known = simulation.logic.from_bits %bits : i8 -> !simulation.logic<8>
      %external = simulation.call @external_fn(%ctx, %known) : (!simulation.context, !simulation.logic<8>) -> !simulation.logic<8>
      simulation.return
    }

    simulation.func private @external_fn(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<8> {simulation.capture_kind = 1 : i32})
        -> !simulation.logic<8> attributes {entry_kind = 8 : i32}

    simulation.func @joiner(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000004 : i64} {
      %condition = arith.constant true
      %known = simulation.logic.constant 3 : i8, 0 : i8 : !simulation.logic<8>
      %unknown = simulation.logic.constant 0 : i8, -1 : i8 : !simulation.logic<8>
      cf.cond_br %condition, ^join(%known : !simulation.logic<8>),
          ^join(%unknown : !simulation.logic<8>)
    ^join(%value: !simulation.logic<8>):
      simulation.return
    }

    simulation.func private @loop(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<8> {simulation.capture_kind = 1 : i32})
        -> !simulation.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 9000005 : i64} {
      cf.br ^header(%value : !simulation.logic<8>)
    ^header(%current: !simulation.logic<8>):
      %next = simulation.logic.unary bit_not %current : (!simulation.logic<8>) -> !simulation.logic<8>
      %done = arith.constant true
      cf.cond_br %done, ^exit(%current : !simulation.logic<8>), ^header(%next : !simulation.logic<8>)
    ^exit(%result: !simulation.logic<8>):
      simulation.return %result : !simulation.logic<8>
    }

    simulation.func @mixed_continuation(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000006 : i64} {
      %condition = arith.constant true
      %known = simulation.logic.constant 6 : i8, 0 : i8 : !simulation.logic<8>
      cf.cond_br %condition, ^suspend, ^ordinary
    ^suspend:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^join(%known : !simulation.logic<8>)
    ^ordinary:
      cf.br ^join(%known : !simulation.logic<8>)
    ^join(%value: !simulation.logic<8>):
      simulation.return
    }

    simulation.func @nested_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000007 : i64} {
      %bits = arith.constant 4 : i8
      %known = simulation.logic.from_bits %bits : i8 -> !simulation.logic<8>
      %direct = simulation.call @nested_target(%ctx, %known) : (!simulation.context, !simulation.logic<8>) -> !simulation.logic<8>
      %region_result = scf.execute_region -> !simulation.logic<8> {
        %unknown = simulation.logic.constant 0 : i8, -1 : i8 : !simulation.logic<8>
        %nested = simulation.call @nested_target(%ctx, %unknown) : (!simulation.context, !simulation.logic<8>) -> !simulation.logic<8>
        scf.yield %nested : !simulation.logic<8>
      }
      simulation.return
    }

    simulation.func private @nested_target(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<8> {simulation.capture_kind = 1 : i32})
        -> !simulation.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 9000008 : i64} {
      %resized = simulation.logic.resize %value signed = false : !simulation.logic<8> -> !simulation.logic<8>
      simulation.return %resized : !simulation.logic<8>
    }

    simulation.func private @recursive_a(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<8> {simulation.capture_kind = 1 : i32})
        -> !simulation.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 9000009 : i64} {
      %from_b = simulation.call @recursive_b(%ctx, %value) : (!simulation.context, !simulation.logic<8>) -> !simulation.logic<8>
      %zero = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      %known = simulation.logic.binary and %from_b, %zero : !simulation.logic<8>
      simulation.return %known : !simulation.logic<8>
    }

    simulation.func private @recursive_b(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<8> {simulation.capture_kind = 1 : i32})
        -> !simulation.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 9000010 : i64} {
      %from_a = simulation.call @recursive_a(%ctx, %value) : (!simulation.context, !simulation.logic<8>) -> !simulation.logic<8>
      simulation.return %from_a : !simulation.logic<8>
    }

    simulation.func private @task_sink(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<8> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 12 : i32, code_unit_id = 9000017 : i64} {
      %resized = simulation.logic.resize %value signed = false : !simulation.logic<8> -> !simulation.logic<8>
      simulation.return
    }

    simulation.func @task_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000018 : i64} {
      %bits = arith.constant 12 : i8
      %known = simulation.logic.from_bits %bits : i8 -> !simulation.logic<8>
      simulation.task.call @task_sink(%ctx, %known, %known) arguments 2 to ^done : !simulation.context, !simulation.logic<8>, !simulation.logic<8>
    ^done(%continued: !simulation.logic<8>):
      simulation.return
    }

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %bits = arith.constant 9 : i8
      %known = simulation.logic.from_bits %bits : i8 -> !simulation.logic<8>
      %unknown = simulation.logic.constant 0 : i8, -1 : i8 : !simulation.logic<8>
      %worker = simulation.spawn @worker(%ctx, %known) : !simulation.context, !simulation.logic<8> -> !simulation.process
      %unknown_process = simulation.spawn @unknown_worker(%ctx, %unknown) : !simulation.context, !simulation.logic<8> -> !simulation.process
      simulation.return
    }

    simulation.func @unbound(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<8> {simulation.capture_kind = 1 : i32})
        -> !simulation.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 9000011 : i64} {
      simulation.return %value : !simulation.logic<8>
    }

    simulation.func private @unknown_worker(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<8> {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000012 : i64} {
      simulation.return
    }

    simulation.func private @worker(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<8> {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000013 : i64} {
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^resume(%value : !simulation.logic<8>)
    ^resume(%continued: !simulation.logic<8>):
      simulation.return
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
// CHECK-NEXT:   bb0.op5.result0: may-four-state (call-result)
// CHECK-NEXT:   bb0.op6.result0: may-four-state (call-result)
// CHECK-NEXT:   bb0.op7.result0: may-four-state (call-result)
// CHECK-LABEL: func @external_caller
// CHECK-NEXT:   bb0.op1.result0: two-state (logic-from-bits)
// CHECK-NEXT:   bb0.op2.result0: may-four-state (external-declaration)
// CHECK-LABEL: func @external_fn
// CHECK-LABEL: func @joiner
// CHECK-NEXT:   bb0.op1.result0: two-state (logic-constant)
// CHECK-NEXT:   bb0.op2.result0: may-four-state (unknown-constant)
// CHECK-NEXT:   bb1.arg0: two-state (cfg-join)
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
// CHECK-NEXT:   bb1.op0.result0: two-state (infeasible-cfg)
// CHECK-NEXT:   bb1.op1.result0: two-state (infeasible-cfg)
// CHECK-LABEL: func @nested_known
// CHECK-NEXT:   bb0.arg1: may-four-state (function-entry)
// CHECK-NEXT:   bb0.op0.result0: may-four-state (logic-resize)
// CHECK-LABEL: func @nested_target
// CHECK-NEXT:   bb0.arg1: may-four-state (call-actual)
// CHECK-NEXT:   bb0.op0.result0: may-four-state (logic-resize)
// CHECK-LABEL: func @noncall_known
// CHECK-NEXT:   bb0.arg1: may-four-state (function-entry)
// CHECK-NEXT:   bb0.op0.result0: may-four-state (logic-resize)
// CHECK-LABEL: func @public_known
// CHECK-NEXT:   bb0.arg1: may-four-state (function-entry)
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
// CHECK-LABEL: func @task_caller
// CHECK-NEXT:   bb0.op1.result0: two-state (logic-from-bits)
// CHECK-NEXT:   bb1.arg0: two-state (continuation)
// CHECK-LABEL: func @task_sink
// CHECK-NEXT:   bb0.arg1: two-state (call-actual)
// CHECK-NEXT:   bb0.op0.result0: two-state (logic-resize)
// CHECK-LABEL: func @unbound
// CHECK-NEXT:   bb0.arg1: may-four-state (function-entry)
// CHECK-LABEL: func @unknown_worker
// CHECK-NEXT:   bb0.arg1: may-four-state (spawn-actual)
// CHECK-LABEL: func @worker
// CHECK-NEXT:   bb0.arg1: two-state (spawn-actual)
// CHECK-NEXT:   bb1.arg0: two-state (continuation)
