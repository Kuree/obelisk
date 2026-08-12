// RUN: obelisk-opt %s --test-obelisk-managed-class-layout-analysis 2>&1 | FileCheck %s

!tagged = !obelisk_sim.unpacked_union<fields = [
  #obelisk_sim.field<name = "object", type = !obelisk_sim.class_handle<@Referent>, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "bits", type = i32, ordinal = 1, packedOffset = 0>,
  #obelisk_sim.field<name = "text", type = !obelisk_sim.string, ordinal = 2, packedOffset = 0>
], isTagged = true>

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8"
} {
  obelisk_sim.design @classes {
    obelisk_sim.scope.decl 0

    obelisk_sim.class.decl @Referent id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @Base id 2 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @Weak id 3 extends @Base {
      is_abstract = false, is_final = false, is_interface = false,
      weak_referent = @Referent
    }
    obelisk_sim.class.decl @Derived id 4 extends @Weak {
      is_abstract = false, is_final = true, is_interface = false
    }
    obelisk_sim.class.decl @TaggedHolder id 5 {
      is_abstract = false, is_final = false, is_interface = false
    }

    obelisk_sim.class.field @Base_value of @Base at 0 :
        !obelisk_sim.logic<8> {
      is_static = false, is_weak = false
    }
    obelisk_sim.class.field @Base_static of @Base at 1 : i64 {
      is_static = true, is_weak = false
    }
    obelisk_sim.class.field @Weak_count of @Weak at 0 : i32 {
      is_static = false, is_weak = false
    }
    obelisk_sim.class.field @Derived_owner of @Derived at 0 :
        !obelisk_sim.class_handle<@Referent> {
      is_static = false, is_weak = false
    }
    obelisk_sim.class.field @TaggedHolder_value of @TaggedHolder at 0 :
        !tagged {
      is_static = false, is_weak = false
    }
  }
}

// CHECK: managed-class Referent id=1 size=8 alignment=8
// CHECK-NEXT: managed-class Base id=2 size=16 alignment=8
// CHECK-NEXT:   field Base_value offset=8 size=1 alignment=1 planes=2 roots=[]
// CHECK-NEXT: managed-class Weak id=3 size=32 alignment=8 weak-referent-offset=16
// CHECK-NEXT:   field Weak_count offset=24 size=4 alignment=4 planes=1 roots=[]
// CHECK-NEXT: managed-class Derived id=4 size=40 alignment=8
// CHECK-NEXT:   field Derived_owner offset=32 size=8 alignment=8 planes=1 roots=[0]
// CHECK-NEXT: managed-class TaggedHolder id=5 size=40 alignment=8
// CHECK-NEXT:   field TaggedHolder_value offset=8 size=25 alignment=8 planes=1 roots=[0, 16]
