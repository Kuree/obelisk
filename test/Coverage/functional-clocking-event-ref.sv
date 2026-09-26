// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional --compile-threads=12 -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov > %t.native.out
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional --compile-threads=12 -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s < %t.native.out

module functional_clocking_event_ref;
  class holder;
    bit clock;
  endclass

  bit clock_a;
  bit clock_b;
  bit dynamic_clock;
  bit self_clock;
  bit dynamic_values[$] = '{0, 0};
  int sampled;

  covergroup cg(ref bit event_clock) @(posedge event_clock);
    point: coverpoint sampled {
      bins one = {1};
      bins two = {2};
      bins three = {3};
    }
  endgroup

  covergroup dynamic_cg(ref bit enabled)
      @(posedge dynamic_clock iff enabled);
    point: coverpoint enabled {
      bins one = {1};
    }
  endgroup

  covergroup self_cg @(posedge self_clock);
    point: coverpoint self_clock {
      bins one = {1};
    }
  endgroup

  holder object = new;
  cg a;
  cg b;
  cg member;
  cg automatic_instance;
  dynamic_cg dynamic_instance;
  self_cg self_instance;
  int covered;
  int total;
  real percentage;

  task automatic exercise_automatic;
    bit automatic_clock;
    automatic_instance = new(automatic_clock);
    sampled = 2;
    automatic_clock = 1;
    #0;
    percentage = automatic_instance.get_inst_coverage(covered, total);
    $display("automatic %.6f %0d %0d", percentage, covered, total);
  endtask

  initial begin
    a = new(clock_a);
    b = new(clock_b);
    member = new(object.clock);

    sampled = 1;
    clock_a = 1;
    #0;
    percentage = a.get_inst_coverage(covered, total);
    $display("a %.6f %0d %0d", percentage, covered, total);
    percentage = b.get_inst_coverage(covered, total);
    $display("b %.6f %0d %0d", percentage, covered, total);

    sampled = 2;
    clock_b = 1;
    #0;
    percentage = b.get_inst_coverage(covered, total);
    $display("b2 %.6f %0d %0d", percentage, covered, total);

    sampled = 3;
    object.clock = 1;
    #0;
    percentage = member.get_inst_coverage(covered, total);
    $display("member %.6f %0d %0d", percentage, covered, total);
    exercise_automatic();

    dynamic_instance = new(dynamic_values[0]);
    dynamic_clock = 1;
    #0;
    percentage = dynamic_instance.get_inst_coverage(covered, total);
    $display("dynamic0 %.6f %0d %0d", percentage, covered, total);
    dynamic_values[0] = 1;
    #0;
    percentage = dynamic_instance.get_inst_coverage(covered, total);
    $display("dynamic1 %.6f %0d %0d", percentage, covered, total);
    dynamic_clock = 0;
    dynamic_clock = 1;
    #0;
    percentage = dynamic_instance.get_inst_coverage(covered, total);
    $display("dynamic2 %.6f %0d %0d", percentage, covered, total);

    self_instance = new;
    self_clock = 1;
    #0;
    percentage = self_instance.get_inst_coverage(covered, total);
    $display("self %.6f %0d %0d", percentage, covered, total);
    $finish;
  end
endmodule

// CHECK: a 33.333333 1 3
// CHECK-NEXT: b 0.000000 0 3
// CHECK-NEXT: b2 33.333333 1 3
// CHECK-NEXT: member 33.333333 1 3
// CHECK-NEXT: automatic 33.333333 1 3
// CHECK-NEXT: dynamic0 0.000000 0 1
// CHECK-NEXT: dynamic1 0.000000 0 1
// CHECK-NEXT: dynamic2 100.000000 1 1
// CHECK-NEXT: self 100.000000 1 1
