// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -emit-sim %s \
// RUN:   | FileCheck %s --check-prefix=IR
// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -emit-sim %s \
// RUN:   | %python %S/../Conversion/Inputs/dump-coverage-schema.py \
// RUN:   | FileCheck %s --check-prefix=SCHEMA
// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov > %t.native.out
// RUN: obelisk-cov report --format=json %t.native.obcov \
// RUN:   | FileCheck %s --check-prefix=REPORT
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s --check-prefix=QUERY < %t.native.out

class coverage_base;
  covergroup group(input bit chosen) with function sample(bit sampled);
    option.get_inst_coverage = 1;
    option.weight = 7;
    cp: coverpoint sampled { bins zero = {0}; }
    q: coverpoint sampled { bins chosen_value = {chosen}; }
    base_cross: cross cp, q {
      bins chosen_zero = binsof(cp.zero) && binsof(q.chosen_value);
    }
    replaced_cross: cross cp, q;
  endgroup

  function new(bit chosen);
    group = new(chosen);
  endfunction
endclass

class coverage_derived extends coverage_base;
  covergroup extends group;
    option.weight = 1;
    cp: coverpoint sampled { bins one = {1}; }
    added: coverpoint sampled { bins chosen_value = {chosen}; }
    derived_cross: cross cp, added;
    replaced_cross: cross cp, added;
  endgroup : group

  function new(bit chosen);
    super.new(chosen);
  endfunction
endclass

class coverage_leaf extends coverage_derived;
  covergroup extends group;
    leaf: coverpoint sampled { bins one = {1}; }
  endgroup : group

  function new(bit chosen);
    super.new(chosen);
  endfunction
endclass

module top;
  coverage_derived object;
  coverage_leaf leaf_object;
  coverage_base base_view;
  coverage_base base_object;
  int covered;
  int total;
  real percentage;

  initial begin
    object = new(1);
    base_view = object;
    base_view.group.sample(1);

    percentage = object.group.cp.get_inst_coverage(covered, total);
    $display("derived point %.6f %0d %0d", percentage, covered, total);
    percentage = object.group.q.get_inst_coverage(covered, total);
    $display("inherited point %.6f %0d %0d", percentage, covered, total);
    percentage = object.group.base_cross.get_inst_coverage(covered, total);
    $display("base cross %.6f %0d %0d", percentage, covered, total);
    percentage = object.group.derived_cross.get_inst_coverage(covered, total);
    $display("derived cross %.6f %0d %0d", percentage, covered, total);
    percentage = object.group.replaced_cross.get_inst_coverage(covered, total);
    $display("overridden cross %.6f %0d %0d", percentage, covered, total);
    percentage = base_view.group.get_inst_coverage(covered, total);
    $display("replacement group %.6f %0d %0d", percentage, covered, total);

    leaf_object = new(1);
    base_view = leaf_object;
    base_view.group.q.stop();
    base_view.group.sample(1);
    percentage = base_view.group.q.get_inst_coverage(covered, total);
    $display("base-view stopped item %.6f %0d %0d", percentage, covered,
             total);
    base_view.group.q.start();
    base_view.group.sample(1);
    percentage = base_view.group.q.get_inst_coverage(covered, total);
    $display("base-view inherited item %.6f %0d %0d", percentage, covered,
             total);
    percentage = base_view.group.cp.get_inst_coverage(covered, total);
    $display("base-view overridden item %.6f %0d %0d", percentage, covered,
             total);
    percentage = leaf_object.group.get_inst_coverage(covered, total);
    $display("transitive replacement %.6f %0d %0d", percentage, covered,
             total);

    base_object = new(1);
    base_object.group.sample(0);
    percentage = base_object.group.get_inst_coverage(covered, total);
    $display("separate base type %.6f %0d %0d", percentage, covered, total);
    $finish;
  end
endmodule

// QUERY: derived point 100.000000 1 1
// QUERY-NEXT: inherited point 100.000000 1 1
// QUERY-NEXT: base cross 0.000000 0 1
// QUERY-NEXT: derived cross 100.000000 1 1
// QUERY-NEXT: overridden cross 100.000000 1 1
// QUERY-NEXT: replacement group 83.333333 5 6
// QUERY-NEXT: base-view stopped item 0.000000 0 1
// QUERY-NEXT: base-view inherited item 100.000000 1 1
// QUERY-NEXT: base-view overridden item 100.000000 1 1
// QUERY-NEXT: transitive replacement 85.714286 6 7
// QUERY-NEXT: separate base type 25.000000 1 4

// The base cross keeps the hidden base coverpoint target. The hidden point is
// sampled but has zero aggregation weight in the derived type.
// SCHEMA: functional_item id={{[1-9][0-9]*}} type={{[1-9][0-9]*}} name=cp$inherited${{[1-9][0-9]*}} kind=1 flags=1

// REPORT: "functional":{"available":true,"covered":12,"total":17
// REPORT: "name":"cp$inherited${{[1-9][0-9]*}}"{{.*}}"aggregating":false

// IR: obelisk_sim.covergroup.decl {{.*}} base
// IR: obelisk_sim.class.is_instance
// IR: obelisk_sim.covergroup.cast
