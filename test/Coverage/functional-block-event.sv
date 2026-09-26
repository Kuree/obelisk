// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=block-event > %t.native.out
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=block-event > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s < %t.native.out

class holder;
  int sampled;

  task target;
    sampled = 1;
  endtask

  covergroup method_group @@(begin target or end target);
    point: coverpoint sampled {
      bins zero = {0};
      bins one = {1};
    }
  endgroup

  function new;
    method_group = new;
  endfunction
endclass

module top;
  int sampled;
  holder first;
  holder second;
  holder first_alias;

  task automatic target;
    sampled++;
  endtask

  // Strobe has no effect on block-event sampling. All four same-time-slot
  // begin/end samples below must commit synchronously.
  covergroup task_group @@(begin target or end target);
    type_option.strobe = 1;
    point: coverpoint sampled {
      bins zero = {0};
      bins one = {1};
      bins two = {2};
      bins three = {3};
    }
  endgroup

  covergroup disable_group @@(begin watched or end watched);
    point: coverpoint sampled {
      bins four = {4};
      bins five = {5};
    }
  endgroup

  covergroup qualified_group @@(begin first.target or end second.target);
    point: coverpoint first.sampled {
      bins zero = {0};
      bins one = {1};
    }
  endgroup

  covergroup duplicate_group @@(begin first.target or begin first.target or
                                begin first_alias.target);
    point: coverpoint first.sampled {
      option.at_least = 2;
      bins zero = {0};
    }
  endgroup

  task_group task_coverage;
  disable_group disable_coverage;
  qualified_group qualified_coverage;
  duplicate_group duplicate_coverage;
  int covered;
  int total;
  real percentage;

  initial begin
    task_coverage = new;
    disable_coverage = new;

    sampled = 0;
    target();
    sampled = 2;
    target();
    percentage = task_coverage.get_inst_coverage(covered, total);
    $display("task %.6f %0d %0d", percentage, covered, total);

    sampled = 4;
    begin : watched
      sampled = 5;
      disable watched;
    end
    percentage = disable_coverage.get_inst_coverage(covered, total);
    $display("disable %.6f %0d %0d", percentage, covered, total);

    first = new;
    second = new;
    first_alias = first;
    qualified_coverage = new;
    duplicate_coverage = new;
    first.target();
    percentage = duplicate_coverage.get_inst_coverage(covered, total);
    $display("duplicate %.6f %0d %0d", percentage, covered, total);
    second.target();
    percentage = qualified_coverage.get_inst_coverage(covered, total);
    $display("qualified %.6f %0d %0d", percentage, covered, total);
    percentage = first.method_group.get_inst_coverage(covered, total);
    $display("first %.6f %0d %0d", percentage, covered, total);
    percentage = second.method_group.get_inst_coverage(covered, total);
    $display("second %.6f %0d %0d", percentage, covered, total);
    $finish;
  end
endmodule

// CHECK: task 100.000000 4 4
// CHECK-NEXT: disable 50.000000 1 2
// CHECK-NEXT: duplicate 0.000000 0 1
// CHECK-NEXT: qualified 100.000000 2 2
// CHECK-NEXT: first 100.000000 2 2
// CHECK-NEXT: second 100.000000 2 2
