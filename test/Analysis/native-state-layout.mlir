// RUN: obelisk-opt %s --test-obelisk-native-state-layout-analysis 2>&1 | FileCheck %s

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
  }
}

// CHECK: native-state bits=24
// CHECK-NEXT: bound 1 offset=0 width=1 four-state=false
// CHECK-NEXT: bound 2 offset=8 width=8 four-state=false
// CHECK-NEXT: bound 3 offset=16 width=4 four-state=true
// CHECK-NEXT: bound 4 offset=20 width=4 four-state=true
// CHECK-NEXT: net 0 handle=3 offset=16 width=4 four-state=true
// CHECK-NEXT: driver 0 net=0 handle=4 offset=20 width=4 driven=[1,3)
