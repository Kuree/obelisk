// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -emit-sim %s \
// RUN:   | FileCheck %s --check-prefix=IR
// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -emit-sim %s \
// RUN:   | %python %S/../Conversion/Inputs/dump-coverage-schema.py \
// RUN:   | FileCheck %s --check-prefix=SCHEMA
// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=clocking-event-strobe > %t.native.out
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=clocking-event-strobe > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s --check-prefix=QUERY < %t.native.out
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.report
// RUN: FileCheck %s --check-prefix=REPORT < %t.report

module top;
  bit clock;
  bit gate;
  bit sampled;

  covergroup clocked @(posedge clock iff gate);
    type_option.strobe = 1;
    point: coverpoint sampled {
      bins zero = {0};
      bins one = {1};
    }
  endgroup

  clocked coverage;
  int covered;
  int total;
  real percentage;

  initial begin
    coverage = new;

    // IEEE 1800-2023 19.3: both qualifying clock events collapse to one
    // Postponed sample, which observes the final value in this time slot.
    gate = 1;
    sampled = 0;
    clock = 1;
    clock = 0;
    sampled = 1;
    clock = 1;
    clock = 0;
    #0;
    percentage = coverage.get_inst_coverage(covered, total);
    $display("strobe before postponed %.6f %0d %0d", percentage, covered,
             total);
    #1;
    percentage = coverage.get_inst_coverage(covered, total);
    $display("strobe coalesced %.6f %0d %0d", percentage, covered, total);

    // IEEE 1800-2023 9.4.2.3: iff is tested when the primary changes. A gate
    // that becomes true later in the slot cannot admit the earlier edge.
    gate = 0;
    sampled = 0;
    clock = 1;
    clock = 0;
    gate = 1;
    #1;
    percentage = coverage.get_inst_coverage(covered, total);
    $display("strobe gated %.6f %0d %0d", percentage, covered, total);

    // type_option.strobe affects clock-event scheduling only. A procedural
    // sample() call remains immediate and observes sampled=0 here.
    coverage.sample();
    percentage = coverage.get_inst_coverage(covered, total);
    $display("strobe manual %.6f %0d %0d", percentage, covered, total);
    $finish;
  end
endmodule

// QUERY: strobe before postponed 0.000000 0 2
// QUERY-NEXT: strobe coalesced 50.000000 1 2
// QUERY-NEXT: strobe gated 50.000000 1 2
// QUERY-NEXT: strobe manual 100.000000 2 2
// REPORT: functional: 100.00% (2/2)

// The one mutable v1 schema retains the static type option.
// SCHEMA: functional_option_plan owner={{[1-9][0-9]*}} expression={{[1-9][0-9]*}} owner_kind=1 scope=2 option=12 ordinal=12 flags=0

// IR: simulation.covergroup.clock_event.register
// IR: simulation.suspend.forever
