// RUN: obelisk-opt %s --canonicalize | FileCheck %s

module {
  obelisk_sim.design @folds {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design

    // to_bits(from_bits(x)) is the identity; the reverse is not, because
    // from_bits cannot carry an unknown plane.
    // CHECK-LABEL: obelisk_sim.func @round_trip
    // CHECK-NEXT: obelisk_sim.return %arg1
    obelisk_sim.func @round_trip(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %bits: i8 {obelisk_sim.capture_kind = 2 : i32}) -> i8 attributes {entry_kind = 8 : i32} {
      %logic = obelisk_sim.logic.from_bits %bits : i8 -> !obelisk_sim.logic<8>
      %back = obelisk_sim.logic.to_bits %logic : !obelisk_sim.logic<8> -> i8
      obelisk_sim.return %back : i8
    }

    // A resize to the same width is a no-op regardless of signedness.
    // CHECK-LABEL: obelisk_sim.func @same_width_resize
    // CHECK-NOT: obelisk_sim.logic.resize
    obelisk_sim.func @same_width_resize(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %value: !obelisk_sim.logic<8> {obelisk_sim.capture_kind = 2 : i32}) -> !obelisk_sim.logic<8> attributes {entry_kind = 8 : i32} {
      %resized = obelisk_sim.logic.resize %value signed = true : !obelisk_sim.logic<8> -> !obelisk_sim.logic<8>
      obelisk_sim.return %resized : !obelisk_sim.logic<8>
    }

    // Constant time folds and rematerializes through the dialect constant
    // materializer; adding zero disappears entirely.
    // CHECK-LABEL: obelisk_sim.func @time_math
    // CHECK: %[[T:.*]] = obelisk_sim.time.constant 9
    // CHECK: obelisk_sim.suspend.delay %[[T]]
    obelisk_sim.func @time_math(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      %four = obelisk_sim.time.constant 4
      %five = obelisk_sim.time.constant 5
      %zero = obelisk_sim.time.constant 0
      %sum = obelisk_sim.time.add %four, %five
      %same = obelisk_sim.time.add %sum, %zero
      obelisk_sim.suspend.delay %same to ^next
    ^next:
      obelisk_sim.return
    }

    // Identical four-state constants are common subexpressions.
    // CHECK-LABEL: obelisk_sim.func @constants
    // CHECK: obelisk_sim.logic.constant
    // CHECK-NOT: obelisk_sim.logic.constant
    obelisk_sim.func @constants(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32} {
      %a = obelisk_sim.logic.constant 3 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %b = obelisk_sim.logic.constant 3 : i8, 0 : i8 : !obelisk_sim.logic<8>
      obelisk_sim.ref.store %a to %ref : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.ref.store %b to %ref : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.return
    }
  }
}
