// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-clocked-kernels))' | FileCheck %s

// IEEE 1800-2023 4.5/4.7: `sampler` wakes on the same clock edge as the
// kernel, in the same phase, and the scheduler may run it after the kernel.
// It must still read the old `q` (4.9.4), so `q` keeps its NBA. `r` is read
// only after a positive delay, in an earlier phase of a later slot than any
// NBA region, so its NBA becomes a store at the statement.

!bit = !simulation.logic<1>
!ref = !simulation.ref<!bit>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 1 : i32
} {
  simulation.design @retained {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !bit design
    simulation.storage.decl 2 in 0 : !bit design
    simulation.storage.decl 3 in 0 : !bit design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always_ff hierarchy "toggle"
    simulation.code_unit.decl 4 in 0 always_ff hierarchy "follow"
    simulation.code_unit.decl 5 in 0 initial hierarchy "sampler"
    simulation.code_unit.decl 6 in 0 initial hierarchy "report"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !ref
      %q = simulation.context.storage %ctx[1] : !ref
      %r = simulation.context.storage %ctx[2] : !ref
      %seen = simulation.context.storage %ctx[3] : !ref
      %init = simulation.logic.constant false, false : !bit
      simulation.ref.store %init to %q : !bit, !ref
      %p0 = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !ref -> !simulation.process
      %p1 = simulation.spawn @toggle(%ctx, %clk, %q) : !simulation.context, !ref, !ref -> !simulation.process
      %p2 = simulation.spawn @follow(%ctx, %clk, %q, %r) : !simulation.context, !ref, !ref, !ref -> !simulation.process
      %p3 = simulation.spawn @sampler(%ctx, %clk, %q, %seen) : !simulation.context, !ref, !ref, !ref -> !simulation.process
      %p4 = simulation.spawn @report(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %delay = simulation.time.constant 5
      %one = simulation.logic.constant true, false : !bit
      %zero = simulation.logic.constant false, false : !bit
      cf.br ^low
    ^low:
      simulation.suspend.delay %delay to ^rise
    ^rise:
      simulation.nba.enqueue %one to %clk : (!bit, !ref) -> ()
      simulation.suspend.delay %delay to ^fall
    ^fall:
      simulation.nba.enqueue %zero to %clk : (!bit, !ref) -> ()
      cf.br ^low
    }
    // CHECK-LABEL: simulation.func private @__obelisk_clocked_0_1_0
    // CHECK: simulation.suspend.edge posedge
    // `follow` reads `q` before `toggle` would update it.
    // CHECK: simulation.ref.load
    // CHECK: simulation.ref.store %{{.*}} to %[[R:arg[0-9]+]] : !simulation.logic<1>
    // CHECK: simulation.logic.unary logical_not
    // CHECK-NEXT: simulation.nba.enqueue
    // CHECK-NOT: simulation.ref.store
    // CHECK: cf.br
    simulation.func private @toggle(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !ref
    ^body:
      %old = simulation.ref.load %q : !ref -> !bit
      %next = simulation.logic.unary logical_not %old : (!bit) -> !bit
      simulation.nba.enqueue %next to %q : (!bit, !ref) -> ()
      cf.br ^wait
    }
    simulation.func private @follow(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !ref
    ^body:
      %v = simulation.ref.load %q : !ref -> !bit
      simulation.nba.enqueue %v to %r : (!bit, !ref) -> ()
      cf.br ^wait
    }
    // Woken by the clock edge in the kernel's phase; reads `q` immediately.
    simulation.func @sampler(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %seen: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !ref
    ^body:
      %v = simulation.ref.load %q : !ref -> !bit
      simulation.ref.store %v to %seen : !bit, !ref
      cf.br ^wait
    }
    simulation.func @report(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      %delay = simulation.time.constant 100
      simulation.suspend.delay %delay to ^done
    ^done:
      %r = simulation.context.storage %ctx[2] : !ref
      %seen = simulation.context.storage %ctx[3] : !ref
      %rv = simulation.ref.load %r : !ref -> !bit
      %sv = simulation.ref.load %seen : !ref -> !bit
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "%b %b"
      simulation.display %ctx to %stdout(%format, %rv, %sv) newline = true radix = <binary> flags = [0, 0, 0] : !simulation.bytes, !bit, !bit
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
  }
}
