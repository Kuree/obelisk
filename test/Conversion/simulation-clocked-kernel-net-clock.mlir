// RUN: talos-opt %s --split-input-file --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,talos-sim-fuse-clocked-kernels))' -o %t.mlir 2> %t.log
// RUN: FileCheck %s --check-prefix=REPORT < %t.log
// RUN: FileCheck %s < %t.mlir

// A clock reaches a module as a net: a 2-state variable connected to a
// 4-state input port (IEEE 1800-2023 23.3.3) and `wire n = ~clk` (10.3.1).
// Each is a continuous assignment that updates only on a change (10.3.2), so
// the nets follow the variable; `~` swaps posedge and negedge (Table 9-2,
// Table 11-15). A net starts at z (6.6) and a 2-state variable at 0
// (Table 6-7), so at time zero (4.9.1, 23.3.3.3) the copies fall from z to
// 0: a negedge the variable never makes. Posedge members of the variable and
// the nets share a kernel; negedge members of the nets and the posedge member
// of the complement share another; the variable's negedge member stays apart.

// REPORT-DAG: talos clocked kernel: __talos_clocked_0_1_0 members=2
// REPORT-DAG: talos clocked kernel: __talos_clocked_0_2_0 members=2
// REPORT-DAG: talos clocked kernel: __talos_clocked_0_258_0 members=1

