// RUN: %obelisk --std=1800-2023 -O0 -emit-sim %s -o %t.sim.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.sim.mlir
// RUN: obelisk-opt %t.sim.mlir --obelisk-sim-compile-random-plans | diff %t.sim.mlir -
// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// REQUIRES: compiled-sampler

// IEEE 1800-2023 6.12.1 and 20.5: numeric casts after IEEE arithmetic.
// An eight-bit candidate visits 256 adjacent payloads around positive and
// negative halfway boundaries at both precisions. Each numeric conversion and
// finite test observes ((value + 0.5) - 0.5) * 2.0 / 2.0. Intermediate rounding
// can change payloads; these selected intervals retain their integral results.
// Cover 1/32/130-bit casts, truncation, exact promotion, selection, negation,
// values above 2^65, and singleton solves that must preserve sampler RNG state.
// All four arithmetic operations discard direct payload provenance and require
// the compiler to export an exact plan without conversion witnesses.
// PLAN-COUNT-2: simulation.random.solve %{{.*}}SAMP
// PLAN-COUNT-2: simulation.random.solve_wide %{{.*}}SAMP
// CHECK: computed real integer plans valid 1

// Adjacent payloads around 1.5 as binary32 and binary64, and around 2^65.
`define S32P $bitstoshortreal(32'h3fbfff80 + 32'(v))
`define S32N $bitstoshortreal(32'hbfbfff80 + 32'(v))
`define D64P $bitstoreal(64'h3ff7ffffffffff80 + 64'(v))
`define D64N $bitstoreal(64'hbff7ffffffffff80 + 64'(v))
`define D64W $bitstoreal(64'h4400000000000000 + 64'(v))
`define D64WN $bitstoreal(64'hc400000000000000 + 64'(v))
// The same arithmetic chain at binary64 and at binary32 precision.
`define A64(x) (((((x) + 0.5) - 0.5) * 2.0) / 2.0)
`define A32(x) (((((x) + shortreal'(0.5)) - shortreal'(0.5)) * shortreal'(2.0)) / shortreal'(2.0))
`define SEL(x) (wanted < 128 ? real'(x) : real'(x))

typedef bit [129:0] Wide;
typedef bit [0:0] One;

class Narrow;
  rand bit [7:0] v;
  int wanted;
  constraint pick { v == wanted; }
  constraint single {
    int'(`A64(`SEL(`S32P))) == (wanted < 128 ? 1 : 2);
    $rtoi(`A64(`SEL(`S32P))) == 1;
    One'(`A32(`S32P)) == (wanted < 128 ? 1'b1 : 1'b0);
    !$isunknown(integer'(`A32(`S32P)));
    int'(`A64(-`SEL(`S32P))) == (wanted < 128 ? -1 : -2);
    $rtoi(`A64(-`SEL(`S32P))) == -1;
    One'(`A32(`S32N)) == (wanted < 128 ? 1'b1 : 1'b0);
    !$isunknown(integer'(`A32(`S32N)));
  }
  constraint double {
    int'(`A64(`D64P)) == (wanted < 128 ? 1 : 2);
    $rtoi(`A64(`D64P)) == 1;
    One'(`A64(`D64P)) == (wanted < 128 ? 1'b1 : 1'b0);
    !$isunknown(integer'(`A64(`D64P)));
    int'(`A64(`D64N)) == (wanted < 128 ? -1 : -2);
    $rtoi(`A64(`D64N)) == -1;
    One'(`A64(`D64N)) == (wanted < 128 ? 1'b1 : 1'b0);
    !$isunknown(integer'(`A64(`D64N)));
  }
endclass

class WideCasts;
  rand bit [7:0] v;
  int wanted;
  constraint pick { v == wanted; }
  constraint single {
    Wide'(`A64(`SEL(`S32P))) == (wanted < 128 ? Wide'(1) : Wide'(2));
    Wide'($rtoi(`A64(`SEL(`S32P)))) == 1;
    One'(`A32(`S32P)) == (wanted < 128 ? 1'b1 : 1'b0);
    !$isunknown(integer'(`A32(`S32P)));
    Wide'(`A64(-`SEL(`S32P))) == (wanted < 128 ? -Wide'(1) : -Wide'(2));
    Wide'($rtoi(`A64(-`SEL(`S32P)))) == -Wide'(1);
    One'(`A32(`S32N)) == (wanted < 128 ? 1'b1 : 1'b0);
    !$isunknown(integer'(`A32(`S32N)));
  }
  constraint double {
    Wide'(`A64(`D64P)) == (wanted < 128 ? Wide'(1) : Wide'(2));
    Wide'($rtoi(`A64(`D64P))) == 1;
    One'(`A64(`D64P)) == (wanted < 128 ? 1'b1 : 1'b0);
    !$isunknown(integer'(`A64(`D64P)));
    Wide'(`A64(`D64N)) == (wanted < 128 ? -Wide'(1) : -Wide'(2));
    Wide'($rtoi(`A64(`D64N))) == -Wide'(1);
    One'(`A64(`D64N)) == (wanted < 128 ? 1'b1 : 1'b0);
    !$isunknown(integer'(`A64(`D64N)));
  }
  // Values above 2^65 convert exactly into the 130-bit result.
  constraint huge {
    Wide'(`A64(`D64W)) == (Wide'(1) << 65) + (Wide'(wanted) << 13);
    Wide'(`A64(`D64WN)) == -((Wide'(1) << 65) + (Wide'(wanted) << 13));
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
    $display("computed real integer plans valid %0d", valid);
  end
endmodule
