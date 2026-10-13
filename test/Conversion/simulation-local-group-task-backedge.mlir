// RUN: obelisk-opt %s --split-input-file --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s

// Backedge variants of simulation-local-group-task-runtime.mlir, reduced to
// its two clocked actors, which share one activation unchanged. Entry values
// threaded through waits must be proven invariant, never assumed invariant
// merely because the module requests Eval; changing backedge values retain
// their original actors.

// A changing backedge value keeps both clocked actors separate.
// CHECK-LABEL: simulation.design @changing
// CHECK-NOT: __obelisk_fused_
// CHECK: simulation.func private @a(
// CHECK: arith.addi
// CHECK: simulation.func private @b(
// CHECK: arith.addi
// CHECK-NOT: __obelisk_fused_

!ref = !simulation.ref<i32>
!clockref = !simulation.ref<i1>
module attributes {schedule.native_scheduler = 1 : i32} {
  simulation.design @changing {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.storage.decl 2 in 0 : i32 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "a"
    simulation.code_unit.decl 3 in 0 always hierarchy "b"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !clockref
      %a = simulation.context.storage %ctx[1] : !ref
      %b = simulation.context.storage %ctx[2] : !ref
      %pa = simulation.spawn @a(%ctx, %clk, %a) : !simulation.context, !clockref, !ref -> !simulation.process
      %pb = simulation.spawn @b(%ctx, %clk, %b) : !simulation.context, !clockref, !ref -> !simulation.process
      simulation.return
    }
    simulation.func private @a(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clockref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %out: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      %condition = arith.constant true
      %one = arith.constant 1 : i32
      cf.br ^wait(%one : i32)
    ^wait(%state: i32):
      simulation.suspend.edge posedge %clk to ^body(%state : i32) : !clockref
    ^body(%carry: i32):
      %next = arith.addi %carry, %one : i32
      simulation.nba.enqueue %carry to %out : (i32, !ref) -> ()
      cf.br ^wait(%next : i32)
    }
    simulation.func private @b(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clockref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %out: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      %condition = arith.constant true
      %two = arith.constant 2 : i32
      %one = arith.constant 1 : i32
      cf.br ^wait(%two : i32)
    ^wait(%state: i32):
      %waitvalue = arith.constant 2 : i32
      simulation.suspend.edge posedge %clk to ^body(%waitvalue : i32) : !clockref
    ^body(%carry: i32):
      %next = arith.addi %carry, %one : i32
      simulation.nba.enqueue %carry to %out : (i32, !ref) -> ()
      cf.br ^wait(%state : i32)
    }
  }
}

// -----

// Requesting Eval does not make the changing entry value invariant.
// CHECK-LABEL: simulation.design @changing_eval
// CHECK-NOT: __obelisk_fused_
// CHECK: simulation.func private @a(
// CHECK: arith.addi
// CHECK: simulation.func private @b(
// CHECK: arith.addi
// CHECK-NOT: __obelisk_fused_

!ref = !simulation.ref<i32>
!clockref = !simulation.ref<i1>
module attributes {schedule.native_scheduler = 3 : i32} {
  simulation.design @changing_eval {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.storage.decl 2 in 0 : i32 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "a"
    simulation.code_unit.decl 3 in 0 always hierarchy "b"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !clockref
      %a = simulation.context.storage %ctx[1] : !ref
      %b = simulation.context.storage %ctx[2] : !ref
      %pa = simulation.spawn @a(%ctx, %clk, %a) : !simulation.context, !clockref, !ref -> !simulation.process
      %pb = simulation.spawn @b(%ctx, %clk, %b) : !simulation.context, !clockref, !ref -> !simulation.process
      simulation.return
    }
    simulation.func private @a(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clockref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %out: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      %condition = arith.constant true
      %one = arith.constant 1 : i32
      cf.br ^wait(%one : i32)
    ^wait(%state: i32):
      simulation.suspend.edge posedge %clk to ^body(%state : i32) : !clockref
    ^body(%carry: i32):
      %next = arith.addi %carry, %one : i32
      simulation.nba.enqueue %carry to %out : (i32, !ref) -> ()
      cf.br ^wait(%next : i32)
    }
    simulation.func private @b(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clockref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %out: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      %condition = arith.constant true
      %two = arith.constant 2 : i32
      %one = arith.constant 1 : i32
      cf.br ^wait(%two : i32)
    ^wait(%state: i32):
      %waitvalue = arith.constant 2 : i32
      simulation.suspend.edge posedge %clk to ^body(%waitvalue : i32) : !clockref
    ^body(%carry: i32):
      %next = arith.addi %carry, %one : i32
      simulation.nba.enqueue %carry to %out : (i32, !ref) -> ()
      cf.br ^wait(%state : i32)
    }
  }
}

// -----

// Both edges of a conditional backedge carry the same invariant value.
// CHECK-LABEL: simulation.design @duplicate
// CHECK: simulation.spawn @__obelisk_fused_
// CHECK-NOT: simulation.func private @a(
// CHECK-NOT: simulation.func private @b(
// CHECK: simulation.func private @__obelisk_fused_
// CHECK-SAME: schedule.native.region_body

