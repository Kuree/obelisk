// RUN: obelisk -O0 %s -o %t.o0
// RUN: %t.o0 | FileCheck %s
// RUN: obelisk %s -o %t.o3
// RUN: %t.o3 | FileCheck %s
// RUN: obelisk -emit-obelisk %s > %t.semantic.mlir
// RUN: obelisk-opt %t.semantic.mlir '--lower-obelisk-to-sim=opt-level=0' > %t.threaded.mlir
// RUN: obelisk-opt %t.semantic.mlir '--lower-obelisk-to-sim=opt-level=0' --mlir-disable-threading > %t.single.mlir
// RUN: diff -u %t.single.mlir %t.threaded.mlir
// RUN: FileCheck %s --check-prefix=SIM --implicit-check-not=obelisk.sv. < %t.single.mlir

interface full_port_bus;
  logic [3:0] value;
  modport consumer(input value);
endinterface

module full_port_net_leaf(inout wire [3:0] p);
  assign p[1:0] = 2'b10;
endmodule

module full_port_net_middle(inout wire [3:0] p);
  full_port_net_leaf leaf(.p(p));
endmodule

module full_port_uwire(output uwire value);
  assign value = 1'b1;
endmodule

module full_port_concat_output(output logic [1:0] value);
  assign value = 2'b10;
endmodule

module full_port_net_short(inout wire [1:0] value);
  assign value = 2'b10;
endmodule

module full_port_net_nibble(inout wire [3:0] value);
  assign value = 4'b1010;
endmodule

module full_port_nonansi_concat(.pair({left, right}), seen);
  input logic left, right;
  output logic [1:0] seen;
  always_comb seen = {left, right};
endmodule

module full_port_array_element(input logic source, output logic destination);
  assign destination = source;
endmodule

module full_port_expression(
    input logic [3:0] i,
    output logic [3:0] o);
  always_comb o = i + 1'b1;
endmodule

module full_port_dynamic_input(input logic value, output logic observed);
  always_comb observed = value;
endmodule

module full_port_interface(full_port_bus.consumer bus,
                           output logic [3:0] seen);
  always_comb seen = bus.value;
endmodule

module full_port_ref_leaf(ref logic [3:0] value);
  initial begin
    #1;
    value = 4'hb;
  end
endmodule

module full_port_ref_middle(ref logic [3:0] value);
  full_port_ref_leaf leaf(value);
endmodule

module full_port_initialization(
    input logic defaulted = 1'b1,
    input logic four_state,
    input bit two_state,
    input wire net_state);
  initial begin
    #1;
    $display("INIT=%b%b%b%b", defaulted, four_state, two_state, net_state);
  end
endmodule

