// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.producer.obcov \
// RUN:   --coverage-test=merge-producer +producer
// RUN: %t.native --coverage-load=%t.native.producer.obcov \
// RUN:   --coverage-output=%t.native.obcov --coverage-test=merge-consumer \
// RUN:   > %t.native.stdout
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.producer.obcov \
// RUN:   --coverage-test=merge-producer +producer
// RUN: %t.bytecode --coverage-load=%t.bytecode.producer.obcov \
// RUN:   --coverage-output=%t.bytecode.obcov --coverage-test=merge-consumer \
// RUN:   > %t.bytecode.stdout
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.stdout %t.bytecode.stdout
// RUN: FileCheck %s --check-prefix=QUERY < %t.native.stdout
// RUN: FileCheck %s --check-prefix=REPORT < %t.native.txt

module top;
  bit [1:0] sampled;

  covergroup merged(input int low, input int high, input bit track_instance);
    type_option.weight = 3;
    type_option.merge_instances = 1;
    option.get_inst_coverage = track_instance;
    cp: coverpoint sampled {
      bins b[] = {[low:high]};
    }
  endgroup

  covergroup averaged;
    type_option.weight = 1;
    option.get_inst_coverage = 0;
    cp: coverpoint sampled {
      bins zero = {0};
      bins one = {1};
    }
  endgroup

  merged cumulative_view;
  merged instance_view;
  averaged ordinary_first;
  averaged ordinary_second;
  real percentage;
  int covered;
  int total;
  initial begin
    cumulative_view = new(0, 1, 0);
    instance_view = new(1, 2, 1);
    ordinary_first = new;
    ordinary_second = new;

    if ($test$plusargs("producer")) begin
      sampled = 0;
      cumulative_view.sample();
      $finish;
    end

    sampled = 2;
    instance_view.sample();
    sampled = 0;
    ordinary_first.sample();

    percentage = cumulative_view.get_inst_coverage(covered, total);
    $display("cumulative-instance %.6f %0d %0d", percentage, covered, total);
    percentage = instance_view.get_inst_coverage(covered, total);
    $display("tracked-instance %.6f %0d %0d", percentage, covered, total);
    percentage = merged::get_coverage(covered, total);
    $display("merged-type %.6f %0d %0d", percentage, covered, total);
    percentage = ordinary_first.get_inst_coverage(covered, total);
    $display("ordinary-instance %.6f %0d %0d", percentage, covered, total);
    percentage = averaged::get_coverage(covered, total);
    $display("averaged-type %.6f %0d %0d", percentage, covered, total);
    $display("global %.6f", $get_coverage());
    $finish;
  end
endmodule

// With merge_instances, same-named bins form a union across both constructor
// configurations. get_inst_coverage=0 aliases the type union, while 1 keeps
// the local 1/2 result. Without merging, get_inst_coverage remains local.
// QUERY: cumulative-instance 66.666667 2 3
// QUERY-NEXT: tracked-instance 50.000000 1 2
// QUERY-NEXT: merged-type 66.666667 2 3
// QUERY-NEXT: ordinary-instance 50.000000 1 2
// QUERY-NEXT: averaged-type 12.500000 1 8
// QUERY-NEXT: global 53.125000
// REPORT: functional: 53.12% (3/16)
// REPORT-DAG: type merged ({{[0-9]+}}): 66.67%
// REPORT-DAG: type options: goal 100, weight 3, merge_instances true
// REPORT-DAG: type averaged ({{[0-9]+}}): 12.50%
// REPORT-DAG: type options: goal 100, weight 1, merge_instances false
