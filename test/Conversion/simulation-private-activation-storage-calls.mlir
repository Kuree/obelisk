// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-materialize-compute-fusion,schedule-localize-private-eval-storage))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph{vpi=read},obelisk-sim-materialize-compute-fusion,schedule-localize-private-eval-storage))' | FileCheck %s --check-prefix=READ

// Evaluator localization through ordinary function calls and of scalar roots.
// IEEE 1800-2023 13.4 prohibits blocking timing controls in a function. The
// generated native evaluator executes synchronously, and the read-only callee
// proof excludes scheduler effects. A callee that observes the root
// through its own capture or a reference formal (13.5.2), or that writes
// canonical state, keeps the canonical storage. Scalars written before every
// read take a known initializer that no read can observe.
!array = !simulation.unpacked_array<0 : 1 x i3>
!ref = !simulation.ref<!array>
module attributes {schedule.native_scheduler = 3 : i32} {
  simulation.design @pure_call {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "test.pure_call"
    simulation.code_unit.decl 2 in 0 function hierarchy "test.increment"
    simulation.storage.decl 0 in 0 : i3 design
    simulation.storage.decl 1 in 0 : !array design
    simulation.storage.decl 2 in 0 : i3 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %result = simulation.context.storage %ctx[2] : !simulation.ref<i3>
      %observed = simulation.ref.load %result : !simulation.ref<i3> -> i3
      %process = simulation.spawn @pure_call(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @pure_call(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i3>
      %array = simulation.context.storage %ctx[1] : !ref
      %output = simulation.context.storage %ctx[2] : !simulation.ref<i3>
      %first = simulation.ref.subelement %array[[0]] : !ref -> !simulation.ref<i3>
      %second = simulation.ref.subelement %array[[1]] : !ref -> !simulation.ref<i3>
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !simulation.ref<i3> -> i3
      %next = simulation.call @increment(%ctx, %value) : (!simulation.context, i3) -> i3
      simulation.ref.store %value to %first : i3, !simulation.ref<i3>
      simulation.ref.store %next to %second : i3, !simulation.ref<i3>
      %a = simulation.ref.load %first : !simulation.ref<i3> -> i3
      %b = simulation.ref.load %second : !simulation.ref<i3> -> i3
      %sum = arith.addi %a, %b : i3
      simulation.ref.store %sum to %output : i3, !simulation.ref<i3>
      simulation.suspend.change %input to ^body : !simulation.ref<i3>
    }
    simulation.func private @increment(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %value: i3 {simulation.capture_kind = 1 : i32}) -> i3 attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %one = arith.constant 1 : i3
      %next = arith.addi %value, %one : i3
      simulation.return %next : i3
    }
  }
  simulation.design @scalar_bits {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "test.scalar_bits"
    simulation.storage.decl 0 in 0 : i3 design
    simulation.storage.decl 1 in 0 : i3 design
    simulation.storage.decl 2 in 0 : i3 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %result = simulation.context.storage %ctx[2] : !simulation.ref<i3>
      %observed = simulation.ref.load %result : !simulation.ref<i3> -> i3
      %process = simulation.spawn @scalar_bits(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @scalar_bits(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i3>
      %temporary = simulation.context.storage %ctx[1] : !simulation.ref<i3>
      %output = simulation.context.storage %ctx[2] : !simulation.ref<i3>
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !simulation.ref<i3> -> i3
      simulation.ref.store %value to %temporary : i3, !simulation.ref<i3>
      %copy = simulation.ref.load %temporary : !simulation.ref<i3> -> i3
      %sum = arith.addi %copy, %value : i3
      simulation.ref.store %sum to %output : i3, !simulation.ref<i3>
      simulation.suspend.change %input to ^body : !simulation.ref<i3>
    }
  }
  simulation.design @scalar_logic {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "test.scalar_logic"
    simulation.storage.decl 0 in 0 : !simulation.logic<3> design
    simulation.storage.decl 1 in 0 : !simulation.logic<3> design
    simulation.storage.decl 2 in 0 : !simulation.logic<3> design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %result = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<3>>
      %observed = simulation.ref.load %result : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      %process = simulation.spawn @scalar_logic(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @scalar_logic(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<3>>
      %temporary = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<3>>
      %output = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<3>>
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      simulation.ref.store %value to %temporary : !simulation.logic<3>, !simulation.ref<!simulation.logic<3>>
      %copy = simulation.ref.load %temporary : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      simulation.ref.store %copy to %output : !simulation.logic<3>, !simulation.ref<!simulation.logic<3>>
      simulation.suspend.change %input to ^body : !simulation.ref<!simulation.logic<3>>
    }
  }
  simulation.design @callee_capture {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "test.callee_capture"
    simulation.code_unit.decl 2 in 0 function hierarchy "test.peek"
    simulation.storage.decl 0 in 0 : i3 design
    simulation.storage.decl 1 in 0 : i3 design
    simulation.storage.decl 2 in 0 : i3 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %result = simulation.context.storage %ctx[2] : !simulation.ref<i3>
      %observed = simulation.ref.load %result : !simulation.ref<i3> -> i3
      %process = simulation.spawn @callee_capture(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @callee_capture(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i3>
      %temporary = simulation.context.storage %ctx[1] : !simulation.ref<i3>
      %output = simulation.context.storage %ctx[2] : !simulation.ref<i3>
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !simulation.ref<i3> -> i3
      simulation.ref.store %value to %temporary : i3, !simulation.ref<i3>
      %copy = simulation.call @peek(%ctx) : (!simulation.context) -> i3
      simulation.ref.store %copy to %output : i3, !simulation.ref<i3>
      simulation.suspend.change %input to ^body : !simulation.ref<i3>
    }
    simulation.func private @peek(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i3 attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %temporary = simulation.context.storage %ctx[1] : !simulation.ref<i3>
      %copy = simulation.ref.load %temporary : !simulation.ref<i3> -> i3
      simulation.return %copy : i3
    }
  }
  simulation.design @reference_formal {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "test.reference_formal"
    simulation.code_unit.decl 2 in 0 function hierarchy "test.dereference"
    simulation.storage.decl 0 in 0 : i3 design
    simulation.storage.decl 1 in 0 : i3 design
    simulation.storage.decl 2 in 0 : i3 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %result = simulation.context.storage %ctx[2] : !simulation.ref<i3>
      %observed = simulation.ref.load %result : !simulation.ref<i3> -> i3
      %process = simulation.spawn @reference_formal(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @reference_formal(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i3>
      %temporary = simulation.context.storage %ctx[1] : !simulation.ref<i3>
      %output = simulation.context.storage %ctx[2] : !simulation.ref<i3>
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !simulation.ref<i3> -> i3
      simulation.ref.store %value to %temporary : i3, !simulation.ref<i3>
      %copy = simulation.call @dereference(%ctx, %temporary) : (!simulation.context, !simulation.ref<i3>) -> i3
      simulation.ref.store %copy to %output : i3, !simulation.ref<i3>
      simulation.suspend.change %input to ^body : !simulation.ref<i3>
    }
    simulation.func private @dereference(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %reference: !simulation.ref<i3> {simulation.capture_kind = 1 : i32}) -> i3 attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %copy = simulation.ref.load %reference : !simulation.ref<i3> -> i3
      simulation.return %copy : i3
    }
  }
  simulation.design @callee_writes {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "test.callee_writes"
    simulation.code_unit.decl 2 in 0 function hierarchy "test.record"
    simulation.storage.decl 0 in 0 : i3 design
    simulation.storage.decl 1 in 0 : i3 design
    simulation.storage.decl 2 in 0 : i3 design
    simulation.storage.decl 3 in 0 : i3 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %result = simulation.context.storage %ctx[2] : !simulation.ref<i3>
      %observed = simulation.ref.load %result : !simulation.ref<i3> -> i3
      %log = simulation.context.storage %ctx[3] : !simulation.ref<i3>
      %logged = simulation.ref.load %log : !simulation.ref<i3> -> i3
      %process = simulation.spawn @callee_writes(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @callee_writes(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i3>
      %temporary = simulation.context.storage %ctx[1] : !simulation.ref<i3>
      %output = simulation.context.storage %ctx[2] : !simulation.ref<i3>
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !simulation.ref<i3> -> i3
      simulation.ref.store %value to %temporary : i3, !simulation.ref<i3>
      %copy = simulation.ref.load %temporary : !simulation.ref<i3> -> i3
      simulation.call @record(%ctx, %copy) : (!simulation.context, i3) -> ()
      simulation.ref.store %copy to %output : i3, !simulation.ref<i3>
      simulation.suspend.change %input to ^body : !simulation.ref<i3>
    }
    simulation.func private @record(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %value: i3 {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %log = simulation.context.storage %ctx[3] : !simulation.ref<i3>
      simulation.ref.store %value to %log : i3, !simulation.ref<i3>
      simulation.return
    }
  }
  simulation.design @scalar_live_in {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "test.scalar_live_in"
    simulation.storage.decl 0 in 0 : i3 design
    simulation.storage.decl 1 in 0 : i3 design
    simulation.storage.decl 2 in 0 : i3 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %result = simulation.context.storage %ctx[2] : !simulation.ref<i3>
      %observed = simulation.ref.load %result : !simulation.ref<i3> -> i3
      %process = simulation.spawn @scalar_live_in(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @scalar_live_in(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i3>
      %previous = simulation.context.storage %ctx[1] : !simulation.ref<i3>
      %output = simulation.context.storage %ctx[2] : !simulation.ref<i3>
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !simulation.ref<i3> -> i3
      %old = simulation.ref.load %previous : !simulation.ref<i3> -> i3
      %sum = arith.addi %old, %value : i3
      simulation.ref.store %value to %previous : i3, !simulation.ref<i3>
      simulation.ref.store %sum to %output : i3, !simulation.ref<i3>
      simulation.suspend.change %input to ^body : !simulation.ref<i3>
    }
  }
}
// CHECK-LABEL: simulation.func private @pure_call.__obelisk_eval_body_
// CHECK: %[[LOCAL:.*]] = simulation.ref.alloc
// CHECK: %[[ELEMENT:.*]] = simulation.ref.subelement %[[LOCAL]][
// CHECK: simulation.call @increment
// CHECK: simulation.ref.store %{{.*}} to %[[ELEMENT]]
// CHECK: simulation.return
// CHECK-LABEL: simulation.func private @scalar_bits.__obelisk_eval_body_
// CHECK: %[[ZERO:.*]] = arith.constant 0 : i3
// CHECK: %[[SLOT:.*]] = simulation.ref.alloc %[[ZERO]] : i3
// CHECK: simulation.ref.store %{{.*}} to %[[SLOT]]
// CHECK: simulation.ref.load %[[SLOT]]
// CHECK: simulation.return
// CHECK-LABEL: simulation.func private @scalar_logic.__obelisk_eval_body_
// CHECK: %[[KNOWN:.*]] = simulation.logic.constant 0 : i3, 0 : i3 : !simulation.logic<3>
// CHECK: %[[LOGIC:.*]] = simulation.ref.alloc %[[KNOWN]] : !simulation.logic<3>
// CHECK: simulation.ref.store %{{.*}} to %[[LOGIC]]
// CHECK: simulation.ref.load %[[LOGIC]]
// CHECK: simulation.return
// CHECK-LABEL: simulation.func private @callee_capture.__obelisk_eval_body_
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.call @peek
// CHECK: simulation.return
// CHECK-LABEL: simulation.func private @reference_formal.__obelisk_eval_body_
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.call @dereference
// CHECK: simulation.return
// CHECK-LABEL: simulation.func private @callee_writes.__obelisk_eval_body_
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.call @record
// CHECK: simulation.return
// CHECK-LABEL: simulation.func private @scalar_live_in.__obelisk_eval_body_
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.return
// READ-NOT: simulation.ref.alloc
