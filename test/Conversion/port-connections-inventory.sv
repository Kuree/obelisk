// RUN: obelisk -emit-slang %s 2>/dev/null | FileCheck %s --check-prefix=SLANG
// RUN: obelisk -emit-obelisk %s 2>/dev/null | FileCheck %s --check-prefix=OBELISK
// RUN: obelisk -emit-obelisk %s -o %t.semantic.mlir 2>/dev/null
// RUN: obelisk-opt %t.semantic.mlir '--lower-obelisk-to-sim=opt-level=0' -o %t.sim.mlir
// RUN: sed '1s/module {/module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"} {/' %t.sim.mlir > %t.bytecode.mlir
// RUN: obelisk-opt %t.bytecode.mlir --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=BYTECODE

module inventory_leaf(
    input logic defaulted = 1'b1,
    input logic input_value,
    output logic output_value,
    inout wire net_value);
endmodule

module inventory_nonansi(a, b, c);
  input a, b;
  output c;
endmodule

module inventory_multiport(.pair({left, right}));
  input left, right;
endmodule

program inventory_program(input logic value);
endprogram

module port_connections_inventory;
  logic defaulted;
  logic input_value;
  logic output_value;
  wire net_value;

  inventory_leaf ordered(defaulted, input_value, output_value, net_value);
  inventory_leaf named(.defaulted(defaulted), .input_value(input_value),
                       .output_value(output_value), .net_value(net_value));
  inventory_leaf implicit(.defaulted, .input_value, .output_value,
                          .net_value);
  inventory_leaf wildcard_instance(.*);
  inventory_leaf opened(.defaulted(), .input_value(input_value),
                        .output_value(), .net_value());
  inventory_leaf omitted(input_value, input_value);
  inventory_leaf uses_default(.input_value(input_value),
                              .output_value(output_value),
                              .net_value(net_value));
  inventory_nonansi nonansi(input_value, defaulted, output_value);
  inventory_multiport multiport({input_value, defaulted});
  inventory_leaf arrayed[1:0](defaulted, input_value, output_value,
                              net_value);
  inventory_program program_instance(input_value);

  for (genvar i = 0; i != 2; ++i) begin : generated
    inventory_leaf element(defaulted, input_value, output_value, net_value);
  end
endmodule

interface inventory_bus;
  logic value;
  modport consumer(input value);
endinterface

module inventory_interface_child(inventory_bus.consumer bus);
endmodule

module inventory_interface_array_child(inventory_bus.consumer bus[1:0]);
endmodule

module inventory_generic_interface_child(interface bus);
endmodule

module port_connections_interface_inventory;
  inventory_bus bus();
  inventory_bus buses[1:0]();
  inventory_interface_child child(.bus(bus));
  inventory_interface_array_child array_child(.bus(buses));
  inventory_generic_interface_child generic_child(.bus(bus));
endmodule

// Ordered, named, implicit, wildcard, explicit-open, omitted, and defaulted
// connections retain distinct provenance after Slang resolves the leaves.
// SLANG-DAG: slang.port.connection attributes {{.*}}formal_name = "defaulted"{{.*}}formal_path = "port_connections_inventory.ordered.defaulted"{{.*}}provenance = 0 : i32
// SLANG-DAG: slang.port.connection attributes {{.*}}formal_name = "defaulted"{{.*}}formal_path = "port_connections_inventory.named.defaulted"{{.*}}provenance = 1 : i32
// SLANG-DAG: slang.port.connection attributes {{.*}}formal_name = "defaulted"{{.*}}formal_path = "port_connections_inventory.implicit.defaulted"{{.*}}provenance = 2 : i32
// SLANG-DAG: slang.port.connection attributes {{.*}}formal_name = "defaulted"{{.*}}formal_path = "port_connections_inventory.wildcard_instance.defaulted"{{.*}}provenance = 3 : i32
// SLANG-DAG: slang.port.connection attributes {{.*}}formal_name = "defaulted"{{.*}}formal_path = "port_connections_inventory.opened.defaulted"{{.*}}provenance = 5 : i32
// SLANG-DAG: slang.port.connection attributes {{.*}}formal_name = "output_value"{{.*}}formal_path = "port_connections_inventory.omitted.output_value"{{.*}}provenance = 4 : i32
// SLANG-DAG: slang.port.connection attributes {{.*}}formal_name = "defaulted"{{.*}}formal_path = "port_connections_inventory.uses_default.defaulted"{{.*}}provenance = 6 : i32

