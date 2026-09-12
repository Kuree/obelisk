// RUN: %obelisk -fno-lto --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov --coverage-test=type-options \
// RUN:   > %t.native.out
// RUN: %obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=type-options > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s --check-prefix=QUERY < %t.native.out
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s --check-prefix=REPORT < %t.native.txt
// RUN: obelisk-cov report --format=json %t.native.obcov -o %t.native.json
// RUN: %python -c "import json,sys; d=json.load(open(sys.argv[1])); t=next(t for t in d['functional_types'] if t['name']=='cg'); assert t['goal']==80 and t['weight']==7 and t['merge_instances'] and t['comment']=='group type'; i=d['functional_instance_groups'][0]['items'][0]; assert i['type_goal']==50 and i['type_weight']==5 and i['type_comment']=='point type'; assert len([o for o in d['resolved_instance_options'] if o['instance']==0])==7" %t.native.json
// RUN: obelisk-cov report --format=html %t.native.obcov -o %t.native.html
// RUN: %python -c 'import json,re,sys; h=open(sys.argv[1]).read(); m=re.search(r"<script[^>]*id=\"coverage-data\"[^>]*>(.*?)</script>",h,re.S); assert m; d=json.loads(m.group(1)); t=next(t for t in d["functional_types"] if t["name"]=="cg"); assert t["goal"]=="80" and t["weight"]=="7" and t["merge_instances"] and t["comment"]=="group type"' %t.native.html
// RUN: %t.native --coverage-output=%t.conflict.obcov \
// RUN:   --coverage-test=type-options-conflict +conflict > /dev/null
// RUN: not obelisk-cov report --format=text %t.native.obcov \
// RUN:   %t.conflict.obcov -o %t.conflict.txt 2>&1 \
// RUN:   | FileCheck %s --check-prefix=CONFLICT
// RUN: %t.native --coverage-output=%t.default.obcov \
// RUN:   --coverage-test=type-options-default +defaults > /dev/null
// RUN: not obelisk-cov report --format=text %t.native.obcov \
// RUN:   %t.default.obcov -o %t.default-conflict.txt 2>&1 \
// RUN:   | FileCheck %s --check-prefix=CONFLICT
// RUN: obelisk-cov report --format=json --test=type-options \
// RUN:   %t.native.obcov %t.default.obcov -o %t.filtered-override.json
// RUN: %python -c "import json,sys; d=json.load(open(sys.argv[1])); t=next(t for t in d['functional_types'] if t['name']=='cg'); assert t['goal']==80 and t['weight']==7 and t['merge_instances']" %t.filtered-override.json
// RUN: obelisk-cov report --format=json --test=type-options-default \
// RUN:   %t.native.obcov %t.default.obcov -o %t.filtered-default.json
// RUN: %python -c "import json,sys; d=json.load(open(sys.argv[1])); t=next(t for t in d['functional_types'] if t['name']=='cg'); assert t['goal']==100 and t['weight']==1 and t['merge_instances'] is False" %t.filtered-default.json

// IEEE 1800-2023 19.7.1 permits these type options to be changed at any
// time. The pre-construction writes must affect configurations resolved by
// new(), and the later goal / merge writes must affect live queries at once.
module top;
  int value;
  covergroup cg;
    cp: coverpoint value { bins zero = {0}; bins one = {1}; }
  endgroup
  cg a, b;
  int covered, total;
  real pct;
  initial begin
    if (!$test$plusargs("defaults")) begin
      cg::type_option.weight = 7;
      cg::type_option.comment = "group type";
      cg::cp::type_option.weight = 5;
      cg::cp::type_option.comment = "point type";
      $display("stored-initial %0d %0d %s|%s", cg::type_option.weight,
               cg::cp::type_option.weight, cg::type_option.comment,
               cg::cp::type_option.comment);
    end
    a = new;
    b = new;
    value = 0;
    a.sample();
    value = 1;
    b.sample();
    pct = cg::get_coverage(covered, total);
    $display("group-average %.0f %0d/%0d", pct, covered, total);
    pct = cg::cp::get_coverage(covered, total);
    $display("point-before %.0f %0d/%0d", pct, covered, total);
    if (!$test$plusargs("defaults"))
      cg::cp::type_option.goal = 50;
    pct = cg::cp::get_coverage(covered, total);
    $display("point-after %.0f %0d/%0d", pct, covered, total);
    if (!$test$plusargs("defaults")) begin
      if ($test$plusargs("conflict"))
        cg::type_option.goal = 90;
      else
        cg::type_option.goal = 80;
      cg::type_option.merge_instances = 1;
      $display("stored-final %0d %0d", cg::cp::type_option.goal,
               cg::type_option.merge_instances);
    end
    pct = cg::get_coverage(covered, total);
    $display("group-merged %.0f %0d/%0d", pct, covered, total);
    $finish;
  end
endmodule

// QUERY: stored-initial 7 5 group type|point type
// QUERY: group-average 50 2/4
// QUERY: point-before 50 2/4
// QUERY: point-after 100 1/1
// QUERY: stored-final 50 1
// QUERY: group-merged 100 2/2

// REPORT: type cg ({{[0-9]+}}): 100.00%
// REPORT: type options: goal 80, weight 7, merge_instances true
// REPORT: type comment: "group type"
// REPORT: type comment: "point type"

// CONFLICT: obelisk-cov: conflicting type goal for functional type
