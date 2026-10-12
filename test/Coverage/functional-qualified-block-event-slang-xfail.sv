// XFAIL: *
// IEEE 1800-2023 19.3 permits class methods in block event expressions.
// Slang v11.0 rejects the object-qualified method before AST import.

// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov --coverage-test=block-event | FileCheck %s

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
  holder first;
  holder second;
  holder first_alias;

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

  qualified_group qualified_coverage;
  duplicate_group duplicate_coverage;
  int covered;
  int total;
  real percentage;

  initial begin
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

// CHECK: duplicate 0.000000 0 1
// CHECK-NEXT: qualified 100.000000 2 2
// CHECK-NEXT: first 100.000000 2 2
// CHECK-NEXT: second 100.000000 2 2
