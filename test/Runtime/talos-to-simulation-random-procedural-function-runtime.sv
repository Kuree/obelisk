// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// RUN: %talos --std=1800-2023 -emit-talos %s | talos-opt --lower-talos-to-sim=opt-level=0 | FileCheck %s --check-prefix=IR
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.5.11: automatic local assignments, truncation, compound
// RHS, mutable input copies, if/else, early return, return-variable assignment
// and fallthrough preserve one-way function priority. The input y is solved
// independently of the output x, so this identity function makes the
// `x == 0` constraint fail whenever y solves to 1 (about half the solves), and a
// failed solve leaves the random properties unchanged (18.6.3).
// The statement body is folded into the Z3 sampling plan rather than rejected.
// IR: simulation.random.solve
// IR-SAME: "ODR1

// CHECK: function priority valid 1
// CHECK-NEXT: function priority failures 1
class C;
  rand bit x, y;
  function automatic bit id(bit value);
    bit [2:0] bias = 7;
    bit [2:0] temp = 0;
    temp = value + bias;
    temp += 1;
    if (temp == 0) return 0;
    temp = temp ^ 1;
    value = temp;
    if (value == 0) begin
      id = 1;
      return id;
    end
    id = 0;
  endfunction
  constraint relation { x == id(y); }
  constraint limit { x == 0; }
endclass
module top;
  C object = new;
  bit valid = 1;
  int accepted = 0;
  int status;
  bit oldx, oldy;
  initial begin
    repeat (256) begin
      oldx = object.x;
      oldy = object.y;
      status = object.randomize();
      valid &= (status == 0 -> (object.x == oldx && object.y == oldy)) &&
               (status != 0 -> (object.x == 0 && object.y == 0));
      accepted += status;
    end
    $display("function priority valid %0d", valid);
    $display("function priority failures %0d", accepted > 96 && accepted < 160);
  end
endmodule
