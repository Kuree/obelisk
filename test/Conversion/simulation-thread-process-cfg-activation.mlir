// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(talos-sim-thread-process-cfg)))' | FileCheck %s
// RUN: talos-opt %s --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null

// Frame lanes are needed only for values that cross a suspension. A value
// defined after a resume and used later in the same activation keeps its SSA
// definition, and a design view of a process argument is recomputed where it
// is used. A suspension's own watch operand remains a lane. A restored lane
// is reused only where its continuation dominates on activation paths: a
// later resume target starts another activation that never restored it. The
// second RUN line lowers every case, so a use that a native activation
// cannot reach fails dominance verification.

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @thread_process_cfg_activation {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.code_unit.decl 1 in 0 initial hierarchy "activation.local"
    simulation.code_unit.decl 2 in 0 initial hierarchy "activation.view"
    simulation.code_unit.decl 3 in 0 initial hierarchy "activation.later_resume"
    simulation.code_unit.decl 4 in 0 initial hierarchy "activation.same_activation"

    simulation.func @activation_local(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %input: i32 {simulation.capture_kind = 2 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %before = arith.addi %input, %input : i32
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^resume
    ^resume:
      %after = arith.muli %input, %input : i32
      %condition = arith.constant true
      cf.cond_br %condition, ^left, ^right
    ^left:
      cf.br ^use
    ^right:
      cf.br ^use
    ^use:
      %sum = arith.addi %before, %after : i32
      simulation.file.flush %ctx, %sum :
          (!simulation.context, i32) -> ()
      cf.br ^wait
    }

    simulation.func @captured_view(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %clock = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clock to ^body
          {site = #schedule.continuation<id = 1>} :
          !simulation.ref<!simulation.logic<1>>
    ^body:
      %value = simulation.ref.load %clock :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.ref.store %value to %clock :
          !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    }

    simulation.func @restored_view_later_resume(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 3 : i64, entry_kind = 1 : i32} {
      %clock = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      simulation.suspend.change %clock to
          ^first(%clock : !simulation.ref<!simulation.logic<1>>)
          {site = #schedule.continuation<id = 1>} :
          !simulation.ref<!simulation.logic<1>>
    ^first(%lane: !simulation.ref<!simulation.logic<1>>):
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^second
    ^second:
      simulation.suspend.change %clock to
          ^first(%clock : !simulation.ref<!simulation.logic<1>>)
          {site = #schedule.continuation<id = 2>} :
          !simulation.ref<!simulation.logic<1>>
    }

    simulation.func @restored_view_same_activation(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 4 : i64, entry_kind = 1 : i32} {
      %clock = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      simulation.suspend.change %clock to
          ^first(%clock : !simulation.ref<!simulation.logic<1>>)
          {site = #schedule.continuation<id = 1>} :
          !simulation.ref<!simulation.logic<1>>
    ^first(%lane: !simulation.ref<!simulation.logic<1>>):
      %condition = arith.constant true
      cf.cond_br %condition, ^left, ^right
    ^left:
      cf.br ^join
    ^right:
      cf.br ^join
    ^join:
      simulation.suspend.change %clock to
          ^first(%clock : !simulation.ref<!simulation.logic<1>>)
          {site = #schedule.continuation<id = 1>} :
          !simulation.ref<!simulation.logic<1>>
    }
  }
}

// %before crosses the suspension and arrives through a lane. %after is
// defined in the resumed activation and is used directly.
// CHECK-LABEL: simulation.func @activation_local
// CHECK: simulation.suspend.delay
// CHECK: %[[AFTER:.*]] = arith.muli
// CHECK: ^{{bb[0-9]+}}(%[[BEFORE:.*]]: i32):{{.*}}2 preds
// CHECK-NEXT: arith.addi %[[BEFORE]], %[[AFTER]] : i32

// The loaded view is rematerialized in the resumed block; the watched view
// keeps its lane into the wait block.
// CHECK-LABEL: simulation.func @captured_view
// CHECK: ^{{bb[0-9]+}}(%[[WATCHED:.*]]: !simulation.ref<!simulation.logic<1>>):
// CHECK-NEXT: simulation.suspend.change %[[WATCHED]]
// CHECK-NEXT: ^{{bb[0-9]+}}:
// CHECK-NEXT: %[[VIEW:.*]] = simulation.context.storage %{{.*}}[0]
// CHECK-NEXT: simulation.ref.load %[[VIEW]]

// The delay resumes a new activation at ^second, which ^first does not
// dominate on activation paths, so the view needs its own lane there.
// CHECK-LABEL: simulation.func @restored_view_later_resume
// CHECK: ^[[FIRST:bb[0-9]+]](%[[LANE:.*]]: !simulation.ref<!simulation.logic<1>>):
// CHECK: simulation.suspend.delay %{{.*}} to ^[[SECOND:bb[0-9]+]](%[[LANE]] : !simulation.ref<!simulation.logic<1>>)
// CHECK-NEXT: ^[[SECOND]](%[[SECOND_LANE:.*]]: !simulation.ref<!simulation.logic<1>>):
// CHECK-NEXT: simulation.suspend.change %[[SECOND_LANE]] to ^[[FIRST]](%[[SECOND_LANE]] :

// Every path from a resume to ^join passes ^first in the same activation, so
// its restored lane is used directly without a lane through the diamond.
// CHECK-LABEL: simulation.func @restored_view_same_activation
// CHECK: ^[[SAME_FIRST:bb[0-9]+]](%[[SAME_LANE:.*]]: !simulation.ref<!simulation.logic<1>>):
// CHECK: ^{{bb[0-9]+}}:{{ *}}// 2 preds
// CHECK-NEXT: simulation.suspend.change %[[SAME_LANE]] to ^[[SAME_FIRST]](%[[SAME_LANE]] :
