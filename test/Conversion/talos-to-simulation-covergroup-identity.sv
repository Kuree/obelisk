// RUN: talos -emit-slang %s | talos-opt --convert-slang-to-talos --lower-talos-to-sim=opt-level=0 | FileCheck %s

// Covergroup handles compare instance identities (8.4, 11.4.5, 19.3), and null
// is an identity of its own. ==, === and ==? share one comparison; !handle is
// the null test.
// CHECK: %[[NULL:.*]] = simulation.covergroup.null : !simulation.covergroup_handle<@[[CG:[^>]+]]>
// CHECK: simulation.covergroup.equal %{{.*}}, %[[NULL]]
// CHECK: simulation.covergroup.equal %{{.*}}, %[[NULL]]
// CHECK: %[[ABSENT_NULL:.*]] = simulation.covergroup.equal %{{.*}}, %[[NULL]]
// CHECK-NEXT: simulation.display {{.*}}, %[[ABSENT_NULL]], %[[ABSENT_NULL]])
// CHECK: %[[SAME:.*]] = simulation.covergroup.equal %{{.*}}, %{{.*}} : !simulation.covergroup_handle<@[[CG]]>, !simulation.covergroup_handle<@[[CG]]>
// CHECK-NEXT: simulation.display {{.*}}(%{{.*}}, %[[SAME]], %[[SAME]], %[[SAME]])
// CHECK: %[[DIFFERENT:.*]] = simulation.covergroup.equal
// CHECK-NEXT: %[[UNEQUAL:.*]] = arith.xori %[[DIFFERENT]]
// CHECK-NEXT: simulation.display {{.*}}(%{{.*}}, %[[UNEQUAL]], %[[UNEQUAL]], %[[UNEQUAL]])

// Handles of different covergroup types still compare identities.
// CHECK: simulation.covergroup.equal %{{.*}}, %{{.*}} : !simulation.covergroup_handle<@[[CG]]>, !simulation.covergroup_handle<@__talos_covergroup_other_cg_

// A handle is true exactly when it is not null, as an operand and as an if
// condition.
// CHECK: %[[LIVE_NULL:.*]] = simulation.covergroup.equal %{{.*}}, %[[NULL]]
// CHECK-NEXT: %[[LIVE:.*]] = arith.xori %[[LIVE_NULL]]
// CHECK-NEXT: simulation.display {{.*}}(%{{.*}}, %[[LIVE]], %[[LIVE]], %[[LIVE_NULL]])
// CHECK: %[[UNSET:.*]] = simulation.covergroup.equal %{{.*}}, %[[NULL]]
// CHECK-NEXT: %[[SET:.*]] = arith.xori %[[UNSET]]
// CHECK-NEXT: cf.cond_br %[[SET]]

// An ambiguous condition yields an operand only when both name the same
// instance, and otherwise the uninitialized value, null (11.4.11).
// CHECK: simulation.bytes.constant "SELECT %0d"
// CHECK: %[[BOTH:.*]] = simulation.covergroup.equal %[[EITHER:[^,]+]], %{{.*}} :
// CHECK-NEXT: %[[DEFAULT:.*]] = simulation.covergroup.null
// CHECK-NEXT: arith.select %[[BOTH]], %[[EITHER]], %[[DEFAULT]]

// Container equality compares each element's identity.
// CHECK: simulation.bytes.constant "DEFAULTS %0d %0d %0d"
// CHECK: simulation.covergroup.equal
// CHECK: simulation.logic.binary and
// CHECK: simulation.bytes.constant "ARRAY EQUAL %0d %0d"

module top;
  covergroup cg with function sample(input int value);
    option.per_instance = 1;
    cp: coverpoint value { bins selected = {1}; }
  endgroup
  covergroup other_cg;
    cp: coverpoint 1 { bins selected = {1}; }
  endgroup
  cg first, second, alias_group, absent;
  other_cg other;
  cg dynamic_groups[], dynamic_copy[];
  cg queued_groups[$];
  cg associative_groups[int];
  int calls, covered, total;
  bit condition;
  real result;

  function bit tick();
    ++calls;
    return 1;
  endfunction

  initial begin
    $display("NULL %0d %0d %0d %0d", first == null, null === second,
             absent ==? null, !absent);
    first = new();
    second = new();
    alias_group = first;
    other = new();
    $display("EQUAL %0d %0d %0d", first == alias_group,
             first === alias_group, first ==? alias_group);
    $display("DISTINCT %0d %0d %0d", first != second,
             first !== second, first !=? second);
    $display("TYPES %0d %0d %0d", first == other, first != other,
             absent == other);
    $display("LIVE %0d %0d %0d", first != null, null !== first, !first);
    if (absent)
      $fatal(1, "uninitialized covergroup is true");
    if (first)
      $display("IF LIVE");
    condition = (absent != null) && tick();
    condition = (first != null) || tick();
    condition = (first != null) && tick();
    condition = (absent != null) || tick();
    $display("SHORT CIRCUIT %0d %0d", calls, condition);

    alias_group = absent ? second : first;
    $display("SELECT %0d", alias_group == first);
    alias_group = 1'bx ? first : alias_group;
    $display("AMBIGUOUS SAME %0d", alias_group == first);
    alias_group = 1'bx ? first : second;
    $display("AMBIGUOUS DIFFERENT %0d", alias_group == null);

    dynamic_groups = new[2];
    dynamic_groups[0] = first;
    dynamic_groups = new[4](dynamic_groups);
    dynamic_copy = dynamic_groups;
    $display("DEFAULTS %0d %0d %0d", dynamic_groups[1] == null,
             dynamic_groups[2] == null, dynamic_groups[3] === null);
    $display("ARRAY EQUAL %0d %0d", dynamic_groups == dynamic_copy,
             dynamic_groups === dynamic_copy);
    dynamic_groups[0] = second;
    $display("ARRAY DISTINCT %0d", dynamic_groups != dynamic_copy);
    queued_groups.push_back(first);
    $display("QUEUE DEFAULT %0d", queued_groups[9] == null);
    $display("ASSOCIATIVE DEFAULT %0d", associative_groups[37] == null);

    first.sample(1);
    second.sample(1);
    result = cg::get_coverage(covered, total);
    $display("COVERAGE %.0f %0d %0d", result, covered, total);
    $finish;
  end
endmodule
