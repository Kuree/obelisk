// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-sccp))' > %t.threaded
// RUN: obelisk-opt %s --mlir-disable-threading --pass-pipeline='builtin.module(simulation.design(obelisk-sim-sccp))' > %t.serial
// RUN: diff -u %t.serial %t.threaded
// RUN: FileCheck %s < %t.threaded
// RUN: obelisk-opt %s --verify-each=false --pass-pipeline='builtin.module(simulation.design(test-obelisk-erase-marked-sim-function,obelisk-sim-sccp))' | FileCheck %s --check-prefix=UNRESOLVED

module {
  simulation.design @sccp {
    simulation.code_unit.decl 9000001 in 0 function hierarchy "test.sccp.add1.9000001"
    simulation.code_unit.decl 9000002 in 0 function hierarchy "test.sccp.constant_caller.9000002"
    simulation.code_unit.decl 9000003 in 0 function hierarchy "test.sccp.identity.9000003"
    simulation.code_unit.decl 9000004 in 0 function hierarchy "test.sccp.conflicting_calls.9000004"
    simulation.code_unit.decl 9000005 in 0 function hierarchy "test.sccp.recursive.9000005"
    simulation.code_unit.decl 9000006 in 0 function hierarchy "test.sccp.recursive_caller.9000006"
    simulation.code_unit.decl 9000007 in 0 initial hierarchy "test.sccp.leaf.9000007"
    simulation.code_unit.decl 9000008 in 0 initial hierarchy "test.sccp.handle_sink.9000008"
    simulation.code_unit.decl 9000009 in 0 initial hierarchy "test.sccp.spawn_target.9000009"
    simulation.code_unit.decl 9000010 in 0 initial hierarchy "test.sccp.continuation.9000010"
    simulation.code_unit.decl 9000011 in 0 function hierarchy "test.sccp.cfg_join.9000011"
    simulation.code_unit.decl 9000012 in 0 function hierarchy "test.sccp.outer_with_nested.9000012"
    simulation.code_unit.decl 9000013 in 0 function hierarchy "test.sccp.nested_code_unit.9000013"
    simulation.code_unit.decl 9000014 in 0 function hierarchy "test.sccp.nested_isolation_caller.9000014"
    simulation.code_unit.decl 9000015 in 0 function hierarchy "test.sccp.public_identity.9000015"
    simulation.code_unit.decl 9000016 in 0 function hierarchy "test.sccp.nested_identity.9000016"
    simulation.code_unit.decl 9000017 in 0 function hierarchy "test.sccp.public_boundary_caller.9000017"
    simulation.code_unit.decl 9000018 in 0 function hierarchy "test.sccp.external_caller.9000018"
    simulation.code_unit.decl 9000019 in 0 function hierarchy "test.sccp.address_taken.9000019"
    simulation.code_unit.decl 9000020 in 0 function hierarchy "test.sccp.address_taken_caller.9000020"
    simulation.code_unit.decl 9000021 in 0 function hierarchy "test.sccp.unresolved_caller.9000021"
    simulation.code_unit.decl 9000022 in 0 task hierarchy "test.sccp.task_constant.9000022"
    simulation.code_unit.decl 9000023 in 0 initial hierarchy "test.sccp.task_caller.9000023"
    simulation.code_unit.decl 9000024 in 0 function hierarchy "test.sccp.bottom_recursive.9000024"
    simulation.code_unit.decl 9000025 in 0 function hierarchy "test.sccp.bottom_caller.9000025"
    simulation.code_unit.decl 9000026 in 0 function hierarchy "test.sccp.bottom_top.9000026"
    simulation.scope.decl 0 {callback = @address_taken}

    // Exact call arguments and results cross both sides of the boundary.
    // CHECK-LABEL: simulation.func private @add1
    // CHECK: %[[ANSWER:.*]] = arith.constant 42 : i32
    // CHECK: simulation.return %[[ANSWER]] : i32
    simulation.func private @add1(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %one = arith.constant 1 : i32
      %sum = arith.addi %value, %one : i32
      simulation.return %sum : i32
    }

    // CHECK-LABEL: simulation.func @constant_caller
    // CHECK: %[[CALL_ANSWER:.*]] = arith.constant 42 : i32
    // CHECK: simulation.call @add1
    // CHECK: simulation.return %[[CALL_ANSWER]] : i32
    simulation.func @constant_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000002 : i64} {
      %input = arith.constant 41 : i32
      %result = simulation.call @add1(%ctx, %input) : (!simulation.context, i32) -> i32
      simulation.return %result : i32
    }

    // Direct task boundaries participate in the same fixed point as function
    // calls, while their continuation operands are not mistaken for formals.
    // CHECK-LABEL: simulation.func private @task_constant
    // CHECK: %[[TASK_ANSWER:.*]] = arith.constant 42 : i32
    // CHECK: simulation.ref.alloc %[[TASK_ANSWER]]
    simulation.func private @task_constant(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 12 : i32, code_unit_id = 9000022 : i64} {
      %one = arith.constant 1 : i32
      %sum = arith.addi %value, %one : i32
      %local = simulation.ref.alloc %sum : i32 -> !simulation.ref<i32>
      simulation.return
    }

    simulation.func @task_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000023 : i64} {
      %input = arith.constant 41 : i32
      simulation.task.call @task_constant(%ctx, %input) arguments 2 to ^done : !simulation.context, i32
    ^done:
      simulation.return
    }

    // Conflicting executable callsites make the shared function boundary
    // unknown rather than specializing it per caller.
    // CHECK-LABEL: simulation.func private @identity
    // CHECK: simulation.return %{{.*}} : i32
    simulation.func private @identity(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000003 : i64} {
      simulation.return %value : i32
    }

    // CHECK-LABEL: simulation.func @conflicting_calls
    // CHECK: %[[FIRST:.*]] = simulation.call @identity
    // CHECK: simulation.call @identity
    // CHECK: simulation.return %[[FIRST]] : i32
    simulation.func @conflicting_calls(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000004 : i64} {
      %one = arith.constant 1 : i32
      %two = arith.constant 2 : i32
      %first = simulation.call @identity(%ctx, %one) : (!simulation.context, i32) -> i32
      %second = simulation.call @identity(%ctx, %two) : (!simulation.context, i32) -> i32
      simulation.return %first : i32
    }

    // The recursive edge is dead for the only executable argument. SCCP uses
    // the constant branch to retain the exact base-case result.
    simulation.func private @recursive(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000005 : i64} {
      %zero = arith.constant 0 : i32
      %is_zero = arith.cmpi eq, %value, %zero : i32
      cf.cond_br %is_zero, ^base, ^step
    ^base:
      %seven = arith.constant 7 : i32
      simulation.return %seven : i32
    ^step:
      %one = arith.constant 1 : i32
      %next = arith.subi %value, %one : i32
      %result = simulation.call @recursive(%ctx, %next) : (!simulation.context, i32) -> i32
      simulation.return %result : i32
    }

    // CHECK-LABEL: simulation.func @recursive_caller
    // CHECK: %[[SEVEN:.*]] = arith.constant 7 : i32
    // CHECK: simulation.return %[[SEVEN]] : i32
    simulation.func @recursive_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000006 : i64} {
      %zero = arith.constant 0 : i32
      %result = simulation.call @recursive(%ctx, %zero) : (!simulation.context, i32) -> i32
      simulation.return %result : i32
    }

    simulation.func private @leaf(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000007 : i64} {
      simulation.return
    }

    simulation.func private @handle_sink(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %handle: !simulation.process {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000008 : i64} {
      simulation.return
    }

    // Spawn operands flow into process formals. The scheduler handle remains
    // unknown and is forwarded as an SSA value, never as a constant.
    // CHECK-LABEL: simulation.func private @spawn_target
    // CHECK: %[[FIVE:.*]] = arith.constant 5 : i32
    // CHECK: %[[HANDLE:.*]] = simulation.spawn @leaf({{.*}}%[[FIVE]])
    // CHECK: simulation.spawn @handle_sink({{.*}}%[[HANDLE]])
    simulation.func private @spawn_target(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000009 : i64} {
      %one = arith.constant 1 : i32
      %next = arith.addi %value, %one : i32
      %handle = simulation.spawn @leaf(%ctx, %next) : !simulation.context, i32 -> !simulation.process
      %forwarded = simulation.spawn @handle_sink(%ctx, %handle) : !simulation.context, !simulation.process -> !simulation.process
      simulation.return
    }

    // Constants also propagate through suspension continuation arguments.
    // CHECK-LABEL: simulation.func private @continuation
    // CHECK: %[[SIX:.*]] = arith.constant 6 : i32
    // CHECK: simulation.suspend.delay
    // CHECK: ^{{.*}}(%{{.*}}: i32):
    // CHECK: simulation.spawn @leaf({{.*}}%[[SIX]])
    simulation.func private @continuation(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000010 : i64} {
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^resume(%value : i32)
    ^resume(%continued: i32):
      %handle = simulation.spawn @leaf(%ctx, %continued) : !simulation.context, i32 -> !simulation.process
      simulation.return
    }

    // Unknown control flow with equal incoming constants preserves the exact
    // CFG join fact.
    // CHECK-LABEL: simulation.func @cfg_join
    // CHECK: %[[NINE:.*]] = arith.constant 9 : i32
    // CHECK: simulation.return %[[NINE]] : i32
    simulation.func @cfg_join(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %condition: i1 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000011 : i64} {
      cf.cond_br %condition, ^left, ^right
    ^left:
      %lhs = arith.constant 9 : i32
      cf.br ^join(%lhs : i32)
    ^right:
      %rhs = arith.constant 9 : i32
      cf.br ^join(%rhs : i32)
    ^join(%value: i32):
      simulation.return %value : i32
    }

    // Nested isolated operations belong to neither the enclosing function's
    // boundary summary nor its final rewrite worker.
    // CHECK-LABEL: simulation.func private @outer_with_nested
    // CHECK: %[[OUTER_ZERO:.*]] = arith.constant 0 : i32
    // CHECK: builtin.module {
    // CHECK: %[[INNER_SUM:.*]] = arith.addi
    // CHECK: simulation.return %[[INNER_SUM]] : i32
    // CHECK: }
    // CHECK: }
    // CHECK: simulation.return %[[OUTER_ZERO]] : i32
    simulation.func private @outer_with_nested(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000012 : i64} {
      %zero = arith.constant 0 : i32
      builtin.module {
        simulation.func @nested_code_unit(
            %nested_ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
            attributes {entry_kind = 8 : i32, code_unit_id = 9000013 : i64} {
          %one = arith.constant 1 : i32
          %two = arith.constant 2 : i32
          %sum = arith.addi %one, %two : i32
          simulation.return %sum : i32
        }
      }
      simulation.return %zero : i32
    }

    // CHECK-LABEL: simulation.func @nested_isolation_caller
    // CHECK: %[[NESTED_OUTER_RESULT:.*]] = arith.constant 0 : i32
    // CHECK: simulation.call @outer_with_nested
    // CHECK: simulation.return %[[NESTED_OUTER_RESULT]] : i32
    simulation.func @nested_isolation_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000014 : i64} {
      %result = simulation.call @outer_with_nested(%ctx) : (!simulation.context) -> i32
      simulation.return %result : i32
    }

    // Public and nested entry arguments are unknown even when all visible
    // callsites pass the same constant.
    // CHECK: simulation.func @public_identity({{.*}}%[[PUBLIC_VALUE:[a-zA-Z0-9_]+]]: i32
    // CHECK: simulation.return %[[PUBLIC_VALUE]] : i32
    simulation.func @public_identity(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000015 : i64} {
      simulation.return %value : i32
    }

    // CHECK: simulation.func nested @nested_identity({{.*}}%[[NESTED_VALUE:[a-zA-Z0-9_]+]]: i32
    // CHECK: simulation.return %[[NESTED_VALUE]] : i32
    simulation.func nested @nested_identity(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000016 : i64} {
      simulation.return %value : i32
    }

    // CHECK-LABEL: simulation.func @public_boundary_caller
    // CHECK: %[[PUBLIC_RESULT:.*]] = simulation.call @public_identity
    // CHECK: simulation.return %[[PUBLIC_RESULT]] : i32
    simulation.func @public_boundary_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000017 : i64} {
      %five = arith.constant 5 : i32
      %public = simulation.call @public_identity(%ctx, %five) : (!simulation.context, i32) -> i32
      %nested = simulation.call @nested_identity(%ctx, %five) : (!simulation.context, i32) -> i32
      simulation.return %public : i32
    }

    // External declarations seed their results as unknown.
    simulation.func private @external(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32}

    // CHECK-LABEL: simulation.func @external_caller
    // CHECK: %[[EXTERNAL_RESULT:.*]] = simulation.call @external
    // CHECK: simulation.return %[[EXTERNAL_RESULT]] : i32
    simulation.func @external_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000018 : i64} {
      %five = arith.constant 5 : i32
      %result = simulation.call @external(%ctx, %five) : (!simulation.context, i32) -> i32
      simulation.return %result : i32
    }

    // A non-call symbol reference prevents visible calls from defining a
    // closed boundary.
    // CHECK: simulation.func private @address_taken({{.*}}%[[ADDRESS_VALUE:[a-zA-Z0-9_]+]]: i32
    // CHECK: simulation.return %[[ADDRESS_VALUE]] : i32
    simulation.func private @address_taken(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000019 : i64} {
      simulation.return %value : i32
    }

    // CHECK-LABEL: simulation.func @address_taken_caller
    // CHECK: %[[ADDRESS_RESULT:.*]] = simulation.call @address_taken
    // CHECK: simulation.return %[[ADDRESS_RESULT]] : i32
    simulation.func @address_taken_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000020 : i64} {
      %five = arith.constant 5 : i32
      %result = simulation.call @address_taken(%ctx, %five) : (!simulation.context, i32) -> i32
      simulation.return %result : i32
    }

    // This declaration is erased by a test-only pass after input verification.
    // SCCP must then treat its call result as unknown without consulting a
    // symbol table from a worker thread.
    simulation.func private @unresolved_external(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, test.erase_before_sccp}

    // The deliberately invalid output uses generic printing.
    // UNRESOLVED-NOT: sym_name = "unresolved_external"
    // UNRESOLVED: %[[UNRESOLVED_RESULT:[0-9]+]] = "simulation.call"{{.*}}callee = @unresolved_external
    // UNRESOLVED-NEXT: "simulation.return"(%[[UNRESOLVED_RESULT]])
    // UNRESOLVED: sym_name = "unresolved_caller"
    simulation.func @unresolved_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000021 : i64} {
      %eleven = arith.constant 11 : i32
      %result = simulation.call @unresolved_external(%ctx, %eleven) : (!simulation.context, i32) -> i32
      simulation.return %result : i32
    }

    // A recursive callee with no base return leaves its result at optimistic
    // bottom through the boundary fixed point. Initializing that result to
    // unknown must refresh its caller before rewriting retained solver state.
    simulation.func private @bottom_recursive(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000024 : i64} {
      %result = simulation.call @bottom_recursive(%ctx) :
          (!simulation.context) -> i32
      simulation.return %result : i32
    }

    // A stale caller solver joins bottom with one as one. After the callee's
    // bottom result is initialized to unknown, refreshing the caller instead
    // keeps the join and its use unknown.
    // CHECK-LABEL: simulation.func private @bottom_caller
    // CHECK: %[[ONE:.*]] = arith.constant 1 : i32
    // CHECK: %[[BOTTOM:.*]] = simulation.call @bottom_recursive
    // CHECK: cf.cond_br %{{.*}}, ^[[BOTTOM_PATH:.*]], ^[[ONE_PATH:.*]]
    // CHECK: ^[[BOTTOM_PATH]]:
    // CHECK: cf.br ^[[BOTTOM_JOIN:.*]](%[[BOTTOM]] : i32)
    // CHECK: ^[[ONE_PATH]]:
    // CHECK: cf.br ^[[BOTTOM_JOIN]](%[[ONE]] : i32)
    // CHECK: ^[[BOTTOM_JOIN]](%[[JOINED:.*]]: i32):
    // CHECK: simulation.return %[[JOINED]] : i32
    simulation.func private @bottom_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %condition: i1 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000025 : i64} {
      %result = simulation.call @bottom_recursive(%ctx) :
          (!simulation.context) -> i32
      cf.cond_br %condition, ^bottom, ^one
    ^bottom:
      cf.br ^join(%result : i32)
    ^one:
      %one = arith.constant 1 : i32
      cf.br ^join(%one : i32)
    ^join(%joined: i32):
      simulation.return %joined : i32
    }

    // CHECK-LABEL: simulation.func @bottom_top
    // CHECK: %[[TOP_RESULT:.*]] = simulation.call @bottom_caller
    // CHECK: simulation.return %[[TOP_RESULT]] : i32
    simulation.func @bottom_top(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %condition: i1 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000026 : i64} {
      %result = simulation.call @bottom_caller(%ctx, %condition) :
          (!simulation.context, i1) -> i32
      simulation.return %result : i32
    }

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %four = arith.constant 4 : i32
      %six = arith.constant 6 : i32
      %spawned = simulation.spawn @spawn_target(%ctx, %four) : !simulation.context, i32 -> !simulation.process
      %continued = simulation.spawn @continuation(%ctx, %six) : !simulation.context, i32 -> !simulation.process
      simulation.return
    }
  }
}
