// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 19.4: an embedded covergroup is a class member. The class declaration
// carries the covergroup handle as a field, and the constructor creates the
// covergroup and stores it through the receiver. The field is marked so a
// shallow copy sets it to null (IEEE 1800-2023 8.12).
// CHECK: simulation.covergroup.decl
// CHECK: simulation.class.decl @[[BASE:[^ ]+]] id 1
// CHECK: simulation.class.field {{.*}}debug_name = "value", is_static = false, is_weak = false}
// CHECK: simulation.class.field {{.*}}debug_name = "cg", is_static = false, is_weak = false, simulation.embedded_covergroup}
// CHECK: simulation.class.decl @[[DERIVED:[^ ]+]] id 2 extends @[[BASE]]
// CHECK: simulation.class.field {{.*}}of @[[DERIVED]]{{.*}}debug_name = "bias"
// CHECK: simulation.func private @{{unit_[0-9]+}}({{.*}}%arg1: !simulation.class_handle
// CHECK: simulation.covergroup.create
// CHECK: simulation.class.field_ref %arg1[{{.*}}field_1]
// CHECK: simulation.managed.store

// IEEE 19.4 and 19.8: the coverpoint expression `value + extra` is evaluated
// in the covergroup's class scope, so `value` is read from the receiver.
// CHECK: simulation.func private @[[SELF:unit_[0-9]+]]({{.*}}%arg1: !simulation.class_handle
// CHECK: %[[SELF_CG_REF:[0-9]+]] = simulation.class.field_ref %arg1[{{.*}}field_1]
// CHECK: %[[SELF_VALUE_REF:[0-9]+]] = simulation.class.field_ref %arg1[{{.*}}field_0]
// CHECK: %[[SELF_ACTUAL:[0-9]+]] = simulation.managed.load %[[SELF_VALUE_REF]]
// CHECK: simulation.covergroup.sample_enabled
// CHECK: %[[SELF_RECEIVER_VALUE:[0-9]+]] = simulation.managed.load %[[SELF_VALUE_REF]]
// CHECK: %[[SELF_SUM:[0-9]+]] = arith.addi %[[SELF_RECEIVER_VALUE]], %[[SELF_ACTUAL]]
// CHECK: %[[SELF_EXPR:[0-9]+]] = simulation.logic.from_bits %[[SELF_SUM]]
// CHECK: simulation.covergroup.sample {{.*}}values[%[[SELF_EXPR]]]

// IEEE 19.8.1: in other.cg.sample(bias) the actual argument is evaluated in
// the caller's context (%arg1, the derived receiver), while the covergroup's
// own expression continues to read the selected object (%arg2).
// CHECK: simulation.func private @[[OTHER:unit_[0-9]+]]({{.*}}%arg1: !simulation.class_handle<@[[DERIVED]]>{{.*}}%arg2: !simulation.class_handle
// CHECK: %[[OTHER_CG_REF:[0-9]+]] = simulation.class.field_ref %arg2[{{.*}}field_1]
// CHECK: simulation.managed.load %[[OTHER_CG_REF]]
// CHECK: %[[BIAS_REF:[0-9]+]] = simulation.class.field_ref %arg1[{{.*}}field_0]
// CHECK: %[[BIAS:[0-9]+]] = simulation.managed.load %[[BIAS_REF]]
// CHECK: simulation.covergroup.sample_enabled
// CHECK: %[[OTHER_VALUE_REF:[0-9]+]] = simulation.class.field_ref %arg2[{{.*}}field_0]
// CHECK: %[[OTHER_VALUE:[0-9]+]] = simulation.managed.load %[[OTHER_VALUE_REF]]
// CHECK: %[[OTHER_SUM:[0-9]+]] = arith.addi %[[OTHER_VALUE]], %[[BIAS]]
// CHECK: %[[OTHER_EXPR:[0-9]+]] = simulation.logic.from_bits %[[OTHER_SUM]]
// CHECK: simulation.covergroup.sample {{.*}}values[%[[OTHER_EXPR]]]

// The calls from outside the class dispatch directly with the upcast receiver.
// CHECK-LABEL: simulation.func private @unit_{{[0-9]+}}({{.*}}%arg1: !simulation.ref
// CHECK: %[[CALLER:[0-9]+]] = simulation.ref.load
// CHECK: %[[CAST:[0-9]+]] = simulation.class.cast %[[CALLER]]
// CHECK: simulation.class.direct_call @[[SELF]] %[[CAST]]()
// CHECK: %[[CALLER2:[0-9]+]] = simulation.ref.load
// CHECK: %[[RECEIVER:[0-9]+]] = simulation.class.cast %[[CALLER2]]
// CHECK: simulation.class.direct_call @[[OTHER]] %[[CALLER2]](%[[RECEIVER]])
// CHECK-NOT: obelisk.sv.

module top;
  class C;
    int value;
    covergroup cg with function sample(int extra);
      cp: coverpoint value + extra { bins one = {1}; }
    endgroup
    function new();
      cg = new;
    endfunction
    function void sample_it();
      cg.sample(value);
    endfunction
  endclass
  class D extends C;
    int bias;
    function void sample_other(C other);
      other.cg.sample(bias);
    endfunction
  endclass
  D d;
  initial begin
    d = new;
    d.sample_it();
    d.sample_other(d);
  end
endmodule
