// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,talos-sim-fuse-clocked-kernels))' | FileCheck %s

// IEEE 1800-2023 4.9.4: every iteration of the loop reads the value `r` had
// before this activation, because its NBA has not been applied yet. The
// snapshot is taken once, in the block the wait resumes into, not in the loop
// header that the first member begins with. The clock is a two-state `bit`.
// A root nothing reads keeps its NBA: removing the update entirely (4.3) is
// better than storing it.

!bit = !simulation.logic<1>
!byte = !simulation.logic<8>
!bitref = !simulation.ref<!bit>
!clkref = !simulation.ref<i1>
!byteref = !simulation.ref<!byte>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 1 : i32
} {
  simulation.design @snapshot_loop {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : !byte design
    simulation.storage.decl 2 in 0 : !bit design
    simulation.storage.decl 3 in 0 : !byte design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always_ff hierarchy "count"
    simulation.code_unit.decl 4 in 0 initial hierarchy "report"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !clkref
      %r = simulation.context.storage %ctx[1] : !byteref
      %again = simulation.context.storage %ctx[2] : !bitref
      %unread = simulation.context.storage %ctx[3] : !byteref
      %init = simulation.logic.constant 0 : i8, 0 : i8 : !byte
      simulation.ref.store %init to %r : !byte, !byteref
      %low = simulation.logic.constant false, false : !bit
      simulation.ref.store %low to %again : !bit, !bitref
      %p0 = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !clkref -> !simulation.process
      %p1 = simulation.spawn @count(%ctx, %clk, %r, %again, %unread) : !simulation.context, !clkref, !byteref, !bitref, !byteref -> !simulation.process
      %p2 = simulation.spawn @report(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clkref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %delay = simulation.time.constant 5
      %one = arith.constant true
      %zero = arith.constant false
      cf.br ^low
    ^low:
      simulation.suspend.delay %delay to ^rise
    ^rise:
      simulation.nba.enqueue %one to %clk : (i1, !clkref) -> ()
      simulation.suspend.delay %delay to ^fall
    ^fall:
      simulation.nba.enqueue %zero to %clk : (i1, !clkref) -> ()
      cf.br ^low
    }
    // CHECK-LABEL: simulation.func private @__talos_clocked_0_1_0
    // CHECK: simulation.suspend.edge posedge %{{.*}} to ^[[START:bb[0-9]+]]
    // CHECK: ^[[START]]:
    // CHECK-NEXT: %[[OLD:.*]] = simulation.ref.load %[[R:arg[0-9]+]]
    // CHECK-NEXT: %[[SNAP:.*]] = simulation.ref.alloc %[[OLD]]
    // CHECK-NEXT: cf.br ^[[LOOP:bb[0-9]+]]
    // CHECK: ^[[LOOP]]:
    // CHECK: simulation.ref.load %[[SNAP]]
    // CHECK: simulation.ref.store %{{.*}} to %[[R]]
    // Nothing reads `unread`; unread-NBA elimination removes it later.
    // CHECK-NEXT: simulation.nba.enqueue
    simulation.func private @count(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clkref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %again: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %unread: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 3 : i64} {
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !byte
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^loop : !clkref
    ^loop:
      %v = simulation.ref.load %r : !byteref -> !byte
      %n = simulation.logic.binary add %v, %one : !byte
      simulation.nba.enqueue %n to %r : (!byte, !byteref) -> ()
      simulation.nba.enqueue %n to %unread : (!byte, !byteref) -> ()
      %flag = simulation.ref.load %again : !bitref -> !bit
      %repeat = simulation.logic.is_true %flag : !bit
      cf.cond_br %repeat, ^loop, ^wait
    }
    simulation.func @report(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %delay = simulation.time.constant 100
      simulation.suspend.delay %delay to ^done
    ^done:
      %r = simulation.context.storage %ctx[1] : !byteref
      %rv = simulation.ref.load %r : !byteref -> !byte
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "%d"
      simulation.display %ctx to %stdout(%format, %rv) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !byte
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
  }
}
