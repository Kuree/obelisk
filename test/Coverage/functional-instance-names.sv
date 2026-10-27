// RUN: %talos --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov --coverage-test=instance-names
// RUN: talos-cov report --format=text %t.obcov | FileCheck %s --check-prefix=REPORT
// RUN: talos-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON

module top;
  bit value;

  covergroup cg(input string configured_name);
    option.name = configured_name;
    cp: coverpoint value {
      bins zero = {0};
      bins one = {1};
    }
  endgroup

  cg first;
  cg second;
  initial begin
    first = new("configured-first-managed-name");
    second = new("shared-managed-instance-name");
    first.set_inst_name("shared-managed-instance-name");
    value = 0;
    first.sample();
    value = 1;
    second.sample();
    $finish;
  end
endmodule

// Names are per-instance metadata and therefore do not change the resolved
// configuration fingerprint. Equal named instances aggregate into one report
// instance while retaining both physical source contributions.
// REPORT: functional: 100.00% (2/2)
// REPORT: instance shared-managed-instance-name: 100.00% (2/2)
// REPORT-NEXT: coverpoint cp: 2/2 (100.00%)

// JSON: "resolved_instances":[{"run":"{{[^"]*}}","id":1,"type":{{[0-9]+}},"name":"shared-managed-instance-name","flags":0,"configuration":"[[CFG:[0-9a-f]+]]"},{"run":"{{[^"]*}}","id":2,"type":{{[0-9]+}},"name":"shared-managed-instance-name","flags":0,"configuration":"[[CFG]]"}]
// JSON: "functional_instance_groups":[{"type":{{[0-9]+}},"name":"shared-managed-instance-name","configuration":"[[CFG]]"
// JSON-SAME: "sources":[{"run":"{{[^"]*}}","id":1},{"run":"{{[^"]*}}","id":2}]
