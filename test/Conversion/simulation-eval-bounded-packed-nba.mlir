// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(talos-sim-thread-process-cfg),talos-sim-build-compute-graph,talos-sim-verify-compute-graph,talos-sim-materialize-graph-regions,talos-sim-materialize-compute-fusion,talos-sim-specialize-static-state-nba,talos-sim-plan-static-superstep),encode-talos-sim-to-bytecode{vpi=off},convert-talos-sim-processes-to-llvm-coroutines)' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: talos-opt %s --pass-pipeline='builtin.module(test-talos-edit-attributes{has=test.byte_stride operand=1 constant="9 : i64"},simulation.design(simulation.func(talos-sim-thread-process-cfg),talos-sim-build-compute-graph,talos-sim-verify-compute-graph,talos-sim-materialize-graph-regions,talos-sim-materialize-compute-fusion,talos-sim-specialize-static-state-nba,talos-sim-plan-static-superstep),encode-talos-sim-to-bytecode{vpi=off},convert-talos-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=CLIPPED

// Each byte select fits its immediate 64-bit parent, even though the selected
// array element remains dynamic and can be invalid. The loop revisits one NBA
// site, so preserve every captured payload/target in the generated queue
// (IEEE 1800-2023 4.6(b), 4.9.4, 7.4.5, 11.5.1). See the paired Runtime test.
// CHECK-LABEL: llvm.func @write.__talos_eval_body_0(
// CHECK-NOT: llvm.call @talos_rt_v1_scheduler_packed_slice_nba
// CHECK-NOT: llvm.call @talos_rt_v1_scheduler_static_nba
// CHECK: llvm.mlir.addressof @__talos_eval_ordered_nba_queue_v1
// CHECK-NOT: llvm.call @talos_rt_v1_scheduler_packed_slice_nba
// CHECK: llvm.return
// With a stride of nine, the last byte starts at bit 63 and must be clipped
// to this word rather than overwriting its neighbor (LRM 11.5.1).
// CLIPPED: llvm.call @talos_rt_v1_scheduler_packed_slice_nba

