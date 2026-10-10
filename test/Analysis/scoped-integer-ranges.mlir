// RUN: obelisk-opt %s --test-obelisk-scoped-integer-ranges -o %t.analyzed 2>&1 | FileCheck %s
// RUN: obelisk-opt %s -o %t.original
// RUN: diff %t.original %t.analyzed

// Range snapshots must retain dominating live values and map all predecessor
// operands before discarding dead facts. Swapping successor lanes must not
// overwrite the other input. Logic joins include every incoming X/Z case.
!array = !simulation.unpacked_array<0 : 7 x i8>
module {
  simulation.design @scoped_ranges {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !array design
    simulation.code_unit.decl 1 in 0 function hierarchy "live_values"
    simulation.func @live_values(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %seed: i8 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %mask = arith.constant 3 : i8
      %four = arith.constant 4 : i8
      %low = arith.andi %seed, %mask : i8
      %high = arith.addi %low, %four : i8
      cf.br ^bridge(%high, %low : i8, i8)
    ^bridge(%a: i8, %b: i8):
      cf.br ^join(%b, %a : i8, i8)
    ^join(%left: i8, %right: i8):
      %l = simulation.ref.array_element %root[%left] {test.scoped = "left"} : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %r = simulation.ref.array_element %root[%right] {test.scoped = "right"} : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %saved = simulation.ref.array_element %root[%high] {test.scoped = "dominating"} : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      simulation.return
    }
    // CHECK: left umin=0 umax=3
    // CHECK: right umin=4 umax=7
    // CHECK: dominating umin=4 umax=7

    simulation.code_unit.decl 2 in 0 function hierarchy "loop_scopes"
    simulation.func @loop_scopes(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %zero = arith.constant 0 : i8
      %one = arith.constant 1 : i8
      %four = arith.constant 4 : i8
      cf.br ^head(%zero : i8)
    ^head(%i: i8):
      %more = arith.cmpi ult, %i, %four : i8
      cf.cond_br %more, ^body, ^exit
    ^body:
      %element = simulation.ref.array_element %root[%i] {test.scoped = "body"} : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      cf.br ^landing(%i : i8)
    ^landing(%carried: i8):
      %other = simulation.ref.array_element %root[%carried] {test.scoped = "carried"} : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %next = arith.addi %i, %one : i8
      cf.br ^head(%next : i8)
    ^exit:
      %last = simulation.ref.array_element %root[%i] {test.scoped = "exit"} : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      simulation.return
    }
    // CHECK: body umin=0 umax=3
    // CHECK: carried umin=0 umax=3
    // CHECK: exit umin=4 umax=4

    simulation.code_unit.decl 3 in 0 function hierarchy "parallel_unknown"
    simulation.func @parallel_unknown(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %choose: i1 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %known = simulation.logic.constant 3 : i8, 0 : i8 : !simulation.logic<8>
      %unknown = simulation.logic.constant 0 : i8, 1 : i8 : !simulation.logic<8>
      cf.cond_br %choose, ^join(%known, %unknown : !simulation.logic<8>, !simulation.logic<8>), ^join(%unknown, %known : !simulation.logic<8>, !simulation.logic<8>)
    ^join(%a: !simulation.logic<8>, %b: !simulation.logic<8>):
      %l = simulation.ref.array_element %root[%a] {test.scoped = "known-then-unknown"} : (!simulation.ref<!array>, !simulation.logic<8>) -> !simulation.ref<i8>
      %r = simulation.ref.array_element %root[%b] {test.scoped = "unknown-then-known"} : (!simulation.ref<!array>, !simulation.logic<8>) -> !simulation.ref<i8>
      simulation.return
    }
    // CHECK: known-then-unknown unknown
    // CHECK: unknown-then-known unknown
  }
}
