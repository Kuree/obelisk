// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov --coverage-test=functional
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov --coverage-test=functional
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s < %t.native.txt

module top;
  bit sampled;
  covergroup cg(ref bit source);
    cp: coverpoint source {
      bins zero = {0};
      bins one = {1};
    }
  endgroup

  cg first, second;
  initial begin
    first = new(sampled);
    second = new(sampled);
    sampled = 0;
    first.sample();
    sampled = 1;
    second.sample();
    $finish;
  end
endmodule

// CHECK: functional: 50.00% (2/4)
// CHECK: functional detail:
// CHECK: type cg ({{[1-9][0-9]*}}): 50.00%
// CHECK: instance $auto$1: 50.00% (1/2)
// CHECK: coverpoint cp: 1/2
// CHECK: bin zero: 1 [covered]
// CHECK: bin one: 0 [uncovered]
// CHECK: instance $auto$2: 50.00% (1/2)
// CHECK: coverpoint cp: 1/2
// CHECK: bin zero: 0 [uncovered]
// CHECK: bin one: 1 [covered]
