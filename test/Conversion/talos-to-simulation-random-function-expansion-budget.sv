// RUN: %talos --std=1800-2023 -emit-talos %s | not talos-opt --lower-talos-to-sim=opt-level=0 -o /dev/null 2>&1 | FileCheck %s

// Constraint function calls expand into the constraint's expression tree, and
// each use of a computed local copies its expression. Fifteen iterations of
// t = (t + t) - t would copy 3^15 trees; the expansion is bounded and
// diagnosed instead of exhausting compile time.
class C;
  rand bit [7:0] v;
  function automatic bit [7:0] f(bit [7:0] x);
    bit [7:0] t = x;
    repeat (15) t = (t + t) - t;
    return t;
  endfunction
  // CHECK: [[@LINE+1]]:{{[0-9]+}}: error: constraint function calls expand to more than 1048576 expression nodes
  constraint c { f(v) >= 10; }
endclass

module top;
  C c = new;
  initial void'(c.randomize());
endmodule
