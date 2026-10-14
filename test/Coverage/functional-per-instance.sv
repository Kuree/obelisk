// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov --coverage-test=per-instance
// RUN: obelisk-cov report --format=text %t.obcov | FileCheck %s --check-prefix=REPORT
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON

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

// IEEE 19.7: option.per_instance is resolved per constructor configuration.
// Obelisk retains per-test detail by default even when the LRM permits an
// implementation to omit it, so both instances keep their own bins and the
// option is reported independently for each.
// REPORT: functional: 50.00% (2/4)
// REPORT: instance $auto$1: 50.00% (1/2) [per_instance=true, distribute_first=false]
// REPORT-NEXT: coverpoint cp: 1/2 (50.00%)
// REPORT-NEXT: bin zero: 1 [covered]
// REPORT-NEXT: bin one: 0 [uncovered]
// REPORT: instance $auto$2: 50.00% (1/2) [per_instance=false, distribute_first=false]
// REPORT-NEXT: coverpoint cp: 1/2 (50.00%)
// REPORT-NEXT: bin zero: 0 [uncovered]
// REPORT-NEXT: bin one: 1 [covered]

// JSON: "name":"$auto$1","configuration":"{{[0-9a-f]+}}","comment":"","goal":100,"weight":1,"per_instance":true
// JSON: "name":"$auto$2","configuration":"{{[0-9a-f]+}}","comment":"","goal":100,"weight":1,"per_instance":false
