// RUN: %talos --std=1800-2023 -O0 -emit-sim %s -o %t.sim.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.sim.mlir
// RUN: talos-opt %t.sim.mlir --talos-sim-compile-random-plans | diff %t.sim.mlir -
// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// REQUIRES: compiled-sampler

// IEEE 1800-2023 6.12.1 and 20.5: compiler Z3 numeric integral conversions.
// An eight-bit candidate visits 256 adjacent IEEE payloads around each positive
// and negative halfway boundary at both precisions. Exact binary32 promotion,
// conditional promotion, negation, one-bit, 32-bit and 130-bit casts,
// truncation, finite classification and wide values above 2^65 preserve every
// selector and the singleton solve RNG.
// PLAN-COUNT-2: simulation.random.solve %{{.*}}SAMP
// PLAN-COUNT-2: simulation.random.solve_wide %{{.*}}SAMP
// CHECK: real integer plans valid 1

// Adjacent payloads around 1.5 as binary32 and binary64, and around 2^65.
`define S32P $bitstoshortreal(32'h3fbfff80 + 32'(v))
`define S32N $bitstoshortreal(32'hbfbfff80 + 32'(v))
`define D64P $bitstoreal(64'h3ff7ffffffffff80 + 64'(v))
`define D64N $bitstoreal(64'hbff7ffffffffff80 + 64'(v))
`define D64W $bitstoreal(64'h4400000000000000 + 64'(v))
`define D64WN $bitstoreal(64'hc400000000000000 + 64'(v))

typedef bit [129:0] Wide;
typedef bit [0:0] One;

class Narrow;
  rand bit [7:0] v;
  int wanted;
  constraint pick { v == wanted; }
  // Exact promotion through a conditional and negation, 32-bit rounding and
  // truncation, and 1-bit rounding.
  constraint single {
    int'(wanted < 128 ? real'(`S32P) : real'(`S32P)) == (wanted < 128 ? 1 : 2);
    $rtoi(wanted < 128 ? real'(`S32P) : real'(`S32P)) == 1;
    One'(`S32P) == (wanted < 128 ? 1'b1 : 1'b0);
    !$isunknown(integer'(`S32P));
    int'(-(wanted < 128 ? real'(`S32P) : real'(`S32P))) == (wanted < 128 ? -1 : -2);
    $rtoi(-(wanted < 128 ? real'(`S32P) : real'(`S32P))) == -1;
    One'(`S32N) == (wanted < 128 ? 1'b1 : 1'b0);
    !$isunknown(integer'(`S32N));
  }
  constraint double {
    int'(`D64P) == (wanted < 128 ? 1 : 2);
    $rtoi(`D64P) == 1;
    One'(`D64P) == (wanted < 128 ? 1'b1 : 1'b0);
    !$isunknown(integer'(`D64P));
    int'(`D64N) == (wanted < 128 ? -1 : -2);
    $rtoi(`D64N) == -1;
    One'(`D64N) == (wanted < 128 ? 1'b1 : 1'b0);
    !$isunknown(integer'(`D64N));
  }
endclass

class WideCasts;
  rand bit [7:0] v;
  int wanted;
  constraint pick { v == wanted; }
  constraint single {
    Wide'(wanted < 128 ? real'(`S32P) : real'(`S32P)) == (wanted < 128 ? Wide'(1) : Wide'(2));
    Wide'($rtoi(wanted < 128 ? real'(`S32P) : real'(`S32P))) == 1;
    One'(`S32P) == (wanted < 128 ? 1'b1 : 1'b0);
    !$isunknown(integer'(`S32P));
    Wide'(-(wanted < 128 ? real'(`S32P) : real'(`S32P))) == (wanted < 128 ? -Wide'(1) : -Wide'(2));
    Wide'($rtoi(-(wanted < 128 ? real'(`S32P) : real'(`S32P)))) == -Wide'(1);
    One'(`S32N) == (wanted < 128 ? 1'b1 : 1'b0);
    !$isunknown(integer'(`S32N));
  }
  constraint double {
    Wide'(`D64P) == (wanted < 128 ? Wide'(1) : Wide'(2));
    Wide'($rtoi(`D64P)) == 1;
    One'(`D64P) == (wanted < 128 ? 1'b1 : 1'b0);
    !$isunknown(integer'(`D64P));
    Wide'(`D64N) == (wanted < 128 ? -Wide'(1) : -Wide'(2));
    Wide'($rtoi(`D64N)) == -Wide'(1);
    One'(`D64N) == (wanted < 128 ? 1'b1 : 1'b0);
    !$isunknown(integer'(`D64N));
  }
  // Values above 2^65 convert exactly into the 130-bit result.
  constraint huge {
    Wide'(`D64W) == (Wide'(1) << 65) + (Wide'(wanted) << 13);
    Wide'(`D64WN) == -((Wide'(1) << 65) + (Wide'(wanted) << 13));
  }
endclass

module top;
  Narrow n = new;
  WideCasts w = new;
  bit valid = 1;
  string prior;
  initial begin
    for (int k = 0; k < 256; k++) begin
      n.wanted = k;
      prior = n.get_randstate();
      valid &= n.randomize() && n.v == k && n.get_randstate() == prior;
      w.wanted = k;
      prior = w.get_randstate();
      valid &= w.randomize() && w.v == k && w.get_randstate() == prior;
    end
    $display("real integer plans valid %0d", valid);
  end
endmodule
