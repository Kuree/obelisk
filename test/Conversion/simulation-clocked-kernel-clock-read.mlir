// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-clocked-kernels))' -o %t.mlir 2> %t.log
// RUN: FileCheck %s --check-prefix=REPORT < %t.log
// RUN: FileCheck %s < %t.mlir

// `clk_i` and `clk_j` are port copies of `clk` (IEEE 1800-2023 23.3.3). The
// checker waits on `clk_i` and prints it: in the reference algorithm it runs
// after its copy's update event, when the copy equals the source, and the
// source is current whenever the kernel runs because every copy wakes on it.
// The kernel therefore admits the checker and reads the source `clk` in its
// place (4.9.1). A member reading another copy of the clock could see a stale
// value and a member writing the clock changes its own trigger: both stay out.
// REPORT: obelisk clocked kernel: __obelisk_clocked_0_258_0 members=2 eligible=1 retained=0 snapshots=0 deferred=0 delayed=0 clock_reads=1
// REPORT: obelisk clocked kernel rejection: member accesses its clock = 2
// CHECK-LABEL: simulation.design @clock_read
// CHECK-NOT: simulation.func private @flop
// CHECK-NOT: simulation.func private @checker
// CHECK: simulation.func private @peer_reader
// CHECK: simulation.func private @clock_writer
// CHECK-LABEL: simulation.func private @__obelisk_clocked_0_258_0(
// CHECK-SAME: %[[CTX:.*]]: !simulation.context {{.*}}, %[[CLK_I:.*]]: !simulation.ref<i1>
// CHECK: simulation.suspend.edge negedge %[[CLK_I]]
// The display reads the clock source, not the copy the member waits on.
// CHECK: %[[SOURCE:.*]] = simulation.context.storage %[[CTX]][0] : !simulation.ref<i1>
// CHECK-NEXT: %[[LEVEL:.*]] = simulation.ref.load %[[SOURCE]] : !simulation.ref<i1> -> i1
// CHECK-NOT: simulation.ref.load %[[CLK_I]]
// CHECK: simulation.display %[[CTX]] to %{{.*}}(%{{.*}}, %{{.*}}, %[[LEVEL]])
// CHECK-NOT: simulation.ref.load %[[CLK_I]]
// CHECK: simulation.ref.store
// CHECK: cf.br
!byte = !simulation.logic<8>
!byteref = !simulation.ref<!byte>
!clk = !simulation.ref<i1>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  native.debug_timing,
  schedule.native_scheduler = 1 : i32
} {
  simulation.design @clock_read {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i1 design
    simulation.storage.decl 2 in 0 : i1 design
    simulation.storage.decl 3 in 0 : !byte design
    simulation.storage.decl 4 in 0 : !byte design
    simulation.storage.decl 5 in 0 : !byte design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 continuous hierarchy "copy_i"
    simulation.code_unit.decl 4 in 0 continuous hierarchy "copy_j"
    simulation.code_unit.decl 5 in 0 always_ff hierarchy "flop"
    simulation.code_unit.decl 6 in 0 always_ff hierarchy "checker"
    simulation.code_unit.decl 7 in 0 always_ff hierarchy "peer_reader"
    simulation.code_unit.decl 8 in 0 always_ff hierarchy "clock_writer"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !clk
      %clk_i = simulation.context.storage %ctx[1] : !clk
      %clk_j = simulation.context.storage %ctx[2] : !clk
      %q = simulation.context.storage %ctx[3] : !byteref
      %data = simulation.context.storage %ctx[4] : !byteref
      %r = simulation.context.storage %ctx[5] : !byteref
      %p0 = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !clk -> !simulation.process
      %p1 = simulation.spawn @copy_i(%ctx, %clk, %clk_i) : !simulation.context, !clk, !clk -> !simulation.process
      %p2 = simulation.spawn @copy_j(%ctx, %clk, %clk_j) : !simulation.context, !clk, !clk -> !simulation.process
      %p3 = simulation.spawn @flop(%ctx, %clk_i, %q) : !simulation.context, !clk, !byteref -> !simulation.process
      %p4 = simulation.spawn @checker(%ctx, %clk_i, %data) : !simulation.context, !clk, !byteref -> !simulation.process
      %p5 = simulation.spawn @peer_reader(%ctx, %clk_i, %clk_j, %r) : !simulation.context, !clk, !clk, !byteref -> !simulation.process
      %p6 = simulation.spawn @clock_writer(%ctx, %clk_i, %clk) : !simulation.context, !clk, !clk -> !simulation.process
      simulation.return
    }
    simulation.func private @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clk {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      %five = simulation.time.constant 5
      %true = arith.constant true
      cf.br ^wait
    ^wait:
      simulation.suspend.delay %five to ^toggle
    ^toggle:
      %v = simulation.ref.load %clk : !clk -> i1
      %t = arith.xori %v, %true : i1
      simulation.ref.store %t to %clk : i1, !clk
      cf.br ^wait
    }
    simulation.func private @copy_i(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !clk {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %dst: !clk {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 3 : i64} {
      cf.br ^copy
    ^copy:
      %v = simulation.ref.load %src : !clk -> i1
      simulation.ref.store %v to %dst : i1, !clk
      simulation.suspend.change %src to ^copy : !clk
    }
    simulation.func private @copy_j(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !clk {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %dst: !clk {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 4 : i64} {
      cf.br ^copy
    ^copy:
      %v = simulation.ref.load %src : !clk -> i1
      simulation.ref.store %v to %dst : i1, !clk
      simulation.suspend.change %src to ^copy : !clk
    }
    simulation.func private @flop(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clk {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %q: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 5 : i64} {
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !byte
      cf.br ^wait
    ^wait:
      simulation.suspend.edge negedge %clk to ^body : !clk
    ^body:
      %old = simulation.ref.load %q : !byteref -> !byte
      %next = simulation.logic.binary add %old, %one : !byte
      simulation.nba.enqueue %next to %q : (!byte, !byteref) -> ()
      cf.br ^wait
    }
    // Prints its own clock: a read of the copy it waits on.
    simulation.func private @checker(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clk {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %data: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 6 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge negedge %clk to ^body : !clk
    ^body:
      %d = simulation.ref.load %data : !byteref -> !byte
      %c = simulation.ref.load %clk : !clk -> i1
      %fmt = simulation.bytes.constant "data %0d clk %b"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%fmt, %d, %c) newline = true radix = <decimal> flags = [0, 0, 0] {scope = "checker"} : !simulation.bytes, !byte, i1
      cf.br ^wait
    }
    // Reads another copy of the same clock: it may be stale, so it stays out.
    simulation.func private @peer_reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clk {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %peer: !clk {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %r: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 7 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge negedge %clk to ^body : !clk
    ^body:
      %p = simulation.ref.load %peer : !clk -> i1
      %v = simulation.logic.from_bits %p : i1 -> !simulation.logic<1>
      %old = simulation.ref.load %r : !byteref -> !byte
      %wide = simulation.logic.resize %v signed = false : !simulation.logic<1> -> !byte
      %next = simulation.logic.binary add %old, %wide : !byte
      simulation.nba.enqueue %next to %r : (!byte, !byteref) -> ()
      cf.br ^wait
    }
    // Writes the clock source: never a kernel member.
    simulation.func private @clock_writer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clk {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %peer: !clk {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 8 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge negedge %clk to ^body : !clk
    ^body:
      %c = simulation.ref.load %clk : !clk -> i1
      simulation.nba.enqueue %c to %peer : (i1, !clk) -> ()
      cf.br ^wait
    }
  }
}
