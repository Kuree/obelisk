// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,talos-sim-materialize-compute-fusion))' | FileCheck %s
// RUN: talos-opt %s --pass-pipeline='builtin.module(convert-scf-to-cf,simulation.design(talos-sim-build-compute-graph,talos-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=CFG

// LRM 6.21: threaded identity does not imply persistent contents. A full
// activation definition must reach every private read on every path before scratch can be
// reconstructed without the coroutine frame. A partial or late reset cannot
// discard state from the previous activation.
!ref = !simulation.ref<i128>
module attributes {schedule.native_scheduler = 3 : i32} {
  simulation.design @scratch_continuations {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i128 design
    simulation.storage.decl 1 in 0 : i128 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "reset"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "carried"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "late_reset"
    simulation.code_unit.decl 4 in 0 always_comb hierarchy "partial_reset"
    simulation.code_unit.decl 5 in 0 always_comb hierarchy "forwarded_reset"

    simulation.code_unit.decl 6 in 0 always_comb hierarchy "branch_reset"
    simulation.code_unit.decl 7 in 0 always_comb hierarchy "one_branch_reset"
    simulation.code_unit.decl 8 in 0 always_comb hierarchy "region_reset"
    simulation.code_unit.decl 9 in 0 always_comb hierarchy "loop_reset"
    simulation.code_unit.decl 10 in 0 always_comb hierarchy "zero_trip_reset"

    simulation.func @reset(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %output: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %initial = simulation.ref.load %input : !ref -> i128
      %local = simulation.ref.alloc %initial : i128 -> !ref
      cf.br ^body(%local : !ref)
    ^body(%scratch: !ref):
      %next = simulation.ref.load %input : !ref -> i128
      simulation.ref.store %next to %scratch : i128, !ref
      cf.br ^use(%scratch : !ref)
    ^use(%alias: !ref):
      %slice = simulation.ref.extract %alias from 65 : !ref -> !simulation.ref<i7>
      %value = simulation.ref.load %slice : !simulation.ref<i7> -> i7
      %wide = arith.extui %value : i7 to i128
      simulation.ref.store %wide to %output : i128, !ref
      simulation.suspend.change %input to ^body(%alias : !ref) : !ref
    }

    simulation.func @forwarded_reset(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %output: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 5 : i64} {
      %initial = simulation.ref.load %input : !ref -> i128
      %local = simulation.ref.alloc %initial : i128 -> !ref
      cf.br ^use(%local : !ref)
    ^body(%scratch: !ref):
      %next = simulation.ref.load %input : !ref -> i128
      simulation.ref.store %next to %scratch : i128, !ref
      cf.br ^use(%scratch : !ref)
    ^use(%alias: !ref):
      %slice = simulation.ref.extract %alias from 65 : !ref -> !simulation.ref<i7>
      %value = simulation.ref.load %slice : !simulation.ref<i7> -> i7
      %wide = arith.extui %value : i7 to i128
      simulation.ref.store %wide to %output : i128, !ref
      simulation.suspend.change %input to ^body(%alias : !ref) : !ref
    }

    simulation.func @carried(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %output: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %initial = arith.constant 0 : i128
      %local = simulation.ref.alloc %initial : i128 -> !ref
      cf.br ^body(%local : !ref)
    ^body(%scratch: !ref):
      %old = simulation.ref.load %scratch : !ref -> i128
      %one = arith.constant 1 : i128
      %next = arith.addi %old, %one : i128
      simulation.ref.store %next to %scratch : i128, !ref
      simulation.ref.store %next to %output : i128, !ref
      simulation.suspend.change %input to ^body(%scratch : !ref) : !ref
    }

    simulation.func @late_reset(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %output: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      %initial = arith.constant 0 : i128
      %local = simulation.ref.alloc %initial : i128 -> !ref
      cf.br ^body(%local : !ref)
    ^body(%scratch: !ref):
      %old = simulation.ref.load %scratch : !ref -> i128
      %next = simulation.ref.load %input : !ref -> i128
      simulation.ref.store %next to %scratch : i128, !ref
      simulation.ref.store %old to %output : i128, !ref
      simulation.suspend.change %input to ^body(%scratch : !ref) : !ref
    }

    simulation.func @partial_reset(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %output: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 4 : i64} {
      %initial = arith.constant 0 : i128
      %local = simulation.ref.alloc %initial : i128 -> !ref
      cf.br ^body(%local : !ref)
    ^body(%scratch: !ref):
      %slice = simulation.ref.extract %scratch from 65 : !ref -> !simulation.ref<i7>
      %zero = arith.constant 0 : i7
      simulation.ref.store %zero to %slice : i7, !simulation.ref<i7>
      %old = simulation.ref.load %scratch : !ref -> i128
      simulation.ref.store %old to %output : i128, !ref
      simulation.suspend.change %input to ^body(%scratch : !ref) : !ref
    }
    simulation.func @branch_reset(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %output: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 6 : i64} {
      %initial = arith.constant 0 : i128
      %local = simulation.ref.alloc %initial : i128 -> !ref
      cf.br ^body(%local : !ref)
    ^body(%scratch: !ref):
      %value = simulation.ref.load %input : !ref -> i128
      %choose = arith.trunci %value : i128 to i1
      cf.cond_br %choose, ^left, ^right
    ^left:
      simulation.ref.store %value to %scratch : i128, !ref
      cf.br ^use
    ^right:
      simulation.ref.store %value to %scratch : i128, !ref
      cf.br ^use
    ^use:
      %read = simulation.ref.load %scratch : !ref -> i128
      simulation.ref.store %read to %output : i128, !ref
      simulation.suspend.change %input to ^body(%scratch : !ref) : !ref
    }
    simulation.func @one_branch_reset(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %output: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 7 : i64} {
      %initial = arith.constant 0 : i128
      %local = simulation.ref.alloc %initial : i128 -> !ref
      cf.br ^body(%local : !ref)
    ^body(%scratch: !ref):
      %value = simulation.ref.load %input : !ref -> i128
      %choose = arith.trunci %value : i128 to i1
      cf.cond_br %choose, ^left, ^right
    ^left:
      simulation.ref.store %value to %scratch : i128, !ref
      cf.br ^use
    ^right:
      cf.br ^use
    ^use:
      %read = simulation.ref.load %scratch : !ref -> i128
      simulation.ref.store %read to %output : i128, !ref
      simulation.suspend.change %input to ^body(%scratch : !ref) : !ref
    }
    simulation.func @region_reset(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %output: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 8 : i64} {
      %initial = arith.constant 0 : i128
      %local = simulation.ref.alloc %initial : i128 -> !ref
      cf.br ^body(%local : !ref)
    ^body(%scratch: !ref):
      %value = simulation.ref.load %input : !ref -> i128
      %choose = arith.trunci %value : i128 to i1
      scf.if %choose {
        simulation.ref.store %value to %scratch : i128, !ref
      } else {
        simulation.ref.store %value to %scratch : i128, !ref
      }
      %read = simulation.ref.load %scratch : !ref -> i128
      simulation.ref.store %read to %output : i128, !ref
      simulation.suspend.change %input to ^body(%scratch : !ref) : !ref
    }
    simulation.func @loop_reset(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %output: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 9 : i64} {
      %initial = arith.constant 0 : i128
      %local = simulation.ref.alloc %initial : i128 -> !ref
      cf.br ^body(%local : !ref)
    ^body(%scratch: !ref):
      %value = simulation.ref.load %input : !ref -> i128
      %count = arith.trunci %value : i128 to i32
      %zero = arith.constant 0 : i32
      simulation.ref.store %value to %scratch : i128, !ref
      cf.br ^head(%zero : i32)
    // Layout order differs from dominance order on purpose.
    ^use:
      %read = simulation.ref.load %scratch : !ref -> i128
      simulation.ref.store %read to %output : i128, !ref
      simulation.suspend.change %input to ^body(%scratch : !ref) : !ref
    ^step:
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^head(%i: i32):
      %more = arith.cmpi ult, %i, %count : i32
      cf.cond_br %more, ^step, ^use
    }
    simulation.func @zero_trip_reset(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %output: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 10 : i64} {
      %initial = arith.constant 0 : i128
      %local = simulation.ref.alloc %initial : i128 -> !ref
      cf.br ^body(%local : !ref)
    ^body(%scratch: !ref):
      %value = simulation.ref.load %input : !ref -> i128
      %count = arith.trunci %value : i128 to i32
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    // Layout order differs from dominance order on purpose.
    ^use:
      %read = simulation.ref.load %scratch : !ref -> i128
      simulation.ref.store %read to %output : i128, !ref
      simulation.suspend.change %input to ^body(%scratch : !ref) : !ref
    ^step:
      simulation.ref.store %value to %scratch : i128, !ref
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^head(%i: i32):
      %more = arith.cmpi ult, %i, %count : i32
      cf.cond_br %more, ^step, ^use
    }
  }
}

