// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t +ntb_random_seed=13 | FileCheck %s
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.5.11 and 12.5: a constraint function's case body is
// evaluated only when its item is selected. The null dereference in the
// initializer is an evaluation error, so randomize() fails and commits no
// property writes when the item is selected, and is never evaluated when the
// selector does not match. The selector uses case equality, so an unknown
// condition matches no label, while 3'b1x0 is known true under 11.4.7.
// CHECK: inactive body read 1 1
// CHECK-NEXT: unused initializer error 0 1
// CHECK-NEXT: body guard recovery 1 1
// CHECK-NEXT: unknown condition false 1 1
// CHECK-NEXT: known one condition true 0 1

class D;
  bit [2:0] v;
endclass

class C;
  rand bit x = 1;
  bit y = 0;
  logic [2:0] enabled;
  D n;

  function bit id(bit value);
    case (!(!enabled))
      1'b1: begin
        bit [2:0] unused = n.v;
      end
    endcase
    return value;
  endfunction

  constraint relation { x == id(y); }
  constraint limit { x == 1'b0; }
endclass

module top;
  C object = new;
  int status;
  initial begin
    object.x = 1;
    status = object.randomize();
    $display("inactive body read %0d %0d", status, object.x == 0);

    object.enabled = 3'd2;
    object.x = 1;
    status = object.randomize();
    $display("unused initializer error %0d %0d", status, object.x == 1);

    object.enabled = 3'd0;
    status = object.randomize();
    $display("body guard recovery %0d %0d", status, object.x == 0);

    object.enabled = 3'bxxx;
    object.x = 1;
    status = object.randomize();
    $display("unknown condition false %0d %0d", status, object.x == 0);

    object.enabled = 3'b1x0;
    object.x = 1;
    status = object.randomize();
    $display("known one condition true %0d %0d", status, object.x == 1);
  end
endmodule
