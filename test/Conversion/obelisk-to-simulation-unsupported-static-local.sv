// RUN: obelisk -emit-sim %s | FileCheck %s

module supported_static_local;
  initial begin
    static logic value = 1'b1;
  end
endmodule

// CHECK: obelisk_sim.static.once
// CHECK: cf.cond_br
// CHECK: obelisk_sim.ref.store