// CHECK-LABEL: simulation.func @carried(
// CHECK-NOT: schedule.eval.body
// CHECK: simulation.suspend.change
// CHECK-LABEL: simulation.func @late_reset(
// CHECK-NOT: schedule.eval.body
// CHECK: simulation.suspend.change
// CHECK-LABEL: simulation.func @partial_reset(
// CHECK-NOT: schedule.eval.body
// CHECK: simulation.suspend.change
// CHECK-LABEL: simulation.func @one_branch_reset(
// CHECK-NOT: schedule.eval.body
// CHECK: simulation.suspend.change
// CHECK-LABEL: simulation.func private @reset.__talos_eval_body_
// CHECK-SAME: schedule.eval.reconstructs_continuation
// CHECK: simulation.ref.alloc
// CHECK: simulation.return
// CHECK-LABEL: simulation.func private @forwarded_reset.__talos_eval_body_
// CHECK-SAME: schedule.eval.reconstructs_continuation
// CHECK: simulation.return
// CHECK-LABEL: simulation.func private @branch_reset.__talos_eval_body_
// CHECK-SAME: schedule.eval.reconstructs_continuation
// CHECK: simulation.return
// CHECK-LABEL: simulation.func private @region_reset.__talos_eval_body_
// CHECK-SAME: schedule.eval.reconstructs_continuation
// CHECK: scf.if
// CHECK: simulation.return

