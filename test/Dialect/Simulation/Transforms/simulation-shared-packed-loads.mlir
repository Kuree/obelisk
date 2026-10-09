// RUN: obelisk-opt %s --canonicalize | FileCheck %s

// Packed elements belong to one contiguous vector (LRM 7.4.1). A shared word
// load should remain a shared SSA value, with value extracts. Narrowing a sole
// extract is still useful; large aggregates can still use field addresses.
!word = !simulation.packed_array<7 : 0 x i8>
!wide = !simulation.packed_array<15 : 0 x i8>
!logic_word = !simulation.packed_array<7 : 0 x !simulation.logic<8>>
module {
  simulation.design @loads {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !word design
    simulation.storage.decl 1 in 0 : !wide design
    simulation.storage.decl 2 in 0 : !logic_word design
    simulation.code_unit.decl 1 in 0 function hierarchy "shared"
    simulation.code_unit.decl 2 in 0 function hierarchy "single"
    simulation.code_unit.decl 3 in 0 function hierarchy "wide"
    simulation.code_unit.decl 4 in 0 function hierarchy "four_state_snapshot"
    simulation.code_unit.decl 5 in 0 function hierarchy "single_snapshot"

    // CHECK-LABEL: simulation.func @shared(
    // CHECK-NOT: simulation.ref.subelement
    // CHECK: %[[WORD:.*]] = simulation.ref.load
    // CHECK-NOT: simulation.ref.load
    // CHECK: %[[A:.*]] = simulation.aggregate.extract %[[WORD]][0]
    // CHECK: %[[B:.*]] = simulation.aggregate.extract %[[WORD]][1]
    // CHECK-NOT: simulation.ref.
    // CHECK: simulation.return %[[A]], %[[B]]
    simulation.func @shared(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> (i8, i8) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!word>
      %value = simulation.ref.load %root : !simulation.ref<!word> -> !word
      %a = simulation.aggregate.extract %value[0] : (!word) -> i8
      %b = simulation.aggregate.extract %value[1] : (!word) -> i8
      simulation.return %a, %b : i8, i8
    }

    // CHECK-LABEL: simulation.func @single(
    // CHECK: %[[ELEMENT:.*]] = simulation.ref.subelement %{{.*}}{{\[\[0\]\]}}
    // CHECK-NEXT: %[[VALUE:.*]] = simulation.ref.load %[[ELEMENT]]
    // CHECK-NEXT: simulation.return %[[VALUE]] : i8
    simulation.func @single(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!word>
      %value = simulation.ref.load %root : !simulation.ref<!word> -> !word
      %a = simulation.aggregate.extract %value[0] : (!word) -> i8
      simulation.return %a : i8
    }

    // CHECK-LABEL: simulation.func @wide(
    // CHECK-DAG: %[[FIRST:.*]] = simulation.ref.subelement %{{.*}}{{\[\[0\]\]}}
    // CHECK-DAG: %[[SECOND:.*]] = simulation.ref.subelement %{{.*}}{{\[\[1\]\]}}
    // CHECK-DAG: %[[A:.*]] = simulation.ref.load %[[FIRST]]
    // CHECK-DAG: %[[B:.*]] = simulation.ref.load %[[SECOND]]
    // CHECK: simulation.return %[[A]], %[[B]]
    simulation.func @wide(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> (i8, i8) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %root = simulation.context.storage %ctx[1] : !simulation.ref<!wide>
      %value = simulation.ref.load %root : !simulation.ref<!wide> -> !wide
      %a = simulation.aggregate.extract %value[0] : (!wide) -> i8
      %b = simulation.aggregate.extract %value[1] : (!wide) -> i8
      simulation.return %a, %b : i8, i8
    }

    // Extracts after a store still read the earlier snapshot, including its
    // unknown plane (LRM 4.6, 10.4.1 and 7.4.1).
    // CHECK-LABEL: simulation.func @four_state_snapshot(
    // CHECK: %[[ROOT:.*]] = simulation.context.storage %arg0[2]
    // CHECK: %[[SNAPSHOT:.*]] = simulation.ref.load %[[ROOT]]
    // CHECK: simulation.ref.store %arg1 to %[[ROOT]]
    // CHECK: %[[A:.*]] = simulation.aggregate.extract %[[SNAPSHOT]][0]
    // CHECK: %[[B:.*]] = simulation.aggregate.extract %[[SNAPSHOT]][1]
    // CHECK-NOT: simulation.ref.load
    // CHECK: simulation.return %[[A]], %[[B]]
    simulation.func @four_state_snapshot(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %replacement: !logic_word {simulation.capture_kind = 1 : i32}) -> (!simulation.logic<8>, !simulation.logic<8>) attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %root = simulation.context.storage %ctx[2] : !simulation.ref<!logic_word>
      %value = simulation.ref.load %root : !simulation.ref<!logic_word> -> !logic_word
      simulation.ref.store %replacement to %root : !logic_word, !simulation.ref<!logic_word>
      %a = simulation.aggregate.extract %value[0] : (!logic_word) -> !simulation.logic<8>
      %b = simulation.aggregate.extract %value[1] : (!logic_word) -> !simulation.logic<8>
      simulation.return %a, %b : !simulation.logic<8>, !simulation.logic<8>
    }

    // Narrowing a sole extract also retains the original load point.
    // CHECK-LABEL: simulation.func @single_snapshot(
    // CHECK: %[[ROOT:.*]] = simulation.context.storage %arg0[0]
    // CHECK: %[[FIELD:.*]] = simulation.ref.subelement %[[ROOT]]{{\[\[0\]\]}}
    // CHECK-NEXT: %[[SNAPSHOT:.*]] = simulation.ref.load %[[FIELD]]
    // CHECK: simulation.ref.store %arg1 to %[[ROOT]]
    // CHECK-NEXT: simulation.return %[[SNAPSHOT]]
    simulation.func @single_snapshot(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %replacement: !word {simulation.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!word>
      %value = simulation.ref.load %root : !simulation.ref<!word> -> !word
      simulation.ref.store %replacement to %root : !word, !simulation.ref<!word>
      %a = simulation.aggregate.extract %value[0] : (!word) -> i8
      simulation.return %a : i8
    }
  }
}
