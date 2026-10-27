// RUN: %talos --std=1800-2023 -emit-slang %s -o /dev/null
// RUN: %talos --std=1800-2023 -emit-talos %s -o /dev/null

module top;
  bit [7:0] \a.with.dot ;
  logic [7:0] l;
  wire [7:0] \n.with.dot  = \a.with.dot ;
  integer index;
  localparam bit [7:0] P = 8'ha5;
  bit constant_bit, dynamic_bit, parameter_bit, computed_bit;
  logic logic_bit, net_bit;
  initial constant_bit = \a.with.dot [7];
  initial computed_bit = \a.with.dot [3 + 4];
  initial dynamic_bit = \a.with.dot [index];
  initial parameter_bit = P[7];
  initial logic_bit = l[index];
  initial net_bit = \n.with.dot [index];
endmodule
