// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,talos-sim-fuse-clocked-kernels))' -o %t.mlir 2> %t.log
// RUN: FileCheck %s < %t.mlir
// RUN: FileCheck %s --check-prefix=REPORT < %t.log

// An immediate assertion's failure branch reads the assertion enable
// (IEEE 1800-2023 20.11), reads $time (20.3.1), records a nonterminating
// $error (20.10) and prints a report. None of these can be observed by
// another member, so the checker joins its clock's kernel with the counter;
// the members run back to back within the slot, which 4.7 permits.

// REPORT: talos clocked kernel: __talos_clocked_0_1_0 members=2
// REPORT-NOT: member body eligibility
// The checker reads q before the counter stores it directly, as it would
// before the NBA region (4.9.4).
// CHECK-LABEL: simulation.func private @__talos_clocked_0_1_0
// CHECK: simulation.assert.enabled
// CHECK: simulation.ref.load
// CHECK: simulation.time.now
// CHECK: simulation.error
// CHECK: simulation.display
// CHECK: simulation.ref.store

!bit = !simulation.logic<1>
!byte = !simulation.logic<8>
!bitref = !simulation.ref<!bit>
!byteref = !simulation.ref<!byte>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  native.debug_timing,
  schedule.native_scheduler = 1 : i32
} {
  simulation.design @assertion_report {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !byte design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always_ff hierarchy "count"
    simulation.code_unit.decl 4 in 0 always_ff hierarchy "check"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !bitref
      %q = simulation.context.storage %ctx[1] : !byteref
      %p0 = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !bitref -> !simulation.process
      %p1 = simulation.spawn @count(%ctx, %clk, %q) : !simulation.context, !bitref, !byteref -> !simulation.process
      %p2 = simulation.spawn @check(%ctx, %clk, %q) : !simulation.context, !bitref, !byteref -> !simulation.process
      simulation.return
    }
    simulation.func private @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      %five = simulation.time.constant 5
      cf.br ^wait
    ^wait:
      simulation.suspend.delay %five to ^toggle
    ^toggle:
      %v = simulation.ref.load %clk : !bitref -> !bit
      %t = simulation.logic.unary bit_not %v : (!bit) -> !bit
      simulation.ref.store %t to %clk : !bit, !bitref
      cf.br ^wait
    }
    simulation.func private @count(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 3 : i64} {
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !byte
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !bitref
    ^body:
      %old = simulation.ref.load %q : !byteref -> !byte
      %next = simulation.logic.binary add %old, %one : !byte
      simulation.nba.enqueue %next to %q : (!byte, !byteref) -> ()
      cf.br ^wait
    }
    simulation.func private @check(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 4 : i64} {
      %limit = simulation.logic.constant 200 : i8, 0 : i8 : !byte
      %stderr = arith.constant -2147483646 : i32
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !bitref
    ^body:
      %enabled = simulation.assert.enabled %ctx assertion 1
      cf.cond_br %enabled, ^test, ^wait
    ^test:
      %value = simulation.ref.load %q : !byteref -> !byte
      %below = simulation.logic.compare ult %value, %limit : (!byte, !byte) -> !bit
      %ok = simulation.logic.is_true %below : !bit
      cf.cond_br %ok, ^wait, ^fail
    ^fail:
      %now = simulation.time.now %ctx
      simulation.error %ctx
      %format = simulation.bytes.constant "overflow at %0t: %0d"
      simulation.display %ctx to %stderr(%format, %now, %value) newline = true radix = <decimal> flags = [0, 0, 0] {scope = "assertion_report"} : !simulation.bytes, i64, !byte
      cf.br ^wait
    }
  }
}
