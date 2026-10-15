// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-instrument-reference-lifetimes)))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-instrument-reference-lifetimes,obelisk-sim-instrument-reference-lifetimes)))' | FileCheck %s

module {
  simulation.design @reference_lifetimes {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "reference_lifetimes.direct"
    simulation.code_unit.decl 2 in 0 function hierarchy "reference_lifetimes.loop"
    simulation.code_unit.decl 3 in 0 function hierarchy "reference_lifetimes.branch"

    simulation.func @direct(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %initial: i64 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %local = simulation.ref.alloc %initial :
          i64 -> !simulation.ref<i64>
      simulation.return
    }

    simulation.code_unit.decl 5 in 0 function hierarchy "reference_lifetimes.observe"
    simulation.func @observe(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %reference: !simulation.ref<i64> {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %value = simulation.ref.load %reference : !simulation.ref<i64> -> i64
      simulation.return
    }

    simulation.func @loop(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %initial: i64 {simulation.capture_kind = 2 : i32},
        %repeat: i1 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      cf.br ^allocate
    ^allocate:
      %local = simulation.ref.alloc %initial :
          i64 -> !simulation.ref<i64>
      simulation.call @observe(%ctx, %local) : (!simulation.context, !simulation.ref<i64>) -> ()
      cf.br ^use(%local : !simulation.ref<i64>)
    ^use(%reference: !simulation.ref<i64>):
      %value = simulation.ref.load %reference :
          !simulation.ref<i64> -> i64
      cf.cond_br %repeat, ^allocate, ^done
    ^done:
      simulation.return
    }

    simulation.code_unit.decl 4 in 0 function hierarchy "reference_lifetimes.shared_exit"
    // Both owners leave on the same edge. The cleanup must forward and
    // release each exactly once, including when instrumentation is repeated.
    simulation.func @shared_exit(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %initial: i64 {simulation.capture_kind = 2 : i32},
        %choose: i1 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %a = simulation.ref.alloc %initial : i64 -> !simulation.ref<i64>
      %b = simulation.ref.alloc %initial : i64 -> !simulation.ref<i64>
      simulation.call @observe(%ctx, %a) : (!simulation.context, !simulation.ref<i64>) -> ()
      simulation.call @observe(%ctx, %b) : (!simulation.context, !simulation.ref<i64>) -> ()
      cf.cond_br %choose, ^use(%a, %b : !simulation.ref<i64>, !simulation.ref<i64>), ^done
    ^use(%ra: !simulation.ref<i64>, %rb: !simulation.ref<i64>):
      %va = simulation.ref.load %ra : !simulation.ref<i64> -> i64
      %vb = simulation.ref.load %rb : !simulation.ref<i64> -> i64
      simulation.return
    ^done:
      simulation.return
    }

    simulation.func @branch(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %initial: i64 {simulation.capture_kind = 2 : i32},
        %use_reference: i1 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %local = simulation.ref.alloc %initial :
          i64 -> !simulation.ref<i64>
      simulation.call @observe(%ctx, %local) : (!simulation.context, !simulation.ref<i64>) -> ()
      cf.cond_br %use_reference,
          ^use(%local : !simulation.ref<i64>), ^done
    ^use(%reference: !simulation.ref<i64>):
      %value = simulation.ref.load %reference :
          !simulation.ref<i64> -> i64
      simulation.return
    ^done:
      simulation.return
    }

    simulation.code_unit.decl 6 in 0 function hierarchy "reference_lifetimes.argument_alias"
    simulation.code_unit.decl 7 in 0 function hierarchy "reference_lifetimes.observe_alias"
    simulation.func @observe_alias(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %reference: !simulation.argument_ref<i64> {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64} {
      %value = simulation.argument_ref.load %reference : !simulation.argument_ref<i64> -> i64
      simulation.return
    }

    // An exact alias can outlive its conversion to argument_ref, including
    // after retyping and forwarding through block arguments. Keep the same
    // owner alive until the final call, and release ordinary storage once.
    simulation.func @argument_alias(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %initial: i64 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %local = simulation.ref.alloc %initial : i64 -> !simulation.ref<i64>
      %alias = simulation.argument_ref.from_ref %local : !simulation.ref<i64> -> !simulation.argument_ref<i64>
      simulation.call @observe_alias(%ctx, %alias) : (!simulation.context, !simulation.argument_ref<i64>) -> ()
      cf.br ^next
    ^next:
      %retyped = simulation.argument_ref.retype %alias : !simulation.argument_ref<i64> -> !simulation.argument_ref<i64>
      cf.br ^last(%retyped : !simulation.argument_ref<i64>)
    ^last(%forwarded: !simulation.argument_ref<i64>):
      simulation.call @observe_alias(%ctx, %forwarded) : (!simulation.context, !simulation.argument_ref<i64>) -> ()
      simulation.return
    }

    // A task writes its output formal until it returns, even when the caller
    // never reads that formal again.
    simulation.code_unit.decl 8 in 0 task hierarchy "reference_lifetimes.fill"
    simulation.code_unit.decl 9 in 0 initial hierarchy "reference_lifetimes.task_argument"
    simulation.func @fill(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %out: !simulation.ref<i64> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 12 : i32, code_unit_id = 8 : i64} {
      %value = arith.constant 7 : i64
      simulation.ref.store %value to %out : i64, !simulation.ref<i64>
      simulation.return
    }
    simulation.func @task_argument(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %initial: i64 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9 : i64} {
      %formal = simulation.ref.alloc %initial : i64 -> !simulation.ref<i64>
      simulation.task.call @fill(%ctx, %formal) arguments 2 to ^returned : !simulation.context, !simulation.ref<i64>
    ^returned:
      simulation.return
    }
  }
}

// CHECK-LABEL: simulation.func @direct
// CHECK: simulation.ref.alloc
// CHECK-NOT: native.owner_release_instrumented
// CHECK-NOT: simulation.ref.release_owner
// CHECK-NEXT: simulation.return

// CHECK-LABEL: simulation.func @loop
// CHECK: simulation.ref.alloc
// CHECK-SAME: native.owner_release_instrumented
// CHECK: simulation.ref.release_owner
// CHECK-NEXT: cf.cond_br

// CHECK-LABEL: simulation.func @shared_exit
// CHECK: cf.cond_br %{{.*}}, ^{{.*}}(%{{.*}}, %{{.*}} : !simulation.ref<i64>, !simulation.ref<i64>), ^[[CLEANUP:.*]](%{{.*}}, %{{.*}} : !simulation.ref<i64>, !simulation.ref<i64>)
// CHECK-COUNT-2: simulation.ref.release_owner

// CHECK: ^[[CLEANUP]](%[[A:.*]]: !simulation.ref<i64>, %[[B:.*]]: !simulation.ref<i64>):
// CHECK-NEXT: simulation.ref.release_owner %[[A]]
// CHECK-NEXT: simulation.ref.release_owner %[[B]]
// CHECK-NEXT: cf.br

// CHECK-LABEL: simulation.func @branch
// CHECK: simulation.ref.alloc
// CHECK-SAME: native.owner_release_instrumented
// CHECK-COUNT-2: simulation.ref.release_owner

// CHECK-LABEL: simulation.func @argument_alias
// CHECK: %[[LOCAL:.*]] = simulation.ref.alloc
// CHECK-SAME: native.owner_release_instrumented
// CHECK: simulation.argument_ref.from_ref %[[LOCAL]]
// CHECK: simulation.call @observe_alias
// CHECK-NOT: simulation.ref.release_owner
// CHECK: simulation.argument_ref.retype
// CHECK-NOT: simulation.ref.release_owner
// CHECK: simulation.call @observe_alias
// CHECK-NEXT: simulation.ref.release_owner %[[LOCAL]]
// CHECK-NEXT: simulation.return

// CHECK-LABEL: simulation.func @task_argument
// CHECK: %[[FORMAL:.*]] = simulation.ref.alloc
// CHECK-SAME: native.owner_release_instrumented
// CHECK-NEXT: simulation.task.call @fill(%{{.*}}, %[[FORMAL]], %[[FORMAL]]) arguments 2 to ^[[CLEANUP:bb[0-9]+]]
// CHECK: ^[[CLEANUP]](%[[RETURNED:.*]]: !simulation.ref<i64>):
// CHECK-NEXT: simulation.ref.release_owner %[[RETURNED]]
// CHECK-NEXT: cf.br
