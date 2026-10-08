// RUN: obelisk-opt %s --test-obelisk-native-state-layout-analysis 2>&1 | FileCheck %s

// Native containers own their padding; stable handle bounds remain logical.
// Neither adjacent roots nor the end of a plane may occupy a container tail.
module {
  simulation.design @containers {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<21> design
    simulation.storage.decl 1 in 0 : i1 design
    simulation.storage.decl 2 in 0 : !simulation.logic<37> design
    simulation.storage.decl 3 in 0 : !simulation.logic<49> design
    simulation.storage.decl 4 in 0 : !simulation.logic<9> design
  }
}
// CHECK: native-state bits=208
// CHECK-NEXT: bound 1 offset=0 width=21 four-state=true
// CHECK-NEXT: bound 2 offset=32 width=1 four-state=false
// CHECK-NEXT: bound 3 offset=64 width=37 four-state=true
// CHECK-NEXT: bound 4 offset=128 width=49 four-state=true
// CHECK-NEXT: bound 5 offset=192 width=9 four-state=true