!ref = !simulation.ref<i32>
!clockref = !simulation.ref<i1>
module attributes {schedule.native_scheduler = 1 : i32} {
  simulation.design @duplicate {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.storage.decl 2 in 0 : i32 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "a"
    simulation.code_unit.decl 3 in 0 always hierarchy "b"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !clockref
      %a = simulation.context.storage %ctx[1] : !ref
      %b = simulation.context.storage %ctx[2] : !ref
      %pa = simulation.spawn @a(%ctx, %clk, %a) : !simulation.context, !clockref, !ref -> !simulation.process
      %pb = simulation.spawn @b(%ctx, %clk, %b) : !simulation.context, !clockref, !ref -> !simulation.process
      simulation.return
    }
    simulation.func private @a(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clockref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %out: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      %condition = arith.constant true
      %one = arith.constant 1 : i32
      cf.br ^wait(%one : i32)
    ^wait(%state: i32):
      simulation.suspend.edge posedge %clk to ^body(%state : i32) : !clockref
    ^body(%carry: i32):
      %next = arith.addi %carry, %one : i32
      simulation.nba.enqueue %carry to %out : (i32, !ref) -> ()
      cf.cond_br %condition, ^wait(%carry : i32), ^wait(%carry : i32)
    }
    simulation.func private @b(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clockref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %out: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      %condition = arith.constant true
      %two = arith.constant 2 : i32
      %one = arith.constant 1 : i32
      cf.br ^wait(%two : i32)
    ^wait(%state: i32):
      %waitvalue = arith.constant 2 : i32
      simulation.suspend.edge posedge %clk to ^body(%waitvalue : i32) : !clockref
    ^body(%carry: i32):
      %next = arith.addi %carry, %one : i32
      simulation.nba.enqueue %carry to %out : (i32, !ref) -> ()
      cf.br ^wait(%state : i32)
    }
  }
}

// -----

// One edge of a conditional backedge carries the changing value.
// CHECK-LABEL: simulation.design @conflicting
// CHECK-NOT: __obelisk_fused_
// CHECK: simulation.func private @a(
// CHECK: arith.addi
// CHECK: simulation.func private @b(
// CHECK: arith.addi
// CHECK-NOT: __obelisk_fused_

!ref = !simulation.ref<i32>
!clockref = !simulation.ref<i1>
module attributes {schedule.native_scheduler = 1 : i32} {
  simulation.design @conflicting {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.storage.decl 2 in 0 : i32 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "a"
    simulation.code_unit.decl 3 in 0 always hierarchy "b"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !clockref
      %a = simulation.context.storage %ctx[1] : !ref
      %b = simulation.context.storage %ctx[2] : !ref
      %pa = simulation.spawn @a(%ctx, %clk, %a) : !simulation.context, !clockref, !ref -> !simulation.process
      %pb = simulation.spawn @b(%ctx, %clk, %b) : !simulation.context, !clockref, !ref -> !simulation.process
      simulation.return
    }
    simulation.func private @a(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clockref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %out: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      %condition = arith.constant true
      %one = arith.constant 1 : i32
      cf.br ^wait(%one : i32)
    ^wait(%state: i32):
      simulation.suspend.edge posedge %clk to ^body(%state : i32) : !clockref
    ^body(%carry: i32):
      %next = arith.addi %carry, %one : i32
      simulation.nba.enqueue %carry to %out : (i32, !ref) -> ()
      cf.cond_br %condition, ^wait(%carry : i32), ^wait(%next : i32)
    }
    simulation.func private @b(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clockref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %out: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      %condition = arith.constant true
      %two = arith.constant 2 : i32
      %one = arith.constant 1 : i32
      cf.br ^wait(%two : i32)
    ^wait(%state: i32):
      %waitvalue = arith.constant 2 : i32
      simulation.suspend.edge posedge %clk to ^body(%waitvalue : i32) : !clockref
    ^body(%carry: i32):
      %next = arith.addi %carry, %one : i32
      simulation.nba.enqueue %carry to %out : (i32, !ref) -> ()
      cf.br ^wait(%state : i32)
    }
  }
}

// -----

// An actor that writes its own clock cannot be rearmed by the group.
// CHECK-LABEL: simulation.design @clock_write
// CHECK-NOT: __obelisk_fused_
// CHECK: simulation.func private @a(
// CHECK: arith.addi
// CHECK: simulation.func private @b(
// CHECK: arith.addi
// CHECK-NOT: __obelisk_fused_

!ref = !simulation.ref<i32>
!clockref = !simulation.ref<i1>
module attributes {schedule.native_scheduler = 1 : i32} {
  simulation.design @clock_write {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.storage.decl 2 in 0 : i32 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "a"
    simulation.code_unit.decl 3 in 0 always hierarchy "b"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !clockref
      %a = simulation.context.storage %ctx[1] : !ref
      %b = simulation.context.storage %ctx[2] : !ref
      %pa = simulation.spawn @a(%ctx, %clk, %a) : !simulation.context, !clockref, !ref -> !simulation.process
      %pb = simulation.spawn @b(%ctx, %clk, %b) : !simulation.context, !clockref, !ref -> !simulation.process
      simulation.return
    }
    simulation.func private @a(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clockref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %out: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      %condition = arith.constant true
      %one = arith.constant 1 : i32
      cf.br ^wait(%one : i32)
    ^wait(%state: i32):
      simulation.suspend.edge posedge %clk to ^body(%state : i32) : !clockref
    ^body(%carry: i32):
      %next = arith.addi %carry, %one : i32
      simulation.ref.store %condition to %clk : i1, !clockref
      simulation.nba.enqueue %carry to %out : (i32, !ref) -> ()
      cf.br ^wait(%carry : i32)
    }
    simulation.func private @b(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clockref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %out: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      %condition = arith.constant true
      %two = arith.constant 2 : i32
      %one = arith.constant 1 : i32
      cf.br ^wait(%two : i32)
    ^wait(%state: i32):
      %waitvalue = arith.constant 2 : i32
      simulation.suspend.edge posedge %clk to ^body(%waitvalue : i32) : !clockref
    ^body(%carry: i32):
      %next = arith.addi %carry, %one : i32
      simulation.ref.store %condition to %clk : i1, !clockref
      simulation.nba.enqueue %carry to %out : (i32, !ref) -> ()
      cf.br ^wait(%state : i32)
    }
  }
}