// A kernel waits on one member's own root and edge.
// CHECK-LABEL: simulation.design @late
// CHECK-NOT: simulation.func private @pos_net
// CHECK-LABEL: simulation.func private @__talos_clocked_0_1_0
// CHECK: simulation.suspend.edge posedge %{{.*}} : !simulation.ref<i1>
// CHECK-LABEL: simulation.func private @__talos_clocked_0_2_0
// CHECK: simulation.suspend.edge negedge %{{.*}} : !simulation.net<!simulation.logic<1>>
// CHECK-LABEL: simulation.func private @__talos_clocked_0_258_0
// CHECK: simulation.suspend.edge negedge %{{.*}} : !simulation.ref<i1>
!bit = !simulation.logic<1>
!byte = !simulation.logic<8>
!byteref = !simulation.ref<!byte>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  native.debug_timing,
  schedule.native_scheduler = 1 : i32
} {
  simulation.design @late {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : !byte design
    simulation.storage.decl 2 in 0 : !byte design
    simulation.storage.decl 3 in 0 : !byte design
    simulation.storage.decl 4 in 0 : !byte design
    simulation.storage.decl 5 in 0 : !byte design
    simulation.net.decl 0 in 0 : !bit design
    simulation.net.decl 1 in 0 : !bit design
    simulation.driver.decl 0 in 0 drives 0 : !bit design
    simulation.driver.decl 1 in 0 drives 1 : !bit design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 port_input hierarchy "port"
    simulation.code_unit.decl 4 in 0 continuous hierarchy "invert"
    simulation.code_unit.decl 5 in 0 always_ff hierarchy "pos_net"
    simulation.code_unit.decl 6 in 0 always_ff hierarchy "inv_pos"
    simulation.code_unit.decl 7 in 0 always_ff hierarchy "neg_net"
    simulation.code_unit.decl 8 in 0 always_ff hierarchy "neg_var"
    simulation.code_unit.decl 9 in 0 always_ff hierarchy "pos_var"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %net0 = simulation.context.net %ctx[0] : !simulation.net<!bit>
      %net1 = simulation.context.net %ctx[1] : !simulation.net<!bit>
      %q1 = simulation.context.storage %ctx[1] : !byteref
      %q2 = simulation.context.storage %ctx[2] : !byteref
      %q3 = simulation.context.storage %ctx[3] : !byteref
      %q4 = simulation.context.storage %ctx[4] : !byteref
      %q5 = simulation.context.storage %ctx[5] : !byteref
      %p0 = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !simulation.ref<i1> -> !simulation.process
      %p1 = simulation.spawn @port(%ctx) : !simulation.context -> !simulation.process
      %p2 = simulation.spawn @invert(%ctx) : !simulation.context -> !simulation.process
      %s5 = simulation.spawn @pos_net(%ctx, %net0, %q1) : !simulation.context, !simulation.net<!bit>, !byteref -> !simulation.process
      %s6 = simulation.spawn @inv_pos(%ctx, %net1, %q2) : !simulation.context, !simulation.net<!bit>, !byteref -> !simulation.process
      %s7 = simulation.spawn @neg_net(%ctx, %net0, %q3) : !simulation.context, !simulation.net<!bit>, !byteref -> !simulation.process
      %s8 = simulation.spawn @neg_var(%ctx, %clk, %q4) : !simulation.context, !simulation.ref<i1>, !byteref -> !simulation.process
      %s9 = simulation.spawn @pos_var(%ctx, %clk, %q5) : !simulation.context, !simulation.ref<i1>, !byteref -> !simulation.process
      simulation.return
    }
    simulation.func private @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      %five = simulation.time.constant 5
      %true = arith.constant true
      cf.br ^wait
    ^wait:
      simulation.suspend.delay %five to ^toggle
    ^toggle:
      %v = simulation.ref.load %clk : !simulation.ref<i1> -> i1
      %t = arith.xori %v, %true : i1
      simulation.ref.store %t to %clk : i1, !simulation.ref<i1>
      cf.br ^wait
    }
    simulation.func private @port(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 9 : i32, code_unit_id = 3 : i64} {
      %clk = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<!bit>
      cf.br ^copy
    ^copy:
      %v = simulation.ref.load %clk : !simulation.ref<i1> -> i1
      %logic = simulation.logic.from_bits %v : i1 -> !bit
      simulation.driver.drive %driver = %logic : !simulation.driver<!bit>, !bit
      simulation.suspend.change %clk to ^copy : !simulation.ref<i1>
    }
    simulation.func private @invert(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 4 : i64} {
      %net = simulation.context.net %ctx[0] : !simulation.net<!bit>
      %driver = simulation.context.driver %ctx[1] : !simulation.driver<!bit>
      cf.br ^copy
    ^copy:
      %v = simulation.net.read %net : !simulation.net<!bit> -> !bit
      %n = simulation.logic.unary bit_not %v : (!bit) -> !bit
      simulation.driver.drive %driver = %n : !simulation.driver<!bit>, !bit
      simulation.suspend.change %net to ^copy : !simulation.net<!bit>
    }
    simulation.func private @pos_net(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !simulation.net<!bit> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64}, %q: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 5 : i64} {
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !byte
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !simulation.net<!bit>
    ^body:
      %old = simulation.ref.load %q : !byteref -> !byte
      %next = simulation.logic.binary add %old, %one : !byte
      simulation.nba.enqueue %next to %q : (!byte, !byteref) -> ()
      cf.br ^wait
    }
    simulation.func private @inv_pos(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !simulation.net<!bit> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 1 : i64}, %q: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 6 : i64} {
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !byte
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !simulation.net<!bit>
    ^body:
      %old = simulation.ref.load %q : !byteref -> !byte
      %next = simulation.logic.binary add %old, %one : !byte
      simulation.nba.enqueue %next to %q : (!byte, !byteref) -> ()
      cf.br ^wait
    }
    simulation.func private @neg_net(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !simulation.net<!bit> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64}, %q: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 7 : i64} {
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !byte
      cf.br ^wait
    ^wait:
      simulation.suspend.edge negedge %clk to ^body : !simulation.net<!bit>
    ^body:
      %old = simulation.ref.load %q : !byteref -> !byte
      %next = simulation.logic.binary add %old, %one : !byte
      simulation.nba.enqueue %next to %q : (!byte, !byteref) -> ()
      cf.br ^wait
    }
    simulation.func private @neg_var(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 8 : i64} {
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !byte
      cf.br ^wait
    ^wait:
      simulation.suspend.edge negedge %clk to ^body : !simulation.ref<i1>
    ^body:
      %old = simulation.ref.load %q : !byteref -> !byte
      %next = simulation.logic.binary add %old, %one : !byte
      simulation.nba.enqueue %next to %q : (!byte, !byteref) -> ()
      cf.br ^wait
    }
    simulation.func private @pos_var(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 9 : i64} {
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !byte
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !simulation.ref<i1>
    ^body:
      %old = simulation.ref.load %q : !byteref -> !byte
      %next = simulation.logic.binary add %old, %one : !byte
      simulation.nba.enqueue %next to %q : (!byte, !byteref) -> ()
      cf.br ^wait
    }
  }
}

