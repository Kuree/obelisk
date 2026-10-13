// RUN: %obelisk --std=1800-2023 -O0 --target=native --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov > %t.native.out
// RUN: FileCheck %s < %t.native.out
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out

// IEEE 1800-2023 11.4.11: an inactive arm must not be evaluated. A
// constructor call in an arm keeps the expression's short-circuit evaluator.
module top;
  bit [1:0] value;
  bit select;
  function automatic bit forbidden();
    $fatal(1, "inactive conditional arm was evaluated");
    return 1;
  endfunction
  covergroup cg;
    option.cross_retain_auto_bins = 0;
    bit [1:0] ca: coverpoint value { bins all = {[0:3]}; }
    bit cb: coverpoint select { bins zero = {0}; }
    product: cross ca, cb {
      bins selected = product with (cb ? forbidden() : ca == 0);
    }
  endgroup
  cg cov;
  initial begin
    cov = new;
    cov.sample();
    $display("short circuit %0.0f", cov.product.get_inst_coverage());
    $finish;
  end
endmodule
// CHECK: short circuit 100
