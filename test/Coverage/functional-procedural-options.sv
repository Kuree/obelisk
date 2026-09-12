// RUN: %obelisk -fno-lto --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=procedural-options > %t.native.stdout
// RUN: %obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=procedural-options > %t.bytecode.stdout
// RUN: diff -u %t.native.stdout %t.bytecode.stdout
// RUN: FileCheck %s --check-prefix=QUERY < %t.native.stdout
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s --check-prefix=REPORT < %t.native.txt
// RUN: obelisk-cov report --format=json %t.native.obcov -o %t.native.json
// RUN: %python -c "import json; d=json.load(open(r'%t.native.json')); g=d['functional_instance_groups'][0]; assert g['name']=='mutated' and g['comment']=='group' and g['goal']==80 and g['weight']==2 and g['cross_num_print_missing']==0; items={i['name']:i for i in g['items']}; p=items['cp0']; assert p['comment']=='point' and p['goal']==50 and p['weight']==3 and all(b['at_least']==2 for b in p['bins']); x=items['cx']; assert x['comment']=='cross' and x['goal']==25 and x['weight']==0 and x['automatic_at_least']==3; assert any(o['owner_kind']==1 and o['option']==5 and o['value']==0 for o in d['resolved_instance_options'])"

module top;
  int value;
  covergroup cg;
    cp0: coverpoint value { bins zero = {0}; bins one = {1}; }
    cp1: coverpoint value { bins zero = {0}; bins one = {1}; }
    cx: cross cp0, cp1;
  endgroup
  cg c;
  int covered, total;
  real pct;
  initial begin
    c = new;
    c.option.name = "mutated";
    c.option.comment = "group";
    c.option.weight = 2;
    c.cp0.option.comment = "point";
    c.sample();
    pct = c.cp0.get_inst_coverage(covered, total);
    $display("initial %.0f %0d/%0d", pct, covered, total);
    c.cp0.option.at_least = 2;
    pct = c.cp0.get_inst_coverage(covered, total);
    $display("raised %.0f %0d/%0d", pct, covered, total);
    c.cp0.option.weight = 3;
    c.cp0.option.goal = 50;
    c.sample();
    pct = c.cp0.get_inst_coverage(covered, total);
    $display("point %.0f %0d/%0d", pct, covered, total);
    c.option.at_least = 3;
    c.cx.option.weight = 0;
    c.cx.option.goal = 25;
    c.cx.option.comment = "cross";
    c.cx.option.at_least = 3;
    pct = c.cx.get_inst_coverage(covered, total);
    $display("cross-before %.0f %0d/%0d", pct, covered, total);
    c.sample();
    pct = c.cx.get_inst_coverage(covered, total);
    $display("cross-after %.0f %0d/%0d", pct, covered, total);
    pct = c.cp1.get_inst_coverage(covered, total);
    $display("second-point %.0f %0d/%0d", pct, covered, total);
    pct = c.cp1.get_coverage(covered, total);
    $display("second-point-type %.0f %0d/%0d", pct, covered, total);
    c.option.goal = 80;
    pct = c.get_inst_coverage(covered, total);
    $display("group %.1f %0d/%0d", pct, covered, total);
    $finish;
  end
endmodule

// QUERY: initial 50 1/2
// QUERY: raised 0 0/2
// QUERY: point 100 1/2
// QUERY: cross-before 0 0/4
// QUERY: cross-after 100 1/4
// QUERY: second-point 50 1/2
// QUERY: second-point-type 50 1/2
// QUERY: group 100.0 3/8

// REPORT: functional: 100.00% (3/8)
// REPORT: instance mutated: 100.00% (3/8)
// REPORT: comment: "group"
// REPORT: coverpoint cp0: 1/2 (100.00%)
// REPORT: comment: "point"
// REPORT: bin zero: 3 [covered] {at_least 2}
// REPORT: coverpoint cp1: 1/2 (50.00%)
// REPORT: bin zero: 3 [covered] {at_least 3}
// REPORT: cross cx: 1/4 (100.00%)
// REPORT: comment: "cross"
