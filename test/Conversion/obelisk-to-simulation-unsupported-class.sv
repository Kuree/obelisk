// RUN: obelisk -emit-sim %s | FileCheck %s

class supported_object;
  int field;
endclass

module supported_class_use;
  initial begin
    automatic supported_object object = new;
    object.field = 42;
  end
endmodule

// CHECK: obelisk_sim.class.decl
// CHECK-SAME: debug_name = "supported_object"
// CHECK: obelisk_sim.class.field {{.*}}debug_name = "__obelisk_rng_state"
// CHECK: obelisk_sim.class.field {{.*}}debug_name = "__obelisk_rng_increment"
// CHECK: obelisk_sim.class.alloc
// CHECK-NEXT: {{.*}} = obelisk_sim.random.next
// CHECK-NEXT: {{.*}} = obelisk_sim.random.next
// CHECK: obelisk_sim.class.field_ref
