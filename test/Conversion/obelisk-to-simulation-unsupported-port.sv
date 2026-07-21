// RUN: not obelisk -emit-sim %s 2>&1 | FileCheck %s

module sim_port_child(input logic value);
endmodule

module unsupported_port;
  logic value;
  sim_port_child child(~value);
endmodule

// CHECK: non-identity port connections are not supported by the first simulation slice
