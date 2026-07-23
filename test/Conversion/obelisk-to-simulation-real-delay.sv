// RUN: obelisk -O0 -emit-sim %s | FileCheck %s

`timescale 1ns/100ps
module simulation_real_delay;
  initial begin
    #0.14;
    #0.15;
    #1ns;
    #(-0.2);
  end
endmodule

// Real delays are rounded to the lexical 100 ps precision before they become
// design ticks: 0.14 ns -> 1 tick, 0.15 ns -> 2 ticks, 1 ns -> 10 ticks,
// and a negative delay -> 0 ticks.
// CHECK: obelisk_sim.design @design attributes {{.*}}time_precision_fs = 100000
// CHECK-DAG: obelisk_sim.time.constant 1{{$}}
// CHECK-DAG: obelisk_sim.time.constant 2{{$}}
// CHECK-DAG: obelisk_sim.time.constant 10{{$}}
// CHECK-DAG: obelisk_sim.time.constant 0{{$}}
// CHECK-COUNT-4: obelisk_sim.suspend.delay
