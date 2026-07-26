// RUN: obelisk -emit-sim %s | FileCheck %s

module unsupported_foreach;
  int values[];

  initial
    foreach (values[index])
      values[index] = index;
endmodule

// CHECK: obelisk_sim.container.size
// CHECK: cf.cond_br
// CHECK: obelisk_sim.container.write
