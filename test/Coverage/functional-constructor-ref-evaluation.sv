// RUN: %talos --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: talos-cov report --format=json %t.obcov | FileCheck %s --check-prefix=REPORT

// IEEE 1800-2023 19.3 and 13.5.2: constructor refs track their actual storage.
// All actuals finish evaluation before constructor options and bin expressions.
// Mutations made by another actual must therefore be visible independently of
// the unspecified argument evaluation order. Resolved bin bounds stay fixed
// after construction; subsequent samples still read the live ref.
class Base;
  int value = 1;
  function int bump;
    value++;
    return 0;
  endfunction
  covergroup cg(ref int live, input int ignored);
    option.per_instance = 1;
    option.get_inst_coverage = 1;
    option.goal = live;
    cp: coverpoint value { bins selected[] = {[0:live]}; }
  endgroup
  function new;
    cg = new(value, bump());
  endfunction
endclass

class Derived extends Base;
  covergroup extends cg;
    option.auto_bin_max = live + 1;
    added: coverpoint value { bins selected = {live}; }
  endgroup
endclass

module top;
  int value = 1, default_value = 1;
  string text = "a";
  real number = 1.0;
  function automatic int bump(inout int target);
    target++;
    return 0;
  endfunction
  function automatic int append(inout string target);
    target = {target, "!"};
    return 0;
  endfunction
  function automatic int increase(inout real target);
    target += 1.0;
    return 0;
  endfunction
  covergroup integral(ref int live, input int ignored);
    option.per_instance = 1;
    option.get_inst_coverage = 1;
    option.auto_bin_max = live;
    cp: coverpoint live { bins selected[] = {[0:live]}; }
  endgroup
  covergroup strings(input string frozen, ref string live, input int ignored);
    option.name = {frozen, ":", live};
    option.comment = live;
    cp: coverpoint live.len() { bins selected = {live.len()}; }
  endgroup
  covergroup reals(ref real live, input int ignored);
    option.goal = live;
    cp: coverpoint live { bins selected = {live}; }
  endgroup
  covergroup defaults(ref int live = default_value,
                      input int ignored = bump(default_value));
    option.auto_bin_max = live;
    cp: coverpoint live { bins selected[] = {[0:live]}; }
  endgroup
  integral scalar_cov;
  strings string_cov;
  reals real_cov;
  defaults default_cov;
  Derived derived;
  int covered, total;
  real percentage;
  initial begin
    scalar_cov = new(value, bump(value));
    scalar_cov.sample();
    percentage = scalar_cov.cp.get_inst_coverage(covered, total);
    $display("scalar value=%0d limit=%0d coverage=%.3f bins=%0d/%0d", value,
             scalar_cov.option.auto_bin_max, percentage, covered, total);
    value = 3;
    scalar_cov.sample();
    value = 0;
    scalar_cov.sample();
    percentage = scalar_cov.cp.get_inst_coverage(covered, total);
    $display("fixed limit=%0d coverage=%.3f bins=%0d/%0d",
             scalar_cov.option.auto_bin_max, percentage, covered, total);

    string_cov = new("frozen", text, append(text));
    string_cov.sample();
    percentage = string_cov.cp.get_inst_coverage(covered, total);
    $display("string %s %s coverage=%.0f bins=%0d/%0d", string_cov.option.name,
             string_cov.option.comment, percentage, covered, total);

    real_cov = new(number, increase(number));
    real_cov.sample();
    percentage = real_cov.cp.get_inst_coverage(covered, total);
    $display("real value=%.0f goal=%0d coverage=%.0f bins=%0d/%0d", number,
             real_cov.option.goal, percentage, covered, total);

    default_cov = new;
    default_cov.sample();
    percentage = default_cov.cp.get_inst_coverage(covered, total);
    $display("default value=%0d limit=%0d coverage=%.3f bins=%0d/%0d",
             default_value, default_cov.option.auto_bin_max, percentage, covered, total);

    derived = new;
    derived.cg.sample();
    percentage = derived.cg.cp.get_inst_coverage(covered, total);
    $display("inherited value=%0d goal=%0d limit=%0d coverage=%.3f bins=%0d/%0d",
             derived.value, derived.cg.option.goal, derived.cg.option.auto_bin_max,
             percentage, covered, total);
    percentage = derived.cg.added.get_inst_coverage(covered, total);
    $display("added coverage=%.0f bins=%0d/%0d", percentage, covered, total);
    $finish;
  end
endmodule

// CHECK: scalar value=2 limit=2 coverage=33.333 bins=1/3
// CHECK-NEXT: fixed limit=2 coverage=66.667 bins=2/3
// CHECK-NEXT: string frozen:a! a! coverage=100 bins=1/1
// CHECK-NEXT: real value=2 goal=2 coverage=100 bins=1/1
// CHECK-NEXT: default value=2 limit=2 coverage=33.333 bins=1/3
// CHECK-NEXT: inherited value=2 goal=2 limit=3 coverage=33.333 bins=1/3
// CHECK-NEXT: added coverage=100 bins=1/1

// REPORT-DAG: "name":{{ *}}"frozen:a!"
// REPORT-DAG: "string_value":{{ *}}"a!"
