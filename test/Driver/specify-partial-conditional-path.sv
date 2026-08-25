// RUN: obelisk -fno-lto -O0 %s -o %t.native
// RUN: %t.native | FileCheck %s
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s
// RUN: obelisk -O0 -emit-sim %s -o %t.sim.mlir
// RUN: FileCheck %s --check-prefix=GROUP < %t.sim.mlir
// RUN: FileCheck %s --check-prefix=KIND-IF < %t.sim.mlir
// RUN: FileCheck %s --check-prefix=KIND-NONE < %t.sim.mlir
// RUN: FileCheck %s --check-prefix=POL-POS < %t.sim.mlir
// RUN: FileCheck %s --check-prefix=POL-NEG < %t.sim.mlir
// RUN: FileCheck %s --check-prefix=DRIVE < %t.sim.mlir

`timescale 1ns / 1ns

module partial_conditional_paths(
    input wire [3:0] source,
    input wire enable,
    output wire [3:0] destination);
  assign destination = source;
  specify
    // Group 0: parallel low selection with both polarity spellings.
    if (enable)
      (source[1:0] +=> destination[1:0]) = 2;
    ifnone
      (source[1:0] -=> destination[1:0]) = 5;

    // Group 1: a different selection must have an independent ifnone set.
    if (enable)
      (source[3:2] -=> destination[3:2]) = 3;
    ifnone
      (source[3:2] +=> destination[3:2]) = 6;

    // Group 2: the same terminals as group 0 but a full connection. The
    // connection kind is part of ifnone identity even when ranges match.
    if (enable)
      (source[1:0] +*> destination[1:0]) = 2;
    ifnone
      (source[1:0] -*> destination[1:0]) = 5;
  endspecify
endmodule

module specify_partial_conditional_path;
  logic [3:0] source;
  logic enable;
  wire [3:0] destination;

  partial_conditional_paths dut(source, enable, destination);

  initial begin
    source = 4'b0000;
    enable = 1'b0;
    #7;
    if (destination !== 4'b0000)
      $fatal(1, "initial conditional partial paths did not settle");

    // The false `if` selects only low-range ifnone rules. A later change in
    // the high source window must neither activate nor cancel the low event.
    source[0] = 1'b1;
    #1;
    source[3] = 1'b1;
    #4;
    if (destination !== 4'b0001)
      $fatal(1, "outside-window change disturbed low ifnone deadline");
    #2;
    if (destination !== 4'b1001)
      $fatal(1, "selection-local high ifnone path did not mature");

    // True conditions exercise positive and negative polarity metadata on
    // masked parallel paths without inverting the propagated data value.
    enable = 1'b1;
    source[1] = 1'b1;
    #2;
    if (destination !== 4'b1011)
      $fatal(1, "positive-polarity partial condition did not apply");
    source[2] = 1'b1;
    #3;
    if (destination !== 4'b1111)
      $fatal(1, "negative-polarity partial condition did not apply");

    $display("PASSED");
    $finish;
  end
endmodule

// CHECK: PASSED

// The three exact ifnone identities are (parallel, low), (parallel, high),
// and (full, low). Each ordinary `if` and its ifnone fallback share one group;
// neither the terminal selection nor connection kind may be omitted.
// GROUP-COUNT-2: condition_group = 0 : i32
// GROUP-COUNT-2: condition_group = 1 : i32
// GROUP-COUNT-2: condition_group = 2 : i32
// KIND-IF-COUNT-3: condition_kind = 1 : i32
// KIND-NONE-COUNT-3: condition_kind = 2 : i32
// POL-POS-COUNT-3: polarity = 1 : i32
// POL-NEG-COUNT-3: polarity = 2 : i32
// Four distinct static delays are shared across the six conditional rules.
// DRIVE-COUNT-4: obelisk_sim.driver.drive_inertial_path
