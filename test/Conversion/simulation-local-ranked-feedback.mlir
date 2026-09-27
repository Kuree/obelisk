// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' -o %t.group.mlir
// RUN: FileCheck %s --check-prefix=GROUP < %t.group.mlir

// Runtime behavior is checked in ../Runtime/simulation-local-ranked-feedback.test.

// A static sensitivity SCC is not a reason to exclude all of its actors.
// Forward segments remain native groups and the shared loop handles their
// backward publication. The OR gate breaks the value-level loop at runtime.
// GROUP: simulation.spawn @__obelisk_region_kernel_
// GROUP: simulation.func private @__obelisk_region_kernel_
// GROUP-SAME: schedule.native.region_body

!bit = !simulation.logic<1>
!ref = !simulation.ref<!bit>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 1 : i32
} {
  simulation.design @feedback {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !bit design
    simulation.storage.decl 2 in 0 : !bit design
    simulation.storage.decl 3 in 0 : !bit design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "a"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "b"
    simulation.code_unit.decl 4 in 0 always_comb hierarchy "c"
    simulation.code_unit.decl 5 in 0 always_comb hierarchy "d"
    simulation.code_unit.decl 6 in 0 initial hierarchy "report"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %a = simulation.context.storage %ctx[0] : !ref
      %b = simulation.context.storage %ctx[1] : !ref
      %c = simulation.context.storage %ctx[2] : !ref
      %d = simulation.context.storage %ctx[3] : !ref
      %pa = simulation.spawn @a(%ctx, %d, %a) : !simulation.context, !ref, !ref -> !simulation.process
      %pb = simulation.spawn @b(%ctx, %a, %b) : !simulation.context, !ref, !ref -> !simulation.process
      %pc = simulation.spawn @c(%ctx, %b, %c) : !simulation.context, !ref, !ref -> !simulation.process
      %pd = simulation.spawn @d(%ctx, %c, %d) : !simulation.context, !ref, !ref -> !simulation.process
      %pr = simulation.spawn @report(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @a(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}, %output: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %old = simulation.ref.load %input : !ref -> !bit
      %one = simulation.logic.constant true, false : !bit
      %next = simulation.logic.binary or %old, %one : !bit
      simulation.ref.store %next to %output : !bit, !ref
      simulation.suspend.change %input to ^body : !ref
    }
    simulation.func @b(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %output: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %next = simulation.ref.load %input : !ref -> !bit
      simulation.ref.store %next to %output : !bit, !ref
      simulation.suspend.change %input to ^body : !ref
    }
    simulation.func @c(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %output: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 4 : i64} {
      cf.br ^body
    ^body:
      %next = simulation.ref.load %input : !ref -> !bit
      simulation.ref.store %next to %output : !bit, !ref
      simulation.suspend.change %input to ^body : !ref
    }
    simulation.func @d(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %output: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 5 : i64} {
      cf.br ^body
    ^body:
      %next = simulation.ref.load %input : !ref -> !bit
      simulation.ref.store %next to %output : !bit, !ref
      simulation.suspend.change %input to ^body : !ref
    }
    simulation.func @report(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^done
    ^done:
      %a = simulation.context.storage %ctx[0] : !ref
      %b = simulation.context.storage %ctx[1] : !ref
      %c = simulation.context.storage %ctx[2] : !ref
      %d = simulation.context.storage %ctx[3] : !ref
      %av = simulation.ref.load %a : !ref -> !bit
      %bv = simulation.ref.load %b : !ref -> !bit
      %cv = simulation.ref.load %c : !ref -> !bit
      %dv = simulation.ref.load %d : !ref -> !bit
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "settled %b %b %b %b"
      simulation.display %ctx to %stdout(%format, %av, %bv, %cv, %dv) newline = true radix = <binary> flags = [0, 0, 0, 0, 0] : !simulation.bytes, !bit, !bit, !bit, !bit
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
  }
}
