// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-unroll-bounded-loops)))' | FileCheck %s --check-prefix=RAW
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-unroll-bounded-loops{maximum-loop-size=1000},canonicalize,cse),obelisk-sim-sccp,simulation.func(canonicalize,cse)))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-unroll-bounded-loops{maximum-loop-size=1},canonicalize)))' | FileCheck %s --check-prefix=SMALL

// IEEE 1800-2023 4.6: "Statements within a begin-end block shall be executed
// in the order in which they appear". Replication preserves every store and
// loop-carried value. Section 12.7.1 requires a test before each iteration,
// and a step after its body, including the step preceding the final test.
// Charge complete nests, including all inner copies and final headers.
module {
  simulation.design @nests {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i32 design
    simulation.code_unit.decl 1 in 0 function hierarchy "triangular"
    // CHECK-LABEL: simulation.func @triangular(
    // CHECK-COUNT-6: simulation.ref.store
    // CHECK-NOT: cf.
    // CHECK: simulation.return
    simulation.func @triangular(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %limit = arith.constant 4 : i32
      cf.br ^outer(%zero : i32)
    ^outer(%i: i32):
      %test = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %test, ^entry, ^done
    ^entry:

      cf.br ^inner(%zero : i32)
    ^inner(%j: i32):
      %more = arith.cmpi slt, %j, %i : i32
      cf.cond_br %more, ^body, ^next
    ^body:
      %value = arith.addi %i, %j : i32
      simulation.ref.store %value to %out : i32, !simulation.ref<i32>
      %nextj = arith.addi %j, %one : i32
      cf.br ^inner(%nextj : i32)
    ^next:

      %nexti = arith.addi %i, %one : i32
      cf.br ^outer(%nexti : i32)
    ^done:
      simulation.return
    }
    simulation.code_unit.decl 2 in 0 function hierarchy "dependent_start"
    // CHECK-LABEL: simulation.func @dependent_start(
    // CHECK-COUNT-10: simulation.ref.store
    // CHECK-NOT: cf.
    // CHECK: simulation.return
    simulation.func @dependent_start(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %limit = arith.constant 4 : i32
      cf.br ^outer(%zero : i32)
    ^outer(%i: i32):
      %test = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %test, ^entry, ^done
    ^entry:

      cf.br ^inner(%i : i32)
    ^inner(%j: i32):
      %more = arith.cmpi slt, %j, %limit : i32
      cf.cond_br %more, ^body, ^next
    ^body:
      %value = arith.addi %i, %j : i32
      simulation.ref.store %value to %out : i32, !simulation.ref<i32>
      %nextj = arith.addi %j, %one : i32
      cf.br ^inner(%nextj : i32)
    ^next:

      %nexti = arith.addi %i, %one : i32
      cf.br ^outer(%nexti : i32)
    ^done:
      simulation.return
    }
    simulation.code_unit.decl 3 in 0 function hierarchy "dependent_expression"
    // CHECK-LABEL: simulation.func @dependent_expression(
    // CHECK-COUNT-10: simulation.ref.store
    // CHECK-NOT: cf.
    // CHECK: simulation.return
    simulation.func @dependent_expression(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %limit = arith.constant 4 : i32
      cf.br ^outer(%zero : i32)
    ^outer(%i: i32):
      %test = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %test, ^entry, ^done
    ^entry:
      %bound = arith.addi %i, %one : i32
      cf.br ^inner(%zero : i32)
    ^inner(%j: i32):
      %more = arith.cmpi slt, %j, %bound : i32
      cf.cond_br %more, ^body, ^next
    ^body:
      %value = arith.addi %i, %j : i32
      simulation.ref.store %value to %out : i32, !simulation.ref<i32>
      %nextj = arith.addi %j, %one : i32
      cf.br ^inner(%nextj : i32)
    ^next:

      %nexti = arith.addi %i, %one : i32
      cf.br ^outer(%nexti : i32)
    ^done:
      simulation.return
    }
    simulation.code_unit.decl 4 in 0 function hierarchy "rectangular"
    // CHECK-LABEL: simulation.func @rectangular(
    // CHECK-COUNT-16: simulation.ref.store
    // CHECK-NOT: cf.
    // CHECK: simulation.return
    simulation.func @rectangular(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %limit = arith.constant 4 : i32
      cf.br ^outer(%zero : i32)
    ^outer(%i: i32):
      %test = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %test, ^entry, ^done
    ^entry:

      cf.br ^inner(%zero : i32)
    ^inner(%j: i32):
      %more = arith.cmpi slt, %j, %limit : i32
      cf.cond_br %more, ^body, ^next
    ^body:
      %value = arith.addi %i, %j : i32
      simulation.ref.store %value to %out : i32, !simulation.ref<i32>
      %nextj = arith.addi %j, %one : i32
      cf.br ^inner(%nextj : i32)
    ^next:

      %nexti = arith.addi %i, %one : i32
      cf.br ^outer(%nexti : i32)
    ^done:
      simulation.return
    }
    simulation.code_unit.decl 5 in 0 function hierarchy "unknown_inner"
    // CHECK-LABEL: simulation.func @unknown_inner(
    // CHECK: cf.cond_br
    // CHECK: cf.cond_br
    // CHECK: simulation.ref.store
    simulation.func @unknown_inner(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}, %n: i32 {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %limit = arith.constant 4 : i32
      cf.br ^outer(%zero : i32)
    ^outer(%i: i32):
      %test = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %test, ^entry, ^done
    ^entry:

      cf.br ^inner(%zero : i32)
    ^inner(%j: i32):
      %more = arith.cmpi slt, %j, %n : i32
      cf.cond_br %more, ^body, ^next
    ^body:
      %value = arith.addi %i, %j : i32
      simulation.ref.store %value to %out : i32, !simulation.ref<i32>
      %nextj = arith.addi %j, %one : i32
      cf.br ^inner(%nextj : i32)
    ^next:

      %nexti = arith.addi %i, %one : i32
      cf.br ^outer(%nexti : i32)
    ^done:
      simulation.return
    }
    simulation.code_unit.decl 6 in 0 function hierarchy "too_many_outer"
    // CHECK-LABEL: simulation.func @too_many_outer(
    // CHECK: cf.cond_br
    // CHECK: cf.cond_br
    // CHECK: simulation.ref.store
    simulation.func @too_many_outer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %limit = arith.constant 65 : i32
      cf.br ^outer(%zero : i32)
    ^outer(%i: i32):
      %test = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %test, ^entry, ^done
    ^entry:

      cf.br ^inner(%zero : i32)
    ^inner(%j: i32):
      %more = arith.cmpi slt, %j, %one : i32
      cf.cond_br %more, ^body, ^next
    ^body:
      %value = arith.addi %i, %j : i32
      simulation.ref.store %value to %out : i32, !simulation.ref<i32>
      %nextj = arith.addi %j, %one : i32
      cf.br ^inner(%nextj : i32)
    ^next:

      %nexti = arith.addi %i, %one : i32
      cf.br ^outer(%nexti : i32)
    ^done:
      simulation.return
    }
    simulation.code_unit.decl 7 in 0 function hierarchy "inner_break"
    // CHECK-LABEL: simulation.func @inner_break(
    // CHECK: cf.cond_br
    // CHECK: cf.cond_br
    // CHECK: simulation.ref.store
    // CHECK: cf.cond_br
    simulation.func @inner_break(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}, %stop: i1 {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64} {
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %limit = arith.constant 4 : i32
      cf.br ^outer(%zero : i32)
    ^outer(%i: i32):
      %test = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %test, ^entry, ^done
    ^entry:

      cf.br ^inner(%zero : i32)
    ^inner(%j: i32):
      %more = arith.cmpi slt, %j, %limit : i32
      cf.cond_br %more, ^body, ^next
    ^body:
      %value = arith.addi %i, %j : i32
      simulation.ref.store %value to %out : i32, !simulation.ref<i32>
      cf.cond_br %stop, ^next, ^latch
    ^latch:
      %nextj = arith.addi %j, %one : i32
      cf.br ^inner(%nextj : i32)
    ^next:

      %nexti = arith.addi %i, %one : i32
      cf.br ^outer(%nexti : i32)
    ^done:
      simulation.return
    }
    // SMALL-LABEL: simulation.func @triangular(
    // SMALL: cf.cond_br
    // SMALL: cf.cond_br
    // SMALL: simulation.ref.store
    // SMALL-LABEL: simulation.func @dependent_start(
    // SMALL: cf.cond_br
    // SMALL: cf.cond_br
    // SMALL: simulation.ref.store
    // SMALL-LABEL: simulation.func @dependent_expression(
    // SMALL: cf.cond_br
    // SMALL: cf.cond_br
    // SMALL: simulation.ref.store
    // SMALL-LABEL: simulation.func @rectangular(
    // SMALL: cf.cond_br
    // SMALL: cf.cond_br
    // SMALL: simulation.ref.store
    simulation.code_unit.decl 8 in 0 function hierarchy "exactly_64"
    // CHECK-LABEL: simulation.func @exactly_64(
    // CHECK-COUNT-64: simulation.ref.store
    // CHECK-NOT: cf.
    // CHECK: simulation.return
    simulation.func @exactly_64(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 8 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^done
    ^body:
      simulation.ref.store %i to %out : i32, !simulation.ref<i32>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^done:
      simulation.return
    }
    simulation.code_unit.decl 9 in 0 function hierarchy "over_64"
    // CHECK-LABEL: simulation.func @over_64(
    // CHECK: cf.cond_br
    // CHECK: simulation.ref.store
    simulation.func @over_64(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 9 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 65 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^done
    ^body:
      simulation.ref.store %i to %out : i32, !simulation.ref<i32>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^done:
      simulation.return
    }
    simulation.code_unit.decl 10 in 0 root_initializer hierarchy "once_root"
    // CHECK-LABEL: simulation.func @once_root(
    // CHECK: cf.cond_br
    // CHECK: simulation.ref.store
    simulation.func @once_root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 10 : i64} {
      %out = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 2 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^done
    ^body:
      simulation.ref.store %i to %out : i32, !simulation.ref<i32>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^done:
      simulation.return
    }
    simulation.code_unit.decl 11 in 0 initial hierarchy "once_initial"
    // CHECK-LABEL: simulation.func @once_initial(
    // CHECK: cf.cond_br
    // CHECK: simulation.ref.store
    simulation.func @once_initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 11 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 2 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^done
    ^body:
      simulation.ref.store %i to %out : i32, !simulation.ref<i32>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^done:
      simulation.return
    }
    simulation.code_unit.decl 12 in 0 final hierarchy "once_final"
    // CHECK-LABEL: simulation.func @once_final(
    // CHECK: cf.cond_br
    // CHECK: simulation.ref.store
    simulation.func @once_final(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 2 : i32, code_unit_id = 12 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 2 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^done
    ^body:
      simulation.ref.store %i to %out : i32, !simulation.ref<i32>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^done:
      simulation.return
    }
    simulation.code_unit.decl 13 in 0 port_initialize hierarchy "once_port"
    // CHECK-LABEL: simulation.func @once_port(
    // CHECK: cf.cond_br
    // CHECK: simulation.ref.store
    simulation.func @once_port(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 11 : i32, code_unit_id = 13 : i64} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 2 : i32
      %test = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %test, ^body, ^done
    ^body:
      simulation.ref.store %i to %out : i32, !simulation.ref<i32>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^done:
      simulation.return
    }
    simulation.code_unit.decl 14 in 0 function hierarchy "unknown_four_state_bound"
    // CHECK-LABEL: simulation.func @unknown_four_state_bound(
    // CHECK: cf.cond_br
    // CHECK-NOT: simulation.ref.store
    // CHECK: simulation.return
    // RAW-LABEL: simulation.func @unknown_four_state_bound(
    // RAW: cf.cond_br
    // RAW: simulation.logic.compare
    // RAW: cf.cond_br
    // RAW: simulation.ref.store
    simulation.func @unknown_four_state_bound(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 14 : i64} {
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %limit = arith.constant 4 : i32
      cf.br ^outer(%zero : i32)
    ^outer(%i: i32):
      %test = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %test, ^entry, ^done
    ^entry:

      cf.br ^inner(%zero : i32)
    ^inner(%j: i32):
      %jl = simulation.logic.from_bits %j : i32 -> !simulation.logic<32>
      %limitx = simulation.logic.constant 4 : i32, 1 : i32 : !simulation.logic<32>
      %cmp = simulation.logic.compare slt %jl, %limitx : (!simulation.logic<32>, !simulation.logic<32>) -> !simulation.logic<1>
      %more = simulation.logic.is_true %cmp : !simulation.logic<1>
      cf.cond_br %more, ^body, ^next
    ^body:
      %value = arith.addi %i, %j : i32
      simulation.ref.store %value to %out : i32, !simulation.ref<i32>
      %nextj = arith.addi %j, %one : i32
      cf.br ^inner(%nextj : i32)
    ^next:

      %nexti = arith.addi %i, %one : i32
      cf.br ^outer(%nexti : i32)
    ^done:
      simulation.return
    }
    simulation.code_unit.decl 15 in 0 function hierarchy "straight_line"
    // CHECK-LABEL: simulation.func @straight_line(
    // CHECK: simulation.return
    simulation.func @straight_line(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 15 : i64} {
      simulation.return
    }
    // CHECK: simulation.func private @external
    simulation.func private @external(!simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32}

    // Both loops have termination envelopes, but the inner trip count remains
    // unknown after outer replication. Discard the entire trial expansion.
    simulation.code_unit.decl 16 in 0 function hierarchy "bounded_unknown_inner"
    // RAW-LABEL: simulation.func @bounded_unknown_inner(
    // RAW: cf.cond_br
    // RAW: cf.cond_br
    // RAW: simulation.ref.store
    // RAW-NOT: simulation.ref.store
    // RAW: simulation.return
    // CHECK-LABEL: simulation.func @bounded_unknown_inner(
    // CHECK: cf.cond_br
    // CHECK: cf.cond_br
    // CHECK: simulation.ref.store
    // CHECK-NOT: simulation.ref.store
    // CHECK: simulation.return
    simulation.func @bounded_unknown_inner(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}, %n: i32 {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 16 : i64} {
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %four = arith.constant 4 : i32
      %mask = arith.constant 3 : i32
      %bound = arith.andi %n, %mask : i32
      cf.br ^outer(%zero : i32)
    ^outer(%i: i32):
      %test = arith.cmpi ult, %i, %four : i32
      cf.cond_br %test, ^inner(%zero : i32), ^done
    ^inner(%j: i32):
      %more = arith.cmpi ult, %j, %bound : i32
      cf.cond_br %more, ^body, ^latch
    ^body:
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      %nextj = arith.addi %j, %one : i32
      cf.br ^inner(%nextj : i32)
    ^latch:
      %nexti = arith.addi %i, %one : i32
      cf.br ^outer(%nexti : i32)
    ^done:
      simulation.return
    }
  }
}
