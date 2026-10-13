// RUN: %obelisk --std=1800-2023 -O0 --target=native --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov > %t.native.out
// RUN: FileCheck %s < %t.native.out
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out

// IEEE 1800-2023 11.4.12.1: even a zero multiplier evaluates its payload
// exactly once. Each cross evaluates two candidate value tuples.
module top;
  bit value;
  int calls;
  function automatic bit tick();
    ++calls;
    return 0;
  endfunction
  covergroup cg;
    option.cross_retain_auto_bins = 0;
    bit ca: coverpoint value { bins all = {[0:1]}; }
    bit cb: coverpoint 1'b0 { bins zero = {0}; }
    zero_size: cross ca, cb {
      bins selected = zero_size with ({{0{tick()}}, ca} === 1'b0);
    }
    positive_size: cross ca, cb {
      bins selected = positive_size with ({{3{tick()}}, ca} === 4'b0);
    }
  endgroup
  cg cov;
  initial begin
    cov = new;
    cov.sample();
    $display("calls %0d zero %0.0f positive %0.0f", calls,
             cov.zero_size.get_inst_coverage(), cov.positive_size.get_inst_coverage());
    $finish;
  end
endmodule
// CHECK: calls 4 zero 100 positive 100
