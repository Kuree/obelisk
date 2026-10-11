// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt --lower-obelisk-to-sim=opt-level=0 | FileCheck %s --check-prefix=IR
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.5.11, 18.12 and 12.7.2: bounded repeat loops in a constraint
// function keep lexical break/continue targets, early returns, local
// reinitialization and standalone updates of automatic locals and copied input
// arguments. Negative and zero counts do not execute, X/Z counts are zero, and
// a two-state cast of 2'b1x keeps the known high bit. The initializer after
// `continue` is never evaluated. Every wrong path returns !value, so the
// identity result keeps its independent argument priority.
// The loops are expanded into the Z3 sampling plan instead of being rejected.
// IR: simulation.random.solve
// IR-SAME: "ODR1

// CHECK: function priority valid 1
// CHECK-NEXT: function priority failures 1
typedef bit [1:0] pair_t;
module top;
  bit [0:0] x = 1;
  bit y = 1;
  function automatic bit id(bit value);
    logic [1:0] uninitialized;
    bit [7:0] counter;
    if (uninitialized) return !value;
    repeat (3) begin
      bit [7:0] fresh;
      fresh++;
      if (fresh != 1) return !value;
      counter++;
      if (counter == 1) begin
        continue;
        begin
          bit unused = x[1];
        end
      end
      if (counter == 2) break;
    end
    if (counter != 2) return !value;
    repeat (-1) begin return !value; end
    repeat (0) begin return !value; end
    repeat (32'b1x) begin return !value; end
    repeat (32'bz) begin return !value; end
    counter = 0;
    repeat (pair_t'(2'b1x)) counter++;
    if (counter != 2) return !value;
    counter = 0;
    repeat (3) begin
      repeat (4) begin break; end
      counter++;
      break;
    end
    if (counter != 1) return !value;
    counter = 0;
    repeat (3) begin
      repeat (2) begin break; end
      ++counter;
      continue;
      counter = 255;
    end
    if (counter != 3) return !value;
    value += 1;
    value -= 1;
    ++value;
    value--;
    repeat (1) begin return value; end
    return !value;
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
