// RUN: %talos --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov --coverage-test=distribution | FileCheck %s --check-prefix=QUERY
// RUN: %t +load_only --coverage-load=%t.obcov | FileCheck %s --check-prefix=LOAD
// RUN: talos-cov report --format=text %t.obcov | FileCheck %s --check-prefix=REPORT
// RUN: %t +final_false --coverage-load=%t.obcov --coverage-output=%t.mixed.obcov --coverage-test=distribution-zero | FileCheck %s --check-prefix=FALSE
// RUN: talos-cov report --format=json %t.mixed.obcov | FileCheck %s --check-prefix=MIXED
// RUN: talos-cov merge --discard-test-detail -o %t.aggregate.obcov %t.mixed.obcov
// RUN: talos-cov report --format=json %t.aggregate.obcov | FileCheck %s --check-prefix=AGGREGATE --implicit-check-not='"option":10,"value_kind":1,'

// IEEE 19.5.1.1 and 19.7.1. Three instances, two configurations: changing the
// policy captures different partitions at new(), while a later reset reuses
// the first configuration. Existing bins and counters never change.
module top;
  int value, covered, total;
  real percentage;
  covergroup cg;
    type_option.distribute_first = 1;
    cp: coverpoint value {
      bins selected[2] = {[0:5]} with (item >= 2);
    }
  endgroup
  cg filter_first, distribute_first, filter_again;
  initial begin
    $display("initial policy=%0d", cg::type_option.distribute_first);
    if ($test$plusargs("load_only")) begin
      percentage = cg::get_coverage(covered, total);
      $display("loaded=%0.2f %0d/%0d", percentage, covered, total);
      $finish;
    end
    cg::type_option.distribute_first = 0;
    filter_first = new;
    filter_first.option.name = "filter";
    cg::type_option.distribute_first = 1;
    distribute_first = new;
    distribute_first.option.name = "distributed";
    // Instance syntax still addresses the one static type property.
    filter_first.type_option.distribute_first = 0;
    filter_again = new;
    filter_again.option.name = "filter_again";
    value = 2;
    filter_first.sample(); distribute_first.sample(); filter_again.sample();
    value = 3;
    filter_first.sample(); distribute_first.sample(); filter_again.sample();
    // Changing it after sampling cannot redistribute bins or reset counters.
    cg::type_option.distribute_first = !$test$plusargs("final_false");
    percentage = filter_first.get_inst_coverage(covered, total);
    $display("filter=%0.2f %0d/%0d", percentage, covered, total);
    percentage = distribute_first.get_inst_coverage(covered, total);
    $display("distributed=%0.2f %0d/%0d", percentage, covered, total);
    percentage = filter_again.get_inst_coverage(covered, total);
    $display("filter_again=%0.2f %0d/%0d", percentage, covered, total);
    percentage = cg::get_coverage(covered, total);
    $display("type=%0.2f %0d/%0d policy=%0d", percentage, covered, total,
             cg::type_option.distribute_first);
    cg::type_option.merge_instances = 1;
    percentage = cg::get_coverage(covered, total);
    $display("merged bins=%0.2f %0d/%0d", percentage, covered, total);
    cg::type_option.merge_instances = 0;
    $finish;
  end
endmodule

// QUERY: initial policy=1
// QUERY-NEXT: filter=50.00 1/2
// QUERY-NEXT: distributed=100.00 2/2
// QUERY-NEXT: filter_again=50.00 1/2
// QUERY-NEXT: type=66.67 4/6 policy=1
// QUERY-NEXT: merged bins=100.00 2/2
// LOAD: loaded=66.67 4/6
// REPORT: functional: 66.67%
// REPORT: type distribute_first: true [test "distribution"]
// REPORT-DAG: instance filter: 50.00% (1/2) [per_instance=false, distribute_first=false]
// REPORT-DAG: instance distributed: 100.00% (2/2) [per_instance=false, distribute_first=true]
// REPORT-DAG: instance filter_again: 50.00% (1/2) [per_instance=false, distribute_first=false]

// A second run that ends with the opposite policy keeps its own run-local
// value; the instances and their two configurations are unchanged.
// FALSE: type={{.*}} policy=0
// Run-local records are ordered by run identity.
// MIXED: "distribute_first_overrides":[
// MIXED-DAG: {"run":"[[TRUE_TEST:[^"]+]]","value":true}
// MIXED-DAG: {"run":"[[FALSE_TEST:[^"]+]]","value":false}
// MIXED: "functional_configurations":[{"type":{{[0-9]+}},"configuration":"{{[0-9a-f]+}}","flags":0,"options":[{{[^]]*}}]},{"type":{{[0-9]+}},"configuration":"{{[0-9a-f]+}}","flags":0,"options":[{{[^]]*}}]}],
// MIXED-DAG: {"run":"[[TRUE_TEST]]","instance":0,"owner":{{[0-9]+}},"owner_kind":1,"option":10,"value_kind":1,"value":1,
// MIXED-DAG: {"run":"[[FALSE_TEST]]","instance":0,"owner":{{[0-9]+}},"owner_kind":1,"option":10,"value_kind":1,"value":0,
// MIXED: "name":"filter","configuration":"[[FILTERED:[0-9a-f]+]]",{{[^{]*}}"distribute_first":false,{{[^{]*}}"covered":1,"total":2,
// MIXED: "name":"distributed","configuration":"{{[0-9a-f]+}}",{{[^{]*}}"distribute_first":true,{{[^{]*}}"covered":2,"total":2,
// MIXED: "name":"filter_again","configuration":"[[FILTERED]]",{{[^{]*}}"distribute_first":false,{{[^{]*}}"covered":1,"total":2,

// Discarding test detail drops run-local policy records along with the runs.
// AGGREGATE: "runs":[{{[^]]*}}"name":"aggregate"{{[^]]*}}}],"files"
// AGGREGATE: "distribute_first_overrides":[]
