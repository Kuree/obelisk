// RUN: %talos --std=1800-2023 -emit-talos %s | talos-opt --lower-talos-to-sim=opt-level=0 | FileCheck %s
// REQUIRES: compiled-sampler

// A 32-bit ** with a random exponent is a chain of 32 conditional squarings
// (IEEE 1800-2023 11.4.3). Flattening those products into sorted monomials
// grows exponentially, so plan compilation keeps them nested and the optional
// relation analysis skips such products; both relations compile to plans.
// CHECK-COUNT-2: simulation.random.solve{{(_wide)?}} {{.*}}SAMP
module top;
  int base, exp, value, ok;
  initial begin
    ok = std::randomize(base, exp, value) with {
      base == 0; exp == -1; value == base ** exp;
    };
    ok = std::randomize(base, exp, value) with {
      base == -2; exp == -3; value == base ** exp; value != 0;
    };
  end
endmodule
