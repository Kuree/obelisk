// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov --coverage-test=producer | FileCheck %s --check-prefix=QUERY
// RUN: %t --coverage-load=%t.obcov --coverage-output=%t.loaded.obcov --coverage-test=loaded +loaded | FileCheck %s --check-prefix=LOADED
// RUN: obelisk-cov report --format=text %t.obcov | FileCheck %s --check-prefix=REPORT
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON

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

// IEEE 1800-2023 19.11.1: the merged type of empty_merged_cg contributes no
// covered bins, and its two instances keep their own at_least and bin totals.
// JSON: "id":[[TYPE:[0-9]+]],"scope":{{[0-9]+}},"name":"empty_merged_cg",{{[^{]*}}"percent":0.000000,
// JSON-DAG: {"type":[[TYPE]],"name":"{{[^"]*}}",{{[^[]*}}"sources":[{{[^]]*}}],"items":[{{[{]}}"id":{{[0-9]+}},"template_item":{{[0-9]+}},"name":"cp",{{[^{]*}}"at_least":1,{{[^{]*}}"covered":1,"total":1,
// JSON-DAG: {"type":[[TYPE]],"name":"{{[^"]*}}",{{[^[]*}}"sources":[{{[^]]*}}],"items":[{{[{]}}"id":{{[0-9]+}},"template_item":{{[0-9]+}},"name":"cp",{{[^{]*}}"at_least":2,{{[^{]*}}"covered":0,"total":0,
