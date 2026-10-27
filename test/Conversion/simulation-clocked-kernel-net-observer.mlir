// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,talos-sim-fuse-clocked-kernels))' | FileCheck %s

// IEEE 1800-2023 10.3.1 and 4.9.1: `wire t = q;` is a continuous assignment
// process. Connecting `t` to a port merges it with the port net into one
// simulated net (23.3.3.7); with one strong contribution that net resolves to
// the driven value (6.6.1, Table 6-2), so the net initializer is a transparent
// combinational observer of `q` and the kernel stores `q` at its NBA statement
// (4.3). When the port net has a contribution of its own, the initializer's
// drive is a resolution, so `q` keeps its NBA.

!bit = !simulation.logic<1>
!ref = !simulation.ref<!bit>
!net = !simulation.net<!bit>
!drv = !simulation.driver<!bit>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 1 : i32
} {
  simulation.design @exclusive {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !bit design
    simulation.net.decl 0 in 0 : !bit design
    simulation.net.decl 1 in 0 : !bit design
    simulation.net.decl 2 in 0 : !bit design
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false provenance "named" rhs_dominates = false
    simulation.driver.decl 0 in 0 drives 0 : !bit design
    simulation.driver.decl 1 in 0 drives 2 : !bit design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always_ff hierarchy "toggle"
    simulation.code_unit.decl 4 in 0 continuous hierarchy "initializer"
    simulation.code_unit.decl 5 in 0 continuous hierarchy "child"
    simulation.code_unit.decl 6 in 0 initial hierarchy "report"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !ref
      %q = simulation.context.storage %ctx[1] : !ref
      %init = simulation.logic.constant false, false : !bit
      simulation.ref.store %init to %q : !bit, !ref
      %p0 = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !ref -> !simulation.process
      %p1 = simulation.spawn @toggle(%ctx, %clk, %q) : !simulation.context, !ref, !ref -> !simulation.process
      %p2 = simulation.spawn @initializer(%ctx) : !simulation.context -> !simulation.process
      %p3 = simulation.spawn @child(%ctx) : !simulation.context -> !simulation.process
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
    // CHECK-LABEL: simulation.func private @__talos_clocked_0_1_0
    // CHECK: simulation.suspend.edge posedge
    // CHECK: simulation.logic.unary logical_not
    // CHECK-NEXT: simulation.ref.store %{{.*}} to %{{.*}} : !simulation.logic<1>
    // CHECK-NOT: simulation.nba.enqueue
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
    // `wire t = q;` with `t` connected to a child port.
    simulation.func @initializer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 4 : i64} {
      %q = simulation.context.storage %ctx[1] : !ref
      %driver = simulation.context.driver %ctx[0] : !drv
      cf.br ^body
    ^body:
      %v = simulation.ref.load %q : !ref -> !bit
      simulation.driver.drive %driver = %v : !drv, !bit
      simulation.suspend.change %q to ^body : !ref
    }
    // The child reads its port and drives an isolated output net.
    simulation.func @child(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 5 : i64} {
      %port = simulation.context.net %ctx[1] : !net
      %driver = simulation.context.driver %ctx[1] : !drv
      cf.br ^body
    ^body:
      %v = simulation.net.read %port : !net -> !bit
      %n = simulation.logic.unary logical_not %v : (!bit) -> !bit
      simulation.driver.drive %driver = %n : !drv, !bit
      simulation.suspend.change %port to ^body : !net
    }
    simulation.func @report(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      %delay = simulation.time.constant 100
      simulation.suspend.delay %delay to ^done
    ^done:
      %out = simulation.context.net %ctx[2] : !net
      %v = simulation.net.read %out : !net -> !bit
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "%b"
      simulation.display %ctx to %stdout(%format, %v) newline = true radix = <binary> flags = [0, 0] : !simulation.bytes, !bit
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
  }
  // The port net has a second strong contribution: resolution, not a write.
  // `follow` reads `q` in the kernel and NBAs `r`, which only `report` reads
  // after a delay, so the kernel forms with a direct store of `r` while `q`
  // keeps its NBA.
  simulation.design @competing {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !bit design
    simulation.storage.decl 2 in 0 : !bit design
    simulation.net.decl 0 in 0 : !bit design
    simulation.net.decl 1 in 0 : !bit design
    simulation.net.decl 2 in 0 : !bit design
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false provenance "named" rhs_dominates = false
    simulation.driver.decl 0 in 0 drives 0 : !bit design
    simulation.driver.decl 1 in 0 drives 2 : !bit design
    simulation.driver.decl 2 in 0 drives 1 : !bit design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always_ff hierarchy "toggle"
    simulation.code_unit.decl 4 in 0 continuous hierarchy "initializer"
    simulation.code_unit.decl 5 in 0 continuous hierarchy "child"
    simulation.code_unit.decl 6 in 0 initial hierarchy "report"
    simulation.code_unit.decl 7 in 0 continuous hierarchy "other"
    simulation.code_unit.decl 8 in 0 always_ff hierarchy "follow"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !ref
      %q = simulation.context.storage %ctx[1] : !ref
      %r = simulation.context.storage %ctx[2] : !ref
      %init = simulation.logic.constant false, false : !bit
      simulation.ref.store %init to %q : !bit, !ref
      %p0 = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !ref -> !simulation.process
      %p1 = simulation.spawn @toggle(%ctx, %clk, %q) : !simulation.context, !ref, !ref -> !simulation.process
      %p2 = simulation.spawn @initializer(%ctx) : !simulation.context -> !simulation.process
      %p3 = simulation.spawn @child(%ctx) : !simulation.context -> !simulation.process
      %p4 = simulation.spawn @report(%ctx) : !simulation.context -> !simulation.process
      %p5 = simulation.spawn @other(%ctx) : !simulation.context -> !simulation.process
      %p6 = simulation.spawn @follow(%ctx, %clk, %q, %r) : !simulation.context, !ref, !ref, !ref -> !simulation.process
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
    // CHECK-LABEL: simulation.func private @__talos_clocked_0_1_0
    // CHECK: simulation.suspend.edge posedge
    // CHECK: simulation.ref.load
    // CHECK: simulation.ref.store %{{.*}} to %{{.*}} : !simulation.logic<1>
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
    simulation.func private @follow(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 8 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !ref
    ^body:
      %v = simulation.ref.load %q : !ref -> !bit
      simulation.nba.enqueue %v to %r : (!bit, !ref) -> ()
      cf.br ^wait
    }
    simulation.func @initializer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 4 : i64} {
      %q = simulation.context.storage %ctx[1] : !ref
      %driver = simulation.context.driver %ctx[0] : !drv
      cf.br ^body
    ^body:
      %v = simulation.ref.load %q : !ref -> !bit
      simulation.driver.drive %driver = %v : !drv, !bit
      simulation.suspend.change %q to ^body : !ref
    }
    simulation.func @other(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 7 : i64} {
      %q = simulation.context.storage %ctx[1] : !ref
      %driver = simulation.context.driver %ctx[2] : !drv
      cf.br ^body
    ^body:
      %v = simulation.ref.load %q : !ref -> !bit
      simulation.driver.drive %driver = %v : !drv, !bit
      simulation.suspend.change %q to ^body : !ref
    }
    simulation.func @child(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 5 : i64} {
      %port = simulation.context.net %ctx[1] : !net
      %driver = simulation.context.driver %ctx[1] : !drv
      cf.br ^body
    ^body:
      %v = simulation.net.read %port : !net -> !bit
      %n = simulation.logic.unary logical_not %v : (!bit) -> !bit
      simulation.driver.drive %driver = %n : !drv, !bit
      simulation.suspend.change %port to ^body : !net
    }
    simulation.func @report(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      %delay = simulation.time.constant 100
      simulation.suspend.delay %delay to ^done
    ^done:
      %out = simulation.context.net %ctx[2] : !net
      %r = simulation.context.storage %ctx[2] : !ref
      %v = simulation.net.read %out : !net -> !bit
      %rv = simulation.ref.load %r : !ref -> !bit
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "%b %b"
      simulation.display %ctx to %stdout(%format, %v, %rv) newline = true radix = <binary> flags = [0, 0, 0] : !simulation.bytes, !bit, !bit
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
  }
}
