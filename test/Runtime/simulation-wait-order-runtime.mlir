// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s --implicit-check-not=FAIL
// RUN: %t.exe --execution-tier=native | FileCheck %s --implicit-check-not=FAIL

// IEEE 1800-2017 15.5.4 requires occurrence order to be retained even when
// the publisher triggers several events before the waiter is rescheduled.
// It also permits a repeated preceding event, permits persistent state only
// for the first event, and treats an aliased later event as already triggered.
// CHECK-DAG: ordered success: PASS
// CHECK-DAG: out of order: PASS
// CHECK-DAG: persistent first: PASS
// CHECK-DAG: persistent later: PASS
// CHECK-DAG: aliased entries: PASS
// CHECK-DAG: single persistent: PASS
// CHECK-DAG: nonblocking ordered: PASS

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @wait_order_runtime {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 9930000 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 9930001 in 0 initial hierarchy "top.parent"
    simulation.code_unit.decl 9930002 in 0 fork hierarchy "top.ordered"
    simulation.code_unit.decl 9930003 in 0 fork hierarchy "top.out_of_order"
    simulation.code_unit.decl 9930004 in 0 fork hierarchy "top.persistent_first"
    simulation.code_unit.decl 9930005 in 0 fork hierarchy "top.persistent_later"
    simulation.code_unit.decl 9930006 in 0 fork hierarchy "top.aliased"
    simulation.code_unit.decl 9930007 in 0 fork hierarchy "top.single_persistent"
    simulation.code_unit.decl 9930008 in 0 fork hierarchy "top.nonblocking_ordered"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9930000 : i64} {
      %parent = simulation.spawn @parent(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func private @parent(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9930001 : i64} {
      %ordered_a = simulation.event.create
      %ordered_b = simulation.event.create
      %ordered = simulation.spawn @ordered(%ctx, %ordered_a, %ordered_b) :
          !simulation.context, !simulation.event, !simulation.event ->
          !simulation.process
      simulation.event.trigger %ordered_a nonblocking = false
      simulation.event.trigger %ordered_a nonblocking = false
      simulation.event.trigger %ordered_b nonblocking = false

      %wrong_a = simulation.event.create
      %wrong_b = simulation.event.create
      %wrong = simulation.spawn @out_of_order(%ctx, %wrong_a, %wrong_b) :
          !simulation.context, !simulation.event, !simulation.event ->
          !simulation.process
      simulation.event.trigger %wrong_b nonblocking = false

      %persistent_a = simulation.event.create
      %persistent_b = simulation.event.create
      simulation.event.trigger %persistent_a nonblocking = false
      %persistent_first = simulation.spawn @persistent_first(
          %ctx, %persistent_a, %persistent_b) : !simulation.context,
          !simulation.event, !simulation.event -> !simulation.process
      simulation.event.trigger %persistent_b nonblocking = false

      %late_a = simulation.event.create
      %late_b = simulation.event.create
      simulation.event.trigger %late_b nonblocking = false
      %persistent_later = simulation.spawn @persistent_later(
          %ctx, %late_a, %late_b) : !simulation.context,
          !simulation.event, !simulation.event -> !simulation.process

      %alias = simulation.event.create
      %aliased = simulation.spawn @aliased(%ctx, %alias, %alias) :
          !simulation.context, !simulation.event, !simulation.event ->
          !simulation.process
      simulation.event.trigger %alias nonblocking = false

      %single_event = simulation.event.create
      simulation.event.trigger %single_event nonblocking = false
      %single = simulation.spawn @single_persistent(%ctx, %single_event) :
          !simulation.context, !simulation.event -> !simulation.process

      %nba_a = simulation.event.create
      %nba_b = simulation.event.create
      %nba = simulation.spawn @nonblocking_ordered(%ctx, %nba_a, %nba_b) :
          !simulation.context, !simulation.event, !simulation.event ->
          !simulation.process
      simulation.event.trigger %nba_a nonblocking = true
      simulation.event.trigger %nba_b nonblocking = true

      simulation.suspend.join all %ordered, %wrong, %persistent_first,
          %persistent_later, %aliased, %single, %nba processes 7 to ^done :
          !simulation.process, !simulation.process, !simulation.process,
          !simulation.process, !simulation.process, !simulation.process,
          !simulation.process

    ^done:
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }

    simulation.func private @ordered(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.event {simulation.capture_kind = 1 : i32},
        %b: !simulation.event {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 9930002 : i64,
                    internal, schedule.detached_controls,
                    schedule.prime_on_spawn} {
      %carried = arith.constant 73 : i32
      simulation.suspend.event_order %a, %b, %carried events 2 to ^resumed :
          !simulation.event, !simulation.event, i32
    ^resumed(%restored: i32):
      %failed = simulation.wait_order.failed
      cf.cond_br %failed, ^bad, ^verify_carry
    ^verify_carry:
      %expected = arith.constant 73 : i32
      %carry_ok = arith.cmpi eq, %restored, %expected : i32
      cf.cond_br %carry_ok, ^good, ^bad
    ^good:
      %good_message = simulation.bytes.constant "ordered success: PASS"
      %good_stdout = arith.constant 1 : i32
      simulation.display %ctx to %good_stdout(%good_message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    ^bad:
      %bad_message = simulation.bytes.constant "ordered success: FAIL"
      %bad_stdout = arith.constant 1 : i32
      simulation.display %ctx to %bad_stdout(%bad_message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    }

    simulation.func private @out_of_order(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.event {simulation.capture_kind = 1 : i32},
        %b: !simulation.event {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 9930003 : i64,
                    internal, schedule.detached_controls,
                    schedule.prime_on_spawn} {
      simulation.suspend.event_order %a, %b events 2 to ^resumed :
          !simulation.event, !simulation.event
    ^resumed:
      %failed = simulation.wait_order.failed
      cf.cond_br %failed, ^good, ^bad
    ^good:
      %good_message = simulation.bytes.constant "out of order: PASS"
      %good_stdout = arith.constant 1 : i32
      simulation.display %ctx to %good_stdout(%good_message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    ^bad:
      %bad_message = simulation.bytes.constant "out of order: FAIL"
      %bad_stdout = arith.constant 1 : i32
      simulation.display %ctx to %bad_stdout(%bad_message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    }

    simulation.func private @persistent_first(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.event {simulation.capture_kind = 1 : i32},
        %b: !simulation.event {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 9930004 : i64,
                    internal, schedule.detached_controls,
                    schedule.prime_on_spawn} {
      simulation.suspend.event_order %a, %b events 2 to ^resumed :
          !simulation.event, !simulation.event
    ^resumed:
      %failed = simulation.wait_order.failed
      cf.cond_br %failed, ^bad, ^good
    ^good:
      %good_message = simulation.bytes.constant "persistent first: PASS"
      %good_stdout = arith.constant 1 : i32
      simulation.display %ctx to %good_stdout(%good_message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    ^bad:
      %bad_message = simulation.bytes.constant "persistent first: FAIL"
      %bad_stdout = arith.constant 1 : i32
      simulation.display %ctx to %bad_stdout(%bad_message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    }

    simulation.func private @persistent_later(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.event {simulation.capture_kind = 1 : i32},
        %b: !simulation.event {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 9930005 : i64,
                    internal, schedule.detached_controls,
                    schedule.prime_on_spawn} {
      simulation.suspend.event_order %a, %b events 2 to ^resumed :
          !simulation.event, !simulation.event
    ^resumed:
      %failed = simulation.wait_order.failed
      cf.cond_br %failed, ^good, ^bad
    ^good:
      %good_message = simulation.bytes.constant "persistent later: PASS"
      %good_stdout = arith.constant 1 : i32
      simulation.display %ctx to %good_stdout(%good_message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    ^bad:
      %bad_message = simulation.bytes.constant "persistent later: FAIL"
      %bad_stdout = arith.constant 1 : i32
      simulation.display %ctx to %bad_stdout(%bad_message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    }

    simulation.func private @aliased(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.event {simulation.capture_kind = 1 : i32},
        %b: !simulation.event {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 9930006 : i64,
                    internal, schedule.detached_controls,
                    schedule.prime_on_spawn} {
      simulation.suspend.event_order %a, %b events 2 to ^resumed :
          !simulation.event, !simulation.event
    ^resumed:
      %failed = simulation.wait_order.failed
      cf.cond_br %failed, ^good, ^bad
    ^good:
      %good_message = simulation.bytes.constant "aliased entries: PASS"
      %good_stdout = arith.constant 1 : i32
      simulation.display %ctx to %good_stdout(%good_message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    ^bad:
      %bad_message = simulation.bytes.constant "aliased entries: FAIL"
      %bad_stdout = arith.constant 1 : i32
      simulation.display %ctx to %bad_stdout(%bad_message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    }

    simulation.func private @single_persistent(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %event: !simulation.event {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 9930007 : i64,
                    internal, schedule.detached_controls,
                    schedule.prime_on_spawn} {
      simulation.suspend.event_order %event events 1 to ^resumed :
          !simulation.event
    ^resumed:
      %failed = simulation.wait_order.failed
      cf.cond_br %failed, ^bad, ^good
    ^good:
      %message = simulation.bytes.constant "single persistent: PASS"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    ^bad:
      %message_bad = simulation.bytes.constant "single persistent: FAIL"
      %stdout_bad = arith.constant 1 : i32
      simulation.display %ctx to %stdout_bad(%message_bad)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    }

    simulation.func private @nonblocking_ordered(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.event {simulation.capture_kind = 1 : i32},
        %b: !simulation.event {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 9930008 : i64,
                    internal, schedule.detached_controls,
                    schedule.prime_on_spawn} {
      simulation.suspend.event_order %a, %b events 2 to ^resumed :
          !simulation.event, !simulation.event
    ^resumed:
      %failed = simulation.wait_order.failed
      cf.cond_br %failed, ^bad, ^good
    ^good:
      %message = simulation.bytes.constant "nonblocking ordered: PASS"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    ^bad:
      %message_bad = simulation.bytes.constant "nonblocking ordered: FAIL"
      %stdout_bad = arith.constant 1 : i32
      simulation.display %ctx to %stdout_bad(%message_bad)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    }
  }
}
