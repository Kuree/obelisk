// RUN: talos -emit-slang %s | talos-opt --convert-slang-to-talos -o %t.talos.mlir
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=0' | FileCheck %s

// A polymorphic descendant is inspected only after all ancestor handles are
// proven non-null. Its runtime class selects a frozen whole-graph solver plan.
// A constraint declared by the descendant's one-level owner participates in
// every concrete plan alongside the recursively flattened leaves. The null
// descendant selects the null plan without replacing either object handle.
// Polymorphic lifecycle hooks remain in their selected whole-graph plan and
// are guarded by the same ancestor null and rand_mode checks.
// CHECK-DAG: simulation.func private @[[LEAF_A_PRE:unit_[0-9]+]]{{.*}}simulation.hierarchical_name = "leaf_a::pre_randomize"
// CHECK-DAG: simulation.func private @[[LEAF_A_POST:unit_[0-9]+]]{{.*}}simulation.hierarchical_name = "leaf_a::post_randomize"
// CHECK-LABEL: simulation.func private @{{unit_[0-9]+}}{{.*}}simulation.hierarchical_name = "top"
// CHECK-DAG: %[[MIDDLE_REF:.*]] = simulation.class.field_ref {{.*}}[@__talos_class_s66_root_field_0]
// CHECK-DAG: %[[DISPATCH_MIDDLE:.*]] = simulation.managed.load %[[MIDDLE_REF]]
// CHECK-DAG: simulation.managed.is_null %[[DISPATCH_MIDDLE]]
// CHECK-DAG: %[[LEAF_REF:.*]] = simulation.class.field_ref %[[DISPATCH_MIDDLE]][@__talos_class_s49_middle_field_1]
// CHECK-DAG: %[[DISPATCH_LEAF:.*]] = simulation.managed.load %[[LEAF_REF]]
// CHECK-DAG: simulation.managed.is_null %[[DISPATCH_LEAF]]
// CHECK-DAG: simulation.class.is_instance %[[DISPATCH_LEAF]] is @__talos_class_s17_leaf_a
// CHECK-DAG: simulation.class.is_instance %[[DISPATCH_LEAF]] is @__talos_class_s34_leaf_b
// CHECK-DAG: simulation.random.solve_wide
// CHECK-DAG: arith.cmpi sge, {{.*}}, {{.*}} : i32
// CHECK-DAG: arith.cmpi sle, {{.*}}, {{.*}} : i32
// CHECK-DAG: simulation.class.field_ref {{.*}}[@__talos_class_s49_middle_field_0]
// CHECK-DAG: simulation.class.field_ref {{.*}}[@__talos_class_s3_leaf_base_field_0] {{.*}}class_handle<@__talos_class_s17_leaf_a>
// CHECK-DAG: simulation.class.field_ref {{.*}}[@__talos_class_s17_leaf_a_field_0]
// CHECK-DAG: simulation.class.field_ref {{.*}}[@__talos_class_s3_leaf_base_field_0] {{.*}}class_handle<@__talos_class_s34_leaf_b>
// CHECK-DAG: simulation.class.field_ref {{.*}}[@__talos_class_s34_leaf_b_field_0]
// CHECK-DAG: simulation.class.direct_call @[[LEAF_A_PRE]]
// CHECK-DAG: simulation.class.direct_call @[[LEAF_A_POST]]
// CHECK-NOT: simulation.managed.store {{.*}} to %[[MIDDLE_REF]] :
// CHECK-NOT: simulation.managed.store {{.*}} to %[[LEAF_REF]] :

virtual class leaf_base;
  rand int common;
endclass
class leaf_a extends leaf_base;
  rand byte a;
  function void pre_randomize();
  endfunction
  function void post_randomize();
  endfunction
endclass
class leaf_b extends leaf_base;
  rand shortint b;
endclass
class middle;
  rand int limit;
  rand leaf_base l;
  constraint bounded {
    (limit inside {[1:9]});
  }
endclass
class root;
  rand middle m;
endclass
`timescale 1ns / 1ns
module top;
  root r;
  initial r.randomize();
endmodule
