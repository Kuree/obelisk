// RUN: obelisk -O2 --vpi=off --native-scheduler=generic %s -o %t.generic
// RUN: %t.generic | FileCheck %s
// RUN: obelisk -O2 --vpi=off --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s
// RUN: obelisk -O2 --vpi=off --native-scheduler=aot %s -o %t.aot
// RUN: %t.aot | FileCheck %s

module wide_forward #(
    parameter int W = 257
) (
    input  wire logic [W-1:0] source4,
    input       bit   [W-1:0] source2,
    output wire logic [W-1:0] sink4,
    output      bit   [W-1:0] sink2,
    inout  wire logic [W-1:0] bus
);
  assign sink4 = source4;
  always_comb sink2 = source2;
  assign bus = source4;
endmodule

module extend_forward (
    input  wire logic signed [64:0] signed_source,
    input  wire logic        [64:0] unsigned_source,
    output wire logic signed [128:0] signed_sink,
    output wire logic        [128:0] unsigned_sink
);
  assign signed_sink = signed_source;
  assign unsigned_sink = unsigned_source;
endmodule

module ascending_forward (
    input  wire logic [0:64] source,
    output wire logic [64:0] sink,
    inout  wire logic [0:64] bus
);
  assign sink = source;
endmodule

// Both input ports collapse onto one driven component.  Bulk publication must
// update every member before notifying either observer.
module atomic_alias_observers #(
    parameter int W = 257
) (
    inout wire logic [W-1:0] left,
    inout wire logic [W-1:0] right
);
  always @(left)
    if (left !== right)
      $display("atomic left observer failed at %0t", $time);
  always @(right)
    if (right !== left)
      $display("atomic right observer failed at %0t", $time);
endmodule

module top;
  logic [256:0] source4;
  logic [256:0] atomic_source;
  bit [256:0] source2;
  wire logic [256:0] sink4;
  bit [256:0] sink2;
  wire logic [256:0] wide_bus;
  wire logic [256:0] atomic_net;

  logic signed [64:0] signed_source;
  logic [64:0] unsigned_source;
  wire logic signed [128:0] signed_sink;
  wire logic [128:0] unsigned_sink;

  logic [64:0] descending_source;
  wire logic [64:0] ascending_sink;
  wire logic [64:0] ascending_bus;

  wide_forward wide (
      .source4(source4), .source2(source2), .sink4(sink4), .sink2(sink2),
      .bus(wide_bus));
  atomic_alias_observers atomic_alias (.left(atomic_net), .right(atomic_net));
  extend_forward extend (
      .signed_source(signed_source), .unsigned_source(unsigned_source),
      .signed_sink(signed_sink), .unsigned_sink(unsigned_sink));
  ascending_forward ascending (
      .source(descending_source), .sink(ascending_sink), .bus(ascending_bus));

  assign ascending_bus = descending_source;
  assign atomic_net = atomic_source;

  initial begin
    source4 = '0;
    atomic_source = '0;
    source2 = '1;
    signed_source = -65'sd7;
    unsigned_source = 65'h1_2345_6789_abcd_ef01;
    descending_source = 65'h1_1357_9bdf_0246_8ace;
    #1;
    source4 = '1;
    source4[3:0] = 4'bxz01;
    atomic_source = '1;
    atomic_source[3:0] = 4'bzx10;
    #1;

    if (sink4 !== source4 || wide_bus !== source4 ||
        atomic_net !== atomic_source || sink2 !== source2 ||
        signed_sink !== {{64{1'b1}}, signed_source} ||
        unsigned_sink !== {{64{1'b0}}, unsigned_source} ||
        ascending_sink !== descending_source ||
        ascending_bus !== descending_source)
      $display("wide port forwarding failed");
    else
      $display("wide port forwarding passed");
  end
endmodule

// CHECK-NOT: atomic {{(left|right)}} observer failed
// CHECK: wide port forwarding passed
