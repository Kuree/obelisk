// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-materialize-compute-fusion,schedule-localize-private-eval-storage))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph{vpi=read},obelisk-sim-materialize-compute-fusion,schedule-localize-private-eval-storage))' | FileCheck %s --check-prefix=READ

// Localize only the evaluator, preserving the canonical actor's ABI and static
// lifetime. Partial initialization, another accessor, and read-only VPI each
// prevent removal of canonical storage.
!array = !simulation.unpacked_array<0 : 1 x i3>
!ref = !simulation.ref<!array>
module attributes {schedule.native_scheduler = 3 : i32} {
  simulation.design @complete {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "test.complete"
    simulation.storage.decl 0 in 0 : i3 design
    simulation.storage.decl 1 in 0 : !array design
    simulation.storage.decl 2 in 0 : i3 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %result = simulation.context.storage %ctx[2] : !simulation.ref<i3>
      %observed = simulation.ref.load %result : !simulation.ref<i3> -> i3
      %process = simulation.spawn @complete(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @complete(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i3>
      %array = simulation.context.storage %ctx[1] : !ref
      %output = simulation.context.storage %ctx[2] : !simulation.ref<i3>
      %first = simulation.ref.subelement %array[[0]] : !ref -> !simulation.ref<i3>
      %second = simulation.ref.subelement %array[[1]] : !ref -> !simulation.ref<i3>
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !simulation.ref<i3> -> i3
      simulation.ref.store %value to %first : i3, !simulation.ref<i3>
      simulation.ref.store %value to %second : i3, !simulation.ref<i3>
      %a = simulation.ref.load %first : !simulation.ref<i3> -> i3
      %b = simulation.ref.load %second : !simulation.ref<i3> -> i3
      %sum = arith.addi %a, %b : i3
      simulation.ref.store %sum to %output : i3, !simulation.ref<i3>
      simulation.suspend.change %input to ^body : !simulation.ref<i3>
    }
  }
  simulation.design @partial {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "test.partial"
    simulation.storage.decl 0 in 0 : i3 design
    simulation.storage.decl 1 in 0 : !array design
    simulation.storage.decl 2 in 0 : i3 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %result = simulation.context.storage %ctx[2] : !simulation.ref<i3>
      %observed = simulation.ref.load %result : !simulation.ref<i3> -> i3
      %process = simulation.spawn @partial(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @partial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i3>
      %array = simulation.context.storage %ctx[1] : !ref
      %output = simulation.context.storage %ctx[2] : !simulation.ref<i3>
      %first = simulation.ref.subelement %array[[0]] : !ref -> !simulation.ref<i3>
      %second = simulation.ref.subelement %array[[1]] : !ref -> !simulation.ref<i3>
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !simulation.ref<i3> -> i3
      simulation.ref.store %value to %first : i3, !simulation.ref<i3>
      %a = simulation.ref.load %first : !simulation.ref<i3> -> i3
      %b = simulation.ref.load %second : !simulation.ref<i3> -> i3
      %sum = arith.addi %a, %b : i3
      simulation.ref.store %sum to %output : i3, !simulation.ref<i3>
      simulation.suspend.change %input to ^body : !simulation.ref<i3>
    }
  }
  simulation.design @shared {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "test.shared"
    simulation.storage.decl 0 in 0 : i3 design
    simulation.storage.decl 1 in 0 : !array design
    simulation.storage.decl 2 in 0 : i3 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %result = simulation.context.storage %ctx[2] : !simulation.ref<i3>
      %observed = simulation.ref.load %result : !simulation.ref<i3> -> i3
      %array = simulation.context.storage %ctx[1] : !ref
      %value = simulation.ref.load %array : !ref -> !array
      %process = simulation.spawn @shared(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @shared(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i3>
      %array = simulation.context.storage %ctx[1] : !ref
      %output = simulation.context.storage %ctx[2] : !simulation.ref<i3>
      %first = simulation.ref.subelement %array[[0]] : !ref -> !simulation.ref<i3>
      %second = simulation.ref.subelement %array[[1]] : !ref -> !simulation.ref<i3>
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !simulation.ref<i3> -> i3
      simulation.ref.store %value to %first : i3, !simulation.ref<i3>
      simulation.ref.store %value to %second : i3, !simulation.ref<i3>
      %a = simulation.ref.load %first : !simulation.ref<i3> -> i3
      %b = simulation.ref.load %second : !simulation.ref<i3> -> i3
      %sum = arith.addi %a, %b : i3
      simulation.ref.store %sum to %output : i3, !simulation.ref<i3>
      simulation.suspend.change %input to ^body : !simulation.ref<i3>
    }
  }
  simulation.design @disjoint_watch {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "test.disjoint_watch"
    simulation.storage.decl 0 in 0 : i3 design
    simulation.storage.decl 1 in 0 : !array design
    simulation.storage.decl 2 in 0 : i3 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %result = simulation.context.storage %ctx[2] : !simulation.ref<i3>
      %observed = simulation.ref.load %result : !simulation.ref<i3> -> i3
      %process = simulation.spawn @disjoint_watch(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @disjoint_watch(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i3>
      %array = simulation.context.storage %ctx[1] : !ref
      %output = simulation.context.storage %ctx[2] : !simulation.ref<i3>
      %first = simulation.ref.subelement %array[[0]] : !ref -> !simulation.ref<i3>
      %second = simulation.ref.subelement %array[[1]] : !ref -> !simulation.ref<i3>
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !simulation.ref<i3> -> i3
      simulation.ref.store %value to %first : i3, !simulation.ref<i3>
      %a = simulation.ref.load %first : !simulation.ref<i3> -> i3
      %b = simulation.ref.load %first : !simulation.ref<i3> -> i3
      %sum = arith.addi %a, %b : i3
      simulation.ref.store %sum to %output : i3, !simulation.ref<i3>
      simulation.suspend.change %second to ^body : !simulation.ref<i3>
    }
  }
  simulation.design @overlap_watch {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "test.overlap_watch"
    simulation.storage.decl 0 in 0 : i3 design
    simulation.storage.decl 1 in 0 : !array design
    simulation.storage.decl 2 in 0 : i3 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %result = simulation.context.storage %ctx[2] : !simulation.ref<i3>
      %observed = simulation.ref.load %result : !simulation.ref<i3> -> i3
      %process = simulation.spawn @overlap_watch(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @overlap_watch(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i3>
      %array = simulation.context.storage %ctx[1] : !ref
      %output = simulation.context.storage %ctx[2] : !simulation.ref<i3>
      %first = simulation.ref.subelement %array[[0]] : !ref -> !simulation.ref<i3>
      %second = simulation.ref.subelement %array[[1]] : !ref -> !simulation.ref<i3>
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !simulation.ref<i3> -> i3
      simulation.ref.store %value to %first : i3, !simulation.ref<i3>
      %a = simulation.ref.load %first : !simulation.ref<i3> -> i3
      %b = simulation.ref.load %first : !simulation.ref<i3> -> i3
      %sum = arith.addi %a, %b : i3
      simulation.ref.store %sum to %output : i3, !simulation.ref<i3>
      simulation.suspend.change %first to ^body : !simulation.ref<i3>
    }
  }
  simulation.design @bootstrap_live_in {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "test.bootstrap_live_in"
    simulation.storage.decl 0 in 0 : i3 design
    simulation.storage.decl 1 in 0 : !array design
    simulation.storage.decl 2 in 0 : i3 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %result = simulation.context.storage %ctx[2] : !simulation.ref<i3>
      %observed = simulation.ref.load %result : !simulation.ref<i3> -> i3
      %process = simulation.spawn @bootstrap_live_in(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @bootstrap_live_in(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i3>
      %array = simulation.context.storage %ctx[1] : !ref
      %output = simulation.context.storage %ctx[2] : !simulation.ref<i3>
      %first = simulation.ref.subelement %array[[0]] : !ref -> !simulation.ref<i3>
      %second = simulation.ref.subelement %array[[1]] : !ref -> !simulation.ref<i3>
      %old = simulation.ref.load %first : !simulation.ref<i3> -> i3
      simulation.ref.store %old to %output : i3, !simulation.ref<i3>
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !simulation.ref<i3> -> i3
      simulation.ref.store %value to %first : i3, !simulation.ref<i3>
      simulation.ref.store %value to %second : i3, !simulation.ref<i3>
      %a = simulation.ref.load %first : !simulation.ref<i3> -> i3
      %b = simulation.ref.load %second : !simulation.ref<i3> -> i3
      %sum = arith.addi %a, %b : i3
      simulation.ref.store %sum to %output : i3, !simulation.ref<i3>
      simulation.suspend.change %input to ^body : !simulation.ref<i3>
    }
  }

}
// CHECK-LABEL: simulation.func @complete(
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.ref.store
// CHECK: simulation.suspend.change
// CHECK-LABEL: simulation.func private @complete.__obelisk_eval_body_
// CHECK: %[[LOCAL:.*]] = simulation.ref.alloc
// CHECK: simulation.ref.subelement %[[LOCAL]]
// CHECK: simulation.ref.store
// CHECK: simulation.return
// CHECK-LABEL: simulation.func private @partial.__obelisk_eval_body_
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.ref.load
// CHECK: simulation.return
// CHECK-LABEL: simulation.func private @shared.__obelisk_eval_body_
// Unwatched shared storage retains its final canonical value for other actors.
// CHECK: %[[CANON:.*]] = simulation.context.storage %{{.*}}[1]
// CHECK: %[[SHARED:.*]] = simulation.ref.alloc
// CHECK: %[[FINAL:.*]] = simulation.ref.load %[[SHARED]]
// CHECK-NEXT: simulation.ref.store %[[FINAL]] to %[[CANON]]
// CHECK: simulation.return
// CHECK-LABEL: simulation.func private @disjoint_watch.__obelisk_eval_body_
// CHECK: simulation.ref.alloc
// CHECK: simulation.return
// CHECK-LABEL: simulation.func private @overlap_watch.__obelisk_eval_body_
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.ref.store
// CHECK: simulation.return
// CHECK-LABEL: simulation.func private @bootstrap_live_in.__obelisk_eval_body_
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.ref.store
// CHECK: simulation.return
// READ-NOT: simulation.ref.alloc
// READ: simulation.func private @complete.__obelisk_eval_body_
// READ-NOT: simulation.ref.alloc
