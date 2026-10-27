// RUN: %talos --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: not %t --coverage-output=%t.obcov --coverage-test=default-array 2>&1 | FileCheck %s --check-prefix=ERROR --implicit-check-not=ERROR:
// RUN: talos-cov report --format=text %t.obcov | FileCheck %s --implicit-check-not='bin others[2]' --implicit-check-not='bin others[3]' --implicit-check-not='bin others[4]' --implicit-check-not='bin others[6]' --implicit-check-not='bin others[7]' --implicit-check-not='bin empty_default['

module top;
  bit [3:0] sampled;
  bit [1:0] fully_defined;
  bit [99:0] wide_sampled;
  bit signed [31:0] signed_sampled;

  covergroup cg(input int low, input int high);
    cp: coverpoint sampled {
      // Deliberately precedes the explicit bins to exercise canonical
      // insertion in source order while its complement is resolved later.
      bins others[] = default;
      bins good = {[low : high]};
      illegal_bins invalid = {6};
      ignore_bins ignored = {7};
    }
    empty_cp: coverpoint fully_defined {
      bins empty_default[] = default;
      bins all_values = {[0:3]};
    }
    wide_cp: coverpoint wide_sampled {
      bins wide_default[] = default;
      bins almost_all = {[128'd0:(128'd1 << 100) - 3]};
    }
    signed_cp: coverpoint signed_sampled {
      bins signed_default[] = default;
      bins signed_almost_all = {[-(64'sd1 << 31):-2], [1:(64'sd1 << 31) - 1]};
    }
  endgroup

  cg cov;
  initial begin
    cov = new(2, 4);
    for (int i = 0; i < 16; i++) begin
      sampled = i;
      fully_defined = i;
      cov.sample();
    end
    wide_sampled = {100{1'b1}} - 1;
    cov.sample();
    wide_sampled = {100{1'b1}};
    cov.sample();
    signed_sampled = 1;
    cov.sample();
    signed_sampled = -1;
    cov.sample();
    $finish;
  end
endmodule

// A default array holds one bin per value left undefined by the other bins
// (19.5.1): values 2-4 (good), 6 (illegal) and 7 (ignored) get no others[]
// bin, and a fully defined coverpoint gets no default bins at all.
// CHECK: functional: 100.00% (4/4)
// CHECK-DAG: coverpoint cp: 1/1 (100.00%)
// CHECK-DAG: coverpoint empty_cp: 1/1 (100.00%)
// CHECK-DAG: coverpoint wide_cp: 1/1 (100.00%)
// CHECK-DAG: coverpoint signed_cp: 1/1 (100.00%)
// CHECK-DAG: bin good: 3 [covered]
// CHECK-DAG: bin invalid: 0 [excluded]
// CHECK-DAG: bin ignored: 0 [excluded]
// CHECK-DAG: bin others[0]: 1 [excluded]
// CHECK-DAG: bin others[1]: 1 [excluded]
// CHECK-DAG: bin others[15]: {{[1-9][0-9]*}} [excluded]
// CHECK-DAG: bin all_values: {{[0-9]+}} [covered]
// CHECK-DAG: bin almost_all: {{[0-9]+}} [covered]
// CHECK-DAG: bin wide_default[1267650600228229401496703205374]: 1 [excluded]
// CHECK-DAG: bin wide_default[1267650600228229401496703205375]: {{[1-9][0-9]*}} [excluded]
// CHECK-DAG: bin signed_default[-1]: 1 [excluded]
// CHECK-DAG: bin signed_default[0]: {{[1-9][0-9]*}} [excluded]
// CHECK-DAG: bin signed_almost_all: {{[0-9]+}} [covered]
// ERROR: ERROR: functional coverage illegal bin 'top.cg.cp.invalid' sampled at simulation time 0
