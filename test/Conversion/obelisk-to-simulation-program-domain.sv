// RUN: obelisk -O0 -emit-sim %s | FileCheck %s

program simulation_program_domain;
  initial
    #0;
endprogram

// CHECK: obelisk_sim.func private @unit_{{.*}} attributes {
// CHECK-SAME: domain = 1 : i32
// CHECK-SAME: home_region = 10 : i32