module port_connections;
  parameter int STATIC_BASE = 1;
  typedef struct {
    logic [3:0] value;
  } ref_struct;
  typedef struct packed {
    logic [3:0] high;
    logic [3:0] low;
  } packed_port_struct;
  tri [3:0] connected;
  uwire single_driver;
  uwire [1:0] split_uwire;
  wire concat_left;
  wire concat_right;
  wire concat_short;
  wire topology_short;
  logic driver_a;
  logic driver_b;
  wire conflict;
  wire directional_source;
  wire directional_sink;
  logic [7:0] source_value;
  logic [7:0] destination;
  logic [3:0] ref_values [0:3];
  ref_struct ref_member;
  full_port_bus bus();
  logic [3:0] seen;
  logic [1:0] array_source;
  wire [1:0] array_destination;
  logic dynamic_seen;
  wire [1:0][3:0] multidimensional_net;
  wire packed_port_struct member_net;
  wire [7:0] indexed_up_net;
  wire [7:0] indexed_down_net;
  wire [7:0] parameter_indexed_net;
  wire [0:7] ascending_net;
  logic [1:0] nonansi_source;
  logic [1:0] nonansi_seen;

  function automatic logic read_dynamic_source();
    read_dynamic_source = driver_b;
  endfunction

  assign connected[1:0] = 2'b01;
  full_port_net_middle reversed(
      .p({connected[0], connected[1], connected[2], connected[3]}));
  full_port_uwire uwire_instance(single_driver);
  assign split_uwire[0] = 1'b0;
  assign split_uwire[1] = 1'b1;
  full_port_concat_output distinct_concat({concat_left, concat_right});
  full_port_concat_output meaningful_short({concat_short, concat_short});
  full_port_net_short topology_bit_short({topology_short, topology_short});
  full_port_net_nibble multidimensional(multidimensional_net[1]);
  full_port_net_nibble member(member_net.high);
  full_port_net_nibble indexed_up(indexed_up_net[1 +: 4]);
  full_port_net_nibble indexed_down(indexed_down_net[6 -: 4]);
  full_port_net_nibble parameter_indexed(
      parameter_indexed_net[STATIC_BASE +: 4]);
  full_port_net_nibble ascending(ascending_net[2 +: 4]);
  full_port_nonansi_concat nonansi(.pair(nonansi_source + 2'b01),
                                   .seen(nonansi_seen));
  full_port_array_element arrayed[1:0](array_source, array_destination);
  assign conflict = driver_a;
  assign conflict = driver_b;
  assign directional_source = 1'b1;
  assign directional_sink = directional_source;
  assign directional_sink = 1'b0;
  full_port_expression expression(
      .i(source_value[3:0] + 1'b1),
      .o({destination[7:6], destination[1:0]}));
  full_port_dynamic_input dynamic_input(.value(read_dynamic_source()),
                                        .observed(dynamic_seen));
  full_port_interface interface_instance(.bus(bus), .seen(seen));
  full_port_ref_middle ref_instance(ref_values[2]);
  full_port_ref_middle ref_member_instance(ref_member.value);
  full_port_initialization initialization(.four_state(), .two_state(),
                                          .net_state());

  initial begin
    source_value = 8'h03;
    destination = 8'h00;
    ref_values[2] = 4'h0;
    ref_member.value = 4'h0;
    bus.value = 4'ha;
    array_source = 2'b10;
    driver_a = 1'b0;
    driver_b = 1'b1;
    nonansi_source = 2'b01;
    #2;
    $display("PORT=%b UWIRE=%b/%b CONCAT=%b%b/%b/%b ARRAY=%b EXPR=%h DYNAMIC=%b IFACE=%h REF=%h/%h CONFLICT=%b DIR=%b/%b TOPO=%b/%b/%b/%b/%b/%b NONANSI=%b",
             connected, single_driver, split_uwire, concat_left, concat_right,
             concat_short, topology_short, array_destination, destination,
             dynamic_seen, seen, ref_values[2], ref_member.value, conflict,
             directional_source, directional_sink, multidimensional_net,
             member_net, indexed_up_net, indexed_down_net,
             parameter_indexed_net, ascending_net, nonansi_seen);
    driver_b = 1'bz;
    #1;
    $display("RELEASE=%b DYNAMIC=%b", conflict, dynamic_seen);
  end
endmodule

// CHECK: INIT=1x0z
// CHECK-NEXT: PORT=0101 UWIRE=1/10 CONCAT=10/x/x ARRAY=10 EXPR=41 DYNAMIC=1 IFACE=a REF=b/b CONFLICT=x DIR=1/x TOPO=1010zzzz/1010zzzz/zzz1010z/z1010zzz/zzz1010z/zz1010zz NONANSI=10
// CHECK-NEXT: RELEASE=0 DYNAMIC=z

// Static net associations are explicit topology, while converted value ports
// use hidden connection code units that remain outside ordinary functions.
// SIM-DAG: obelisk_sim.net.connect.decl
// SIM-DAG: port_input hierarchy "port_connections.expression.$port_connection_0"{{.*}}{internal}
// SIM-DAG: port_output hierarchy "port_connections.expression.$port_connection_1"{{.*}}{internal}
// SIM-DAG: port_initialize hierarchy "port_connections.initialization.$port_connection_0"{{.*}}{internal}
// SIM-DAG: obelisk_sim.descriptor_indices = array<i64: 2>
// SIM-DAG: obelisk_sim.descriptor_low = 8 : i64
