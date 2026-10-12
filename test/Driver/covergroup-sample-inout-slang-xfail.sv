// XFAIL: *
// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | FileCheck %s

// IEEE 1800-2023 19.8.1 prohibits output sample arguments, but permits inout.
// Pinned Slang v11.0 rejects inout before producing an AST. Keep the legal
// source as an expected failure without patching the frontend dependency.
module sample_inout;
  int value;
  covergroup cg with function sample(inout int sample_value);
    cp: coverpoint sample_value;
  endgroup
  cg cov;
  initial begin
    cov = new;
    cov.sample(value);
  end
endmodule

// CHECK: obelisk.sv.symbol.formal_argument
// CHECK-SAME: is_coverage_sample_formal
