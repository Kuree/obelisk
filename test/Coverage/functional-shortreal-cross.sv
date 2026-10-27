// RUN: %talos --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: talos-cov report --format=text %t.obcov | FileCheck %s --check-prefix=REPORT
// RUN: talos-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON

// IEEE 1800-2023 19.6.1.2: CrossValType retains each shortreal component's 32-bit
// layout. Real comparisons canonicalize signed zero when deduplicating tuples.
module top;
  shortreal a, b;
  covergroup cg;
    option.cross_retain_auto_bins = 0;
    ca: coverpoint a { bins zero = {0.0}; bins one = {1.0}; }
    cb: coverpoint b { bins zero = {0.0}; bins one = {1.0}; }
    product: cross ca, cb {
      bins single = '{'{-0.0, 1.0}, '{0.0, 1.0}};
      bins diagonal = '{'{0.0, 0.0}, '{1.0, 1.0}};
      bins require_two = '{'{0.0, 0.0}, '{1.0, 1.0}} matches 2;
    }
  endgroup
  cg value;
  initial begin
    value = new;
    a = 0.0; b = 1.0; value.sample();
    a = 0.0; b = 0.0; value.sample();
    a = 1.0; b = 1.0; value.sample();
    $display("cross=%0.0f", value.product.get_inst_coverage());
    $finish;
  end
endmodule

// CHECK: cross=100

// Text report: "single" lists -0.0 and 0.0 as one tuple (count 1), and
// "require_two" is empty and excluded from the totals.
// REPORT: cross product: 2/2
// REPORT-DAG: bin single: 1 [covered]
// REPORT-DAG: bin diagonal: 2 [covered]
// REPORT-DAG: bin require_two: 0 [excluded] {empty}

// The cross has exactly two targets, each a 32-bit shortreal tuple component at
// offsets 0 and 32.
// JSON: "cross_targets":[
// JSON-SAME: "ordinal":0,"tuple_bit_offset":0,"tuple_bit_width":32,"tuple_result_kind":18,
// JSON-SAME: "ordinal":1,"tuple_bit_offset":32,"tuple_bit_width":32,"tuple_result_kind":18,
// JSON-NOT: "tuple_result_kind"
// JSON: "cross_bin_plans"
