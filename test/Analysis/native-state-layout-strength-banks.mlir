// RUN: obelisk-opt %s --test-obelisk-native-state-layout-analysis 2>&1 | FileCheck %s

// A conditional primitive is one logical uwire driver even though its L/H
// interval is retained in two complementary physical driver banks.
module {
  obelisk_sim.design @strength_banks {
    obelisk_sim.scope.decl 0
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design {
      resolution_kind = 2 : i32
    }
    obelisk_sim.driver.decl 0 in 0 drives 0 :
        !obelisk_sim.logic<1> design {
      driven_low = 0 : i64,
      driven_width = 1 : i64,
      strength0 = 6 : i32,
      strength1 = 0 : i32,
      obelisk_sim.strength_group = 42 : i64,
      obelisk_sim.strength_bank = 0 : i32
    }
    obelisk_sim.driver.decl 1 in 0 drives 0 :
        !obelisk_sim.logic<1> design {
      driven_low = 0 : i64,
      driven_width = 1 : i64,
      strength0 = 0 : i32,
      strength1 = 6 : i32,
      obelisk_sim.strength_group = 42 : i64,
      obelisk_sim.strength_bank = 1 : i32
    }
  }
}

// CHECK: native-state bits=8
// CHECK: driver 0 net=0
// CHECK: driver 1 net=0
