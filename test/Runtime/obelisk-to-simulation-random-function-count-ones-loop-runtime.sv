// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt --lower-obelisk-to-sim=opt-level=0 | FileCheck %s --check-prefix=IR
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.5.11, the count_ones example: a stateless method of the randomized class.
// a for loop clears the packed input one bit per iteration and updates the
// return variable. The exact bound is the input width. Function argument
// priority remains independent: x == count_ones(y), x == 0 succeeds for one of
// four higher-stage y choices. Failure preserves fields. Frozen values
// exercise zero and full-width cases.
// The bounded loop is expanded into the Z3 sampling plan.
// IR: simulation.random.solve
// IR-SAME: "ODR1

// CHECK: loop count values valid 1
// CHECK-NEXT: loop priority rollback 1
// CHECK-NEXT: loop priority failures 1
// CHECK-NEXT: frozen loop values 1
// CHECK-NEXT: frozen loop failure 1

class C;
  rand bit [3:0] x = 1;
  rand bit [9:0] y = 512;
  bit only_zero = 0;
  function automatic int count_ones(bit [9:0] w);
    for (count_ones = 0; w != 0; w = w >> 1)
      count_ones += w & 1'b1;
  endfunction
  constraint relation {
    x == count_ones(y);
    y inside {0, 512, 1023, 384};
  }
  constraint limit { if (only_zero) x == 0; }
endclass
module top;
  C object = new;
  int status;
  bit valid = 1;
  int accepted = 0;
  bit [3:0] oldx;
  bit [9:0] oldy;
  initial begin
    repeat (64) begin
      status = object.randomize();
      valid &= status == 1 && object.y inside {0, 512, 1023, 384} &&
               object.x == $countones(object.y);
    end
    $display("loop count values valid %0d", valid);

    object.only_zero = 1;
    valid = 1;
    repeat (256) begin
      oldx = object.x;
      oldy = object.y;
      status = object.randomize();
      valid &= (status == 0 -> (object.x == oldx && object.y == oldy)) &&
               (status == 1 -> (object.x == 0 && object.y == 0));
      accepted += status;
    end
    $display("loop priority rollback %0d", valid);
    $display("loop priority failures %0d", accepted > 32 && accepted < 96);

    object.only_zero = 0;
    object.y.rand_mode(0);
    object.y = 0;
    status = object.randomize();
    valid &= status == 1 && object.x == 0;
    object.y = 512;
    status = object.randomize();
    valid &= status == 1 && object.x == 1;
    object.y = 1023;
    status = object.randomize();
    valid &= status == 1 && object.x == 10;
    object.y = 384;
    status = object.randomize();
    valid &= status == 1 && object.x == 2;
    $display("frozen loop values %0d", valid);

    object.only_zero = 1;
    oldx = object.x;
    status = object.randomize();
    $display("frozen loop failure %0d", status == 0 && object.x == oldx);
  end
endmodule
