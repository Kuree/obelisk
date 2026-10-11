// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s
// REQUIRES: z3

// IEEE 1800-2023 11.3.4, 11.4.3 Table 11-4 and 18.3:
// Z3 must prove each of the four relations below unsatisfiable at compile
// time, one warning per randomize call and in source order:
//   a variable divisor required to be zero;
//   zero raised to a negative signed exponent;
//   a nonzero two-state cast of (n/0 + n), whose whole result is unknown;
//   a nonzero integer reciprocal of -2 raised to -3.
// These exercise the value and unknown planes independently of runtime draws.
// CHECK: [[@LINE+12]]:{{[0-9]+}}: warning: randomize hard constraints are statically unsatisfiable (z3-{{[0-9.]+}})
// CHECK: [[@LINE+12]]:{{[0-9]+}}: warning: randomize hard constraints are statically unsatisfiable (z3-{{[0-9.]+}})
// CHECK: [[@LINE+12]]:{{[0-9]+}}: warning: randomize hard constraints are statically unsatisfiable (z3-{{[0-9.]+}})
// CHECK: [[@LINE+12]]:{{[0-9]+}}: warning: randomize hard constraints are statically unsatisfiable (z3-{{[0-9.]+}})

module top;
  typedef bit [3:0] nib_t;
  int ok;
  bit [3:0] n, d, q;
  bit signed [3:0] base, exp;
  bit signed [7:0] value;
  initial begin
    ok = randomize(n, d, q) with { d == 0; q == n / d; };
    ok = randomize(base, exp, value) with { base == 4'sd0; exp == 4'sb1111; value == base ** exp; };
    ok = randomize(n, q) with { q == nib_t'(n / 4'd0 + n); q != 0; };
    ok = randomize(base, exp, value) with { base == 4'sb1110; exp == 4'sb1101; value != base ** exp; value == 8'sd0; };
  end
endmodule
