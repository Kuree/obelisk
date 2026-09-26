// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s
// RUN: obelisk -emit-slang %s -o - | FileCheck %s --check-prefix=SLANG
// RUN: obelisk -emit-slang %s -o - | FileCheck %s --check-prefix=SLANGMETA
// RUN: obelisk -emit-obelisk %s -o - | FileCheck %s --check-prefix=OBELISK
// RUN: obelisk -emit-obelisk %s -o - | FileCheck %s --check-prefix=OBELISKMETA

`timescale 1ns / 1ns

module vector_posedge(input wire [7:0] clock, input wire [3:0] data,
                      output wire [3:0] destination);
  assign destination = data;
  specify
    (posedge clock[7:4] => (destination +: (data ^ 4'b1010))) = 3;
  endspecify
endmodule

module ascending_posedge(input wire [0:7] clock, input wire [0:3] data,
                         output wire [0:3] destination);
  assign destination = data;
  specify
    (posedge clock[0:3] => (destination +: data)) = 3;
  endspecify
endmodule

module scalar_negedge(input wire clock, input wire data,
                      output wire destination);
  assign destination = data;
  specify
    (negedge clock -=> (destination -: ~data)) = 4;
  endspecify
endmodule

module scalar_posedge(input wire clock, input wire data,
                      output wire destination);
  assign destination = data;
  specify
    (posedge clock => (destination : data)) = 2;
  endspecify
endmodule

module scalar_edge(input wire clock, input wire data,
                   output wire destination);
  assign destination = data;
  specify
    (edge clock *> (destination : data)) = 2;
  endspecify
endmodule

module scalar_omitted(input wire clock, input wire data,
                      output wire destination);
  assign destination = data;
  specify
    (clock => (destination : data)) = 3;
  endspecify
endmodule

module conditional_edge(input wire clock, input wire enable, input wire data,
                        output wire destination);
  assign destination = data;
  specify
    if (enable)
      (posedge clock => (destination +: data)) = 5;
  endspecify
endmodule

module mixed_simple_edge(input wire clock, input wire data,
                         output wire destination);
  assign destination = data;
  specify
    // Keep the simple rule first: group ordering must not enable the legacy
    // whole-driver fast path and erase the following edge qualifier.
    (data => destination) = 5;
    (posedge clock => (destination +: data)) = 2;
  endspecify
endmodule

module derived_nba_edge(input wire clock, input wire data,
                        output wire destination);
  logic state;
  always_ff @(posedge clock)
    state <= data;
  assign destination = state;
  specify
    (posedge clock => (destination +: data)) = 3;
  endspecify
endmodule

module consumed_edge(input wire clock, input wire data,
                     output wire destination);
  logic state, tail;
  initial begin
    state = 0;
    tail = 0;
  end
  always @(posedge clock)
    state <= data;
  always @(state)
    #0 tail <= state;
  assign destination = tail ? 1'bx : state;
  specify
    (posedge clock => (destination : data)) = 3;
  endspecify
endmodule

module specify_edge_path;
  logic [7:0] vector_clock;
  logic [3:0] vector_data;
  wire [3:0] vector_destination;
  logic [0:7] ascending_clock;
  logic [0:3] ascending_data;
  wire [0:3] ascending_destination;
  logic neg_clock, neg_data;
  wire neg_destination;
  logic pos_clock, pos_data;
  wire pos_destination;
  logic both_clock, both_data;
  wire both_destination;
  logic any_clock, any_data;
  wire any_destination;
  logic conditional_clock, conditional_enable, conditional_data;
  wire conditional_destination;
  logic mixed_clock, mixed_data;
  wire mixed_destination;
  logic derived_clock, derived_data;
  wire derived_destination;
  logic consumed_clock, consumed_data;
  wire consumed_destination;

  vector_posedge p(vector_clock, vector_data, vector_destination);
  ascending_posedge ap(ascending_clock, ascending_data,
                       ascending_destination);
  scalar_negedge n(neg_clock, neg_data, neg_destination);
  scalar_posedge ps(pos_clock, pos_data, pos_destination);
  scalar_edge e(both_clock, both_data, both_destination);
  scalar_omitted a(any_clock, any_data, any_destination);
  conditional_edge c(conditional_clock, conditional_enable,
                     conditional_data, conditional_destination);
  mixed_simple_edge m(mixed_clock, mixed_data, mixed_destination);
  derived_nba_edge dn(derived_clock, derived_data, derived_destination);
  consumed_edge ce(consumed_clock, consumed_data, consumed_destination);

  initial begin
    vector_clock = 0;
    vector_data = 0;
    ascending_clock = 0;
    ascending_data = 0;
    neg_clock = 1;
    neg_data = 0;
    pos_clock = 0;
    pos_data = 0;
    both_clock = 0;
    both_data = 0;
    any_clock = 0;
    any_data = 0;
    conditional_clock = 0;
    conditional_enable = 0;
    conditional_data = 0;
    mixed_clock = 0;
    mixed_data = 0;
    derived_clock = 0;
    derived_data = 0;
    consumed_clock = 0;
    consumed_data = 0;
    #6;

    mixed_data = 1;
    #4;
    if (mixed_destination !== 0) $fatal(1, "mixed simple early");
    #1;
    if (mixed_destination !== 1) $fatal(1, "mixed simple late");
    mixed_clock = 1;
    #0;
    mixed_data = 0;
    #1;
    if (mixed_destination !== 1) $fatal(1, "mixed edge early");
    #1;
    if (mixed_destination !== 0) $fatal(1, "mixed edge lost");

    // The clock wakes the destination actor before the NBA updates `state`.
    // Same-time qualification must survive that Active->NBA->Active sequence.
    derived_clock = 1;
    #2;
    if (derived_destination !== 1'bx) $fatal(1, "derived initial early");
    #1;
    if (derived_destination !== 0) $fatal(1, "derived initial late");
    derived_clock = 0;
    #1;
    derived_data = 1;
    derived_clock = 1;
    #2;
    if (derived_destination !== 0) $fatal(1, "derived first early");
    #1;
    if (derived_destination !== 1) $fatal(1, "derived first late");
    derived_clock = 0;
    #1;
    derived_data = 0;
    derived_clock = 1;
    #2;
    if (derived_destination !== 1) $fatal(1, "derived second early");
    #1;
    if (derived_destination !== 0) $fatal(1, "derived second late");

    // One edge qualifies the NBA-derived 0->1 update. A second zero-time
    // tail update changes the same destination to X and must publish
    // immediately rather than reusing already consumed qualification.
    consumed_data = 1;
    #0;
    consumed_clock = 1;
    #1;
    if (consumed_destination !== 1'bx)
      $fatal(1, "edge not consumed: %b", consumed_destination);
    #3;
    if (consumed_destination !== 1'bx) $fatal(1, "consumed edge resurfaced");

    // A transition on another vector bit is not a posedge path event. The
    // functional value therefore publishes immediately.
    vector_clock[6] = 1;
    #0;
    vector_data = 4'h1;
    #1;
    if (vector_destination !== 4'h1) $fatal(1, "vector non-LSB edge");

    // The vector's LSB does qualify and activates the complete parallel path.
    vector_clock[4] = 1;
    #0;
    vector_data = 4'h2;
    #2;
    if (vector_destination !== 4'h1) $fatal(1, "vector posedge early");
    #1;
    if (vector_destination !== 4'h2) $fatal(1, "vector posedge late");

    // Table 9-2 positive edges include 0->X/Z and X/Z->1, but exclude
    // X<->Z. Exercise the full non-binary set in both execution tiers.
    pos_clock = 1'bx;
    #0;
    pos_data = 1;
    #1;
    if (pos_destination !== 0) $fatal(1, "posedge 0x early");
    #1;
    if (pos_destination !== 1) $fatal(1, "posedge 0x late");
    pos_clock = 1'bz;
    #0;
    pos_data = 0;
    #1;
    if (pos_destination !== 0) $fatal(1, "posedge xz rejected");
    pos_clock = 1;
    #0;
    pos_data = 1;
    #1;
    if (pos_destination !== 0) $fatal(1, "posedge z1 early");
    #1;
    if (pos_destination !== 1) $fatal(1, "posedge z1 late");
    pos_clock = 0;
    #0;
    pos_data = 0;
    #1;
    if (pos_destination !== 0) $fatal(1, "posedge reset");
    pos_clock = 1'bz;
    #0;
    pos_data = 1;
    #1;
    if (pos_destination !== 0) $fatal(1, "posedge 0z early");
    #1;
    if (pos_destination !== 1) $fatal(1, "posedge 0z late");
    pos_clock = 1'bx;
    #0;
    pos_data = 0;
    #1;
    if (pos_destination !== 0) $fatal(1, "posedge zx rejected");
    pos_clock = 1;
    #0;
    pos_data = 1;
    #1;
    if (pos_destination !== 0) $fatal(1, "posedge x1 early");
    #1;
    if (pos_destination !== 1) $fatal(1, "posedge x1 late");

    // The right bound is also the semantic LSB for an ascending terminal;
    // its packed storage offset is not assumed to be zero.
    ascending_clock[1] = 1;
    #0;
    ascending_data = 4'h1;
    #1;
    if (ascending_destination !== 4'h1) $fatal(1, "ascending non-LSB");
    ascending_clock[3] = 1;
    #0;
    ascending_data = 4'h2;
    #2;
    if (ascending_destination !== 4'h1)
      $fatal(1, "ascending posedge early: %h", ascending_destination);
    #1;
    if (ascending_destination !== 4'h2) $fatal(1, "ascending posedge late");

    // Neither outer nor suffix polarity transforms propagated data.
    neg_clock = 0;
    #0;
    neg_data = 1;
    #3;
    if (neg_destination !== 0) $fatal(1, "negedge early");
    #1;
    if (neg_destination !== 1) $fatal(1, "negedge polarity");

    // The negative-edge set is symmetric: 1->X/Z and X/Z->0 qualify,
    // while X<->Z does not.
    neg_clock = 1;
    #0;
    neg_data = 0;
    #1;
    if (neg_destination !== 0) $fatal(1, "negedge reset");
    neg_clock = 1'bx;
    #0;
    neg_data = 1;
    #3;
    if (neg_destination !== 0) $fatal(1, "negedge 1x early");
    #1;
    if (neg_destination !== 1) $fatal(1, "negedge 1x late");
    neg_clock = 1'bz;
    #0;
    neg_data = 0;
    #1;
    if (neg_destination !== 0) $fatal(1, "negedge xz rejected");
    neg_clock = 0;
    #0;
    neg_data = 1;
    #3;
    if (neg_destination !== 0) $fatal(1, "negedge z0 early");
    #1;
    if (neg_destination !== 1) $fatal(1, "negedge z0 late");
    neg_clock = 1;
    #0;
    neg_data = 0;
    #1;
    if (neg_destination !== 0) $fatal(1, "negedge second reset");
    neg_clock = 1'bz;
    #0;
    neg_data = 1;
    #3;
    if (neg_destination !== 0) $fatal(1, "negedge 1z early");
    #1;
    if (neg_destination !== 1) $fatal(1, "negedge 1z late");
    neg_clock = 1'bx;
    #0;
    neg_data = 0;
    #1;
    if (neg_destination !== 0) $fatal(1, "negedge zx rejected");
    neg_clock = 0;
    #0;
    neg_data = 1;
    #3;
    if (neg_destination !== 0) $fatal(1, "negedge x0 early");
    #1;
    if (neg_destination !== 1) $fatal(1, "negedge x0 late");

    // `edge` is the union of the standard positive and negative edge sets.
    both_clock = 1;
    #0;
    both_data = 1;
    #1;
    if (both_destination !== 0) $fatal(1, "edge posedge early");
    #1;
    if (both_destination !== 1) $fatal(1, "edge posedge late");
    both_clock = 0;
    #0;
    both_data = 0;
    #1;
    if (both_destination !== 1) $fatal(1, "edge negedge early");
    #1;
    if (both_destination !== 0) $fatal(1, "edge negedge late");

    // An omitted identifier accepts an arbitrary source transition.
    any_clock = 1'bx;
    #0;
    any_data = 1;
    #2;
    if (any_destination !== 0) $fatal(1, "omitted edge early");
    #1;
    if (any_destination !== 1) $fatal(1, "omitted edge late");

    // The condition is sampled only when the source transition occurs.
    conditional_clock = 1;
    #0;
    conditional_data = 1;
    #1;
    if (conditional_destination !== 1) $fatal(1, "false condition");
    conditional_clock = 0;
    #0;
    conditional_data = 0;
    #1;
    conditional_enable = 1;
    conditional_clock = 1;
    #0;
    conditional_data = 1;
    #4;
    if (conditional_destination !== 0) $fatal(1, "condition edge early");
    #1;
    if (conditional_destination !== 1) $fatal(1, "condition edge late");

    $display("PASSED");
    $finish;
  end
endmodule

// CHECK: PASSED
// SLANG-COUNT-10: timing_edge_sensitive
// SLANGMETA-DAG: timing_edge_identifier = 0 : i32
// SLANGMETA-DAG: timing_edge_identifier = 1 : i32
// SLANGMETA-DAG: timing_edge_identifier = 2 : i32
// SLANGMETA-DAG: timing_edge_identifier = 3 : i32
// SLANGMETA-DAG: timing_edge_polarity = 1 : i32
// SLANGMETA-DAG: timing_edge_polarity = 2 : i32
// OBELISK-COUNT-11: obelisk.simple_timing_path
// The arbitrary data-source expression is preserved as analysis metadata.
// OBELISKMETA: obelisk.sv.expression.binary
// OBELISKMETA: obelisk.sv.expression.unary
