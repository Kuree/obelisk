// XFAIL: *
// IEEE 1800-2023 19.4.1 allows a derived cross to replace a base cross
// with the same name. Slang v11.0 diagnoses this as a redefinition.

// RUN: %talos --std=1800-2023 -O0 --coverage=functional -emit-sim %s \
// RUN:   | talos-opt --test-talos-print-coverage-schema -o %t.sim.mlir 2> %t.schema
// RUN: FileCheck %s --check-prefix=SCHEMA < %t.schema
// RUN: FileCheck %s --check-prefix=IR < %t.sim.mlir
// RUN: %talos --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s --check-prefix=QUERY
// RUN: talos-cov report --format=json %t.obcov | FileCheck %s --check-prefix=REPORT

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

// IEEE 19.4.1: a derived covergroup replaces the base instance, keeps the
// inherited components, and is a separate type from its base for
// get_coverage(). A same-named coverpoint hides the base point (still sampled,
// no weight) while a base cross keeps targeting it; a same-named cross
// replaces the base cross.
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

// The hidden base point is kept in each derived type as an inherited item that
// is sampled but not aggregating.
// SCHEMA-DAG: functional_item id={{[1-9][0-9]*}} type={{[1-9][0-9]*}} name=cp$inherited${{[1-9][0-9]*}} kind=1 flags=1 ordinal=0 hierarchy=coverage_derived::group.cp$inherited${{[1-9][0-9]*}}
// SCHEMA-DAG: functional_item id={{[1-9][0-9]*}} type={{[1-9][0-9]*}} name=cp$inherited${{[1-9][0-9]*}} kind=1 flags=1 ordinal=0 hierarchy=coverage_leaf::group.cp$inherited${{[1-9][0-9]*}}

// REPORT: "functional":{"available":true,"covered":12,"total":17
// REPORT-DAG: "name":"cp$inherited${{[1-9][0-9]*}}","comment":"","hierarchy":"coverage_derived::group.cp$inherited${{[1-9][0-9]*}}"{{[^}]*}}"weight":0,{{[^}]*}}"aggregating":false
// REPORT-DAG: "name":"cp$inherited${{[1-9][0-9]*}}","comment":"","hierarchy":"coverage_leaf::group.cp$inherited${{[1-9][0-9]*}}"{{[^}]*}}"weight":0,{{[^}]*}}"aggregating":false

// Each derived covergroup is a new declaration based on the previous one; a
// base handle reaches the derived instance through a covergroup cast guarded
// by the dynamic class of the object.
// IR: simulation.covergroup.decl @[[BASE:__talos_covergroup_[A-Za-z0-9_]+]] schema {{[0-9]+}}
// IR-NEXT: simulation.covergroup.decl @[[DERIVED:__talos_covergroup_[A-Za-z0-9_]+]] schema {{[0-9]+}} base @[[BASE]]
// IR-NEXT: simulation.covergroup.decl @[[LEAF:__talos_covergroup_[A-Za-z0-9_]+]] schema {{[0-9]+}} base @[[DERIVED]]
// IR: simulation.class.is_instance {{.*}} is @__talos_class_{{[A-Za-z0-9_]*}}coverage_derived
// IR: simulation.covergroup.cast {{.*}} : !simulation.covergroup_handle<@[[DERIVED]]> to !simulation.covergroup_handle<@[[BASE]]>
// IR: simulation.covergroup.cast {{.*}} : !simulation.covergroup_handle<@[[LEAF]]> to !simulation.covergroup_handle<@[[DERIVED]]>
