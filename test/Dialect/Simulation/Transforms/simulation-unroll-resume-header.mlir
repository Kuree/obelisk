// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-unroll-bounded-loops)))' -o %t.unrolled
// RUN: FileCheck %s < %t.unrolled
// RUN: obelisk-opt %t.unrolled --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-unroll-bounded-loops)))' -o %t.twice && diff -u %t.unrolled %t.twice
// RUN: obelisk-opt %s -o %t.original
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-unroll-bounded-loops{maximum-loop-size=1})))' -o %t.small && diff -u %t.original %t.small
// RUN: obelisk-opt %s --mlir-disable-threading --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-unroll-bounded-loops)))' -o %t.serial && diff -u %t.unrolled %t.serial

// IEEE 1800-2023 4.6, 9.4.2 and 12.7.1: a repeated process resets its
// procedural induction after each suspension. Its other carried values need
// not reset to the same value. Both initial conditional edges target the same
// header with equal inductions but distinct carried state. Expose the nested
// finite loops without changing those edges' values or the suspension.
// A rejected size budget must leave the original CFG exactly unchanged.
!counter = !simulation.logic<8>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @resume_header {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i32 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "worker"
    simulation.code_unit.decl 3 in 0 always hierarchy "reversed"
    simulation.code_unit.decl 4 in 0 always hierarchy "from_bits"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %out = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %prefix = simulation.string.literal "alternate"
      %alternate = simulation.plusarg.test %ctx, %prefix : (!simulation.context, !simulation.string) -> i32
      %zero = arith.constant 0 : i32
      %choose = arith.cmpi eq, %alternate, %zero : i32
      %process = simulation.spawn @worker(%ctx, %out, %choose) : !simulation.context, !simulation.ref<i32>, i1 -> !simulation.process
      simulation.return
    }

    // CHECK-LABEL: simulation.func @worker
    // CHECK: cf.cond_br
    // CHECK-NOT: simulation.logic.compare
    // CHECK-COUNT-4: simulation.ref.store
    // CHECK-NOT: simulation.logic.compare
    // CHECK: simulation.suspend.delay
    // CHECK-NOT: simulation.suspend.
    // CHECK: simulation.return
    simulation.func @worker(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}, %choose: i1 {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      %zero = simulation.logic.constant 0 : i8, 0 : i8 : !counter
      %start = arith.constant 0 : i32
      %alternate = arith.constant 1000 : i32
      cf.cond_br %choose, ^head(%zero, %start : !counter, i32), ^head(%zero, %alternate : !counter, i32)
    ^head(%i: !counter, %phase: i32):
      %limit = simulation.logic.constant 1 : i8, 0 : i8 : !counter
      %comparison = simulation.logic.compare sle %i, %limit : (!counter, !counter) -> !simulation.logic<1>
      %test = simulation.logic.is_true %comparison : !simulation.logic<1>
      cf.cond_br %test, ^body, ^exit(%phase : i32)
    ^body:
      cf.br ^inner(%zero : !counter)
    ^inner(%j: !counter):
      %inner_limit = simulation.logic.constant 2 : i8, 0 : i8 : !counter
      %inner_comparison = simulation.logic.compare ult %j, %inner_limit : (!counter, !counter) -> !simulation.logic<1>
      %inner_test = simulation.logic.is_true %inner_comparison : !simulation.logic<1>
      cf.cond_br %inner_test, ^write, ^latch
    ^write:
      %outer_bits = simulation.logic.to_bits %i : !counter -> i8
      %outer_wide = arith.extui %outer_bits : i8 to i32
      %inner_bits = simulation.logic.to_bits %j : !counter -> i8
      %inner_wide = arith.extui %inner_bits : i8 to i32
      %ten = arith.constant 10 : i32
      %outer_value = arith.muli %outer_wide, %ten : i32
      %indices = arith.addi %outer_value, %inner_wide : i32
      %value = arith.addi %indices, %phase : i32
      simulation.ref.store %value to %out : i32, !simulation.ref<i32>
      %read = simulation.ref.load %out : !simulation.ref<i32> -> i32
      %format = simulation.bytes.constant "visit %0d"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%format, %read) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, i32
      %inner_step = simulation.logic.constant 1 : i8, 0 : i8 : !counter
      %next_inner = simulation.logic.binary add %j, %inner_step : !counter
      cf.br ^inner(%next_inner : !counter)
    ^latch:
      %step = simulation.logic.constant 1 : i8, 0 : i8 : !counter
      %next = simulation.logic.binary add %i, %step : !counter
      cf.br ^head(%next, %phase : !counter, i32)
    ^exit(%finished_phase: i32):
      %hundred = arith.constant 100 : i32
      %three_hundred = arith.constant 300 : i32
      %next_phase = arith.addi %finished_phase, %hundred : i32
      %again = arith.cmpi ult, %next_phase, %three_hundred : i32
      cf.cond_br %again, ^wait, ^done
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^resume
    ^resume:
      %restart = simulation.logic.constant 0 : i8, 0 : i8 : !counter
      cf.br ^head(%restart, %next_phase : !counter, i32)
    ^done:
      simulation.return
    }

    // The cheap candidate filter follows both sides of integer comparisons;
    // the existing recurrence proof retains responsibility for reversal.
    // CHECK-LABEL: simulation.func @reversed
    // CHECK-NOT: arith.cmpi
    // CHECK-COUNT-3: simulation.ref.store
    // CHECK: simulation.suspend.delay
    simulation.func @reversed(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      %start = arith.constant -1 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %limit = arith.constant 2 : i8
      %test = arith.cmpi sgt, %limit, %i : i8
      cf.cond_br %test, ^body, ^wait
    ^body:
      %value = arith.extsi %i : i8 to i32
      simulation.ref.store %value to %out : i32, !simulation.ref<i32>
      %step = arith.constant 1 : i8
      %next = arith.addi %i, %step : i8
      cf.br ^head(%next : i8)
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^resume
    ^resume:
      %restart = arith.constant -1 : i8
      cf.br ^head(%restart : i8)
    }

    // A two-state induction can use a known four-state comparison.
    // CHECK-LABEL: simulation.func @from_bits
    // CHECK-NOT: simulation.logic.compare
    // CHECK-COUNT-2: simulation.ref.store
    // CHECK: simulation.suspend.delay
    simulation.func @from_bits(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      %start = arith.constant 0 : i8
      cf.br ^head(%start : i8)
    ^head(%i: i8):
      %logic = simulation.logic.from_bits %i : i8 -> !counter
      %limit = simulation.logic.constant 2 : i8, 0 : i8 : !counter
      %comparison = simulation.logic.compare ult %logic, %limit : (!counter, !counter) -> !simulation.logic<1>
      %test = simulation.logic.is_true %comparison : !simulation.logic<1>
      cf.cond_br %test, ^body, ^wait
    ^body:
      %value = arith.extui %i : i8 to i32
      simulation.ref.store %value to %out : i32, !simulation.ref<i32>
      %step = arith.constant 1 : i8
      %next = arith.addi %i, %step : i8
      cf.br ^head(%next : i8)
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^resume
    ^resume:
      %restart = arith.constant 0 : i8
      cf.br ^head(%restart : i8)
    }
  }
}
