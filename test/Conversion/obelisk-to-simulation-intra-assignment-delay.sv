// RUN: obelisk -O0 -emit-sim %s | FileCheck %s

`timescale 1ns/1ps
module simulation_intra_assignment_delay;
  logic [7:0] values;
  logic rhs;
  int index;
  int amount;

  initial begin
    values[index] = #(amount) rhs;
    values[index] <= #(amount) rhs;
  end
endmodule

// Blocking intra-assignment timing evaluates the RHS before suspension and
// resolves the dynamic destination in the continuation.
// CHECK: %[[BLOCKING_RHS:.*]] = obelisk_sim.ref.load
// CHECK: %[[BLOCKING_DELAY:.*]] = obelisk_sim.time.scale
// CHECK: obelisk_sim.suspend.delay %[[BLOCKING_DELAY]] to ^[[COMMIT:bb[0-9]+]](%[[BLOCKING_RHS]] : !obelisk_sim.logic<1>)
// CHECK: ^[[COMMIT]](%[[COMMIT_RHS:.*]]: !obelisk_sim.logic<1>)
// CHECK: obelisk_sim.ref.load
// CHECK: obelisk_sim.ref.array_element
// CHECK: obelisk_sim.ref.store %[[COMMIT_RHS]]

// A timed nonblocking assignment captures its destination and value at
// encounter time and queues the update without suspending the caller.
// CHECK: %[[NBA_DELAY:.*]] = obelisk_sim.time.scale
// CHECK: %[[NBA_DEST:.*]] = obelisk_sim.ref.array_element
// CHECK: obelisk_sim.nba.enqueue {{%.*}} to %[[NBA_DEST]] after %[[NBA_DELAY]]
// CHECK-NOT: obelisk_sim.suspend.delay
