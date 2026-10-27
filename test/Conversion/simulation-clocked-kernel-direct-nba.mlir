// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,talos-sim-fuse-clocked-kernels))' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: FileCheck %s --check-prefix=MEMBERS < %t.mlir

// IEEE 1800-2023 4.5/4.7: the edge-triggered processes of one clock, including
// those waiting on a port copy of it (23.3.3, 4.9.6), run as one activation.
// An NBA whose root only combinational logic and later delay activations read
// becomes a store at its statement (4.3). A read after that store in the same
// activation uses a snapshot of the incoming value (4.9.4): `b <= a` still
// assigns the old `a`. A root an edge observer waits on keeps its NBA, because
// the observer could see the earlier update (9.4.2).

!bit = !simulation.logic<1>
!byte = !simulation.logic<8>
!bitref = !simulation.ref<!bit>
!byteref = !simulation.ref<!byte>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 1 : i32
} {
  simulation.design @clocked {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !bit design
    simulation.storage.decl 2 in 0 : !byte design
    simulation.storage.decl 3 in 0 : !byte design
    simulation.storage.decl 4 in 0 : !byte design
    simulation.storage.decl 5 in 0 : !byte design
    simulation.storage.decl 6 in 0 : !bit design
    simulation.storage.decl 7 in 0 : !bit design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.code_unit.decl 3 in 0 port_input hierarchy "copy"
    simulation.code_unit.decl 4 in 0 always_ff hierarchy "swap"
    simulation.code_unit.decl 5 in 0 always_ff hierarchy "sample"
    simulation.code_unit.decl 6 in 0 always_ff hierarchy "mirror"
    simulation.code_unit.decl 7 in 0 always_comb hierarchy "sum"
    simulation.code_unit.decl 8 in 0 always hierarchy "observer"
    simulation.code_unit.decl 9 in 0 initial hierarchy "report"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !bitref
      %port = simulation.context.storage %ctx[1] : !bitref
      %a = simulation.context.storage %ctx[2] : !byteref
      %b = simulation.context.storage %ctx[3] : !byteref
      %s = simulation.context.storage %ctx[4] : !byteref
      %c = simulation.context.storage %ctx[5] : !byteref
      %d = simulation.context.storage %ctx[6] : !bitref
      %seen = simulation.context.storage %ctx[7] : !bitref
      %init = simulation.logic.constant 0 : i8, 0 : i8 : !byte
      simulation.ref.store %init to %a : !byte, !byteref
      simulation.ref.store %init to %b : !byte, !byteref
      %p0 = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !bitref -> !simulation.process
      %p1 = simulation.spawn @copy(%ctx, %clk, %port) : !simulation.context, !bitref, !bitref -> !simulation.process
      %p2 = simulation.spawn @swap(%ctx, %clk, %a, %b) : !simulation.context, !bitref, !byteref, !byteref -> !simulation.process
      %p3 = simulation.spawn @sample(%ctx, %port, %s, %c) : !simulation.context, !bitref, !byteref, !byteref -> !simulation.process
      %p4 = simulation.spawn @mirror(%ctx, %clk, %a, %d) : !simulation.context, !bitref, !byteref, !bitref -> !simulation.process
      %p5 = simulation.spawn @sum(%ctx, %a, %b, %s) : !simulation.context, !byteref, !byteref, !byteref -> !simulation.process
      %p6 = simulation.spawn @observer(%ctx, %d, %seen) : !simulation.context, !bitref, !bitref -> !simulation.process
      %p7 = simulation.spawn @report(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    // The clock changes only in the NBA region.
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %delay = simulation.time.constant 5
      %one = simulation.logic.constant true, false : !bit
      %zero = simulation.logic.constant false, false : !bit
      cf.br ^low
    ^low:
      simulation.suspend.delay %delay to ^rise
    ^rise:
      simulation.nba.enqueue %one to %clk : (!bit, !bitref) -> ()
      simulation.suspend.delay %delay to ^fall
    ^fall:
      simulation.nba.enqueue %zero to %clk : (!bit, !bitref) -> ()
      cf.br ^low
    }
    simulation.func @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %port: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 9 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %value = simulation.ref.load %clk : !bitref -> !bit
      simulation.ref.store %value to %port {simulation.continuous_store} : !bit, !bitref
      simulation.suspend.change %clk to ^body : !bitref
    }
    // The kernel waits on the clock most members use and runs `mirror`, which
    // reads `a`, before `swap` stores it.
    // CHECK: simulation.spawn @__talos_clocked_0_1_0(
    // CHECK-LABEL: simulation.func private @__talos_clocked_0_1_0
    // CHECK: simulation.suspend.edge posedge
    // CHECK: %[[OLD:.*]] = simulation.ref.load %[[A:arg[0-9]+]] : !simulation.ref<!simulation.logic<8>>
    // CHECK-NEXT: %[[SNAP:.*]] = simulation.ref.alloc %[[OLD]]
    // CHECK: simulation.logic.extract
    // CHECK-NEXT: simulation.nba.enqueue
    // CHECK: simulation.ref.store %{{.*}} to %{{.*}} : !simulation.logic<8>
    // CHECK: simulation.ref.store %{{.*}} to %[[A]] : !simulation.logic<8>
    // CHECK-NEXT: %[[PREV:.*]] = simulation.ref.load %[[SNAP]]
    // CHECK-NEXT: simulation.ref.store %[[PREV]] to %{{.*}} : !simulation.logic<8>
    // CHECK-NOT: simulation.nba.enqueue
    // CHECK: }
    // MEMBERS-NOT: simulation.func private @swap
    // MEMBERS-NOT: simulation.func private @sample
    // MEMBERS-NOT: simulation.func private @mirror
    simulation.func private @swap(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %a: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %b: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !bitref
    ^body:
      %bv = simulation.ref.load %b : !byteref -> !byte
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !byte
      %next = simulation.logic.binary add %bv, %one : !byte
      simulation.nba.enqueue %next to %a : (!byte, !byteref) -> ()
      %av = simulation.ref.load %a : !byteref -> !byte
      simulation.nba.enqueue %av to %b : (!byte, !byteref) -> ()
      cf.br ^wait
    }
    simulation.func private @sample(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %port: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %s: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}, %c: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %port to ^body : !bitref
    ^body:
      %sv = simulation.ref.load %s : !byteref -> !byte
      simulation.nba.enqueue %sv to %c : (!byte, !byteref) -> ()
      cf.br ^wait
    }
    simulation.func private @mirror(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %a: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %d: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 6 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !bitref
    ^body:
      %av = simulation.ref.load %a : !byteref -> !byte
      %low = simulation.logic.extract %av from 0 : !byte -> !bit
      simulation.nba.enqueue %low to %d : (!bit, !bitref) -> ()
      cf.br ^wait
    }
    simulation.func @sum(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %b: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}, %s: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 7 : i64} {
      cf.br ^body
    ^body:
      %av = simulation.ref.load %a : !byteref -> !byte
      %bv = simulation.ref.load %b : !byteref -> !byte
      %sv = simulation.logic.binary add %av, %bv : !byte
      simulation.ref.store %sv to %s : !byte, !byteref
      simulation.suspend.any %a, %b edges [0, 0] to ^body : !byteref, !byteref
    }
    // An edge observer of `d` keeps the NBA of `d`.
    simulation.func @observer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %d: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64}, %seen: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 7 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 8 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %d to ^body : !bitref
    ^body:
      %one = simulation.logic.constant true, false : !bit
      simulation.ref.store %one to %seen : !bit, !bitref
      cf.br ^wait
    }
    // Activations resumed by a positive delay finish before the slot's first
    // NBA region, so they never see a direct store early.
    simulation.func @report(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9 : i64} {
      %delay = simulation.time.constant 100
      simulation.suspend.delay %delay to ^done
    ^done:
      %a = simulation.context.storage %ctx[2] : !byteref
      %c = simulation.context.storage %ctx[5] : !byteref
      %av = simulation.ref.load %a : !byteref -> !byte
      %cv = simulation.ref.load %c : !byteref -> !byte
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "%d %d"
      simulation.display %ctx to %stdout(%format, %av, %cv) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, !byte, !byte
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
  }
}
