// RUN: %obelisk -fno-lto --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=automatic-cross > %t.native.out
// RUN: %obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=automatic-cross > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s --check-prefix=QUERY < %t.native.out
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.report
// RUN: FileCheck %s --check-prefix=REPORT < %t.report
// RUN: obelisk-cov report --format=json %t.native.obcov -o %t.json
// RUN: %python -c "import json,sys; d=json.load(open(sys.argv[1])); g=next(g for g in d['functional_instance_groups'] if any(i['name']=='product' for i in g['items'])); items={i['name']:i for i in g['items']}; assert g['cross_num_print_missing']==2; assert items['product']['cross_num_print_missing']==2 and len([b for b in items['product']['automatic_bins'] if b['missing']])==2; assert items['limited_product']['cross_num_print_missing']==1 and len([b for b in items['limited_product']['automatic_bins'] if b['missing']])==1; assert items['product']['automatic_total']==items['limited_product']['automatic_total']==4" %t.json
// RUN: obelisk-cov report --format=html %t.native.obcov -o %t.html
// RUN: %python -c 'import json,re,sys; h=open(sys.argv[1]).read(); m=re.search(r"<script[^>]*id=\"coverage-data\"[^>]*>(.*?)</script>",h,re.S); assert m; d=json.loads(m.group(1)); g=next(g for g in d["functional_instance_groups"] if any(i["name"]=="product" for i in g["items"])); items={i["name"]:i for i in g["items"]}; assert str(g["cross_num_print_missing"])=="2"; assert str(items["product"]["cross_num_print_missing"])=="2" and sum(b["missing"] for b in items["product"]["automatic_bins"])==2; assert str(items["limited_product"]["cross_num_print_missing"])=="1" and sum(b["missing"] for b in items["limited_product"]["automatic_bins"])==1' %t.html
// RUN: %t.native --coverage-load=%t.native.obcov \
// RUN:   --coverage-output=%t.native.loaded.obcov \
// RUN:   --coverage-test=automatic-cross-loaded > %t.native.loaded.out
// RUN: %t.bytecode --coverage-load=%t.bytecode.obcov \
// RUN:   --coverage-output=%t.bytecode.loaded.obcov \
// RUN:   --coverage-test=automatic-cross-loaded > %t.bytecode.loaded.out
// RUN: diff -u %t.native.loaded.out %t.bytecode.loaded.out
// RUN: FileCheck %s --check-prefix=LOADED < %t.native.loaded.out
// RUN: obelisk-cov report --format=json %t.native.loaded.obcov -o %t.loaded.json
// RUN: %python -c "import json,sys; d=json.load(open(sys.argv[1])); groups=[g for g in d['functional_instance_groups'] if any(i['name']=='product' for i in g['items'])]; assert groups and all(g['cross_num_print_missing']==2 for g in groups); assert all(next(i for i in g['items'] if i['name']=='limited_product')['cross_num_print_missing']==1 for g in groups)" %t.loaded.json

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
