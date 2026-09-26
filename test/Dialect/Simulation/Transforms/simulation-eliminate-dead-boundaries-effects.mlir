// RUN: obelisk-opt %s --allow-unregistered-dialect --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-eliminate-dead-boundaries))' | FileCheck %s

module {
  obelisk_sim.design @effects {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "top.store"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "top.drive"
    obelisk_sim.code_unit.decl 3 in 0 function hierarchy "top.nba"
    obelisk_sim.code_unit.decl 4 in 0 function hierarchy "top.event"
    obelisk_sim.code_unit.decl 5 in 0 function hierarchy "top.allocate"
    obelisk_sim.code_unit.decl 6 in 0 function hierarchy "top.io"
    obelisk_sim.code_unit.decl 7 in 0 function hierarchy "top.status"
    obelisk_sim.code_unit.decl 8 in 0 function hierarchy "top.unknown"
    obelisk_sim.code_unit.decl 9 in 0 function hierarchy "top.io_read"
    obelisk_sim.code_unit.decl 10 in 0 function hierarchy "top.heap"
    obelisk_sim.code_unit.decl 11 in 0 function hierarchy "top.rng"
    obelisk_sim.storage.decl 0 in 0 : i32 design
    obelisk_sim.net.decl 0 in 0 : i32 design
    obelisk_sim.driver.decl 0 in 0 drives 0 : i32 design

    obelisk_sim.func private @store(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %storage = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i32>
      %value = arith.constant 1 : i32
      obelisk_sim.ref.store %value to %storage : i32, !obelisk_sim.ref<i32>
      obelisk_sim.return %value : i32
    }

    obelisk_sim.func private @drive(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %driver = obelisk_sim.context.driver %ctx[0] : !obelisk_sim.driver<i32>
      %value = arith.constant 2 : i32
      obelisk_sim.driver.drive %driver = %value : !obelisk_sim.driver<i32>, i32
      obelisk_sim.return %value : i32
    }

    obelisk_sim.func private @nba(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %storage = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i32>
      %value = arith.constant 3 : i32
      obelisk_sim.nba.enqueue %value to %storage : (i32, !obelisk_sim.ref<i32>) -> ()
      obelisk_sim.return %value : i32
    }

    obelisk_sim.func private @event(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %event = obelisk_sim.context.event %ctx[0] : !obelisk_sim.event
      obelisk_sim.event.trigger %event nonblocking = false
      %value = arith.constant 4 : i32
      obelisk_sim.return %value : i32
    }

    obelisk_sim.func private @allocate(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %value = arith.constant 5 : i32
      %storage = obelisk_sim.ref.alloc %value : i32 -> !obelisk_sim.ref<i32>
      obelisk_sim.return %value : i32
    }

    obelisk_sim.func private @io(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %fd = arith.constant 1 : i32
      %text = obelisk_sim.bytes.constant "effect"
      obelisk_sim.display %ctx to %fd(%text) newline = true radix = 10
          flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return %fd : i32
    }

    obelisk_sim.func private @status(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits : (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return %bits : i32
    }

    obelisk_sim.func private @unknown(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 8 : i64} {
      "mystery.effect"() : () -> ()
      %value = arith.constant 8 : i32
      obelisk_sim.return %value : i32
    }

    // A read is discardable only for simulation storage and nets. Even a
    // read-only IO operation keeps its containing call active.
    obelisk_sim.func private @io_read(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9 : i64} {
      %fd = arith.constant 0 : i32
      %eof = obelisk_sim.file.eof %ctx, %fd
          : (!obelisk_sim.context, i32) -> i32
      obelisk_sim.return %eof : i32
    }

    // Heap and RNG currently have no primitive simulation operations; they
    // enter zero-time functions through opaque/external calls. These wrappers
    // verify that such effects propagate conservatively through defined calls.
    obelisk_sim.func private @heap_external(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32}

    obelisk_sim.func private @rng_external(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32}

    obelisk_sim.func private @heap(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 10 : i64} {
      %value = obelisk_sim.call @heap_external(%ctx)
          : (!obelisk_sim.context) -> i32
      obelisk_sim.return %value : i32
    }

    obelisk_sim.func private @rng(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 11 : i64} {
      %value = obelisk_sim.call @rng_external(%ctx)
          : (!obelisk_sim.context) -> i32
      obelisk_sim.return %value : i32
    }

    obelisk_sim.func private @external(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32}

    // Every effectful call stays active even though its result is unused. The
    // defined callees lose that result; the external ABI remains pinned.
    // CHECK-LABEL: obelisk_sim.func @root(
    // CHECK: obelisk_sim.call @store(%arg0) : (!obelisk_sim.context) -> ()
    // CHECK: obelisk_sim.call @drive(%arg0) : (!obelisk_sim.context) -> ()
    // CHECK: obelisk_sim.call @nba(%arg0) : (!obelisk_sim.context) -> ()
    // CHECK: obelisk_sim.call @event(%arg0) : (!obelisk_sim.context) -> ()
    // CHECK: obelisk_sim.call @allocate(%arg0) : (!obelisk_sim.context) -> ()
    // CHECK: obelisk_sim.call @io(%arg0) : (!obelisk_sim.context) -> ()
    // CHECK: obelisk_sim.call @status(%arg0) : (!obelisk_sim.context) -> ()
    // CHECK: obelisk_sim.call @unknown(%arg0) : (!obelisk_sim.context) -> ()
    // CHECK: obelisk_sim.call @io_read(%arg0) : (!obelisk_sim.context) -> ()
    // CHECK: obelisk_sim.call @heap(%arg0) : (!obelisk_sim.context) -> ()
    // CHECK: obelisk_sim.call @rng(%arg0) : (!obelisk_sim.context) -> ()
    // CHECK: %{{.*}} = obelisk_sim.call @external(%arg0) : (!obelisk_sim.context) -> i32
    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %store = obelisk_sim.call @store(%ctx) : (!obelisk_sim.context) -> i32
      %drive = obelisk_sim.call @drive(%ctx) : (!obelisk_sim.context) -> i32
      %nba = obelisk_sim.call @nba(%ctx) : (!obelisk_sim.context) -> i32
      %event = obelisk_sim.call @event(%ctx) : (!obelisk_sim.context) -> i32
      %allocate = obelisk_sim.call @allocate(%ctx) : (!obelisk_sim.context) -> i32
      %io = obelisk_sim.call @io(%ctx) : (!obelisk_sim.context) -> i32
      %status = obelisk_sim.call @status(%ctx) : (!obelisk_sim.context) -> i32
      %unknown = obelisk_sim.call @unknown(%ctx) : (!obelisk_sim.context) -> i32
      %io_read = obelisk_sim.call @io_read(%ctx)
          : (!obelisk_sim.context) -> i32
      %heap = obelisk_sim.call @heap(%ctx) : (!obelisk_sim.context) -> i32
      %rng = obelisk_sim.call @rng(%ctx) : (!obelisk_sim.context) -> i32
      %external = obelisk_sim.call @external(%ctx) : (!obelisk_sim.context) -> i32
      obelisk_sim.return
    }
  }
}