// CHECK-LABEL: simulation.func private @loop_reset.__talos_eval_body_
// CHECK-SAME: schedule.eval.reconstructs_continuation
// CHECK: simulation.ref.store
// CHECK: simulation.return
// CHECK-NOT: @zero_trip_reset.__talos_eval_body_

// The same semantic admission after lowering structured control flow.
// CFG-LABEL: simulation.func @reset(
// CFG-SAME: schedule.eval.body
// CFG-LABEL: simulation.func @forwarded_reset(
// CFG-SAME: schedule.eval.body
// CFG-LABEL: simulation.func @carried(
// CFG-NOT: schedule.eval.body
// CFG: simulation.suspend.change
// CFG-LABEL: simulation.func @late_reset(
// CFG-NOT: schedule.eval.body
// CFG: simulation.suspend.change
// CFG-LABEL: simulation.func @partial_reset(
// CFG-NOT: schedule.eval.body
// CFG: simulation.suspend.change
// CFG-LABEL: simulation.func @branch_reset(
// CFG-SAME: schedule.eval.body
// CFG-LABEL: simulation.func @one_branch_reset(
// CFG-NOT: schedule.eval.body
// CFG: simulation.suspend.change
// CFG-LABEL: simulation.func @region_reset(
// CFG-SAME: schedule.eval.body
// CFG-LABEL: simulation.func @loop_reset(
// CFG-SAME: schedule.eval.body
// CFG-LABEL: simulation.func @zero_trip_reset(
// CFG-NOT: schedule.eval.body
// CFG: simulation.suspend.change
