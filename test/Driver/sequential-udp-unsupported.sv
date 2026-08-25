// RUN: not obelisk -emit-sim %s -o /dev/null 2>&1 | FileCheck %s

// G2 preserves all validated declaration metadata, including the state and
// edge fields needed by G3.  An executable sequential or edge-sensitive UDP
// must receive a focused boundary diagnostic until G3 lands.

primitive udp_level(output reg out, input in);
  table
    0 : ? : 0;
    1 : ? : 1;
  endtable
endprimitive

primitive udp_edge(output reg out, input clk);
  table
    (01) : ? : 1;
    (10) : ? : 0;
  endtable
endprimitive

module sequential_udp_unsupported(input logic in, clk,
                                  output wire level_out, edge_out);
  udp_level level_instance(level_out, in);
  udp_edge edge_instance(edge_out, clk);
endmodule

// CHECK-COUNT-2: error: sequential user-defined primitives are not supported yet
