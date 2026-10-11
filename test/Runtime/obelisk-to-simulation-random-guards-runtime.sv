// REQUIRES: compiled-sampler
// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s

// IEEE 1800-2023 18.5.12 and Figure 18-3: all 16 conjunctions,
// 16 disjunctions, and four negations of FALSE/TRUE/ERROR/RANDOM.
// Repeat with rand_mode(0) so RANDOM becomes the frozen state FALSE.
// Also cover nested sifting, null guards and handle comparisons, else arms,
// state array bounds (negative, 129-bit, unknown), guarded body errors, soft
// errors, disabled constraint blocks, and a function with an unused rand input.
// Disable soft includes unused direct inputs but excludes indirect body reads.
// ERROR must fail atomically even if a solver-selected guard could hide it.
// CHECK: guard tables 1 1
// CHECK-NEXT: disabled error block 1
// CHECK-NEXT: scope guard 1
// CHECK-NEXT: scope function guard 1

class D;
  bit [3:0] value;
endclass

typedef union tagged {
  bit [3:0] first;
  bit [3:0] second;
} u_t;

function bit global_true(bit [1:0] arg);
  return 1;
endfunction

// The four guard values: FALSE, TRUE, ERROR (null handle read) and RANDOM
// (depends on the rand variable x).
`define GF 0
`define GT 1
`define GE (b.value == 5)
`define GR (x == 0)
`define G(N, GUARD) if (code == N) if (GUARD) x == 1;

class C;
  rand bit [1:0] x;
  rand bit y;
  D a, b;
  bit [3:0] array[-1:0];
  int code;
  bit [128:0] wide;
  rand bit [3:0] rarray[-1:0];
  u_t u;
  bit [3:0] dyn[];
  bit [3:0] queue[$];
  rand bit [128:0] companion;

  function bit always_true(bit [3:0] arg);
    return 1;
  endfunction
  function bit reads_state();
    return y;
  endfunction

  constraint constraints {
    // Figure 18-3 conjunction table, row by row.
    `G(0, `GF && `GF)  `G(1, `GF && `GT)  `G(2, `GF && `GE)  `G(3, `GF && `GR)
    `G(4, `GT && `GF)  `G(5, `GT && `GT)  `G(6, `GT && `GE)  `G(7, `GT && `GR)
    `G(8, `GE && `GF)  `G(9, `GE && `GT)  `G(10, `GE && `GE) `G(11, `GE && `GR)
    `G(12, `GR && `GF) `G(13, `GR && `GT) `G(14, `GR && `GE) `G(15, `GR && `GR)
    // Disjunction table.
    `G(16, `GF || `GF) `G(17, `GF || `GT) `G(18, `GF || `GE) `G(19, `GF || `GR)
    `G(20, `GT || `GF) `G(21, `GT || `GT) `G(22, `GT || `GE) `G(23, `GT || `GR)
    `G(24, `GE || `GF) `G(25, `GE || `GT) `G(26, `GE || `GE) `G(27, `GE || `GR)
    `G(28, `GR || `GF) `G(29, `GR || `GT) `G(30, `GR || `GE) `G(31, `GR || `GR)
    // Negation table.
    `G(32, !`GF) `G(33, !`GT) `G(34, !`GE) `G(35, !`GR)
    // Handle comparisons, else arms and guards whose body is the error.
    if (code == 36) if (b != null) b.value == 5;
    if (code == 37) if (b == null) x == 1;
    if (code == 38) if (b != null) b.value == 5; else x == 1;
    if (code == 39) if (0) b.value == 5;
    if (code == 40) if (x == 0) b.value == 5;
    // Nested sifting.
    `G(41, `GR && (`GE || 1))
    `G(42, ((`GR || `GE) || 1))
    if (code == 43) if (((x == 0) && (b.value == 5)) && 0) b.value == 5;
    // State array bounds: negative, valid negative, 129-bit and unknown.
    `G(44, array[-2] == 0)
    `G(45, array[-2] == 0 || 1)
    `G(46, array[wide] == 0)
    `G(47, array[32'hx] == 0)
    `G(48, array[-1] == 0)
    `G(49, a == b)
    // Soft constraints with an erroring body.
    if (code == 50) if (1) soft b.value == 5;
    if (code == 51) if (0) soft b.value == 5;
    `G(52, always_true(x) || `GE)
    // Rand arrays, unions, dynamic arrays and queues.
    `G(53, rarray[-2] == 0)
    if (code == 54) if (0) rarray[-2] == 0;
    `G(55, rarray[-2] == 0 || 1)
    `G(56, u.second == 0)
    `G(57, u.second == 0 || 1)
    `G(58, dyn[0] == 0)
    `G(59, queue[0] == 0)
    `G(60, dyn[0] == 0 || 1)
    // Function calls with rand and erroring arguments.
    `G(61, always_true(x) || 1)
    if (code == 62) if (always_true(x) && 0) b.value == 5;
    `G(63, always_true(b.value))
    `G(64, always_true(b.value) || 1)
    // disable soft covers the direct (unused) input y of always_true, but not
    // the indirect read of y inside reads_state().
    if (code == 65) if (1) {
      soft x == 0;
      soft x == always_true(y);
      disable soft y;
    }
    if (code == 66) if (1) {
      soft x == 0;
      soft x == reads_state();
      disable soft y;
    }
  }
endclass

typedef enum { ANY, ERR, X1, NZ, X01, X0 } outcome_e;

// Expected outcome per code with x random. ERR: randomize fails and nothing
// is written. X1: the guard is TRUE and x == 1. NZ: the guard is RANDOM, so
// x == 0 would force x == 1 and is excluded. X01 and X0 follow from the
// negated RANDOM guard and from the surviving soft constraint.
function automatic outcome_e expect_random(int code);
  case (code)
    6, 9, 10, 11, 14, 18, 24, 26, 27, 30, 34, 40, 44, 46, 47, 50, 52, 53, 56,
    58, 59, 63: return ERR;
    5, 17, 20, 21, 22, 23, 25, 29, 32, 37, 38, 42, 45, 48, 49, 55, 57, 60, 61,
    64, 66: return X1;
    7, 13, 15, 19, 28, 31, 41: return NZ;
    35: return X01;
    65: return X0;
    default: return ANY;
  endcase
endfunction

// Expected outcome with x frozen at 1: RANDOM guards become FALSE (x != 0),
// so only the other guard values decide.
function automatic outcome_e expect_frozen(int code);
  case (code)
    6, 9, 10, 18, 24, 26, 27, 30, 34, 44, 46, 47, 50, 53, 56, 58, 59,
    63: return ERR;
    5, 17, 20, 21, 22, 23, 25, 29, 32, 35, 37, 38, 42, 45, 48, 49, 52, 55, 57,
    60, 61, 64, 66: return X1;
    default: return ANY;
  endcase
endfunction

function automatic bit conforms(outcome_e kind, int status, bit [1:0] x,
                                bit [1:0] x_before);
  case (kind)
    ERR: return status == 0 && x == x_before;
    ANY: return status == 1;
    X1: return status == 1 && x == 1;
    NZ: return status == 1 && x != 0;
    X01: return status == 1 && x <= 1;
    X0: return status == 1 && x == 0;
  endcase
endfunction

module top;
  C object = new;
  int status;
  int active;
  int frozen;
  bit [1:0] scope_x;
  bit [1:0] scope_y;
  bit scope_enable;
  int scope_passed;
  int scope_function_passed;
  D scope_null;
  bit [1:0] x_before;
  bit [128:0] companion_before;

  initial begin
    object.b = null;
    object.a = null;
    object.wide = 129'h1_0000_0000_0000_0000;
    object.u = tagged first 4'd0;
    object.x.rand_mode(1);
    for (int index = 0; index < 67; index++) begin
      object.code = index;
      object.x = 3;
      object.y = 1;
      x_before = object.x;
      companion_before = object.companion;
      status = object.randomize();
      active += conforms(expect_random(index), status, object.x, x_before) &&
                (expect_random(index) != ERR ||
                 object.companion == companion_before);
    end
    object.x.rand_mode(0);
    for (int index = 0; index < 67; index++) begin
      object.code = index;
      object.x = 1;
      object.y = 1;
      x_before = object.x;
      companion_before = object.companion;
      status = object.randomize();
      frozen += conforms(expect_frozen(index), status, object.x, x_before) &&
                (expect_frozen(index) != ERR ||
                 object.companion == companion_before);
    end
    $display("guard tables %0d %0d", active == 67, frozen == 67);

    // A disabled constraint block does not evaluate its guards even when one
    // would be an error.
    object.x.rand_mode(1);
    object.code = 10;
    object.constraints.constraint_mode(0);
    status = object.randomize();
    $display("disabled error block %0d", status == 1);

    // Inline scope constraints use the same guard sifting: the first call
    // sees an ERROR guard, the second a TRUE guard.
    scope_enable = 0;
    repeat (2) begin
      scope_x = 3;
      status = std::randomize(scope_x) with {
        (scope_x == 0 || scope_null.value == 5 || scope_enable) ->
            scope_x == 1;
      };
      scope_passed += status == scope_enable &&
                      ((!scope_enable && scope_x == 3) ||
                       (scope_enable && scope_x == 1));
      scope_enable = 1;
    end
    $display("scope guard %0d", scope_passed == 2);

    // A scope function with an unused rand input is RANDOM, not TRUE, so the
    // TRUE disjunct decides.
    repeat (8) begin
      scope_x = 3;
      scope_y = 3;
      status = std::randomize(scope_x, scope_y) with {
        (global_true(scope_y) || scope_enable) -> scope_x == 1;
        scope_y == scope_x;
      };
      scope_function_passed += status == 1 && scope_x == 1 && scope_y == 1;
    end
    $display("scope function guard %0d", scope_function_passed == 8);
  end
endmodule
