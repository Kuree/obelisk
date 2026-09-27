// RUN: obelisk-opt %s --test-obelisk-native-state-layout-analysis 2>&1 | FileCheck %s

// A conditional primitive is one logical uwire driver even though its L/H
// interval is retained in two complementary physical driver banks.
module {
  simulation.design @strength_banks {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<1> design {
      resolution_kind = 2 : i32
    }
    simulation.driver.decl 0 in 0 drives 0 :
        !simulation.logic<1> design {
      driven_low = 0 : i64,
      driven_width = 1 : i64,
      strength0 = 6 : i32,
      strength1 = 0 : i32,
      simulation.strength_group = 42 : i64,
      simulation.strength_bank = 0 : i32
    }
    simulation.driver.decl 1 in 0 drives 0 :
        !simulation.logic<1> design {
      driven_low = 0 : i64,
      driven_width = 1 : i64,
      strength0 = 0 : i32,
      strength1 = 6 : i32,
      simulation.strength_group = 42 : i64,
      simulation.strength_bank = 1 : i32
    }
  }
}

// CHECK: native-state bits=17
// CHECK: driver 0 net=0 handle=2 offset=8 width=1
// CHECK: driver 1 net=0 handle=3 offset=16 width=1
