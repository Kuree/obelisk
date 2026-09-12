// RUN: %obelisk -fno-lto --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=per-instance
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: obelisk-cov report --format=json %t.native.obcov -o %t.native.json
// RUN: %obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=per-instance
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s --check-prefix=REPORT < %t.native.txt
// RUN: FileCheck %s --check-prefix=JSON < %t.native.json

module top;
  bit sampled;

  covergroup cg(input bit retain_instance);
    option.per_instance = retain_instance;
    cp: coverpoint sampled {
      bins zero = {0};
      bins one = {1};
    }
  endgroup

  cg retained;
  cg aggregate_only;
  initial begin
    retained = new(1);
    aggregate_only = new(0);
    sampled = 0;
    retained.sample();
    sampled = 1;
    aggregate_only.sample();
    $finish;
  end
endmodule

// Obelisk retains per-test detail by default even when the LRM permits an
// implementation to omit it. The option is nevertheless resolved and
// reported independently for every constructor configuration.
// REPORT: functional: 50.00% (2/4)
// REPORT: instance $auto$1: 50.00% (1/2) [per_instance=true]
// REPORT: instance $auto$2: 50.00% (1/2) [per_instance=false]
// JSON: "name":"$auto$1","configuration":
// JSON: "per_instance":true
// JSON: "name":"$auto$2","configuration":
// JSON: "per_instance":false
