// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt --lower-obelisk-to-sim=opt-level=0 | FileCheck %s --check-prefix=IR
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.5.11 and 18.12: the automatic statement-body function
// called from a std::randomize with-constraint gives its argument an
// independent priority stage, with the same failure and rollback as a class
// object: y solves first, x == id(y) is then checked against `x == 0`, and a
// failed call leaves x and y unchanged.
// The statement body is folded into the Z3 sampling plan rather than rejected.
// IR: simulation.random.solve
// IR-SAME: "ODR1

// CHECK: function priority valid 1
// CHECK-NEXT: function priority failures 1
module top;
  bit x = 1, y = 1;
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
  bit valid = 1;
  int accepted = 0;
  int status;
  bit oldx, oldy;
  initial begin
    repeat (256) begin
      oldx = x;
      oldy = y;
      status = std::randomize(x, y) with { x == id(y); x == 0; };
      valid &= (status == 0 -> (x == oldx && y == oldy)) &&
               (status != 0 -> (x == 0 && y == 0));
      accepted += status;
    end
    $display("function priority valid %0d", valid);
    $display("function priority failures %0d", accepted > 96 && accepted < 160);
  end
endmodule
