// RUN: %obelisk -fno-lto --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov --coverage-test=bin-array
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: %obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=bin-array
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s < %t.native.txt

module top;
  logic [7:0] sampled;
  logic [127:0] wide_sampled;
  logic signed [4:0] signed_sampled;

  covergroup cg(input int fixed_count);
    fixed_cp: coverpoint sampled {
      // IEEE 1800-2017 19.5.1 retains duplicates and assigns the remainder
      // to the last fixed bin.
      bins fixed[fixed_count] = {[1:10], 1, 4, 7};
    }
    overlap_cp: coverpoint sampled {
      // IEEE 1800-2023 19.5.1 retains duplicate source occurrences, including
      // the overlap from 148 through 150.
      bins overlap[] = {[127:150], [148:191]};
    }
    wildcard_cp: coverpoint sampled {
      wildcard bins wildcard_values[] = {8'b000011??};
    }
    fixed_wildcard_cp: coverpoint sampled {
      // Fixed wildcard distribution is represented symbolically; it does not
      // enumerate all values before partitioning the cube.
      wildcard bins fixed_wildcard[3] = {8'b1???????};
    }
    wide_cp: coverpoint wide_sampled {
      // Exercise arbitrary-width range partitioning without materializing the
      // 2**100 individual values.
      bins huge[2] = {[128'd0 : (128'd1 << 100) - 1]};
    }
    signed_unsized_cp: coverpoint signed_sampled {
      bins signed_values[] = {[-2:1]};
    }
    signed_fixed_cp: coverpoint signed_sampled {
      bins signed_ranges[2] = {[-4:3]};
    }
    sparse_cp: coverpoint sampled {
      // B is never less than one, so leading bins receive the values and the
      // remaining fixed bins are empty.
      bins sparse[5] = {20, 21};
    }
  endgroup

  cg cov;
  cg cov2;
  initial begin
    cov = new(4);
    cov2 = new(2);
    wide_sampled = 0;
    signed_sampled = -2;
    for (int i = 0; i < 256; i++) begin
      sampled = i;
      if (i < 4)
        signed_sampled = i - 2;
      cov.sample();
      cov2.sample();
    end
    sampled = 0;
    wide_sampled = 128'd1 << 99;
    cov.sample();
    cov2.sample();
    $finish;
  end
endmodule

// CHECK: functional: 100.00% (176/176)
// CHECK-DAG: coverpoint fixed_cp: 4/4 (100.00%)
// CHECK-DAG: bin fixed[0]: 3 [covered]
// CHECK-DAG: bin fixed[1]: 3 [covered]
// CHECK-DAG: bin fixed[2]: 3 [covered]
// CHECK-DAG: bin fixed[3]: 4 [covered]
// CHECK-DAG: bin fixed[0]: 6 [covered]
// CHECK-DAG: bin fixed[1]: 6 [covered]
// CHECK-DAG: coverpoint overlap_cp: 68/68 (100.00%)
// CHECK-DAG: bin overlap[127]: 1 [covered]
// CHECK-DAG: bin overlap[191]: 1 [covered]
// CHECK-DAG: coverpoint wildcard_cp: 4/4 (100.00%)
// CHECK-DAG: bin wildcard_values[12]: 1 [covered]
// CHECK-DAG: bin wildcard_values[15]: 1 [covered]
// CHECK-DAG: coverpoint fixed_wildcard_cp: 3/3 (100.00%)
// CHECK-DAG: bin fixed_wildcard[0]: 42 [covered]
// CHECK-DAG: bin fixed_wildcard[1]: 42 [covered]
// CHECK-DAG: bin fixed_wildcard[2]: 44 [covered]
// CHECK-DAG: coverpoint wide_cp: 2/2 (100.00%)
// CHECK-DAG: bin huge[0]: 256 [covered]
// CHECK-DAG: bin huge[1]: 1 [covered]
// CHECK-DAG: coverpoint signed_unsized_cp: 4/4 (100.00%)
// CHECK-DAG: bin signed_values[-2]: {{[0-9]+}} [covered]
// CHECK-DAG: bin signed_values[-1]: {{[0-9]+}} [covered]
// CHECK-DAG: bin signed_values[0]: {{[0-9]+}} [covered]
// CHECK-DAG: bin signed_values[1]: {{[0-9]+}} [covered]
// CHECK-DAG: coverpoint signed_fixed_cp: 2/2 (100.00%)
// CHECK-DAG: coverpoint sparse_cp: 2/2 (100.00%)
// CHECK-DAG: bin sparse[0]: 1 [covered]
// CHECK-DAG: bin sparse[1]: 1 [covered]
// CHECK-DAG: bin sparse[4]: 0 [excluded]
