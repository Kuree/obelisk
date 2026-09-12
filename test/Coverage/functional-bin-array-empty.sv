// RUN: %obelisk -fno-lto --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov --coverage-test=empty-array
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: %obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=empty-array
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s < %t.native.txt

module top;
  bit [3:0] sampled;

  covergroup cg;
    cp: coverpoint sampled {
      // IEEE 1800-2017 11.4.13 makes this reversed range empty. The canonical
      // resolved schema retains the unsized group with cardinality zero.
      bins none[] = {[4:3]};
    }
  endgroup

  cg cov;
  initial begin
    cov = new;
    sampled = 3;
    cov.sample();
    $finish;
  end
endmodule

// CHECK: functional: 100.00% (0/0)
// CHECK: coverpoint cp: 0/0 (0.00%)
// CHECK-NOT: bin none