// Non-ANSI and multiport declarations preserve each resolved leaf. The
// multiport leaves both retain the provenance of their one source association.
// SLANG-DAG: slang.port.connection attributes {{.*}}formal_name = "a"{{.*}}formal_path = "port_connections_inventory.nonansi.a"{{.*}}is_ansi = false{{.*}}provenance = 0 : i32
// SLANG-DAG: slang.port.connection attributes {{.*}}formal_name = "right"{{.*}}formal_path = "port_connections_inventory.multiport.right"{{.*}}provenance = 0 : i32
// SLANG-DAG: slang.port.connection attributes {{.*}}formal_name = "left"{{.*}}formal_path = "port_connections_inventory.multiport.left"{{.*}}provenance = 0 : i32

// Scalar instance-array elements and generated instances each receive their
// own resolved inventory.
// SLANG-DAG: formal_path = "port_connections_inventory.arrayed[0].defaulted"
// SLANG-DAG: formal_path = "port_connections_inventory.arrayed[1].defaulted"
// SLANG-DAG: formal_path = "port_connections_inventory.generated[0].element.defaulted"
// SLANG-DAG: formal_path = "port_connections_inventory.generated[1].element.defaulted"
// SLANG-DAG: formal_path = "port_connections_inventory.program_instance.value"

// Interface association metadata identifies the actual instance and modport.
// SLANG-DAG: slang.port.connection attributes {{.*}}formal_name = "bus"{{.*}}interface_instance_path = "port_connections_interface_inventory.bus"{{.*}}interface_shape = array<i64>{{.*}}selected_modport = "consumer"
// SLANG-DAG: slang.port.connection attributes {{.*}}formal_path = "port_connections_interface_inventory.array_child.bus"{{.*}}interface_instance_path = "port_connections_interface_inventory.buses"{{.*}}interface_shape = array<i64: 1, 0>{{.*}}selected_modport = "consumer"
// SLANG-DAG: slang.port.connection attributes {{.*}}formal_path = "port_connections_interface_inventory.generic_child.bus"{{.*}}interface_instance_path = "port_connections_interface_inventory.bus"

// The semantic dialect receives the complete records, including null leaves.
// OBELISK-DAG: obelisk.sv.port.connection attributes {{.*}}formal_path = "port_connections_inventory.opened.defaulted"{{.*}}provenance = 5 : i32
// OBELISK-DAG: obelisk.sv.port.connection attributes {{.*}}formal_path = "port_connections_inventory.omitted.output_value"{{.*}}provenance = 4 : i32
// OBELISK-DAG: obelisk.sv.port.connection attributes {{.*}}formal_path = "port_connections_inventory.uses_default.defaulted"{{.*}}provenance = 6 : i32
// OBELISK-DAG: obelisk.sv.port.connection attributes {{.*}}formal_path = "port_connections_inventory.multiport.right"
// OBELISK-DAG: obelisk.sv.port.connection attributes {{.*}}formal_path = "port_connections_inventory.multiport.left"

// A complete elaborated design, rather than a hand-authored Simulation
// fixture, serializes through design bytecode version 3.
// BYTECODE: obelisk.bytecode.image = array<i8: 79, 66, 66, 67, 68, 83, 49, 0, 3, 0, 0, 0
// BYTECODE: obelisk.execution.state_bits = {{[1-9][0-9]*}} : i64
