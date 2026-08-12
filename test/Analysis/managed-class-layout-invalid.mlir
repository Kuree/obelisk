// RUN: not obelisk-opt %s --test-obelisk-managed-class-layout-analysis 2>&1 | FileCheck %s

!untagged = !obelisk_sim.unpacked_union<fields = [
  #obelisk_sim.field<name = "object", type = !obelisk_sim.class_handle<@Node>, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "bits", type = i64, ordinal = 1, packedOffset = 0>
], isTagged = false>

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8"
} {
  obelisk_sim.design @classes {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @Node id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @Holder id 2 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.field @Holder_value of @Holder at 0 : !untagged {
      is_static = false, is_weak = false
    }
  }
}

// CHECK: class property has no fixed managed layout
