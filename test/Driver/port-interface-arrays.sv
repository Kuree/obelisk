// RUN: obelisk -O0 %s -o %t.o0
// RUN: %t.o0 | FileCheck %s
// RUN: obelisk %s -o %t.o3
// RUN: %t.o3 | FileCheck %s

interface array_port_bus;
  logic value;
  modport consumer(input value);
endinterface

module array_port_child(array_port_bus.consumer bus[1:0],
                        output logic [1:0] seen);
  always_comb begin
    seen[0] = bus[0].value;
    seen[1] = bus[1].value;
  end
endmodule

module generic_port_child(interface bus, output logic seen);
  always_comb seen = bus.value;
endmodule

module port_interface_array_behavior;
  array_port_bus buses[1:0]();
  logic [1:0] seen;
  logic generic_seen;
  array_port_child child(.bus(buses), .seen(seen));
  generic_port_child generic_child(.bus(buses[0]), .seen(generic_seen));
  initial begin
    buses[0].value = 1'b1;
    buses[1].value = 1'b0;
    #1;
    $display("IFACE_ARRAY=%b GENERIC=%b", seen, generic_seen);
  end
endmodule

// CHECK: IFACE_ARRAY=01 GENERIC=1
