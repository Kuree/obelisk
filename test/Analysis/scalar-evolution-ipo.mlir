// RUN: talos-opt %s --test-talos-scoped-integer-ranges -o %t.threaded 2>&1 | FileCheck %s
// RUN: talos-opt %s --mlir-disable-threading --test-talos-scoped-integer-ranges -o %t.serial 2>&1 | FileCheck %s
// RUN: diff %t.threaded %t.serial
// RUN: talos-opt %s -o %t.original
// RUN: diff %t.original %t.threaded

!array = !simulation.unpacked_array<0 : 15 x i8>
module {
  simulation.design @integer_proofs {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !array design
    simulation.code_unit.decl 1 in 0 function hierarchy "caller"
    simulation.func @caller(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %seed: i8 {simulation.capture_kind = 2 : i32}, %memory: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64, test.escape = @escaped_index} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %three = arith.constant 3 : i8
      %four = arith.constant 4 : i8
      %scaled = arith.muli %seed, %four : i8
      %plus = arith.addi %scaled, %three : i8
      %difference = arith.subi %plus, %scaled : i8
      %a = simulation.ref.array_element %root[%difference] {test.scoped = "correlated"} : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %low = arith.andi %seed, %three : i8
      simulation.call @private_index(%ctx, %low) : (!simulation.context, i8) -> ()
      %high = arith.addi %low, %four : i8
      simulation.call @private_index(%ctx, %high) : (!simulation.context, i8) -> ()
      simulation.call @public_index(%ctx, %low) : (!simulation.context, i8) -> ()
      simulation.call @escaped_index(%ctx, %low) : (!simulation.context, i8) -> ()
      %observed = simulation.call @runtime_index(%ctx, %low) : (!simulation.context, i8) -> i8
      %returned = simulation.call @affine(%ctx, %seed) : (!simulation.context, i8) -> i8
      %constant = arith.subi %returned, %scaled : i8
      %b = simulation.ref.array_element %root[%constant] {test.scoped = "ipo-correlated"} : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %first, %second = simulation.call @two_results(%ctx, %seed) : (!simulation.context, i8) -> (i8, i8)
      %multi = arith.subi %first, %scaled : i8
      %c = simulation.ref.array_element %root[%multi] {test.scoped = "ipo-multiple-results"} : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %known = simulation.call @memory_result(%ctx, %memory) : (!simulation.context, !simulation.ref<i8>) -> !simulation.logic<8>
      %cancel = simulation.logic.binary sub %known, %known : !simulation.logic<8>
      %d = simulation.ref.array_element %root[%cancel] {test.scoped = "ipo-opaque-known-result"} : (!simulation.ref<!array>, !simulation.logic<8>) -> !simulation.ref<i8>
      simulation.return
    }
    // CHECK: correlated umin=3 umax=3
    // CHECK: ipo-correlated umin=3 umax=3
    // CHECK: ipo-multiple-results umin=3 umax=3
    // CHECK: ipo-opaque-known-result umin=0 umax=0

    simulation.code_unit.decl 2 in 0 function hierarchy "private_index"
    simulation.func private @private_index(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %index: i8 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %a = simulation.ref.array_element %root[%index] {test.scoped = "ipo-arguments"} : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      simulation.return
    }
    // CHECK: ipo-arguments umin=0 umax=7

    simulation.code_unit.decl 3 in 0 function hierarchy "public_index"
    simulation.func @public_index(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %index: i8 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %a = simulation.ref.array_element %root[%index] {test.scoped = "public-entry"} : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      simulation.return
    }
    // CHECK: public-entry umin=0 umax=255

    simulation.code_unit.decl 4 in 0 function hierarchy "affine"
    simulation.func private @affine(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %index: i8 {simulation.capture_kind = 2 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %four = arith.constant 4 : i8
      %three = arith.constant 3 : i8
      %scaled = arith.muli %index, %four : i8
      %result = arith.addi %scaled, %three : i8
      simulation.return %result : i8
    }

    simulation.code_unit.decl 5 in 0 function hierarchy "recurrences"
    simulation.func @recurrences(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %zero = arith.constant 0 : i8
      %one = arith.constant 1 : i8
      %two = arith.constant 2 : i8
      %four = arith.constant 4 : i8
      cf.br ^head(%zero, %one : i8, i8)
    ^head(%i: i8, %j: i8):
      %more = arith.cmpi ult, %i, %four : i8
      cf.cond_br %more, ^body, ^exit
    ^body:
      %a = simulation.ref.array_element %root[%j] {test.scoped = "secondary-recurrence"} : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %twice = arith.muli %i, %two : i8
      %difference = arith.subi %j, %twice : i8
      %b = simulation.ref.array_element %root[%difference] {test.scoped = "recurrence-relation"} : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %nextI = arith.addi %i, %one : i8
      %nextJ = arith.addi %j, %two : i8
      cf.br ^head(%nextI, %nextJ : i8, i8)
    ^exit:
      simulation.return
    }
    // CHECK: secondary-recurrence umin=1 umax=9
    // CHECK: recurrence-relation umin=1 umax=1

    simulation.code_unit.decl 6 in 0 function hierarchy "path_ranges"
    simulation.func @path_ranges(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %seed: i8 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %mask = arith.constant 15 : i8
      %eight = arith.constant 8 : i8
      %index = arith.andi %seed, %mask : i8
      %small = arith.cmpi ult, %index, %eight : i8
      cf.cond_br %small, ^small, ^large
    ^small:
      %a = simulation.ref.array_element %root[%index] {test.scoped = "true-edge"} : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      simulation.return
    ^large:
      %b = simulation.ref.array_element %root[%index] {test.scoped = "false-edge"} : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      simulation.return
    }
    // CHECK: true-edge umin=0 umax=7
    // CHECK: false-edge umin=8 umax=15

    simulation.code_unit.decl 7 in 0 function hierarchy "four_state"
    simulation.func @four_state(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %unknown: !simulation.logic<8> {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %difference = simulation.logic.binary sub %unknown, %unknown : !simulation.logic<8>
      %a = simulation.ref.array_element %root[%difference] {test.scoped = "xz-cancellation"} : (!simulation.ref<!array>, !simulation.logic<8>) -> !simulation.ref<i8>
      simulation.return
    }
    // CHECK: xz-cancellation unknown

    simulation.code_unit.decl 8 in 0 function hierarchy "escaped_index"
    simulation.func private @escaped_index(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %index: i8 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 8 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %a = simulation.ref.array_element %root[%index] {test.scoped = "escaping-entry"} : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      simulation.return
    }
    // CHECK: escaping-entry umin=0 umax=255

    simulation.code_unit.decl 9 in 0 function hierarchy "logic_recurrences"
    simulation.func @logic_recurrences(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 9 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %zero = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      %two = simulation.logic.constant 2 : i8, 0 : i8 : !simulation.logic<8>
      %four = simulation.logic.constant 4 : i8, 0 : i8 : !simulation.logic<8>
      cf.br ^head(%zero, %one : !simulation.logic<8>, !simulation.logic<8>)
    ^head(%i: !simulation.logic<8>, %j: !simulation.logic<8>):
      %more = simulation.logic.compare ult %i, %four : (!simulation.logic<8>, !simulation.logic<8>) -> !simulation.logic<1>
      %truth = simulation.logic.is_true %more : !simulation.logic<1>
      cf.cond_br %truth, ^body, ^exit
    ^body:
      %a = simulation.ref.array_element %root[%j] {test.scoped = "logic-secondary-recurrence"} : (!simulation.ref<!array>, !simulation.logic<8>) -> !simulation.ref<i8>
      %nextI = simulation.logic.binary add %i, %one : !simulation.logic<8>
      %nextJ = simulation.logic.binary add %j, %two : !simulation.logic<8>
      cf.br ^head(%nextI, %nextJ : !simulation.logic<8>, !simulation.logic<8>)
    ^exit:
      simulation.return
    }
    // CHECK: logic-secondary-recurrence umin=1 umax=9

    // Runtime entries can be invoked without a simulation.call, even when
    // their symbols are private. Direct callers cannot constrain captures.
    simulation.code_unit.decl 10 in 0 observer hierarchy "runtime_index"
    simulation.func private @runtime_index(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %index: i8 {simulation.capture_kind = 2 : i32}) -> i8 attributes {entry_kind = 14 : i32, code_unit_id = 10 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %a = simulation.ref.array_element %root[%index] {test.scoped = "runtime-entry"} : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      simulation.return %index : i8
    }
    // CHECK: runtime-entry umin=0 umax=255

    simulation.code_unit.decl 11 in 0 function hierarchy "two_results"
    simulation.func private @two_results(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %seed: i8 {simulation.capture_kind = 2 : i32}) -> (i8, i8) attributes {entry_kind = 8 : i32, code_unit_id = 11 : i64} {
      %four = arith.constant 4 : i8
      %three = arith.constant 3 : i8
      %scaled = arith.muli %seed, %four : i8
      %sum = arith.addi %scaled, %three : i8
      simulation.return %sum, %seed : i8, i8
    }

    simulation.code_unit.decl 12 in 0 function hierarchy "memory_result"
    simulation.func private @memory_result(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %memory: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}) -> !simulation.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 12 : i64} {
      %read = simulation.ref.load %memory : !simulation.ref<i8> -> i8
      %known = simulation.logic.from_bits %read : i8 -> !simulation.logic<8>
      simulation.return %known : !simulation.logic<8>
    }
  }
}
