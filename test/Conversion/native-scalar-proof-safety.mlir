// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(fold-native-scalar-values)))' | FileCheck %s
// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(fold-native-scalar-values)))' -o %t.threaded
// RUN: talos-opt %s --mlir-disable-threading --pass-pipeline='builtin.module(simulation.design(simulation.func(fold-native-scalar-values)))' -o %t.serial
// RUN: diff %t.threaded %t.serial

module {
  simulation.design @proof_safety {
    simulation.scope.decl 0
    simulation.func private @opaque(!simulation.context {simulation.capture_kind = 0 : i32}) -> !simulation.logic<8> attributes {entry_kind = 8 : i32}
    simulation.func private @opaque_bits(!simulation.context {simulation.capture_kind = 0 : i32}) -> i8 attributes {entry_kind = 8 : i32}

    // Expressions that do not fit the call must neither crash LLVM builders
    // nor certify an opaque logic result as excluding X/Z. Malformed ones are
    // rejected by the attribute verifier.
    simulation.code_unit.decl 1 in 0 function hierarchy "bad_expressions"
    simulation.func @bad_expressions(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %seed: i8 {simulation.capture_kind = 2 : i32}) -> (!simulation.logic<8>, !simulation.logic<8>, !simulation.logic<8>) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %missing = simulation.call @opaque(%ctx) {simulation.result_exprs = [unit], simulation.known_logic_result} : (!simulation.context) -> !simulation.logic<8>
      %index = simulation.call @opaque(%ctx) {simulation.result_exprs = [#simulation.integer_expression<kind = argument, width = 8, index = 99>]} : (!simulation.context) -> !simulation.logic<8>
      %mismatch = simulation.call @opaque(%ctx) {simulation.result_exprs = [#simulation.integer_expression<kind = constant, width = 16, value = 0 : i16>]} : (!simulation.context) -> !simulation.logic<8>
      simulation.return %missing, %index, %mismatch : !simulation.logic<8>, !simulation.logic<8>, !simulation.logic<8>
    }
    // CHECK-LABEL: simulation.func @bad_expressions
    // CHECK-NOT: simulation.known_logic_result
    // CHECK-NOT: simulation.logic.constant
    // CHECK: simulation.return

    // Widths and the intersection of signed/unsigned intervals are checked
    // before an integer-range certificate can drive constant replacement.
    simulation.code_unit.decl 2 in 0 function hierarchy "bad_ranges"
    simulation.func @bad_ranges(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> (i8, i8) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %width = simulation.call @opaque_bits(%ctx) {simulation.result_ranges = [#simulation.integer_range<umin = 0 : i16, umax = 0 : i16, smin = 0 : i16, smax = 0 : i16>]} : (!simulation.context) -> i8
      %empty = simulation.call @opaque_bits(%ctx) {simulation.result_ranges = [#simulation.integer_range<umin = 0 : i8, umax = 0 : i8, smin = 1 : i8, smax = 1 : i8>]} : (!simulation.context) -> i8
      simulation.return %width, %empty : i8, i8
    }
    // CHECK-LABEL: simulation.func @bad_ranges
    // CHECK-NOT: arith.constant
    // CHECK: simulation.return

    // IEEE 1800-2023 6.11.2: a conversion to two-state bits maps X/Z to
    // zero. Converting those bits back to logic therefore excludes X/Z.
    simulation.code_unit.decl 3 in 0 function hierarchy "two_state_conversion"
    simulation.func @two_state_conversion(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.logic<8> {simulation.capture_kind = 2 : i32}) -> !simulation.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %bits = simulation.logic.to_bits %input : !simulation.logic<8> -> i8
      %known = simulation.logic.from_bits %bits : i8 -> !simulation.logic<8>
      %result = simulation.logic.binary sub %known, %known : !simulation.logic<8>
      simulation.return %result : !simulation.logic<8>
    }
    // CHECK-LABEL: simulation.func @two_state_conversion
    // CHECK: %[[ZERO:.*]] = simulation.logic.constant 0 : i8, 0 : i8
    // CHECK-NEXT: simulation.return %[[ZERO]]

    // IEEE 1800-2023 11.4.3: even known operands can produce X (division
    // by zero). Unsupported logic producers must retain the unknown plane.
    simulation.code_unit.decl 4 in 0 function hierarchy "division_unknown"
    simulation.func @division_unknown(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: i8 {simulation.capture_kind = 2 : i32}) -> !simulation.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %known = simulation.logic.from_bits %input : i8 -> !simulation.logic<8>
      %divisor = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      %quotient = simulation.logic.binary udiv %known, %divisor : !simulation.logic<8>
      %difference = simulation.logic.binary sub %quotient, %quotient : !simulation.logic<8>
      simulation.return %difference : !simulation.logic<8>
    }
    // CHECK-LABEL: simulation.func @division_unknown
    // CHECK: simulation.logic.binary udiv
    // CHECK-NOT: simulation.known_logic_result
    // CHECK: %[[DIFF:.*]] = simulation.logic.binary sub
    // CHECK-NEXT: simulation.return %[[DIFF]]
  }
}
