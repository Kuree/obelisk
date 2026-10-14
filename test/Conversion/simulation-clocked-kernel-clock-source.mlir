// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-clocked-kernels))' -o %t.mlir 2> %t.log
// RUN: FileCheck %s < %t.mlir
// RUN: FileCheck %s --check-prefix=REPORT < %t.log

// IEEE 1800-2023 4.4.2.3 and 4.5: the clock rises, falls through an Inactive
// event and rises again before the slot's NBA region. `count` therefore runs
// twice in one phase, and both runs read the `q` the slot started with
// (4.9.4): `q` advances once per period. A kernel storing `q` directly would
// read its own first store in the second run. A kernel must run at most once
// per phase, so this clock keeps every NBA.

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
  simulation.design @clock_source {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !byte design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always_ff hierarchy "count"
    simulation.code_unit.decl 4 in 0 initial hierarchy "stop"
    simulation.code_unit.decl 5 in 0 final hierarchy "report"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !bitref
      %q = simulation.context.storage %ctx[1] : !byteref
      %init = simulation.logic.constant 0 : i8, 0 : i8 : !byte
      simulation.ref.store %init to %q : !byte, !byteref
      %p0 = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !bitref -> !simulation.process
      %p1 = simulation.spawn @count(%ctx, %clk, %q) : !simulation.context, !bitref, !byteref -> !simulation.process
      %p2 = simulation.spawn @stop(%ctx) : !simulation.context -> !simulation.process
      %p3 = simulation.spawn @report(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    // CHECK-LABEL: simulation.func @clock
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %five = simulation.time.constant 5
      %none = simulation.time.constant 0
      %one = simulation.logic.constant true, false : !bit
      %zero = simulation.logic.constant false, false : !bit
      cf.br ^low
    ^low:
      simulation.suspend.delay %five to ^rise
    ^rise:
      simulation.ref.store %one to %clk : !bit, !bitref
      simulation.suspend.delay %none to ^glitch
    ^glitch:
      simulation.ref.store %zero to %clk : !bit, !bitref
      simulation.suspend.delay %none to ^again
    ^again:
      simulation.ref.store %one to %clk : !bit, !bitref
      simulation.suspend.delay %five to ^fall
    ^fall:
      simulation.ref.store %zero to %clk : !bit, !bitref
      cf.br ^low
    }
    // CHECK-NOT: __obelisk_clocked
    // CHECK: simulation.func private @count
    // CHECK: simulation.nba.enqueue
    // REPORT-NOT: obelisk clocked kernel:
    // REPORT: obelisk clocked kernel rejection: clock source = 1
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
    simulation.func @stop(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %delay = simulation.time.constant 100
      simulation.suspend.delay %delay to ^done
    ^done:
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
    simulation.func @report(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 2 : i32, code_unit_id = 5 : i64} {
      %q = simulation.context.storage %ctx[1] : !byteref
      %qv = simulation.ref.load %q : !byteref -> !byte
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "q=%0d"
      simulation.display %ctx to %stdout(%format, %qv) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !byte
      simulation.return
    }
  }
}
