// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov --coverage-test=producer \
// RUN:   > %t.native.out
// RUN: %t.native --coverage-load=%t.native.obcov \
// RUN:   --coverage-output=%t.native.loaded.obcov --coverage-test=loaded \
// RUN:   +loaded > %t.native.loaded.out
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov --coverage-test=producer \
// RUN:   > %t.bytecode.out
// RUN: %t.bytecode --coverage-load=%t.bytecode.obcov \
// RUN:   --coverage-output=%t.bytecode.loaded.obcov --coverage-test=loaded \
// RUN:   +loaded > %t.bytecode.loaded.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: diff -u %t.native.loaded.out %t.bytecode.loaded.out
// RUN: FileCheck %s --check-prefix=QUERY < %t.native.out
// RUN: FileCheck %s --check-prefix=LOADED < %t.native.loaded.out
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.report
// RUN: FileCheck %s --check-prefix=REPORT < %t.report
// RUN: obelisk-cov report --format=json %t.native.obcov -o %t.json
// RUN: %python -c "import json,sys; d=json.load(open(sys.argv[1])); t=next(t for t in d['functional_types'] if t['name']=='empty_merged_cg'); gs=[g for g in d['functional_instance_groups'] if g['type']==t['id']]; assert t['percent']==0.0; assert sorted(i['at_least'] for g in gs for i in g['items'])==[1,2]; assert sorted(i['total'] for g in gs for i in g['items'])==[0,1]" %t.json

module top;
  int value;

  covergroup cumulative_cg;
    cp: coverpoint value {
      bins zero = {0};
    }
  endgroup

  covergroup lowered_cg;
    cp: coverpoint value {
      option.at_least = 3;
      bins zero = {0};
    }
  endgroup

  covergroup merged_cg(input int low_bound, input int high_bound);
    type_option.merge_instances = 1;
    cp: coverpoint value {
      bins values[] = {[low_bound:high_bound]};
    }
  endgroup

  covergroup empty_merged_cg(input int bin_count);
    type_option.merge_instances = 1;
    option.auto_bin_max = bin_count;
    cp: coverpoint value;
  endgroup

  cumulative_cg low, high;
  lowered_cg lowered;
  merged_cg merged_low, merged_high;
  empty_merged_cg populated, empty;
  int covered, total;
  real pct;

  initial begin
    if ($test$plusargs("loaded")) begin
      pct = lowered_cg::get_coverage(covered, total);
      $display("loaded-lowered %.0f %0d/%0d", pct, covered, total);
      pct = empty_merged_cg::get_coverage(covered, total);
      $display("loaded-empty-merged %.0f %0d/%0d", pct, covered, total);
      $finish;
    end

    low = new;
    high = new;
    low.option.name = "low";
    high.option.name = "high";
    low.cp.option.at_least = 1;
    high.cp.option.at_least = 2;
    low.sample();
    pct = low.cp.get_inst_coverage(covered, total);
    $display("low-instance %.0f %0d/%0d", pct, covered, total);
    pct = cumulative_cg::get_coverage(covered, total);
    $display("cumulative-group %.0f %0d/%0d", pct, covered, total);
    pct = low.cp.get_coverage(covered, total);
    $display("cumulative-point %.0f %0d/%0d", pct, covered, total);

    merged_low = new(0, 0);
    merged_high = new(1, 1);
    merged_low.cp.option.at_least = 1;
    merged_high.cp.option.at_least = 2;
    value = 0;
    merged_low.sample();
    pct = merged_cg::get_coverage(covered, total);
    $display("merged-group %.0f %0d/%0d", pct, covered, total);

    populated = new(1);
    empty = new(0);
    populated.cp.option.at_least = 1;
    empty.cp.option.at_least = 2;
    value = 0;
    populated.sample();
    pct = empty_merged_cg::get_coverage(covered, total);
    $display("empty-merged-group %.0f %0d/%0d", pct, covered, total);

    lowered = new;
    lowered.option.name = "lowered";
    lowered.cp.option.at_least = 1;
    lowered.sample();
    pct = lowered_cg::get_coverage(covered, total);
    $display("lowered-live %.0f %0d/%0d", pct, covered, total);
    $finish;
  end
endmodule

// QUERY: low-instance 100 1/1
// QUERY: cumulative-group 0 0/2
// QUERY: cumulative-point 0 0/2
// QUERY: merged-group 0 0/2
// QUERY: empty-merged-group 0 0/1
// QUERY: lowered-live 100 1/1
// LOADED: loaded-lowered 100 1/1
// LOADED: loaded-empty-merged 0 0/1

// REPORT-DAG: type empty_merged_cg ({{[0-9]+}}): 0.00%
// REPORT-DAG: type merged_cg ({{[0-9]+}}): 0.00%
// REPORT-DAG: type cumulative_cg ({{[0-9]+}}): 0.00%
// REPORT-DAG: instance low: 100.00% (1/1)
// REPORT-DAG: instance high: 0.00% (0/1)
// REPORT-DAG: type lowered_cg
// REPORT-DAG: instance lowered: 100.00% (1/1)
// REPORT-DAG: bin zero: 1 [covered]
