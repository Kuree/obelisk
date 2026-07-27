// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s
// RUN: obelisk -emit-sim %s | FileCheck %s --check-prefix=SIM

module native_time_zero_event_order;
  event started;
  int wakeups = 0;

  // Keep the triggering initial block first in source order. The deterministic
  // startup policy must still let the always process establish its event wait.
  initial begin
    ->started;
    #1;
    $display("wakeups=%0d", wakeups);
  end

  always @(started)
    wakeups++;
endmodule

// CHECK: wakeups=1

// SIM-LABEL: obelisk_sim.func @__obelisk_root
// SIM: obelisk_sim.spawn @[[ALWAYS:unit_[0-9]+]]
// SIM: obelisk_sim.spawn @[[INITIAL:unit_[0-9]+]]
// SIM: obelisk_sim.func private @[[INITIAL]]
// SIM-SAME: entry_kind = 1 : i32
// SIM: obelisk_sim.func private @[[ALWAYS]]
// SIM-SAME: entry_kind = 3 : i32
