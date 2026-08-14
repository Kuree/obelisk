// RUN: obelisk -fno-lto -O0 %s -o %t.o0
// RUN: %t.o0 | FileCheck %s
// RUN: obelisk -fno-lto %s -o %t.o3
// RUN: %t.o3 | FileCheck %s

typedef struct packed {
  logic [3:0] high;
  logic [3:0] low;
} packed_pair_t;

module aggregate_port_child(
    input logic [3:0] source [0:1],
    output logic [3:0] destination [0:1],
    input packed_pair_t packed_source,
    output packed_pair_t packed_destination);
  always_comb begin
    destination = source;
    packed_destination = packed_source;
  end
endmodule

module conversion_port_child(
    input logic signed [5:0] input_value,
    output logic signed [3:0] output_value);
  always_comb output_value = input_value;
endmodule

module port_aggregate_conversions;
  logic [3:0] source [0:1];
  logic [3:0] destination [0:1];
  packed_pair_t packed_source;
  packed_pair_t packed_destination;
  logic [3:0] conversion_input;
  logic signed [7:0] conversion_output;

  aggregate_port_child aggregate_child(source, destination, packed_source,
                                       packed_destination);
  conversion_port_child conversion_child(conversion_input, conversion_output);

  initial begin
    source[0] = 4'ha;
    source[1] = 4'h5;
    packed_source.high = 4'hc;
    packed_source.low = 4'h3;
    conversion_input = 4'he;
    #1;
    $display("AGGREGATE=%h%h/%h CONVERSION=%h", destination[0],
             destination[1], packed_destination, conversion_output);
  end
endmodule

// CHECK: AGGREGATE=a5/c3 CONVERSION=fe
