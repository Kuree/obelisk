// REQUIRES: compiled-sampler
// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s

// IEEE 1800-2023 18.5.11 and 18.5.12: executed automatic initializers are
// evaluated even when their values do not contribute to the return value. A
// null state read produces ERROR when its branch is taken; the inactive branch
// is safe. Failure does not write the random fields, and a later call can
// recover. An unknown condition takes the false arm and a condition with a
// known one bit takes the true arm (procedural four-state truth).
// CHECK: inactive body read 1 1
// CHECK-NEXT: unused initializer error 1 1
// CHECK-NEXT: body guard recovery 1 1
// CHECK-NEXT: unknown condition false 1 1
// CHECK-NEXT: known one condition true 1 1

class D;
  bit [2:0] v;
endclass

class C;
  rand bit x;
  rand bit y;
  logic [2:0] enabled;
  D n;
  function automatic bit id(bit value);
    if (enabled) begin
      bit [2:0] unused = n.v;
    end
    return value;
  endfunction
  constraint relation { x == id(y); }
  constraint limit { x == 0; }
endclass

module top;
  C object = new;
  int status;
  initial begin
    object.y = 0;
    object.y.rand_mode(0);
    // enabled starts as x, so the guarded read is not executed.
    status = object.randomize();
    $display("inactive body read %0d %0d", status == 1, object.x == 0);

    // A failed solve leaves the random field unchanged.
    object.x = 1;
    object.enabled = 2;
    status = object.randomize();
    $display("unused initializer error %0d %0d", status == 0, object.x == 1);

    object.enabled = 0;
    status = object.randomize();
    $display("body guard recovery %0d %0d", status == 1, object.x == 0);

    object.x = 1;
    object.enabled = 3'bxxx;
    status = object.randomize();
    $display("unknown condition false %0d %0d", status == 1, object.x == 0);

    object.x = 1;
    object.enabled = 3'b1x0;
    status = object.randomize();
    $display("known one condition true %0d %0d", status == 0, object.x == 1);
  end
endmodule
