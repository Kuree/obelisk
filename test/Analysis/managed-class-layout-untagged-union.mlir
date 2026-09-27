// RUN: obelisk-opt %s --test-obelisk-managed-class-layout-analysis 2>&1 | FileCheck %s

!untagged = !simulation.unpacked_union<fields = [
  #simulation.field<name = "object", type = !simulation.class_handle<@Node>, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "bits", type = !simulation.logic<64>, ordinal = 1, packedOffset = 0>
], isTagged = false>

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8"
} {
  simulation.design @classes {
    simulation.scope.decl 0
    simulation.class.decl @Node id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @Holder id 2 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.field @Holder_value of @Holder at 0 : !untagged {
      is_static = false, is_weak = false
    }
  }
}

// IEEE 1800-2017 7.3: an untagged union retains overlapping storage. Its
// managed arm is represented as a validated candidate root, including when
// another arm contributes a four-state unknown plane.
// CHECK: managed-class Holder id=2 size=24 alignment=8
// CHECK-NEXT: field Holder_value offset=8 size=8 alignment=8 planes=2 roots=[0] candidate-roots=[0:1]
