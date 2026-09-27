// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
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
  obelisk_sim.design @wait_order_runtime {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 9930000 in 0 root_initializer hierarchy "top.root"
    obelisk_sim.code_unit.decl 9930001 in 0 initial hierarchy "top.parent"
    obelisk_sim.code_unit.decl 9930002 in 0 fork hierarchy "top.ordered"
    obelisk_sim.code_unit.decl 9930003 in 0 fork hierarchy "top.out_of_order"
    obelisk_sim.code_unit.decl 9930004 in 0 fork hierarchy "top.persistent_first"
    obelisk_sim.code_unit.decl 9930005 in 0 fork hierarchy "top.persistent_later"
    obelisk_sim.code_unit.decl 9930006 in 0 fork hierarchy "top.aliased"
    obelisk_sim.code_unit.decl 9930007 in 0 fork hierarchy "top.single_persistent"
    obelisk_sim.code_unit.decl 9930008 in 0 fork hierarchy "top.nonblocking_ordered"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9930000 : i64} {
      %parent = obelisk_sim.spawn @parent(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @parent(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9930001 : i64} {
      %ordered_a = obelisk_sim.event.create
      %ordered_b = obelisk_sim.event.create
      %ordered = obelisk_sim.spawn @ordered(%ctx, %ordered_a, %ordered_b) :
          !obelisk_sim.context, !obelisk_sim.event, !obelisk_sim.event ->
          !obelisk_sim.process
      obelisk_sim.event.trigger %ordered_a nonblocking = false
      obelisk_sim.event.trigger %ordered_a nonblocking = false
      obelisk_sim.event.trigger %ordered_b nonblocking = false

      %wrong_a = obelisk_sim.event.create
      %wrong_b = obelisk_sim.event.create
      %wrong = obelisk_sim.spawn @out_of_order(%ctx, %wrong_a, %wrong_b) :
          !obelisk_sim.context, !obelisk_sim.event, !obelisk_sim.event ->
          !obelisk_sim.process
      obelisk_sim.event.trigger %wrong_b nonblocking = false

      %persistent_a = obelisk_sim.event.create
      %persistent_b = obelisk_sim.event.create
      obelisk_sim.event.trigger %persistent_a nonblocking = false
      %persistent_first = obelisk_sim.spawn @persistent_first(
          %ctx, %persistent_a, %persistent_b) : !obelisk_sim.context,
          !obelisk_sim.event, !obelisk_sim.event -> !obelisk_sim.process
      obelisk_sim.event.trigger %persistent_b nonblocking = false

      %late_a = obelisk_sim.event.create
      %late_b = obelisk_sim.event.create
      obelisk_sim.event.trigger %late_b nonblocking = false
      %persistent_later = obelisk_sim.spawn @persistent_later(
          %ctx, %late_a, %late_b) : !obelisk_sim.context,
          !obelisk_sim.event, !obelisk_sim.event -> !obelisk_sim.process

      %alias = obelisk_sim.event.create
      %aliased = obelisk_sim.spawn @aliased(%ctx, %alias, %alias) :
          !obelisk_sim.context, !obelisk_sim.event, !obelisk_sim.event ->
          !obelisk_sim.process
      obelisk_sim.event.trigger %alias nonblocking = false

      %single_event = obelisk_sim.event.create
      obelisk_sim.event.trigger %single_event nonblocking = false
      %single = obelisk_sim.spawn @single_persistent(%ctx, %single_event) :
          !obelisk_sim.context, !obelisk_sim.event -> !obelisk_sim.process

      %nba_a = obelisk_sim.event.create
      %nba_b = obelisk_sim.event.create
      %nba = obelisk_sim.spawn @nonblocking_ordered(%ctx, %nba_a, %nba_b) :
          !obelisk_sim.context, !obelisk_sim.event, !obelisk_sim.event ->
          !obelisk_sim.process
      obelisk_sim.event.trigger %nba_a nonblocking = true
      obelisk_sim.event.trigger %nba_b nonblocking = true

      obelisk_sim.suspend.join all %ordered, %wrong, %persistent_first,
          %persistent_later, %aliased, %single, %nba processes 7 to ^done :
          !obelisk_sim.process, !obelisk_sim.process, !obelisk_sim.process,
          !obelisk_sim.process, !obelisk_sim.process, !obelisk_sim.process,
          !obelisk_sim.process

    ^done:
      %zero = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %zero
      obelisk_sim.return
    }

    obelisk_sim.func private @ordered(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %a: !obelisk_sim.event {obelisk_sim.capture_kind = 1 : i32},
        %b: !obelisk_sim.event {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 9930002 : i64,
                    internal, schedule.detached_controls,
                    schedule.prime_on_spawn} {
      %carried = arith.constant 73 : i32
      obelisk_sim.suspend.event_order %a, %b, %carried events 2 to ^resumed :
          !obelisk_sim.event, !obelisk_sim.event, i32
    ^resumed(%restored: i32):
      %failed = obelisk_sim.wait_order.failed
      cf.cond_br %failed, ^bad, ^verify_carry
    ^verify_carry:
      %expected = arith.constant 73 : i32
      %carry_ok = arith.cmpi eq, %restored, %expected : i32
      cf.cond_br %carry_ok, ^good, ^bad
    ^good:
      %good_message = obelisk_sim.bytes.constant "ordered success: PASS"
      %good_stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %good_stdout(%good_message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    ^bad:
      %bad_message = obelisk_sim.bytes.constant "ordered success: FAIL"
      %bad_stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %bad_stdout(%bad_message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }

    obelisk_sim.func private @out_of_order(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %a: !obelisk_sim.event {obelisk_sim.capture_kind = 1 : i32},
        %b: !obelisk_sim.event {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 9930003 : i64,
                    internal, schedule.detached_controls,
                    schedule.prime_on_spawn} {
      obelisk_sim.suspend.event_order %a, %b events 2 to ^resumed :
          !obelisk_sim.event, !obelisk_sim.event
    ^resumed:
      %failed = obelisk_sim.wait_order.failed
      cf.cond_br %failed, ^good, ^bad
    ^good:
      %good_message = obelisk_sim.bytes.constant "out of order: PASS"
      %good_stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %good_stdout(%good_message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    ^bad:
      %bad_message = obelisk_sim.bytes.constant "out of order: FAIL"
      %bad_stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %bad_stdout(%bad_message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }

    obelisk_sim.func private @persistent_first(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %a: !obelisk_sim.event {obelisk_sim.capture_kind = 1 : i32},
        %b: !obelisk_sim.event {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 9930004 : i64,
                    internal, schedule.detached_controls,
                    schedule.prime_on_spawn} {
      obelisk_sim.suspend.event_order %a, %b events 2 to ^resumed :
          !obelisk_sim.event, !obelisk_sim.event
    ^resumed:
      %failed = obelisk_sim.wait_order.failed
      cf.cond_br %failed, ^bad, ^good
    ^good:
      %good_message = obelisk_sim.bytes.constant "persistent first: PASS"
      %good_stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %good_stdout(%good_message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    ^bad:
      %bad_message = obelisk_sim.bytes.constant "persistent first: FAIL"
      %bad_stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %bad_stdout(%bad_message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }

    obelisk_sim.func private @persistent_later(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %a: !obelisk_sim.event {obelisk_sim.capture_kind = 1 : i32},
        %b: !obelisk_sim.event {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 9930005 : i64,
                    internal, schedule.detached_controls,
                    schedule.prime_on_spawn} {
      obelisk_sim.suspend.event_order %a, %b events 2 to ^resumed :
          !obelisk_sim.event, !obelisk_sim.event
    ^resumed:
      %failed = obelisk_sim.wait_order.failed
      cf.cond_br %failed, ^good, ^bad
    ^good:
      %good_message = obelisk_sim.bytes.constant "persistent later: PASS"
      %good_stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %good_stdout(%good_message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    ^bad:
      %bad_message = obelisk_sim.bytes.constant "persistent later: FAIL"
      %bad_stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %bad_stdout(%bad_message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }

    obelisk_sim.func private @aliased(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %a: !obelisk_sim.event {obelisk_sim.capture_kind = 1 : i32},
        %b: !obelisk_sim.event {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 9930006 : i64,
                    internal, schedule.detached_controls,
                    schedule.prime_on_spawn} {
      obelisk_sim.suspend.event_order %a, %b events 2 to ^resumed :
          !obelisk_sim.event, !obelisk_sim.event
    ^resumed:
      %failed = obelisk_sim.wait_order.failed
      cf.cond_br %failed, ^good, ^bad
    ^good:
      %good_message = obelisk_sim.bytes.constant "aliased entries: PASS"
      %good_stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %good_stdout(%good_message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    ^bad:
      %bad_message = obelisk_sim.bytes.constant "aliased entries: FAIL"
      %bad_stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %bad_stdout(%bad_message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }

    obelisk_sim.func private @single_persistent(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %event: !obelisk_sim.event {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 9930007 : i64,
                    internal, schedule.detached_controls,
                    schedule.prime_on_spawn} {
      obelisk_sim.suspend.event_order %event events 1 to ^resumed :
          !obelisk_sim.event
    ^resumed:
      %failed = obelisk_sim.wait_order.failed
      cf.cond_br %failed, ^bad, ^good
    ^good:
      %message = obelisk_sim.bytes.constant "single persistent: PASS"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    ^bad:
      %message_bad = obelisk_sim.bytes.constant "single persistent: FAIL"
      %stdout_bad = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout_bad(%message_bad)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }

    obelisk_sim.func private @nonblocking_ordered(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %a: !obelisk_sim.event {obelisk_sim.capture_kind = 1 : i32},
        %b: !obelisk_sim.event {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 9930008 : i64,
                    internal, schedule.detached_controls,
                    schedule.prime_on_spawn} {
      obelisk_sim.suspend.event_order %a, %b events 2 to ^resumed :
          !obelisk_sim.event, !obelisk_sim.event
    ^resumed:
      %failed = obelisk_sim.wait_order.failed
      cf.cond_br %failed, ^bad, ^good
    ^good:
      %message = obelisk_sim.bytes.constant "nonblocking ordered: PASS"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    ^bad:
      %message_bad = obelisk_sim.bytes.constant "nonblocking ordered: FAIL"
      %stdout_bad = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout_bad(%message_bad)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }
  }
}
