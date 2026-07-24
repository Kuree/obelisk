// RUN: obelisk -O0 -emit-sim %s | FileCheck %s

module simulation_loop_friends;
  logic [2:0] indices_only;
  integer once;
  integer count;

  initial begin
    once = 0;
    do
      once = once + 1;
    while (0);

    forever begin
      count = count + 1;
      break;
    end

    foreach (indices_only[index])
      count = count + index;
  end
endmodule

// The fixed foreach collection is not a process capture or aggregate read.
// CHECK-LABEL: obelisk_sim.func{{.*}}@unit_0(
// CHECK-NOT: !obelisk_sim.ref<!obelisk_sim.packed_array
// CHECK: arith.cmpi ult
// CHECK: arith.remui
