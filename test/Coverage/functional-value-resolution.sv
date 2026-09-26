// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov --coverage-test=resolution
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=resolution
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s < %t.native.txt

module top;
  bit [2:0] sampled;
  covergroup cg(input int reverse_left, input int reverse_right);
    cp: coverpoint sampled {
      bins clipped = {[6:10]};
      bins lossy = {8};
      bins negative = {-1};
      bins unknown = {3'bx01};
      bins reversed = {[reverse_left:reverse_right]};
    }
  endgroup

  cg cov;
  initial begin
    cov = new(10, 6);
    sampled = 6;
    cov.sample();
    sampled = 7;
    cov.sample();
    sampled = 0;
    cov.sample();
    $finish;
  end
endmodule

// CHECK: functional: 100.00% (1/1)
// CHECK: coverpoint cp: 1/1 (100.00%)
// CHECK: bin clipped: 2 [covered]
// CHECK: bin lossy: 0 [excluded] {empty}
// CHECK: bin negative: 0 [excluded]
// CHECK: bin unknown: 0 [excluded]
// CHECK: bin reversed: 0 [excluded]
