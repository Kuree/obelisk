// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: obelisk-cov report --format=text %t.obcov | FileCheck %s --check-prefix=TEXT
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON
// RUN: obelisk-cov inspect %t.obcov | FileCheck %s --check-prefix=INSPECT

// IEEE 1800-2023 19.6.1.2: the parent sees every associated value of each
// bin selected by its child. A symbolic child's witness is not a value filter
// for the queue-based parent's function call.
module top;
  function automatic bit odd(bit [1:0] value); return value[0]; endfunction
  bit [1:0] a;
  bit b;
  covergroup cg;
    option.cross_retain_auto_bins = 0;
    ca: coverpoint a { bins all = {[0:3]}; }
    cb: coverpoint b { bins zero = {0}; }
    product: cross ca, cb {
      bins selected = (product with (ca == 0)) with (odd(ca));
    }
  endgroup
  cg cov;
  int covered, total;
  real percentage;
  initial begin
    cov = new;
    cov.sample();
    percentage = cov.product.get_inst_coverage(covered, total);
    $display("mixed %.6f %0d %0d", percentage, covered, total);
    $finish;
  end
endmodule
// CHECK: mixed 100.000000 1 1

// The only bin tuple <all,zero> is selected because odd() holds for 2 of its
// 4 value tuples (ca = 1, 3).
// TEXT: cross product: 1/1 (100.00%)
// TEXT-NEXT: automatic: 0/0
// TEXT-NEXT: bin selected: 1 [covered]

// Exactly one selector (the child) has a predicate DAG and exactly one
// function-call predicate (role 11, result kind 7) remains; the parent's
// queue holds exactly the two satisfying value tuples (tuple_count 2).
// JSON: "functional_expressions":[
// JSON: "role":11,"result_kind":7,
// JSON-NOT: "role":11,
// JSON: "cross_selector_nodes":[
// JSON: "predicate_root":{{[1-9][0-9]*}},
// JSON-NOT: "predicate_root":{{[1-9]}}
// JSON: "cross_predicate_nodes":[
// JSON: "resolved_functional_tuple_sets":[
// JSON: "tuple_count":2,
// JSON-NOT: "tuple_count"
// JSON: "resolved_functional_tuples":[

// The child's DAG is iterator, extension, constant and equality (4 nodes, 3
// operand links); the parent's queue has one set.
// INSPECT: cross-predicate-nodes: 4
// INSPECT: cross-predicate-operands: 3
// INSPECT: resolved-cross-plans: 1
