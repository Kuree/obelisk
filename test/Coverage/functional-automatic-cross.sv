// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov --coverage-test=automatic-cross | FileCheck %s --check-prefix=QUERY
// RUN: obelisk-cov report --format=text %t.obcov | FileCheck %s --check-prefix=REPORT
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON --implicit-check-not='"missing":true'
// RUN: obelisk-cov report --format=html %t.obcov | FileCheck %s --check-prefix=JSON --implicit-check-not='"missing":true'
// RUN: %t --coverage-load=%t.obcov --coverage-output=%t.loaded.obcov --coverage-test=automatic-cross-loaded | FileCheck %s --check-prefix=LOADED
// RUN: obelisk-cov report --format=json %t.loaded.obcov | FileCheck %s --check-prefix=LOADED-JSON

module top;
  bit a;
  bit b;
  bit enabled;

  covergroup cg;
    a_point: coverpoint a {
      bins all = {0, 1};
      bins zero = {0};
    }
    b_point: coverpoint b;
    product: cross a_point, b_point iff (enabled) {
      option.at_least = 2;
    }
    limited_product: cross a_point, b_point iff (enabled) {
      option.at_least = 2;
    }
  endgroup

  bit [5:0] w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10;
  covergroup wide;
    type_option.merge_instances = 1;
    p0: coverpoint w0;
    p1: coverpoint w1;
    p2: coverpoint w2;
    p3: coverpoint w3;
    p4: coverpoint w4;
    p5: coverpoint w5;
    p6: coverpoint w6;
    p7: coverpoint w7;
    p8: coverpoint w8;
    p9: coverpoint w9;
    p10: coverpoint w10;
    wide_product: cross p0, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10 {
      option.at_least = 0;
    }
  endgroup

  cg cov;
  wide wide_cov;
  int covered;
  int total;
  real percentage;

  initial begin
    cov = new;
    wide_cov = new;
    cov.limited_product.option.cross_num_print_missing = 1;
    cov.option.cross_num_print_missing = 2;
    $display("print missing %0d %0d",
             cov.option.cross_num_print_missing,
             cov.limited_product.option.cross_num_print_missing);

    enabled = 0;
    a = 0;
    b = 0;
    cov.sample();

    enabled = 1;
    cov.product.stop();
    cov.sample();
    cov.product.start();

    cov.sample();
    a = 1;
    cov.sample();

    percentage = cov.product.get_inst_coverage(covered, total);
    $display("cross %.6f %0d %0d", percentage, covered, total);
    percentage = cg::product::get_coverage(covered, total);
    $display("cross type %.6f %0d %0d", percentage, covered, total);
    percentage = cov.limited_product.get_inst_coverage(covered, total);
    $display("limited cross %.6f %0d %0d", percentage, covered, total);
    wide_cov.sample();
    percentage = wide_cov.wide_product.get_inst_coverage(covered, total);
    $display("wide cross %.6f %0d %0d", percentage, covered, total);
    $finish;
  end
endmodule

// QUERY: print missing 2 1
// QUERY-NEXT: cross 25.000000 1 4
// QUERY-NEXT: cross type 25.000000 1 4
// QUERY-NEXT: limited cross 50.000000 2 4
// QUERY-NEXT: wide cross 100.000000 2147483647 2147483647
// LOADED: cross 25.000000 1 4
// LOADED-NEXT: cross type 25.000000 2 8
// LOADED-NEXT: limited cross 50.000000 2 4
// LOADED-NEXT: wide cross 100.000000 2147483647 2147483647
// REPORT: functional detail:
// REPORT-DAG: type wide ({{[0-9]+}}): 9.77%
// REPORT-DAG: cross wide_product: 73786976294838206464/73786976294838206464
// REPORT-DAG: cross product: 1/4
// REPORT-DAG: cross limited_product: 2/4
// REPORT-DAG: auto bin <all,auto[0]>: 2 [covered, at_least 2]
// REPORT-DAG: auto bin <zero,auto[0]>: 1 [uncovered, at_least 2]
// REPORT-DAG: auto bin <all,auto[1]>: 0 [uncovered, at_least 2]

// cross_num_print_missing is inherited from the covergroup instance (2) unless
// the cross sets its own (1). The listed missing automatic bins are limited to
// that count: product lists both of its missing tuples, limited_product only
// the first. Both crosses have four automatic tuples. The JSON and the data
// embedded in the HTML report agree, and no other bin is missing.
// JSON-DAG: "per_instance":false,"distribute_first":false,"cross_num_print_missing":{{"?}}2{{"?}},
// JSON-DAG: "name":"product","comment":"","hierarchy":"top.cg.product","kind":{{"?}}2{{"?}},{{[^}]*}}"automatic_total":{{"?}}4{{"?}},"automatic_at_least":{{"?}}2{{"?}},"cross_num_print_missing":{{"?}}2{{"?}},
// JSON-DAG: "name":"limited_product","comment":"","hierarchy":"top.cg.limited_product","kind":{{"?}}2{{"?}},{{[^}]*}}"automatic_total":{{"?}}4{{"?}},"automatic_at_least":{{"?}}2{{"?}},"cross_num_print_missing":{{"?}}1{{"?}},
// JSON-DAG: "name":"{{(<|\\u003c)}}zero,auto[1]>","count":{{"?}}0{{"?}},"overflow":false,"at_least":{{"?}}2{{"?}},"covered":false,"missing":true
// JSON-DAG: "name":"{{(<|\\u003c)}}all,auto[1]>","count":{{"?}}0{{"?}},"overflow":false,"at_least":{{"?}}2{{"?}},"covered":false,"missing":true
// JSON-DAG: "name":"{{(<|\\u003c)}}all,auto[1]>","count":{{"?}}0{{"?}},"overflow":false,"at_least":{{"?}}2{{"?}},"covered":false,"missing":true

// Loading the file keeps the option values on both the loaded and the live
// instance groups.
// LOADED-JSON-DAG: "per_instance":false,"distribute_first":false,"cross_num_print_missing":2,
// LOADED-JSON-DAG: "per_instance":false,"distribute_first":false,"cross_num_print_missing":2,
// LOADED-JSON-DAG: "name":"limited_product","comment":"","hierarchy":"top.cg.limited_product","kind":2,{{[^}]*}}"cross_num_print_missing":1,
// LOADED-JSON-DAG: "name":"limited_product","comment":"","hierarchy":"top.cg.limited_product","kind":2,{{[^}]*}}"cross_num_print_missing":1,
