// RUN: %obelisk -fno-lto --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=at-least-zero > %t.native.txt
// RUN: %obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=at-least-zero > %t.bytecode.txt
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s < %t.native.txt

module top;
  bit value;

  covergroup cg;
    cp: coverpoint value {
      option.at_least = 0;
      bins zero = {0};
      bins one = {1};
    }
  endgroup

  cg cov;
  initial begin
    cov = new;
    $display("coverage %.6f", cov.get_inst_coverage());
    $finish;
  end
endmodule

// IEEE 1800-2023 19.7 does not impose a positive lower bound on at_least.
// Both bins satisfy a minimum hit count of zero before the first sample.
// CHECK: coverage 100.000000
