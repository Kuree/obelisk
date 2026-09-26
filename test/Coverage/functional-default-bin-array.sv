// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: not %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=default-array 2> %t.native.err
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: not %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=default-array 2> %t.bytecode.err
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: diff -u %t.native.err %t.bytecode.err
// RUN: FileCheck %s < %t.native.txt
// RUN: FileCheck %s --check-prefix=ERROR < %t.native.err
// RUN: %python -c "import sys; s=open(sys.argv[1]).read(); assert s.count('ERROR: functional coverage illegal bin') == 1" %t.native.err
// RUN: %python -c "p=open(r'%t.native.txt').read(); assert all(p.find(s) < 0 for s in ['bin others[2]', 'bin others[3]', 'bin others[4]', 'bin others[6]', 'bin others[7]', 'bin empty_default['])"

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
