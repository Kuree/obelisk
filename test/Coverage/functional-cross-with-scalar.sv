// RUN: %talos --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: talos-cov report --format=text %t.obcov | FileCheck %s --check-prefix=TEXT
// RUN: talos-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON
// RUN: talos-cov inspect %t.obcov | FileCheck %s --check-prefix=INSPECT

// IEEE 1800-2023 19.6.1.2: a with predicate that does not depend on the cross
// items is evaluated once per bin tuple, and the matches count applies to each
// bin tuple's value tuples separately ($ requires all of them). Bin tuples hold
// 2**52 (auto bins of 32-bit items) or 2**64 value tuples and are never
// enumerated.
module top;
  bit [31:0] a, b;
  bit [64:0] wide;
  bit side;
  bit [2:0] narrow;
  bit [1:0] partner;

  covergroup automatic_group(bit enabled, bit [63:0] need);
    option.per_instance = 1;
    ca: coverpoint a;
    cb: coverpoint b;
    product: cross ca, cb {
      bins selected = product with (enabled) matches need;
    }
    universal: cross ca, cb {
      bins selected = universal with (enabled) matches $;
    }
  endgroup

  covergroup massive_group;
    ca: coverpoint a { bins all = {[0:32'hffff_ffff]}; }
    cb: coverpoint b { bins all = {[0:32'hffff_ffff]}; }
    product: cross ca, cb {
      bins selected = product with (1) matches 64'hffff_ffff_ffff_ffff;
    }
    cw: coverpoint wide { bins all = {[0:65'h1_ffff_ffff_ffff_ffff]}; }
    cs: coverpoint side { bins zero = {0}; }
    wider: cross cw, cs { bins selected = wider with (1) matches $; }
  endgroup

  covergroup uneven_group(bit [63:0] need);
    ca: coverpoint narrow {
      bins low = {[0:1]};
      bins high = {[2:7]};
    }
    cb: coverpoint partner {
      bins zero = {0};
      bins rest = {[1:3]};
    }
    product: cross ca, cb { bins selected = product with (1) matches need; }
  endgroup

  automatic_group accepted, rejected, insufficient;
  massive_group massive;
  uneven_group uneven;
  int covered, total;
  real percentage;
  initial begin
    accepted = new(1, 64'h10_0000_0000_0000);
    rejected = new(0, 1);
    insufficient = new(1, 64'h10_0000_0000_0001);
    massive = new;
    uneven = new(6);
    accepted.sample();
    rejected.sample();
    insufficient.sample();
    massive.sample();
    uneven.sample();
    percentage = accepted.product.get_inst_coverage(covered, total);
    $display("accepted %.6f %0d %0d", percentage, covered, total);
    percentage = accepted.universal.get_inst_coverage(covered, total);
    $display("all %.6f %0d %0d", percentage, covered, total);
    percentage = rejected.product.get_inst_coverage(covered, total);
    $display("false %.6f %0d %0d", percentage, covered, total);
    percentage = insufficient.product.get_inst_coverage(covered, total);
    $display("count %.6f %0d %0d", percentage, covered, total);
    percentage = massive.product.get_inst_coverage(covered, total);
    $display("maximum %.6f %0d %0d", percentage, covered, total);
    percentage = massive.wider.get_inst_coverage(covered, total);
    $display("wide %.6f %0d %0d", percentage, covered, total);
    percentage = uneven.product.get_inst_coverage(covered, total);
    $display("uneven %.6f %0d %0d", percentage, covered, total);
    partner = 1;
    uneven.sample();
    percentage = uneven.product.get_inst_coverage(covered, total);
    $display("uneven full %.6f %0d %0d", percentage, covered, total);
    $finish;
  end
endmodule

// A selected bin that covers all bin tuples leaves no automatic bin; a bin
// that selects nothing is an empty excluded bin and all 4096 automatic bins
// are retained (cross_retain_auto_bins defaults to 1, 19.6.1.1).
// CHECK: accepted 100.000000 1 1
// CHECK-NEXT: all 100.000000 1 1
// CHECK-NEXT: false 0.024414 1 4096
// CHECK-NEXT: count 0.024414 1 4096
// CHECK-NEXT: maximum 100.000000 1 1
// CHECK-NEXT: wide 100.000000 1 1
// CHECK-NEXT: uneven 50.000000 1 2
// CHECK-NEXT: uneven full 100.000000 2 2

// Text report (type order depends on the source path, so every line is a DAG
// item; instance totals pin each group). Instance 1 (need == 2**52) and 3
// (need == 2**52 + 1, "matches $") select all tuples; a false predicate or a
// count above a bin tuple's 2**52 value tuples leaves an empty excluded bin
// and the 4096 automatic bins. uneven_group bin tuples hold 2, 6, 6 and 18
// value tuples: matches 6 selects the last three, so the sampled <low,zero>
// tuple stays an automatic bin until <low,rest> is sampled. The massive and
// wide groups are fully covered by their single selected bin.
// TEXT-DAG: instance $auto$1: 50.78% (4/130)
// TEXT-DAG: instance $auto$2: 0.79% (4/8320)
// TEXT-DAG: instance $auto$3: 25.79% (4/4225)
// TEXT-DAG: instance $auto$4: 100.00% (6/6)
// TEXT-DAG: instance $auto$5: 83.33% (5/6)
// TEXT-DAG: cross universal: 1/1 (100.00%)
// TEXT-DAG: cross universal: 1/1 (100.00%)
// TEXT-DAG: cross universal: 1/4096 (0.02%)
// TEXT-DAG: cross product: 1/1 (100.00%)
// TEXT-DAG: cross product: 1/1 (100.00%)
// TEXT-DAG: cross product: 1/4096 (0.02%)
// TEXT-DAG: cross product: 1/4096 (0.02%)
// TEXT-DAG: cross product: 2/2 (100.00%)
// TEXT-DAG: cross wider: 1/1 (100.00%)
// TEXT-DAG: auto bin <low,zero>: 1 [covered, at_least 1]
// TEXT-DAG: bin selected: 0 [excluded] {empty}
// TEXT-DAG: bin selected: 0 [excluded] {empty}
// TEXT-DAG: bin selected: 0 [excluded] {empty}
// TEXT-DAG: bin selected: 1 [covered]
// TEXT-DAG: bin selected: 1 [covered]
// TEXT-DAG: bin selected: 1 [covered]
// TEXT-DAG: bin selected: 1 [covered]
// TEXT-DAG: bin selected: 1 [covered]
// TEXT-DAG: bin selected: 1 [covered]

// Scalar predicates are never expanded to tuples, so the automatic product
// graph stays below 20 nodes and 600 edges.
// INSPECT: resolved-cross-plans: 9
// INSPECT: resolved-cross-automatic-nodes: {{1?[0-9]}}{{$}}
// INSPECT: resolved-cross-automatic-edges: {{[1-5]?[0-9]?[0-9]}}{{$}}
// INSPECT: sparse-cross-tuples: 4

// Scalar predicate expressions (role 11) are all Boolean (result kind 1) and
// no value tuple sets are resolved.
// JSON: "functional_expressions":[
// JSON-NOT: "role":11,"result_kind":{{[02-9]}}
// JSON: "resolved_cross_selector_bindings":[
// Binding flags: 1 scalar with, 2 scalar predicate true. Matches counts are
// kept per constructor in 64 bits.
// JSON-DAG: "matches_policy":3,"matches_count":0,"flags":3
// JSON-DAG: "matches_policy":3,"matches_count":0,"flags":1
// JSON-DAG: "matches_policy":2,"matches_count":1,"flags":1
// JSON-DAG: "matches_policy":2,"matches_count":4503599627370496,"flags":3
// JSON-DAG: "matches_policy":2,"matches_count":4503599627370497,"flags":3
// JSON-DAG: "matches_policy":2,"matches_count":6,"flags":3
// JSON-DAG: "matches_policy":2,"matches_count":18446744073709551615,"flags":3
// JSON-NOT: "flags":{{([^13]|[13][0-9])}}}
// JSON: "resolved_functional_tuple_sets":[],"resolved_functional_tuples":[]
