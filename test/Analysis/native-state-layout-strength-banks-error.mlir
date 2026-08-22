// RUN: not obelisk-opt %s --test-obelisk-native-state-layout-analysis 2>&1 | FileCheck %s

module {
  obelisk_sim.design @interleaved_strength_banks {
    obelisk_sim.scope.decl 0
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 :
        !obelisk_sim.logic<1> design {
      strength0 = 6 : i32,
      strength1 = 0 : i32,
      obelisk_sim.strength_group = 9 : i64,
      obelisk_sim.strength_bank = 0 : i32
    }
    obelisk_sim.driver.decl 1 in 0 drives 0 :
        !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 2 in 0 drives 0 :
        !obelisk_sim.logic<1> design {
      strength0 = 0 : i32,
      strength1 = 6 : i32,
      obelisk_sim.strength_group = 9 : i64,
      obelisk_sim.strength_bank = 1 : i32
    }
  }
}

// CHECK: complementary driver strength banks must be adjacent low/high
