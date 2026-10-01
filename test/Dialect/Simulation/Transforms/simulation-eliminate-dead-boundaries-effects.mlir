// RUN: obelisk-opt %s --allow-unregistered-dialect --pass-pipeline='builtin.module(simulation.design(obelisk-sim-eliminate-dead-boundaries))' | FileCheck %s

module {
  simulation.design @effects {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "top.store"
    simulation.code_unit.decl 2 in 0 function hierarchy "top.drive"
    simulation.code_unit.decl 3 in 0 function hierarchy "top.nba"
    simulation.code_unit.decl 4 in 0 function hierarchy "top.event"
    simulation.code_unit.decl 5 in 0 function hierarchy "top.allocate"
    simulation.code_unit.decl 6 in 0 function hierarchy "top.io"
    simulation.code_unit.decl 7 in 0 function hierarchy "top.status"
    simulation.code_unit.decl 8 in 0 function hierarchy "top.unknown"
    simulation.code_unit.decl 9 in 0 function hierarchy "top.io_read"
    simulation.code_unit.decl 10 in 0 function hierarchy "top.heap"
    simulation.code_unit.decl 11 in 0 function hierarchy "top.rng"
    simulation.storage.decl 0 in 0 : i32 design
    simulation.net.decl 0 in 0 : i32 design
    simulation.driver.decl 0 in 0 drives 0 : i32 design

    simulation.func private @store(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %storage = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %value = arith.constant 1 : i32
      simulation.ref.store %value to %storage : i32, !simulation.ref<i32>
      simulation.return %value : i32
    }

    simulation.func private @drive(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<i32>
      %value = arith.constant 2 : i32
      simulation.driver.drive %driver = %value : !simulation.driver<i32>, i32
      simulation.return %value : i32
    }

    simulation.func private @nba(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %storage = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %value = arith.constant 3 : i32
      simulation.nba.enqueue %value to %storage : (i32, !simulation.ref<i32>) -> ()
      simulation.return %value : i32
    }

    simulation.func private @event(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %event = simulation.context.event %ctx[0] : !simulation.event
      simulation.event.trigger %event nonblocking = false
      %value = arith.constant 4 : i32
      simulation.return %value : i32
    }

    simulation.func private @allocate(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %value = arith.constant 5 : i32
      %storage = simulation.ref.alloc %value : i32 -> !simulation.ref<i32>
      simulation.return %value : i32
    }

    simulation.func private @io(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %fd = arith.constant 1 : i32
      %text = simulation.bytes.constant "effect"
      simulation.display %ctx to %fd(%text) newline = true radix = <decimal>
          flags = [0] : !simulation.bytes
      simulation.return %fd : i32
    }

    simulation.func private @status(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits
      simulation.status.check %status
      simulation.return %bits : i32
    }

    simulation.func private @unknown(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 8 : i64} {
      "mystery.effect"() : () -> ()
      %value = arith.constant 8 : i32
      simulation.return %value : i32
    }

    // A read is discardable only for simulation storage and nets. Even a
    // read-only IO operation keeps its containing call active.
    simulation.func private @io_read(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9 : i64} {
      %fd = arith.constant 0 : i32
      %eof = simulation.file.eof %ctx, %fd
          : (!simulation.context, i32) -> i32
      simulation.return %eof : i32
    }

    // Heap and RNG currently have no primitive simulation operations; they
    // enter zero-time functions through opaque/external calls. These wrappers
    // verify that such effects propagate conservatively through defined calls.
    simulation.func private @heap_external(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32}

    simulation.func private @rng_external(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32}

    simulation.func private @heap(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 10 : i64} {
      %value = simulation.call @heap_external(%ctx)
          : (!simulation.context) -> i32
      simulation.return %value : i32
    }

    simulation.func private @rng(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 11 : i64} {
      %value = simulation.call @rng_external(%ctx)
          : (!simulation.context) -> i32
      simulation.return %value : i32
    }

    simulation.func private @external(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32}

    // Every effectful call stays active even though its result is unused. The
    // defined callees lose that result; the external ABI remains pinned.
    // CHECK-LABEL: simulation.func @root(
    // CHECK: simulation.call @store(%arg0) : (!simulation.context) -> ()
    // CHECK: simulation.call @drive(%arg0) : (!simulation.context) -> ()
    // CHECK: simulation.call @nba(%arg0) : (!simulation.context) -> ()
    // CHECK: simulation.call @event(%arg0) : (!simulation.context) -> ()
    // CHECK: simulation.call @allocate(%arg0) : (!simulation.context) -> ()
    // CHECK: simulation.call @io(%arg0) : (!simulation.context) -> ()
    // CHECK: simulation.call @status(%arg0) : (!simulation.context) -> ()
    // CHECK: simulation.call @unknown(%arg0) : (!simulation.context) -> ()
    // CHECK: simulation.call @io_read(%arg0) : (!simulation.context) -> ()
    // CHECK: simulation.call @heap(%arg0) : (!simulation.context) -> ()
    // CHECK: simulation.call @rng(%arg0) : (!simulation.context) -> ()
    // CHECK: %{{.*}} = simulation.call @external(%arg0) : (!simulation.context) -> i32
    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %store = simulation.call @store(%ctx) : (!simulation.context) -> i32
      %drive = simulation.call @drive(%ctx) : (!simulation.context) -> i32
      %nba = simulation.call @nba(%ctx) : (!simulation.context) -> i32
      %event = simulation.call @event(%ctx) : (!simulation.context) -> i32
      %allocate = simulation.call @allocate(%ctx) : (!simulation.context) -> i32
      %io = simulation.call @io(%ctx) : (!simulation.context) -> i32
      %status = simulation.call @status(%ctx) : (!simulation.context) -> i32
      %unknown = simulation.call @unknown(%ctx) : (!simulation.context) -> i32
      %io_read = simulation.call @io_read(%ctx)
          : (!simulation.context) -> i32
      %heap = simulation.call @heap(%ctx) : (!simulation.context) -> i32
      %rng = simulation.call @rng(%ctx) : (!simulation.context) -> i32
      %external = simulation.call @external(%ctx) : (!simulation.context) -> i32
      simulation.return
    }
  }
}
