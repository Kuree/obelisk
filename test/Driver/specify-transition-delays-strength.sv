// RUN: obelisk -O0 %s -o %t.native
// RUN: %t.native | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s
// RUN: obelisk -O0 -emit-sim %s -o - | FileCheck %s --check-prefix=SIM

`timescale 1ns / 1ns

module explicit_strength_path(input wire source, output wire destination);
  // An explicit strength is metadata on one ordinary driver. It is not the
  // complementary two-bank representation used for conditional primitives.
  assign (weak0, strong1) destination = source;
  specify
    (source => destination) = (2, 3, 20, 21, 22, 1);
  endspecify
endmodule

module specify_transition_delays_strength;
  logic source;
  wire destination;
  explicit_strength_path path(source, destination);

  initial begin
    source = 0;
    #4;
    if (destination !== 0)
      $fatal(1, "initial strength drive did not settle");
    source = 1;
    #1;
    if (destination !== 0)
      $fatal(1, "explicit strength 01 matured early");
    #1;
    if (destination !== 1)
      $fatal(1, "explicit strength did not use logical 01 delay");
    $display("PASSED");
    $finish;
  end
endmodule

// CHECK: PASSED
// SIM: obelisk_sim.driver.read
// SIM-COUNT-6: obelisk_sim.driver.drive_inertial_path
// SIM-NOT: obelisk_sim.driver.drive_inertial_strength_pair
