// RUN: %talos --std=1800-2023 -O0 --coverage=functional -o %t %s 2>&1 | FileCheck %s --check-prefix=COMPILE --implicit-check-not=error:
// RUN: %t --coverage-output=%t.obcov | FileCheck %s --check-prefix=QUERY
// RUN: talos-cov report --format=text %t.obcov | FileCheck %s --check-prefix=REPORT

// LRM 19.7.1, 19.10 and 19.11. Keep this executable check to two small
// configurations; the initialization order and ABI contracts have MLIR tests.
// Assigning a class-embedded coverpoint's type option lowers without
// diagnosing the coverpoint receiver, which has no value type.
// COMPILE: warning: static variable initialization is skipped in constant function calls

class Owner;
  bit value;
  covergroup first;
    type_option.goal = 25;
    cp: coverpoint value { type_option.weight = 2; bins one = {1}; }
  endgroup
  covergroup second;
    type_option.goal = 75;
    cp: coverpoint value { type_option.weight = 3; bins one = {1}; }
  endgroup
  function new(); first = new; second = new; endfunction
endclass

module top;
  int value, evaluated;
  function automatic int constructor_limit(); evaluated++; return 1; endfunction
  function int initial_goal();
    static int calls = 0;
    return 50 + 25 * ++calls;
  endfunction
  function automatic string initial_comment(); return {"static", " group"}; endfunction
  covergroup cg(input int limit);
    type_option.weight = 0;
    type_option.goal = initial_goal();
    type_option.comment = initial_comment();
    type_option.merge_instances = 1;
    type_option.strobe = 1;
    type_option.distribute_first = 1;
    type_option.real_interval = 0.25;
    cp: coverpoint value {
      type_option.weight = 0;
      type_option.goal = 50;
      type_option.comment = "static point";
      type_option.real_interval = 0.5;
      bins values = {[0:limit]};
    }
    other: coverpoint value { bins values = {[0:limit]}; }
    cx: cross cp, other { type_option.weight = 0; }
  endgroup
  cg a, b;
  Owner owner;
  int qualifier_calls;
  function automatic Owner get_owner(); qualifier_calls++; return owner; endfunction
  real percentage;
  int covered, total;
  initial begin
    $display("group defaults %0d %0d %s %0d %0d %0d %.2f",
      cg::type_option.weight, cg::type_option.goal, cg::type_option.comment,
      cg::type_option.merge_instances, cg::type_option.strobe,
      cg::type_option.distribute_first, cg::type_option.real_interval);
    $display("point defaults %0d %0d %s %.1f", cg::cp::type_option.weight,
      cg::cp::type_option.goal, cg::cp::type_option.comment, cg::cp::type_option.real_interval);
    $display("unset defaults %0d %0d %.1f cross %0d %0d", cg::other::type_option.weight,
      cg::other::type_option.goal, cg::other::type_option.real_interval,
      cg::cx::type_option.weight, cg::cx::type_option.goal);
    percentage = cg::get_coverage(covered, total);
    $display("group %.0f %0d/%0d", percentage, covered, total);
    percentage = cg::cp::get_coverage(covered, total);
    $display("point %.0f %0d/%0d", percentage, covered, total);
    percentage = cg::cx::get_coverage(covered, total);
    $display("cross %.0f %0d/%0d", percentage, covered, total);
    cg::type_option.weight = 2;
    cg::other::type_option.weight = 0;
    percentage = cg::get_coverage(covered, total);
    $display("edited-group %.0f %0d/%0d", percentage, covered, total);
    percentage = cg::other::get_coverage(covered, total);
    $display("edited-point %.0f %0d/%0d", percentage, covered, total);
    $display("before new %0d global %.0f", evaluated, $get_coverage());
    a = new(constructor_limit());
    b = new(2);
    $display("after new %0d %0d %0d %s function %0d", evaluated,
      cg::type_option.weight, cg::type_option.goal, cg::type_option.comment, initial_goal());
    owner = new;
    $display("embedded %0d %0d %0d %0d", owner.first.type_option.goal,
      owner.second.type_option.goal, owner.first.cp.type_option.weight, owner.second.cp.type_option.weight);
    owner.first.type_option.goal = 50;
    get_owner().first.cp.type_option.weight = 4;
    $display("embedded edited %0d %0d %0d %0d qualifier %0d", owner.first.type_option.goal,
      owner.second.type_option.goal, owner.first.cp.type_option.weight,
      owner.second.cp.type_option.weight, qualifier_calls);
    $finish;
  end
endmodule

// QUERY: group defaults 0 75 static group 1 1 1 0.25
// QUERY: point defaults 0 50 static point 0.5
// QUERY: unset defaults 1 100 1.0 cross 0 100
// QUERY: group 100 0/0
// QUERY: point 100 0/0
// QUERY: cross 100 0/0
// QUERY: edited-group 0 0/0
// QUERY: edited-point 100 0/0
// QUERY: before new 0 global 100
// QUERY: after new 1 2 75 static group function 75
// QUERY: embedded 25 75 2 3
// QUERY: embedded edited 50 75 4 3 qualifier 1

// The database records each type's final static options, including those of
// the anonymous types embedded in Owner.
// REPORT-DAG: type options: goal 75, weight 2, merge_instances true
// REPORT-DAG: type comment: "static group"
// REPORT-DAG: type comment: "static point"
// REPORT-DAG: type options: goal 50, weight 1, merge_instances false
// REPORT-DAG: type options: goal 75, weight 1, merge_instances false
