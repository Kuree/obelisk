// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(talos-sim-thread-process-cfg),talos-sim-build-compute-graph,talos-sim-verify-compute-graph,talos-sim-materialize-graph-regions,talos-sim-materialize-compute-fusion,talos-sim-specialize-static-state-nba,talos-sim-plan-static-superstep),encode-talos-sim-to-bytecode{vpi=off},convert-talos-sim-processes-to-llvm-coroutines)' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: talos-opt %s --pass-pipeline='builtin.module(test-talos-set-module-attributes{attrs="{schedule.eval.runtime_calendar}"},simulation.design(simulation.func(talos-sim-thread-process-cfg),talos-sim-build-compute-graph,talos-sim-verify-compute-graph,talos-sim-materialize-graph-regions,talos-sim-materialize-compute-fusion,talos-sim-specialize-static-state-nba,talos-sim-plan-static-superstep),encode-talos-sim-to-bytecode{vpi=off},convert-talos-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=SHARED

// IEEE 1800-2023 4.9.4, 10.4.2: an NBA captures its RHS and selected target.
// Its destination's previous contents are not an activation input. The bounded
// selector can name either array element, but only element zero is written in
// this execution. Element one's permanent X must not prevent two-state entry.
// The commit separately clears old X/Z in the selected element (6.3.1, 7.4.5).
// CHECK-LABEL: llvm.func @work.__talos_eval_body_0.__talos_two_state_0(
// CHECK-SAME: schedule.eval.local_promotion_ranges = array<i64: 16, 8>
// Shared runtime-calendar handoffs retain their conservative closure.
// SHARED-LABEL: llvm.func @work.__talos_eval_body_0.__talos_two_state_0(
// SHARED-SAME: schedule.eval.local_promotion_ranges = array<i64: 16, 8, 32, 16>

!byte = !simulation.logic<8>
!ref = !simulation.ref<!byte>
!bitref = !simulation.ref<i1>
!array = !simulation.unpacked_array<0 : 1 x !byte>
!arrayref = !simulation.ref<!array>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 3 : i32, native.closed_executable} {
  simulation.design @nba_inputs {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i1 design
    simulation.storage.decl 2 in 0 : !byte design
    simulation.storage.decl 3 in 0 : !array design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "work"
    simulation.code_unit.decl 4 in 0 initial hierarchy "test"
    simulation.code_unit.decl 5 in 0 function hierarchy "report"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.spawn @clock(%ctx) : !simulation.context -> !simulation.process
      %work = simulation.spawn @work(%ctx) : !simulation.context -> !simulation.process
      %test = simulation.spawn @test(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      %clk = simulation.context.storage %ctx[0] : !bitref
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^toggle
    ^toggle:
      %old = simulation.ref.load %clk : !bitref -> i1
      %one = arith.constant true
      %next = arith.xori %old, %one : i1
      simulation.ref.store %next to %clk : i1, !bitref
      cf.br ^wait
    }
    simulation.func @work(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      %clk = simulation.context.storage %ctx[0] : !bitref
      %selector = simulation.context.storage %ctx[1] : !bitref
      %input = simulation.context.storage %ctx[2] : !ref
      %output = simulation.context.storage %ctx[3] : !arrayref
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !bitref
    ^body:
      %select = simulation.ref.load %selector : !bitref -> i1
      %index = arith.extui %select : i1 to i64
      %value = simulation.ref.load %input : !ref -> !byte
      %target = simulation.ref.array_element %output[%index] : (!arrayref, i64) -> !ref
      simulation.nba.enqueue %value to %target : (!byte, !ref) -> ()
      cf.br ^wait
    }
    simulation.func @test(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %input = simulation.context.storage %ctx[2] : !ref
      %a = simulation.logic.constant 165 : i8, 0 : i8 : !byte
      %b = simulation.logic.constant 90 : i8, 0 : i8 : !byte
      %x = simulation.logic.constant 0 : i8, 255 : i8 : !byte
      %z = simulation.logic.constant 255 : i8, 255 : i8 : !byte
      %zero = simulation.logic.constant 0 : i8, 0 : i8 : !byte
      %delay = simulation.time.constant 2
      simulation.ref.store %a to %input : !byte, !ref
      simulation.suspend.delay %delay to ^first
    ^first:
      simulation.call @report(%ctx) : (!simulation.context) -> ()
      simulation.ref.store %b to %input : !byte, !ref
      simulation.suspend.delay %delay to ^second
    ^second:
      simulation.call @report(%ctx) : (!simulation.context) -> ()
      simulation.ref.store %x to %input : !byte, !ref
      simulation.suspend.delay %delay to ^unknown
    ^unknown:
      simulation.call @report(%ctx) : (!simulation.context) -> ()
      simulation.ref.store %z to %input : !byte, !ref
      simulation.suspend.delay %delay to ^high_z
    ^high_z:
      simulation.call @report(%ctx) : (!simulation.context) -> ()
      simulation.ref.store %zero to %input : !byte, !ref
      simulation.suspend.delay %delay to ^done
    ^done:
      simulation.call @report(%ctx) : (!simulation.context) -> ()
      %success = arith.constant 0 : i32
      simulation.finish %ctx, %success
      simulation.return
    }
    simulation.func @report(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %output = simulation.context.storage %ctx[3] : !arrayref
      %first = simulation.ref.subelement %output[[0]] : !arrayref -> !ref
      %second = simulation.ref.subelement %output[[1]] : !arrayref -> !ref
      %a = simulation.ref.load %first : !ref -> !byte
      %b = simulation.ref.load %second : !ref -> !byte
      %fmt = simulation.bytes.constant "words %h %h"
      %stdout = arith.constant -2147483647 : i32
      simulation.display %ctx to %stdout(%fmt, %a, %b) newline = true radix = <hex> flags = [0, 0, 0] : !simulation.bytes, !byte, !byte
      simulation.return
    }
  }
}
