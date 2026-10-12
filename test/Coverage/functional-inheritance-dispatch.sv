// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s

// IEEE 19.4.1: the derived group replaces the base instance, including uses
// through a base-class reference. Direct siblings and transitive descendants
// retain their own point overrides. Declaration order need not be name order;
// dispatch must construct one selected group and evaluate its options once.
class Base;
  int constructed;
  function automatic int mark;
    constructed++;
    return 100;
  endfunction
  covergroup cg(input int chosen) with function sample(input int sampled);
    option.per_instance = 1;
    option.goal = mark();
    cp: coverpoint sampled { bins value = {chosen}; }
  endgroup
  function new;
    cg = new(1);
  endfunction
endclass
// Declare siblings in the opposite order from their names.
class Z extends Base;
  covergroup extends cg;
    cp: coverpoint sampled { bins value = {2}; }
  endgroup : cg
endclass
class A extends Base;
  covergroup extends cg;
    cp: coverpoint sampled { bins value = {3}; }
  endgroup : cg
endclass
class Leaf extends Z;
  covergroup extends cg;
    cp: coverpoint sampled { bins value = {4}; }
  endgroup : cg
endclass
module top;
  Base base_object, view;
  Z z;
  A a;
  Leaf leaf_object;
  initial begin
    base_object = new;
    z = new;
    a = new;
    leaf_object = new;
    view = base_object; view.cg.sample(1);
    view = z; view.cg.sample(2);
    view = a; view.cg.sample(3);
    view = leaf_object; view.cg.sample(4);
    $display("coverage=%0.1f,%0.1f,%0.1f,%0.1f", base_object.cg.get_inst_coverage(),
             z.cg.get_inst_coverage(), a.cg.get_inst_coverage(), leaf_object.cg.get_inst_coverage());
    $display("constructed=%0d,%0d,%0d,%0d", base_object.constructed,
             z.constructed, a.constructed, leaf_object.constructed);
    $finish;
  end
endmodule

// CHECK: coverage=100.0,100.0,100.0,100.0
// CHECK: constructed=1,1,1,1
