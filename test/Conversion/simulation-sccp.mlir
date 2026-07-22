// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-sccp))' > %t.threaded
// RUN: obelisk-opt %s --mlir-disable-threading --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-sccp))' > %t.serial
// RUN: diff -u %t.serial %t.threaded
// RUN: FileCheck %s < %t.threaded
// RUN: obelisk-opt %s --verify-each=false --pass-pipeline='builtin.module(obelisk_sim.design(test-obelisk-erase-marked-sim-function,obelisk-sim-sccp))' | FileCheck %s --check-prefix=UNRESOLVED

module {
  obelisk_sim.design @sccp {
    obelisk_sim.scope.decl 0 {callback = @address_taken}

    // Exact call arguments and results cross both sides of the boundary.
    // CHECK-LABEL: obelisk_sim.func private @add1
    // CHECK: %[[ANSWER:.*]] = arith.constant 42 : i32
    // CHECK: obelisk_sim.return %[[ANSWER]] : i32
    obelisk_sim.func private @add1(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32} {
      %one = arith.constant 1 : i32
      %sum = arith.addi %value, %one : i32
      obelisk_sim.return %sum : i32
    }

    // CHECK-LABEL: obelisk_sim.func @constant_caller
    // CHECK: %[[CALL_ANSWER:.*]] = arith.constant 42 : i32
    // CHECK: obelisk_sim.call @add1
    // CHECK: obelisk_sim.return %[[CALL_ANSWER]] : i32
    obelisk_sim.func @constant_caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32} {
      %input = arith.constant 41 : i32
      %result = obelisk_sim.call @add1(%ctx, %input) : (!obelisk_sim.context, i32) -> i32
      obelisk_sim.return %result : i32
    }

    // Conflicting executable callsites make the shared function boundary
    // unknown rather than specializing it per caller.
    // CHECK-LABEL: obelisk_sim.func private @identity
    // CHECK: obelisk_sim.return %{{.*}} : i32
    obelisk_sim.func private @identity(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32} {
      obelisk_sim.return %value : i32
    }

    // CHECK-LABEL: obelisk_sim.func @conflicting_calls
    // CHECK: %[[FIRST:.*]] = obelisk_sim.call @identity
    // CHECK: obelisk_sim.call @identity
    // CHECK: obelisk_sim.return %[[FIRST]] : i32
    obelisk_sim.func @conflicting_calls(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32} {
      %one = arith.constant 1 : i32
      %two = arith.constant 2 : i32
      %first = obelisk_sim.call @identity(%ctx, %one) : (!obelisk_sim.context, i32) -> i32
      %second = obelisk_sim.call @identity(%ctx, %two) : (!obelisk_sim.context, i32) -> i32
      obelisk_sim.return %first : i32
    }

    // The recursive edge is dead for the only executable argument. SCCP uses
    // the constant branch to retain the exact base-case result.
    obelisk_sim.func private @recursive(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32} {
      %zero = arith.constant 0 : i32
      %is_zero = arith.cmpi eq, %value, %zero : i32
      cf.cond_br %is_zero, ^base, ^step
    ^base:
      %seven = arith.constant 7 : i32
      obelisk_sim.return %seven : i32
    ^step:
      %one = arith.constant 1 : i32
      %next = arith.subi %value, %one : i32
      %result = obelisk_sim.call @recursive(%ctx, %next) : (!obelisk_sim.context, i32) -> i32
      obelisk_sim.return %result : i32
    }

    // CHECK-LABEL: obelisk_sim.func @recursive_caller
    // CHECK: %[[SEVEN:.*]] = arith.constant 7 : i32
    // CHECK: obelisk_sim.return %[[SEVEN]] : i32
    obelisk_sim.func @recursive_caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32} {
      %zero = arith.constant 0 : i32
      %result = obelisk_sim.call @recursive(%ctx, %zero) : (!obelisk_sim.context, i32) -> i32
      obelisk_sim.return %result : i32
    }

    obelisk_sim.func private @leaf(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32} {
      obelisk_sim.return
    }

    obelisk_sim.func private @handle_sink(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.process {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32} {
      obelisk_sim.return
    }

    // Spawn operands flow into process formals. The scheduler handle remains
    // unknown and is forwarded as an SSA value, never as a constant.
    // CHECK-LABEL: obelisk_sim.func private @spawn_target
    // CHECK: %[[FIVE:.*]] = arith.constant 5 : i32
    // CHECK: %[[HANDLE:.*]] = obelisk_sim.spawn @leaf({{.*}}%[[FIVE]])
    // CHECK: obelisk_sim.spawn @handle_sink({{.*}}%[[HANDLE]])
    obelisk_sim.func private @spawn_target(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32} {
      %one = arith.constant 1 : i32
      %next = arith.addi %value, %one : i32
      %handle = obelisk_sim.spawn @leaf(%ctx, %next) : !obelisk_sim.context, i32 -> !obelisk_sim.process
      %forwarded = obelisk_sim.spawn @handle_sink(%ctx, %handle) : !obelisk_sim.context, !obelisk_sim.process -> !obelisk_sim.process
      obelisk_sim.return
    }

    // Constants also propagate through suspension continuation arguments.
    // CHECK-LABEL: obelisk_sim.func private @continuation
    // CHECK: %[[SIX:.*]] = arith.constant 6 : i32
    // CHECK: obelisk_sim.suspend.delay
    // CHECK: ^{{.*}}(%{{.*}}: i32):
    // CHECK: obelisk_sim.spawn @leaf({{.*}}%[[SIX]])
    obelisk_sim.func private @continuation(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32} {
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^resume(%value : i32)
    ^resume(%continued: i32):
      %handle = obelisk_sim.spawn @leaf(%ctx, %continued) : !obelisk_sim.context, i32 -> !obelisk_sim.process
      obelisk_sim.return
    }

    // Unknown control flow with equal incoming constants preserves the exact
    // CFG join fact.
    // CHECK-LABEL: obelisk_sim.func @cfg_join
    // CHECK: %[[NINE:.*]] = arith.constant 9 : i32
    // CHECK: obelisk_sim.return %[[NINE]] : i32
    obelisk_sim.func @cfg_join(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %condition: i1 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32} {
      cf.cond_br %condition, ^left, ^right
    ^left:
      %lhs = arith.constant 9 : i32
      cf.br ^join(%lhs : i32)
    ^right:
      %rhs = arith.constant 9 : i32
      cf.br ^join(%rhs : i32)
    ^join(%value: i32):
      obelisk_sim.return %value : i32
    }

    // Nested isolated operations belong to neither the enclosing function's
    // boundary summary nor its final rewrite worker.
    // CHECK-LABEL: obelisk_sim.func private @outer_with_nested
    // CHECK: %[[OUTER_ZERO:.*]] = arith.constant 0 : i32
    // CHECK: builtin.module {
    // CHECK: %[[INNER_SUM:.*]] = arith.addi
    // CHECK: obelisk_sim.return %[[INNER_SUM]] : i32
    // CHECK: }
    // CHECK: }
    // CHECK: obelisk_sim.return %[[OUTER_ZERO]] : i32
    obelisk_sim.func private @outer_with_nested(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32} {
      %zero = arith.constant 0 : i32
      builtin.module {
        obelisk_sim.func @nested_code_unit(
            %nested_ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
            attributes {entry_kind = 8 : i32} {
          %one = arith.constant 1 : i32
          %two = arith.constant 2 : i32
          %sum = arith.addi %one, %two : i32
          obelisk_sim.return %sum : i32
        }
      }
      obelisk_sim.return %zero : i32
    }

    // CHECK-LABEL: obelisk_sim.func @nested_isolation_caller
    // CHECK: %[[NESTED_OUTER_RESULT:.*]] = arith.constant 0 : i32
    // CHECK: obelisk_sim.call @outer_with_nested
    // CHECK: obelisk_sim.return %[[NESTED_OUTER_RESULT]] : i32
    obelisk_sim.func @nested_isolation_caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32} {
      %result = obelisk_sim.call @outer_with_nested(%ctx) : (!obelisk_sim.context) -> i32
      obelisk_sim.return %result : i32
    }

    // Public and nested entry arguments are unknown even when all visible
    // callsites pass the same constant.
    // CHECK: obelisk_sim.func @public_identity({{.*}}%[[PUBLIC_VALUE:[a-zA-Z0-9_]+]]: i32
    // CHECK: obelisk_sim.return %[[PUBLIC_VALUE]] : i32
    obelisk_sim.func @public_identity(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32} {
      obelisk_sim.return %value : i32
    }

    // CHECK: obelisk_sim.func nested @nested_identity({{.*}}%[[NESTED_VALUE:[a-zA-Z0-9_]+]]: i32
    // CHECK: obelisk_sim.return %[[NESTED_VALUE]] : i32
    obelisk_sim.func nested @nested_identity(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32} {
      obelisk_sim.return %value : i32
    }

    // CHECK-LABEL: obelisk_sim.func @public_boundary_caller
    // CHECK: %[[PUBLIC_RESULT:.*]] = obelisk_sim.call @public_identity
    // CHECK: obelisk_sim.return %[[PUBLIC_RESULT]] : i32
    obelisk_sim.func @public_boundary_caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32} {
      %five = arith.constant 5 : i32
      %public = obelisk_sim.call @public_identity(%ctx, %five) : (!obelisk_sim.context, i32) -> i32
      %nested = obelisk_sim.call @nested_identity(%ctx, %five) : (!obelisk_sim.context, i32) -> i32
      obelisk_sim.return %public : i32
    }

    // External declarations seed their results as unknown.
    obelisk_sim.func private @external(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32}

    // CHECK-LABEL: obelisk_sim.func @external_caller
    // CHECK: %[[EXTERNAL_RESULT:.*]] = obelisk_sim.call @external
    // CHECK: obelisk_sim.return %[[EXTERNAL_RESULT]] : i32
    obelisk_sim.func @external_caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32} {
      %five = arith.constant 5 : i32
      %result = obelisk_sim.call @external(%ctx, %five) : (!obelisk_sim.context, i32) -> i32
      obelisk_sim.return %result : i32
    }

    // A non-call symbol reference prevents visible calls from defining a
    // closed boundary.
    // CHECK: obelisk_sim.func private @address_taken({{.*}}%[[ADDRESS_VALUE:[a-zA-Z0-9_]+]]: i32
    // CHECK: obelisk_sim.return %[[ADDRESS_VALUE]] : i32
    obelisk_sim.func private @address_taken(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32} {
      obelisk_sim.return %value : i32
    }

    // CHECK-LABEL: obelisk_sim.func @address_taken_caller
    // CHECK: %[[ADDRESS_RESULT:.*]] = obelisk_sim.call @address_taken
    // CHECK: obelisk_sim.return %[[ADDRESS_RESULT]] : i32
    obelisk_sim.func @address_taken_caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32} {
      %five = arith.constant 5 : i32
      %result = obelisk_sim.call @address_taken(%ctx, %five) : (!obelisk_sim.context, i32) -> i32
      obelisk_sim.return %result : i32
    }

    // This declaration is erased by a test-only pass after input verification.
    // SCCP must then treat its call result as unknown without consulting a
    // symbol table from a worker thread.
    obelisk_sim.func private @unresolved_external(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, test.erase_before_sccp}

    // The deliberately invalid output uses generic printing.
    // UNRESOLVED-NOT: sym_name = "unresolved_external"
    // UNRESOLVED: %[[UNRESOLVED_RESULT:[0-9]+]] = "obelisk_sim.call"{{.*}}callee = @unresolved_external
    // UNRESOLVED-NEXT: "obelisk_sim.return"(%[[UNRESOLVED_RESULT]])
    // UNRESOLVED: sym_name = "unresolved_caller"
    obelisk_sim.func @unresolved_caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32} {
      %eleven = arith.constant 11 : i32
      %result = obelisk_sim.call @unresolved_external(%ctx, %eleven) : (!obelisk_sim.context, i32) -> i32
      obelisk_sim.return %result : i32
    }

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %four = arith.constant 4 : i32
      %six = arith.constant 6 : i32
      %spawned = obelisk_sim.spawn @spawn_target(%ctx, %four) : !obelisk_sim.context, i32 -> !obelisk_sim.process
      %continued = obelisk_sim.spawn @continuation(%ctx, %six) : !obelisk_sim.context, i32 -> !obelisk_sim.process
      obelisk_sim.return
    }
  }
}
