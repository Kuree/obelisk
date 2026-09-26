// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov --coverage-test=wildcard
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=wildcard
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s < %t.native.txt

module top;
  logic [3:0] sampled;
  logic signed [4:0] signed_sampled;

  covergroup cg;
    high_cp: coverpoint sampled {
      wildcard bins high = {4'b10??};
    }
    low_cp: coverpoint sampled {
      wildcard bins low = {4'b0x?z};
    }
    signed_cp: coverpoint signed_sampled {
      wildcard bins signed_widen = {4'sb?001};
    }
    span_cp: coverpoint sampled {
      wildcard bins span = {[4'b01?0:4'b10?1]};
    }
    range_removed_cp: coverpoint sampled {
      bins removed_range = {[0:3]};
      wildcard ignore_bins all_cube = {2'b??};
    }
    cube_removed_cp: coverpoint sampled {
      wildcard bins removed_cube = {2'b??};
      ignore_bins all_range = {[0:3]};
    }
    signed_range_removed_cp: coverpoint signed_sampled {
      bins removed_signed_range = {[-8:-1]};
      wildcard ignore_bins signed_cube = {4'sb1???};
    }
    signed_cube_removed_cp: coverpoint signed_sampled {
      wildcard bins removed_signed_cube = {4'sb1???};
      ignore_bins signed_range = {[-8:-1]};
    }
    split_cube_removed_cp: coverpoint sampled {
      bins removed_by_split_cubes = {[0:3]};
      wildcard ignore_bins split_cubes = {2'b0?, 2'b1?};
    }
    split_range_removed_cp: coverpoint sampled {
      wildcard bins removed_by_split_ranges = {2'b??};
      ignore_bins split_ranges = {[0:1], [2:3]};
    }
    partially_removed_cp: coverpoint sampled {
      bins retained_range = {[0:3]};
      wildcard ignore_bins low_half = {2'b0?};
    }
  endgroup

  cg cov;
  initial begin
    cov = new;

    sampled = 0;
    signed_sampled = 0;
    cov.sample();
    sampled = 3;
    cov.sample();
    sampled = 4;
    cov.sample();
    sampled = 7;
    cov.sample();
    sampled = 8;
    cov.sample();
    sampled = 9;
    cov.sample();
    sampled = 10;
    cov.sample();
    sampled = 11;
    cov.sample();
    sampled = 12;
    cov.sample();

    // IEEE 1800-2017 19.5.4 excludes sampled X/Z values from wildcard bins.
    sampled = 4'b10x0;
    cov.sample();

    sampled = 13;
    signed_sampled = 1;
    cov.sample();
    signed_sampled = -7;
    cov.sample();
    signed_sampled = 9;
    cov.sample();
    $finish;
  end
endmodule

// CHECK: functional: 100.00% (5/5)
// CHECK-DAG: coverpoint high_cp: 1/1 (100.00%)
// CHECK-DAG: bin high: 4 [covered] {wildcard}
// CHECK-DAG: coverpoint low_cp: 1/1 (100.00%)
// CHECK-DAG: bin low: 4 [covered]
// CHECK-DAG: coverpoint signed_cp: 1/1 (100.00%)
// CHECK-DAG: bin signed_widen: 2 [covered]
// CHECK-DAG: coverpoint span_cp: 1/1 (100.00%)
// CHECK-DAG: bin span: 6 [covered]
// CHECK-DAG: coverpoint range_removed_cp: 0/0 (0.00%)
// CHECK-DAG: bin removed_range: {{[0-9]+}} [excluded]
// CHECK-DAG: coverpoint cube_removed_cp: 0/0 (0.00%)
// CHECK-DAG: bin removed_cube: {{[0-9]+}} [excluded]
// CHECK-DAG: coverpoint signed_range_removed_cp: 0/0 (0.00%)
// CHECK-DAG: bin removed_signed_range: {{[0-9]+}} [excluded]
// CHECK-DAG: coverpoint signed_cube_removed_cp: 0/0 (0.00%)
// CHECK-DAG: bin removed_signed_cube: {{[0-9]+}} [excluded]
// CHECK-DAG: coverpoint split_cube_removed_cp: 0/0 (0.00%)
// CHECK-DAG: bin removed_by_split_cubes: {{[0-9]+}} [excluded]
// CHECK-DAG: coverpoint split_range_removed_cp: 0/0 (0.00%)
// CHECK-DAG: bin removed_by_split_ranges: {{[0-9]+}} [excluded]
// CHECK-DAG: coverpoint partially_removed_cp: 1/1 (100.00%)
// CHECK-DAG: bin retained_range: 1 [covered]
