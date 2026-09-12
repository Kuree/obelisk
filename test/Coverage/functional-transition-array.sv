// RUN: %obelisk -fno-lto --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=transition-array
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: %obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=transition-array
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s < %t.native.txt

// IEEE 1800-2023 19.5.2: an unsized transition-bin array creates one bin for
// every concrete bounded sequence. The leftmost range-list item varies first.
module top;
  bit [3:0] sampled;

  covergroup cg;
    cp: coverpoint sampled {
      bins paths[] = (4 => 5 => 6), ([7:9], 10 => 11, 12), (7 => 11);
    }
  endgroup

  cg cov;
  bit [1:0] narrow;
  covergroup empty_cg;
    empty_cp: coverpoint narrow {
      bins gone[] = (4 => 1);
    }
  endgroup
  empty_cg empty_cov;
  logic signed [2:0] signed_sampled;
  covergroup signed_cg;
    signed_cp: coverpoint signed_sampled {
      // The repeated -1 verifies that overlapping source ranges are charged
      // and expanded as their canonical value union.
      bins signed_paths[] = ([-2:-1], -1 => 0);
    }
  endgroup
  signed_cg signed_cov;
  task automatic take(input bit [3:0] value);
    sampled = value;
    cov.sample();
  endtask

  initial begin
    cov = new;
    empty_cov = new;
    signed_cov = new;
    take(4); take(5); take(6); take(0);
    take(7); take(11); take(0);
    take(8); take(11); take(0);
    take(9); take(11); take(0);
    take(10); take(11); take(0);
    take(7); take(12); take(0);
    take(8); take(12); take(0);
    take(9); take(12); take(0);
    take(10); take(12);
    narrow = 1; empty_cov.sample();
    signed_sampled = -2; signed_cov.sample();
    signed_sampled = 0; signed_cov.sample();
    signed_sampled = 3; signed_cov.sample();
    signed_sampled = -1; signed_cov.sample();
    signed_sampled = 0; signed_cov.sample();
    $finish;
  end
endmodule

// CHECK: functional: 100.00% (11/11)
// CHECK-DAG: coverpoint cp: 9/9 (100.00%)
// CHECK-DAG: coverpoint signed_cp: 2/2 (100.00%)
// CHECK-DAG: bin paths[4=>5=>6]: 1 [covered]
// CHECK-DAG: bin paths[7=>11]: 1 [covered]
// CHECK-DAG: bin paths[8=>11]: 1 [covered]
// CHECK-DAG: bin paths[9=>11]: 1 [covered]
// CHECK-DAG: bin paths[10=>11]: 1 [covered]
// CHECK-DAG: bin paths[7=>12]: 1 [covered]
// CHECK-DAG: bin paths[8=>12]: 1 [covered]
// CHECK-DAG: bin paths[9=>12]: 1 [covered]
// CHECK-DAG: bin paths[10=>12]: 1 [covered]
// CHECK-DAG: bin signed_paths[-2=>0]: 1 [covered]
// CHECK-DAG: bin signed_paths[-1=>0]: 1 [covered]
