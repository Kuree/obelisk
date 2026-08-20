// RUN: obelisk-opt %s --test-obelisk-native-state-layout-analysis 2>&1 | FileCheck %s

// IEEE 1800-2017 6.17: every event variable holds its own synchronization
// object, and that object's identity is a whole simulation handle. An array of
// events therefore reserves one handle per element; a narrower stride would
// overlap neighbouring elements, and the second array below would overlap the
// first.

module {
  obelisk_sim.design @event_array_layout {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 :
        !obelisk_sim.unpacked_array<0 : 1 x !obelisk_sim.event> design
    obelisk_sim.storage.decl 1 in 0 :
        !obelisk_sim.unpacked_array<0 : 1 x !obelisk_sim.event> design
  }
}

// CHECK: native-state bits=256
// CHECK-NEXT: bound 1 offset=0 width=128
// CHECK-NEXT: bound 2 offset=128 width=128
