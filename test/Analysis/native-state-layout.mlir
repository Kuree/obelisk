// RUN: obelisk-opt %s --test-obelisk-native-state-layout-analysis 2>&1 | FileCheck %s

!candidate = !obelisk_sim.unpacked_union<fields = [
  #obelisk_sim.field<name = "object", type = !obelisk_sim.class_handle<@Node>, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "bits", type = i64, ordinal = 1, packedOffset = 0>
], isTagged = false>

module {
  obelisk_sim.design @native_state_layout {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.storage.decl 1 in 0 : i8 design
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<4> design
    obelisk_sim.driver.decl 0 in 0 drives 0 :
        !obelisk_sim.logic<4> design {
      driven_low = 1 : i64,
      driven_width = 2 : i64
    }
    obelisk_sim.class.decl @Node id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.storage.decl 2 in 0 : !candidate design
  }
}

// CHECK: native-state bits=128
// CHECK-NEXT: bound 1 offset=0 width=1 four-state=false
// CHECK-NEXT: bound 2 offset=8 width=8 four-state=false
// CHECK-NEXT: bound 3 offset=16 width=4 four-state=true
// CHECK-NEXT: bound 4 offset=20 width=4 four-state=true
// CHECK-NEXT: bound 5 offset=64 width=64 four-state=false roots=0 candidate-roots=0:1
// CHECK-NEXT: net 0 handle=3 offset=16 width=4 four-state=true
// CHECK-NEXT: driver 0 net=0 handle=4 offset=20 width=4 driven=[1,3)