!bitref = !simulation.ref<i1>
!byte = !simulation.logic<8>
!word = !simulation.logic<64>
!ram = !simulation.unpacked_array<0 : 1 x !word>
!ramref = !simulation.ref<!ram>
!indexref = !simulation.ref<!word>
!byteref = !simulation.ref<!byte>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 3 : i32, native.closed_executable} {
  simulation.design @bounded_bytes {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : !ram design
    simulation.storage.decl 2 in 0 : !word design
    simulation.storage.decl 3 in 0 : !byte design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "write"
    simulation.code_unit.decl 4 in 0 initial hierarchy "test"
    simulation.code_unit.decl 5 in 0 function hierarchy "report"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !bitref
      %zero = arith.constant false
      simulation.ref.store %zero to %clk : i1, !bitref
      %c = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !bitref -> !simulation.process
      %w = simulation.spawn @write(%ctx, %clk) : !simulation.context, !bitref -> !simulation.process
      %t = simulation.spawn @test(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
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
    simulation.func @write(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !bitref
    ^body:
      %mem = simulation.context.storage %ctx[1] : !ramref
      %idxref = simulation.context.storage %ctx[2] : !indexref
      %dataref = simulation.context.storage %ctx[3] : !byteref
      %idx = simulation.ref.load %idxref : !indexref -> !word
      %data = simulation.ref.load %dataref : !byteref -> !byte
      %element = simulation.ref.array_element %mem[%idx] : (!ramref, !word) -> !simulation.ref<!word>
      %zero = arith.constant 0 : i64
      %one = arith.constant 1 : i64
      %eight = arith.constant 8 : i64
      cf.br ^loop(%zero : i64)
    ^loop(%i: i64):
      %more = arith.cmpi ult, %i, %eight {test.loop_limit} : i64
      cf.cond_br %more, ^byte, ^captured
    ^byte:
      %low = arith.muli %i, %eight {test.byte_stride} : i64
      %target = simulation.ref.dyn_extract %element from %low : (!simulation.ref<!word>, i64) -> !byteref
      simulation.nba.enqueue %data to %target : (!byte, !byteref) -> ()
      %next = arith.addi %i, %one : i64
      cf.br ^loop(%next : i64)
    ^captured:
      // Changing both sources before the NBA region cannot retarget or
      // replace the already captured writes (IEEE 1800-2023 4.9.4, 10.4.2).
      %invalid = simulation.logic.constant 0 : i64, 1 : i64 : !word
      %replacement = simulation.logic.constant 0 : i8, 0 : i8 : !byte
      simulation.ref.store %invalid to %idxref : !word, !indexref
      simulation.ref.store %replacement to %dataref : !byte, !byteref
      cf.br ^wait
    }
    simulation.func @test(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %idx = simulation.context.storage %ctx[2] : !indexref
      %data = simulation.context.storage %ctx[3] : !byteref
      %zero = simulation.logic.constant 0 : i64, 0 : i64 : !word
      %one = simulation.logic.constant 1 : i64, 0 : i64 : !word
      %outside = simulation.logic.constant 2 : i64, 0 : i64 : !word
      %negative = simulation.logic.constant -1 : i64, 0 : i64 : !word
      %bad = simulation.logic.constant 0 : i64, 1 : i64 : !word
      %value = simulation.logic.constant 165 : i8, 0 : i8 : !byte
      %x = simulation.logic.constant 0 : i8, 255 : i8 : !byte
      %z = simulation.logic.constant 255 : i8, 255 : i8 : !byte
      %delay = simulation.time.constant 4
      simulation.ref.store %zero to %idx : !word, !indexref
      simulation.ref.store %value to %data : !byte, !byteref
      simulation.suspend.delay %delay to ^first
    ^first:
      simulation.call @report(%ctx) : (!simulation.context) -> ()
      simulation.ref.store %bad to %idx : !word, !indexref
      simulation.ref.store %x to %data : !byte, !byteref
      simulation.suspend.delay %delay to ^invalid
    ^invalid:
      simulation.call @report(%ctx) : (!simulation.context) -> ()
      simulation.ref.store %one to %idx : !word, !indexref
      simulation.ref.store %z to %data : !byte, !byteref
      simulation.suspend.delay %delay to ^second
    ^second:
      simulation.call @report(%ctx) : (!simulation.context) -> ()
      simulation.ref.store %outside to %idx : !word, !indexref
      simulation.ref.store %x to %data : !byte, !byteref
      simulation.suspend.delay %delay to ^outside
    ^outside:
      simulation.call @report(%ctx) : (!simulation.context) -> ()
      simulation.ref.store %negative to %idx : !word, !indexref
      simulation.suspend.delay %delay to ^negative
    ^negative:
      simulation.call @report(%ctx) : (!simulation.context) -> ()
      simulation.ref.store %zero to %idx : !word, !indexref
      simulation.ref.store %x to %data : !byte, !byteref
      simulation.suspend.delay %delay to ^last
    ^last:
      simulation.call @report(%ctx) : (!simulation.context) -> ()
      %status = arith.constant 0 : i32
      simulation.finish %ctx, %status
      simulation.return
    }
    simulation.func @report(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %mem = simulation.context.storage %ctx[1] : !ramref
      %a = simulation.ref.subelement %mem[[0]] : !ramref -> !simulation.ref<!word>
      %b = simulation.ref.subelement %mem[[1]] : !ramref -> !simulation.ref<!word>
      %av = simulation.ref.load %a : !simulation.ref<!word> -> !word
      %bv = simulation.ref.load %b : !simulation.ref<!word> -> !word
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "words %h %h"
      simulation.display %ctx to %stdout(%format, %av, %bv) newline = true radix = <hex> flags = [0, 0, 0] : !simulation.bytes, !word, !word
      simulation.return
    }
  }
}
