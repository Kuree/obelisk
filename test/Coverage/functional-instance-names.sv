// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=instance-names
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: obelisk-cov report --format=json %t.native.obcov -o %t.native.json
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=instance-names
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s --check-prefix=REPORT < %t.native.txt
// RUN: FileCheck %s --check-prefix=JSON < %t.native.json

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
// JSON-COUNT-2: "name":"shared-managed-instance-name","flags":0,"configuration":
// JSON: "functional_instance_groups":[{"type":{{[0-9]+}},"name":"shared-managed-instance-name"
// JSON-SAME: "sources":[{"run":{{.*}},"id":1},{"run":{{.*}},"id":2}]
