// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -emit-sim %s \
// RUN:   | FileCheck %s --check-prefix=IR
// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -emit-sim %s \
// RUN:   | obelisk-opt --test-obelisk-print-coverage-schema -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=SCHEMA
// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s --check-prefix=QUERY
// RUN: obelisk-cov report --format=text %t.obcov | FileCheck %s --check-prefix=REPORT

module top;
  bit clock;
  bit gate;
  bit sampled;

  covergroup clocked @(posedge clock iff gate);
    point: coverpoint sampled {
      option.at_least = 2;
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

    // The first edge follows new() in the same uninterrupted activation. The
    // hidden owner must therefore register before construction returns. Both
    // edges occur in one time slot and must sample their event-instant values,
    // even though sampled changes again before this process yields.
    gate = 1;
    sampled = 0;
    clock = 1;
    clock = 0;
    sampled = 1;
    clock = 1;
    clock = 0;
    sampled = 0;
    #0;
    percentage = coverage.get_inst_coverage(covered, total);
    $display("clocking first %.6f %0d %0d", percentage, covered, total);

    // An iff is evaluated when the primary changes. Turning the gate on after
    // the edge cannot retroactively admit this occurrence.
    gate = 0;
    clock = 1;
    clock = 0;
    gate = 1;
    #0;
    percentage = coverage.get_inst_coverage(covered, total);
    $display("clocking gated %.6f %0d %0d", percentage, covered, total);

    // Disabled at the event instant stays disabled even though start() occurs
    // before any scheduler drain point.
    coverage.stop();
    sampled = 0;
    clock = 1;
    clock = 0;
    coverage.start();
    #0;
    percentage = coverage.get_inst_coverage(covered, total);
    $display("clocking stopped %.6f %0d %0d", percentage, covered, total);

    // Conversely this enabled occurrence counts even though stop() follows it
    // before the process yields.
    sampled = 0;
    clock = 1;
    clock = 0;
    coverage.stop();
    #0;
    percentage = coverage.get_inst_coverage(covered, total);
    $display("clocking active %.6f %0d %0d", percentage, covered, total);

    coverage.start();
    sampled = 1;
    clock = 1;
    #0;
    percentage = coverage.get_inst_coverage(covered, total);
    $display("clocking full %.6f %0d %0d", percentage, covered, total);
    $finish;
  end
endmodule

// QUERY: clocking first 0.000000 0 2
// QUERY-NEXT: clocking gated 0.000000 0 2
// QUERY-NEXT: clocking stopped 0.000000 0 2
// QUERY-NEXT: clocking active 50.000000 1 2
// QUERY-NEXT: clocking full 100.000000 2 2
// REPORT: functional: 100.00% (2/2)

// SamplingEvent, Event, Boolean. The schema stays the sole mutable format;
// the automatic sampler does not introduce an alternate execution revision.
// SCHEMA: functional_expression id={{[1-9][0-9]*}} owner={{[1-9][0-9]*}} owner_kind=1 role=1 result_kind=1 width=0 signedness=3 owner_ordinal=0 owner_subordinal=0 phase=3 result_ordinal=0

// IR: simulation.func private @{{[^ ]*}}.$covergroup_event_sample.{{[0-9]+}}
// IR-SAME: entry_kind = 14
// IR: simulation.covergroup.sample
// IR: simulation.func private @{{[^ ]*}}.$covergroup_event.{{[0-9]+}}
// IR-SAME: schedule.covergroup_clocking_sampler
// IR-SAME: schedule.detached_controls
// IR-SAME: schedule.prime_on_spawn
// IR: simulation.observer.bind @{{[^ ]*}}.$covergroup_event_sample.{{[0-9]+}}
// IR: simulation.covergroup.clock_event.register
// IR-SAME: conditions 1 edges [1] indices [0]
// IR: simulation.suspend.forever
// IR: simulation.covergroup.create
// IR-NEXT: {{.*}} = simulation.spawn @{{.*}}.$covergroup_event.{{[0-9]+}}