// -----

// When the variable also changes at time zero, a copy can change twice in
// that phase (z, 0, then 1) and the complement, evaluated after both updates
// (10.3.2, 4.7), may never be 1: the net groups are not equivalent.
// REPORT: talos clocked kernel rejection: net clock source changes at time zero = 2
// CHECK-LABEL: simulation.design @early
// CHECK: simulation.func private @pos_net
// CHECK: simulation.func private @inv_pos
// CHECK: simulation.func private @neg_net
// CHECK-NOT: __talos_clocked_0_1_0
// CHECK: simulation.func private @__talos_clocked_0_258_0
!bit = !simulation.logic<1>
!byte = !simulation.logic<8>
!byteref = !simulation.ref<!byte>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  native.debug_timing,
  schedule.native_scheduler = 1 : i32
} {
  simulation.design @early {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : !byte design
    simulation.storage.decl 2 in 0 : !byte design
    simulation.storage.decl 3 in 0 : !byte design
    simulation.storage.decl 4 in 0 : !byte design
    simulation.storage.decl 5 in 0 : !byte design
    simulation.net.decl 0 in 0 : !bit design
    simulation.net.decl 1 in 0 : !bit design
    simulation.driver.decl 0 in 0 drives 0 : !bit design
    simulation.driver.decl 1 in 0 drives 1 : !bit design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 port_input hierarchy "port"
    simulation.code_unit.decl 4 in 0 continuous hierarchy "invert"
    simulation.code_unit.decl 5 in 0 always_ff hierarchy "pos_net"
    simulation.code_unit.decl 6 in 0 always_ff hierarchy "inv_pos"
    simulation.code_unit.decl 7 in 0 always_ff hierarchy "neg_net"
    simulation.code_unit.decl 8 in 0 always_ff hierarchy "neg_var"
    simulation.code_unit.decl 9 in 0 always_ff hierarchy "pos_var"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %net0 = simulation.context.net %ctx[0] : !simulation.net<!bit>
      %net1 = simulation.context.net %ctx[1] : !simulation.net<!bit>
      %q1 = simulation.context.storage %ctx[1] : !byteref
      %q2 = simulation.context.storage %ctx[2] : !byteref
      %q3 = simulation.context.storage %ctx[3] : !byteref
      %q4 = simulation.context.storage %ctx[4] : !byteref
      %q5 = simulation.context.storage %ctx[5] : !byteref
      %p0 = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !simulation.ref<i1> -> !simulation.process
      %p1 = simulation.spawn @port(%ctx) : !simulation.context -> !simulation.process
      %p2 = simulation.spawn @invert(%ctx) : !simulation.context -> !simulation.process
      %s5 = simulation.spawn @pos_net(%ctx, %net0, %q1) : !simulation.context, !simulation.net<!bit>, !byteref -> !simulation.process
      %s6 = simulation.spawn @inv_pos(%ctx, %net1, %q2) : !simulation.context, !simulation.net<!bit>, !byteref -> !simulation.process
      %s7 = simulation.spawn @neg_net(%ctx, %net0, %q3) : !simulation.context, !simulation.net<!bit>, !byteref -> !simulation.process
      %s8 = simulation.spawn @neg_var(%ctx, %clk, %q4) : !simulation.context, !simulation.ref<i1>, !byteref -> !simulation.process
      %s9 = simulation.spawn @pos_var(%ctx, %clk, %q5) : !simulation.context, !simulation.ref<i1>, !byteref -> !simulation.process
      simulation.return
    }
    simulation.func private @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      %five = simulation.time.constant 5
      %true = arith.constant true
      cf.br ^toggle
    ^toggle:
      %v = simulation.ref.load %clk : !simulation.ref<i1> -> i1
      %t = arith.xori %v, %true : i1
      simulation.ref.store %t to %clk : i1, !simulation.ref<i1>
      simulation.suspend.delay %five to ^toggle
    }
    simulation.func private @port(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 9 : i32, code_unit_id = 3 : i64} {
      %clk = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<!bit>
      cf.br ^copy
    ^copy:
      %v = simulation.ref.load %clk : !simulation.ref<i1> -> i1
      %logic = simulation.logic.from_bits %v : i1 -> !bit
      simulation.driver.drive %driver = %logic : !simulation.driver<!bit>, !bit
      simulation.suspend.change %clk to ^copy : !simulation.ref<i1>
    }
    simulation.func private @invert(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 4 : i64} {
      %net = simulation.context.net %ctx[0] : !simulation.net<!bit>
      %driver = simulation.context.driver %ctx[1] : !simulation.driver<!bit>
      cf.br ^copy
    ^copy:
      %v = simulation.net.read %net : !simulation.net<!bit> -> !bit
      %n = simulation.logic.unary bit_not %v : (!bit) -> !bit
      simulation.driver.drive %driver = %n : !simulation.driver<!bit>, !bit
      simulation.suspend.change %net to ^copy : !simulation.net<!bit>
    }
    simulation.func private @pos_net(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !simulation.net<!bit> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64}, %q: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 5 : i64} {
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !byte
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !simulation.net<!bit>
    ^body:
      %old = simulation.ref.load %q : !byteref -> !byte
      %next = simulation.logic.binary add %old, %one : !byte
      simulation.nba.enqueue %next to %q : (!byte, !byteref) -> ()
      cf.br ^wait
    }
    simulation.func private @inv_pos(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !simulation.net<!bit> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 1 : i64}, %q: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 6 : i64} {
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !byte
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !simulation.net<!bit>
    ^body:
      %old = simulation.ref.load %q : !byteref -> !byte
      %next = simulation.logic.binary add %old, %one : !byte
      simulation.nba.enqueue %next to %q : (!byte, !byteref) -> ()
      cf.br ^wait
    }
    simulation.func private @neg_net(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !simulation.net<!bit> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64}, %q: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 7 : i64} {
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !byte
      cf.br ^wait
    ^wait:
      simulation.suspend.edge negedge %clk to ^body : !simulation.net<!bit>
    ^body:
      %old = simulation.ref.load %q : !byteref -> !byte
      %next = simulation.logic.binary add %old, %one : !byte
      simulation.nba.enqueue %next to %q : (!byte, !byteref) -> ()
      cf.br ^wait
    }
    simulation.func private @neg_var(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 8 : i64} {
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !byte
      cf.br ^wait
    ^wait:
      simulation.suspend.edge negedge %clk to ^body : !simulation.ref<i1>
    ^body:
      %old = simulation.ref.load %q : !byteref -> !byte
      %next = simulation.logic.binary add %old, %one : !byte
      simulation.nba.enqueue %next to %q : (!byte, !byteref) -> ()
      cf.br ^wait
    }
    simulation.func private @pos_var(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 9 : i64} {
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !byte
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !simulation.ref<i1>
    ^body:
      %old = simulation.ref.load %q : !byteref -> !byte
      %next = simulation.logic.binary add %old, %one : !byte
      simulation.nba.enqueue %next to %q : (!byte, !byteref) -> ()
      cf.br ^wait
    }
  }
}
