// RUN: %obelisk -fno-lto --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov --coverage-test=default-sequence
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: %obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=default-sequence
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s < %t.native.txt

// IEEE 1800-2023 19.5.2 example: default sequence counts on 7 and 8. A
// sequence newly started on either sample does not suppress default, whereas
// the goto sequence pending across the two samples of 2 does suppress it.
module top;
  bit [3:0] sampled;
  covergroup cg;
    cp: coverpoint sampled {
      bins paths = (4 => 5 => 6), ([7:9], 10 => 11, 12);
      bins delayed = (12 => 3 [-> 1]);
      bins allother = default sequence;
    }
  endgroup

  cg cov;
  initial begin
    cov = new;
    sampled = 4; cov.sample();
    sampled = 5; cov.sample();
    sampled = 7; cov.sample();
    sampled = 11; cov.sample();
    sampled = 8; cov.sample();
    sampled = 12; cov.sample();
    sampled = 2; cov.sample();
    sampled = 2; cov.sample();
    sampled = 3; cov.sample();
    $finish;
  end
endmodule

// CHECK: functional: 100.00% (2/2)
// CHECK: coverpoint cp: 2/2 (100.00%)
// CHECK-DAG: bin paths: 2 [covered]
// CHECK-DAG: bin delayed: 1 [covered]
// CHECK-DAG: bin allother: 2 [excluded] {default sequence}
