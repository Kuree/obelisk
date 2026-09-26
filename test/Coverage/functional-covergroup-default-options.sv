// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=group-defaults > %t.native.stdout
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: obelisk-cov report --format=json %t.native.obcov -o %t.native.json
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=group-defaults > %t.bytecode.stdout
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.stdout %t.bytecode.stdout
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s --check-prefix=QUERY < %t.native.stdout
// RUN: FileCheck %s --check-prefix=REPORT < %t.native.txt
// RUN: FileCheck %s --check-prefix=JSON < %t.native.json

module top;
  bit [2:0] inherited_value;
  bit [2:0] overridden_value;

  covergroup cg(input int default_max, input int default_hits,
                input int override_max, input int override_hits);
    option.auto_bin_max = default_max;
    option.at_least = default_hits;

    inherited: coverpoint inherited_value;
    overridden: coverpoint overridden_value {
      option.auto_bin_max = override_max;
      option.at_least = override_hits;
    }
  endgroup

  cg cov;
  initial begin
    cov = new(2, 2, 4, 1);
    inherited_value = 0;
    overridden_value = 0;
    cov.sample();
    cov.sample();
    inherited_value = 4;
    cov.sample();
    $display("coverage %.6f", cov.get_inst_coverage());
    $finish;
  end
endmodule

// inherited uses the group defaults: one of two bins reaches two hits (50%).
// overridden replaces both defaults: one of four bins reaches one hit (25%).
// QUERY: coverage 37.500000
// REPORT: functional: 37.50% (2/6)
// REPORT-DAG: coverpoint inherited: 1/2 (50.00%)
// REPORT-DAG: coverpoint overridden: 1/4 (25.00%)
// REPORT-DAG: bin auto[0:3]: 2 [covered] {automatic} {at_least 2}
// REPORT-DAG: bin auto[4:7]: 1 [uncovered]
// REPORT-DAG: bin auto[0:1]: 3 [covered]
// JSON: "options":[
// JSON: "owner_kind":1,"scope":1,"option":3,"value_kind":1,"value":2
// JSON: "owner_kind":1,"scope":1,"option":4,"value_kind":1,"value":2
// JSON: "owner_kind":2,"scope":1,"option":3,"value_kind":1,"value":1
// JSON: "owner_kind":2,"scope":1,"option":4,"value_kind":1,"value":4
