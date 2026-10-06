// RUN: obelisk-opt %s --mlir-print-op-on-diagnostic=false --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,schedule-plan-combinational-statements{explain=true},obelisk-sim-materialize-compute-fusion))' -o %t 2>&1 | FileCheck %s --check-prefix=REASON
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,schedule-plan-combinational-statements,obelisk-sim-materialize-compute-fusion))' -o %t.quiet
// RUN: diff %t %t.quiet
// RUN: FileCheck %s --check-prefix=KEEP --implicit-check-not=__obelisk_region_kernel_ < %t
// A process SCC can contain both schedulable statement paths and actual
// feedback. Report a directed witness without weakening the acyclicity guard.
// The source-order paths between these two cross-owner edges close the cycle.
// REASON: statement component rejected: structured dependence cycle
// REASON-SAME: cycle edge: @c:{{[0-9]+}} (simulation.ref.store) -> @d:{{[0-9]+}} (simulation.ref.load) via s7[0,8)
// REASON-SAME: cycle edge: @d:{{[0-9]+}} (simulation.ref.store) -> @c:{{[0-9]+}} (simulation.ref.load) via s6[0,8)
// REASON: 0 plans
// KEEP: simulation.spawn @a
// KEEP: simulation.spawn @b
// KEEP: simulation.spawn @c
// KEEP: simulation.spawn @d
// A schedulable request/response pair connected to retained feedback owners.
module {
  simulation.design @refinement {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.storage.decl 4 in 0 : i8 design
    simulation.storage.decl 5 in 0 : i8 design
    simulation.storage.decl 6 in 0 : i8 design
    simulation.storage.decl 7 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "a"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "b"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "c"
    simulation.code_unit.decl 4 in 0 always_comb hierarchy "d"
    simulation.code_unit.decl 6 in 0 root_initializer hierarchy "root"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 6 : i64} {
      %r0 = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %r1 = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %r2 = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %r3 = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %r4 = simulation.context.storage %ctx[4] : !simulation.ref<i8>
      %r5 = simulation.context.storage %ctx[5] : !simulation.ref<i8>
      %r6 = simulation.context.storage %ctx[6] : !simulation.ref<i8>
      %r7 = simulation.context.storage %ctx[7] : !simulation.ref<i8>
      %a = simulation.spawn @a(%ctx, %r0, %r1, %r2, %r3, %r4, %r5) : !simulation.context, !simulation.ref<i8>, !simulation.ref<i8>, !simulation.ref<i8>, !simulation.ref<i8>, !simulation.ref<i8>, !simulation.ref<i8> -> !simulation.process
      %b = simulation.spawn @b(%ctx, %r1, %r2) : !simulation.context, !simulation.ref<i8>, !simulation.ref<i8> -> !simulation.process
      %c = simulation.spawn @c(%ctx, %r3, %r4, %r6, %r7) : !simulation.context, !simulation.ref<i8>, !simulation.ref<i8>, !simulation.ref<i8>, !simulation.ref<i8> -> !simulation.process
      %d = simulation.spawn @d(%ctx, %r6, %r7) : !simulation.context, !simulation.ref<i8>, !simulation.ref<i8> -> !simulation.process
      simulation.return
    }
    simulation.func @a(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %x: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %y: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %z: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}, %back: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}, %out: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %zero = arith.constant 0 : i8
      cf.br ^body
    ^body:
      %i = simulation.ref.load %in : !simulation.ref<i8> -> i8
      simulation.ref.store %i to %x : i8, !simulation.ref<i8>
      %v = simulation.ref.load %y : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %z : i8, !simulation.ref<i8>
      %f = simulation.ref.load %back : !simulation.ref<i8> -> i8
      simulation.ref.store %f to %out : i8, !simulation.ref<i8>
      simulation.suspend.any %in, %y, %back edges [0, 0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i8>, !simulation.ref<i8>
    }
    simulation.func @b(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %x: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %y: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %x : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %y : i8, !simulation.ref<i8>
      simulation.suspend.change %x to ^body : !simulation.ref<i8>
    }
    simulation.func @c(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %z: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}, %back: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}, %d: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64}, %e: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 7 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %z : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %back : i8, !simulation.ref<i8>
      %w = simulation.ref.load %d : !simulation.ref<i8> -> i8
      simulation.ref.store %w to %e : i8, !simulation.ref<i8>
      simulation.suspend.any %z, %d edges [0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i8>
    }
    simulation.func @d(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %d: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64}, %e: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 7 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 4 : i64} {
      cf.br ^body
    ^body:
      %v = simulation.ref.load %e : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %d : i8, !simulation.ref<i8>
      simulation.suspend.change %e to ^body : !simulation.ref<i8>
    }
  }
}
