// RUN: obelisk-opt %s --test-obelisk-native-state-layout-analysis 2>&1 | FileCheck %s

!candidate = !simulation.unpacked_union<fields = [
  #simulation.field<name = "object", type = !simulation.class_handle<@Node>, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "bits", type = i64, ordinal = 1, packedOffset = 0>
], isTagged = false>

module {
  simulation.design @native_state_layout {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.net.decl 0 in 0 : !simulation.logic<4> design
    simulation.driver.decl 0 in 0 drives 0 :
        !simulation.logic<4> design {
      driven_low = 1 : i64,
      driven_width = 2 : i64
    }
    simulation.class.decl @Node id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.storage.decl 2 in 0 : !candidate design
  }
}

// CHECK: native-state bits=128
// CHECK-NEXT: bound 1 offset=0 width=1 four-state=false
// CHECK-NEXT: bound 2 offset=8 width=8 four-state=false
// CHECK-NEXT: bound 3 offset=16 width=4 four-state=true
// CHECK-NEXT: bound 4 offset=24 width=4 four-state=true
// CHECK-NEXT: bound 5 offset=64 width=64 four-state=false roots=0 candidate-roots=0:1
// CHECK-NEXT: net 0 handle=3 offset=16 width=4 four-state=true
// CHECK-NEXT: driver 0 net=0 handle=4 offset=24 width=4 driven=[1,3)
