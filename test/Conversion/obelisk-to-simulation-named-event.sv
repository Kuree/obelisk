// RUN: obelisk -O0 -emit-sim %s | FileCheck %s

module simulation_named_event;
  event ready;
  event other;

  initial begin
    $display("equal=%0d distinct=%0d", ready == ready, ready != other);
    @ready;
    $display("blocking=%0d", ready.triggered);
    @ready;
    $display("nonblocking=%0d", ready.triggered);
    #1;
    $display("cleared=%0d", ready.triggered);
  end

  initial begin
    #1 -> ready;
    ->> #2 ready;
  end
endmodule

// A design-lifetime event is a direct scheduler object shared by every
// process capture; it is not represented as packed mutable storage.
// CHECK: %[[EVENT:.*]] = obelisk_sim.context.event %{{.*}}[0] : !obelisk_sim.event
// CHECK: obelisk_sim.spawn {{.*}}%[[EVENT]]
// CHECK: obelisk_sim.spawn {{.*}}%[[EVENT]]
// CHECK: obelisk_sim.event.equal
// CHECK: obelisk_sim.event.equal
// CHECK: obelisk_sim.suspend.event %{{.*}} to
// CHECK: obelisk_sim.event.triggered %{{.*}}
// CHECK: obelisk_sim.suspend.event %{{.*}} to
// CHECK: obelisk_sim.event.triggered %{{.*}}
// CHECK: obelisk_sim.event.triggered %{{.*}}
// CHECK: obelisk_sim.event.trigger %{{.*}} nonblocking = false
// CHECK: obelisk_sim.event.trigger %{{.*}} after %{{.*}} nonblocking = true
