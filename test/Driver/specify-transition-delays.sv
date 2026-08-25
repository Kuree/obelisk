// RUN: obelisk -fno-lto -O0 %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -fno-lto -O3 %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s
// RUN: obelisk -emit-slang %s -o - | FileCheck %s --check-prefix=SLANG

`timescale 1ns / 1ns

module twelve_transition_path(input wire source, output wire destination);
  assign destination = source;
  specify
    (source => destination) = (1, 2, 3, 4, 5, 6,
                               7, 8, 9, 10, 11, 12);
  endspecify
endmodule

module multi_group_same_target(input wire functional_source,
                               input wire path_only_source,
                               output wire destination);
  assign destination = functional_source | (path_only_source & 1'b0);
  specify
    (functional_source, path_only_source *> destination) =
        (5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
  endspecify
endmodule

module overlapping_transition_paths(input wire selected_source,
                                    input wire selector,
                                    input wire base_source,
                                    output wire destination);
  assign destination = selector ? selected_source : base_source;
  specify
    (selected_source *> destination) =
        (20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20);
    (selector *> destination) = (1, 2, 3, 4, 5, 6,
                                 7, 8, 9, 10, 11, 12);
    (base_source *> destination) =
        (30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30);
  endspecify
endmodule

module specify_transition_delays;
  logic source;
  wire destination;
  logic functional_source;
  logic path_only_source;
  wire preserved_destination;
  logic [5:0] overlap_selected;
  logic [5:0] overlap_selector;
  logic [5:0] overlap_base;
  wire [5:0] overlap_destination;

  twelve_transition_path all_transitions(source, destination);
  multi_group_same_target preserved(functional_source, path_only_source,
                                    preserved_destination);
  genvar bit_index;
  generate
    for (bit_index = 0; bit_index < 6; bit_index++) begin : overlap
      overlapping_transition_paths path(overlap_selected[bit_index],
                                        overlap_selector[bit_index],
                                        overlap_base[bit_index],
                                        overlap_destination[bit_index]);
    end
  endgenerate

  initial begin
    source = 0;
    functional_source = 0;
    path_only_source = 0;
    overlap_base = 6'bzxx1x0;
    overlap_selected = 6'bzxx1x0;
    overlap_selector = 0;
    #13;
    if (destination !== 0)
      $fatal(1, "initial path value did not settle");

    source = 1; #1;
    if (destination !== 1) $fatal(1, "01");
    source = 0; #2;
    if (destination !== 0) $fatal(1, "10");
    source = 1'bz; #3;
    if (destination !== 1'bz) $fatal(1, "0z");
    source = 1; #4;
    if (destination !== 1) $fatal(1, "z1");
    source = 1'bz; #5;
    if (destination !== 1'bz) $fatal(1, "1z");
    source = 0; #6;
    if (destination !== 0) $fatal(1, "z0");
    source = 1'bx; #7;
    if (destination !== 1'bx) $fatal(1, "0x");
    source = 1; #8;
    if (destination !== 1) $fatal(1, "x1");
    source = 1'bx; #9;
    if (destination !== 1'bx) $fatal(1, "1x");
    source = 0; #10;
    if (destination !== 0) $fatal(1, "x0");
    source = 1'bx; #7;
    if (destination !== 1'bx) $fatal(1, "0x before xz");
    source = 1'bz; #11;
    if (destination !== 1'bz) $fatal(1, "xz");
    source = 1'bx; #12;
    if (destination !== 1'bx) $fatal(1, "zx");

    // A second source in the same full-path group activates the actor while
    // its functional target is unchanged. The original 0->1 deadline must
    // survive all twelve statically emitted delay groups.
    functional_source = 1;
    #1 path_only_source = 1;
    #3;
    if (preserved_destination !== 0)
      $fatal(1, "same target matured early");
    #1;
    if (preserved_destination !== 1)
      $fatal(1, "same target lost its original deadline");

    // Both selected_source and selector activate on every bit. The selector
    // path wins independently for the six X-related classes.
    #26;
    {overlap_selector, overlap_selected} = {6'b111111, 6'bxz0x1x};
    #6;
    if (overlap_destination !== 6'bzxx1x0)
      $fatal(1, "unknown transitions matured early");
    #1;
    if (overlap_destination[0] !== 1'bx) $fatal(1, "overlap 0x");
    #1;
    if (overlap_destination[1] !== 1'b1) $fatal(1, "overlap x1");
    #1;
    if (overlap_destination[2] !== 1'bx) $fatal(1, "overlap 1x");
    #1;
    if (overlap_destination[3] !== 1'b0) $fatal(1, "overlap x0");
    #1;
    if (overlap_destination[4] !== 1'bz) $fatal(1, "overlap xz");
    #1;
    if (overlap_destination[5] !== 1'bx) $fatal(1, "overlap zx");

    $display("PASSED");
    $finish;
  end
endmodule

// CHECK: PASSED
// SLANG-DAG: timing_delay_count = 12 : i64
// SLANG-DAG: timing_delay_fs = array<i64: 1000000, 2000000, 3000000, 4000000, 5000000, 6000000, 7000000, 8000000, 9000000, 10000000, 11000000, 12000000>
// SLANG-DAG: obelisk.simple_timing_path
