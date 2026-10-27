// RUN: %talos --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: %t --coverage-output=%t.second.obcov > /dev/null
// RUN: talos-cov merge -o %t.merged.obcov %t.obcov %t.second.obcov
// RUN: talos-cov report --format=text %t.obcov | FileCheck %s --check-prefix=TEXT
// RUN: talos-cov report --format=json %t.merged.obcov | FileCheck %s --check-prefix=JSON --implicit-check-not='"tuple_set":0,"matches_policy":{{[0-9]+}},"matches_count":{{[0-9]+}},"flags":{{([0-35-9]|[0-9][0-9]+)}}'
// RUN: talos-cov inspect %t.obcov | FileCheck %s --check-prefix=INSPECT
// RUN: talos-cov inspect %t.merged.obcov | FileCheck %s --check-prefix=MERGED

// IEEE 1800-2023 19.6.1.2 permits symbolic analysis. These bins contain
// 2**64 value tuples: equality has exactly 2**32 satisfying tuples, without
// constructing a tuple queue. Captured bounds belong to each configuration.
module top;
  bit [31:0] a, b;
  bit signed [2:0] signed_value;
  bit [1:0] unsigned_value;

  covergroup wide_group(bit [63:0] need, bit [31:0] boundary);
    option.cross_retain_auto_bins = 0;
    ca: coverpoint a { bins all = {[0:32'hffff_ffff]}; }
    cb: coverpoint b { bins all = {[0:32'hffff_ffff]}; }
    product: cross ca, cb {
      bins selected = product with (ca == cb) matches need;
    }
    universal: cross ca, cb {
      bins selected = universal with (ca == cb) matches $;
    }
    bounded: cross ca, cb {
      bins selected = bounded with (ca < boundary && cb >= boundary);
    }
    nested: cross ca, cb {
      bins selected = (nested with (ca == cb)) with (ca != cb);
    }
  endgroup

  covergroup signed_group;
    option.cross_retain_auto_bins = 0;
    cs: coverpoint signed_value {
      bins negative = {[-4:-1]};
      bins positive = {[0:3]};
    }
    cu: coverpoint unsigned_value { bins all = {[0:3]}; }
    negative: cross cs, cu { bins selected = negative with (cs < 0); }
    mixed: cross cs, cu { bins selected = mixed with (cs < cu); }
    arithmetic: cross cs, cu {
      bins selected = arithmetic with ((cs + 1 == 0) || (cs - 1 == 0));
    }
    logical: cross cs, cu {
      bins selected = logical with (!(cs & 1) && ((~cu | 1) ^ cu));
    }
  endgroup

  wide_group accepted, insufficient, zero_boundary;
  signed_group signed_cov;
  int covered, total;
  real percentage;
  initial begin
    accepted = new(64'h1_0000_0000, 32);
    insufficient = new(64'h1_0000_0001, 32);
    zero_boundary = new(1, 0);
    signed_cov = new;
    accepted.sample();
    insufficient.sample();
    zero_boundary.sample();
    signed_value = -1;
    signed_cov.sample();
    percentage = accepted.product.get_inst_coverage(covered, total);
    $display("equal %.6f %0d %0d", percentage, covered, total);
    percentage = insufficient.product.get_inst_coverage(covered, total);
    $display("insufficient %.6f %0d %0d", percentage, covered, total);
    percentage = accepted.universal.get_inst_coverage(covered, total);
    $display("all %.6f %0d %0d", percentage, covered, total);
    percentage = accepted.bounded.get_inst_coverage(covered, total);
    $display("bound %.6f %0d %0d", percentage, covered, total);
    percentage = zero_boundary.bounded.get_inst_coverage(covered, total);
    $display("zero bound %.6f %0d %0d", percentage, covered, total);
    percentage = accepted.nested.get_inst_coverage(covered, total);
    $display("nested %.6f %0d %0d", percentage, covered, total);
    percentage = signed_cov.negative.get_inst_coverage(covered, total);
    $display("negative %.6f %0d %0d", percentage, covered, total);
    percentage = signed_cov.mixed.get_inst_coverage(covered, total);
    $display("mixed before %.6f %0d %0d", percentage, covered, total);
    percentage = signed_cov.arithmetic.get_inst_coverage(covered, total);
    $display("arithmetic %.6f %0d %0d", percentage, covered, total);
    signed_value = 0;
    signed_cov.sample();
    percentage = signed_cov.mixed.get_inst_coverage(covered, total);
    $display("mixed after %.6f %0d %0d", percentage, covered, total);
    percentage = signed_cov.logical.get_inst_coverage(covered, total);
    $display("logical %.6f %0d %0d", percentage, covered, total);
    $finish;
  end
endmodule

// Counts of 19.6.1.2 with the default matches 1 and with matches/$: equality
// has 2**32 satisfying tuples (product), so 2**32 selects and 2**32 + 1 and $
// do not; bounded depends on each constructor's boundary; the nested child
// selects the bin tuple and the parent's predicate sees all its value tuples.
// CHECK: equal 100.000000 1 1
// CHECK-NEXT: insufficient 0.000000 0 0
// CHECK-NEXT: all 0.000000 0 0
// CHECK-NEXT: bound 100.000000 1 1
// CHECK-NEXT: zero bound 0.000000 0 0
// CHECK-NEXT: nested 100.000000 1 1
// CHECK-NEXT: negative 100.000000 1 1
// CHECK-NEXT: mixed before 0.000000 0 1
// CHECK-NEXT: arithmetic 100.000000 1 1
// CHECK-NEXT: mixed after 100.000000 1 1
// CHECK-NEXT: logical 100.000000 1 1

// Report bins (type order depends on the source path, so DAG). wide_group
// instances 1-3 are accepted, insufficient and zero_boundary: an unsatisfied
// with leaves an empty excluded bin. signed_group (instance 4): the mixed
// signed/unsigned comparison is unsigned (11.8.1), so only <positive,all>
// satisfies cs < cu and it is hit by the second sample; arithmetic and logical
// are hit by both samples.
// TEXT-DAG: instance $auto$1: 100.00% (5/5)
// TEXT-DAG: instance $auto$2: 100.00% (4/4)
// TEXT-DAG: instance $auto$3: 100.00% (4/4)
// TEXT-DAG: instance $auto$4: 100.00% (7/7)
// TEXT-DAG: cross nested: 1/1 (100.00%)
// TEXT-DAG: cross nested: 1/1 (100.00%)
// TEXT-DAG: cross nested: 1/1 (100.00%)
// TEXT-DAG: cross product: 1/1 (100.00%)
// TEXT-DAG: cross product: 1/1 (100.00%)
// TEXT-DAG: cross product: 0/0 (0.00%)
// TEXT-DAG: cross universal: 0/0 (0.00%)
// TEXT-DAG: cross universal: 0/0 (0.00%)
// TEXT-DAG: cross universal: 0/0 (0.00%)
// TEXT-DAG: cross bounded: 1/1 (100.00%)
// TEXT-DAG: cross bounded: 1/1 (100.00%)
// TEXT-DAG: cross bounded: 0/0 (0.00%)
// TEXT-DAG: cross mixed: 1/1 (100.00%)
// TEXT-DAG: cross negative: 1/1 (100.00%)
// TEXT-DAG: cross logical: 1/1 (100.00%)
// TEXT-DAG: cross arithmetic: 1/1 (100.00%)
// TEXT-DAG: bin selected: 0 [excluded] {empty}
// TEXT-DAG: bin selected: 0 [excluded] {empty}
// TEXT-DAG: bin selected: 0 [excluded] {empty}
// TEXT-DAG: bin selected: 0 [excluded] {empty}
// TEXT-DAG: bin selected: 0 [excluded] {empty}
// TEXT-DAG: bin selected: 1 [covered]
// TEXT-DAG: bin selected: 1 [covered]
// TEXT-DAG: bin selected: 1 [covered]
// TEXT-DAG: bin selected: 1 [covered]
// TEXT-DAG: bin selected: 1 [covered]
// TEXT-DAG: bin selected: 1 [covered]
// TEXT-DAG: bin selected: 1 [covered]
// TEXT-DAG: bin selected: 1 [covered]
// TEXT-DAG: bin selected: 1 [covered]
// TEXT-DAG: bin selected: 2 [covered]
// TEXT-DAG: bin selected: 2 [covered]

// Symbolic predicates are not enumerated: no function-call (role 11)
// expressions, no tuple sets, every selector binding is symbolic (flags=4),
// and the matches counts 2**32 and 2**32 + 1 are kept per binding. Captured
// operand value sets (role 5) belong to each configuration. The merged file
// of two identical runs keeps one copy of every table.
// JSON: "functional_expressions":[
// JSON-NOT: "role":11,
// JSON: "functional_container_types":[
// JSON: "resolved_functional_value_sets":[
// JSON: "role":5,
// JSON: "resolved_cross_selector_bindings":[
// JSON-DAG: "matches_policy":2,"matches_count":4294967296,"flags":4}
// JSON-DAG: "matches_policy":2,"matches_count":4294967297,"flags":4}
// JSON-DAG: "matches_policy":3,"matches_count":0,"flags":4}
// JSON-DAG: "matches_policy":3,"matches_count":0,"flags":4}
// JSON-DAG: "matches_policy":3,"matches_count":0,"flags":4}
// JSON-DAG: "matches_policy":2,"matches_count":1,"flags":4}
// JSON-DAG: "matches_policy":2,"matches_count":1,"flags":4}
// JSON-DAG: "matches_policy":2,"matches_count":1,"flags":4}
// JSON-DAG: "matches_policy":2,"matches_count":1,"flags":4}
// JSON-DAG: "matches_policy":2,"matches_count":1,"flags":4}
// JSON-DAG: "matches_policy":2,"matches_count":1,"flags":4}
// JSON-DAG: "matches_policy":2,"matches_count":1,"flags":4}
// JSON-DAG: "matches_policy":2,"matches_count":1,"flags":4}
// JSON-DAG: "matches_policy":2,"matches_count":1,"flags":4}
// JSON-DAG: "matches_policy":2,"matches_count":1,"flags":4}
// JSON-DAG: "matches_policy":2,"matches_count":1,"flags":4}
// JSON-DAG: "matches_policy":2,"matches_count":1,"flags":4}
// JSON-DAG: "matches_policy":2,"matches_count":1,"flags":4}
// JSON-DAG: "matches_policy":2,"matches_count":1,"flags":4}
// JSON: "resolved_functional_tuple_sets":[],"resolved_functional_tuples":[]

// The predicate DAG tables and resolved tables are identical before and after
// merging; two runs only add a run and its instances.
// INSPECT: runs: 1
// INSPECT: cross-predicate-nodes: 55
// INSPECT: cross-predicate-operands: 46
// INSPECT: functional-configurations: 4
// INSPECT: resolved-functional-items: 24
// INSPECT: resolved-functional-bins: 25
// INSPECT: resolved-cross-plans: 16
// INSPECT: resolved-instances: 4
// MERGED: runs: 2
// MERGED: cross-predicate-nodes: 55
// MERGED: cross-predicate-operands: 46
// MERGED: functional-configurations: 4
// MERGED: resolved-functional-items: 24
// MERGED: resolved-functional-bins: 25
// MERGED: resolved-cross-plans: 16
// MERGED: resolved-instances: 8
