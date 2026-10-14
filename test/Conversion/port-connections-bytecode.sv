// RUN: obelisk -emit-slang %s | obelisk-opt --convert-slang-to-obelisk '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\"}"' -o %t.obelisk.mlir
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | FileCheck %s
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=LOWER \
// RUN:     --implicit-check-not=obelisk.sv.

// A complete elaborated design serializes through the unified runtime
// artifact version 1.
// CHECK: bytecode.image = array<i8: 79, 66, 66, 67, 68, 83, 49, 0, 1, 0, 0, 0, 0, 0, 0, 0
// CHECK: execution.state_bits = {{[1-9][0-9]*}} : i64

// Port connection lowering owns the semantic inventory independently of the
// driver: static net topology remains declarative, empty defaulted inputs get
// initialization code units, and generated/arrayed instances retain distinct
// hidden connection identities.
// The port inventory keeps declaration order, direction, canonical source,
// and the child module's scope independently of its backing descriptor.
// Its VPI type is the formal's declared type: a `reg` formal reflects as a reg
// even though reg and logic share one executable representation.
// LOWER-DAG: simulation.port.decl 0 in 9 source 5 net = false at 0 : !simulation.logic<1> input ordinal 0 hierarchy "port_connections_inventory.ordered.defaulted" debug "defaulted" {{.*}}vpi_type = #simulation.vpi_type<kind = reg, isSigned = false, isFourState = true, range = [0, 0], children = [], childNames = []>
// LOWER-DAG: simulation.port.decl 1 in 9 source 6 net = false at 0 : !simulation.logic<1> input ordinal 1 hierarchy "port_connections_inventory.ordered.input_value" debug "input_value"
// LOWER-DAG: simulation.port.decl 2 in 9 source 7 net = false at 0 : !simulation.logic<1> output ordinal 2 hierarchy "port_connections_inventory.ordered.output_value" debug "output_value"
// LOWER-DAG: simulation.port.decl 3 in 9 source 2 net = true at 0 : !simulation.logic<1> inout ordinal 3 hierarchy "port_connections_inventory.ordered.net_value" debug "net_value"
// LOWER-DAG: simulation.port.decl 4 in 10 source 8 net = false at 0 : !simulation.logic<1> input ordinal 0 hierarchy "port_connections_inventory.named.defaulted" debug "defaulted"
// Two non-ANSI formal names may intentionally alias one internal net. Both
// remain distinct declaration-ordered ports in the child instance scope.
// LOWER-DAG: simulation.port.decl 28 in 16 source 9 net = true at 0 : !simulation.logic<1> input ordinal 0 hierarchy "port_connections_inventory.nonansi.a" debug "a"
// LOWER-DAG: simulation.port.decl 29 in 16 source 9 net = true at 0 : !simulation.logic<1> input ordinal 1 hierarchy "port_connections_inventory.nonansi.b" debug "b"
// LOWER-DAG: simulation.net.connect.decl {{[0-9]+}} in {{[0-9]+}} 0[0] to 2[0] width 1 reversed = false provenance "ordered"
// LOWER-DAG: simulation.net.connect.decl {{[0-9]+}} in {{[0-9]+}} 0[0] to 3[0] width 1 reversed = false provenance "named"
// LOWER-DAG: simulation.code_unit.decl {{[0-9]+}} in {{[0-9]+}} port_initialize hierarchy "port_connections_inventory.uses_default.$port_connection_0"{{.*}}{internal}
// LOWER-DAG: simulation.code_unit.decl {{[0-9]+}} in {{[0-9]+}} port_output hierarchy "port_connections_inventory.arrayed[0].$port_connection_2"{{.*}}{internal}
// LOWER-DAG: simulation.code_unit.decl {{[0-9]+}} in {{[0-9]+}} port_output hierarchy "port_connections_inventory.arrayed[1].$port_connection_2"{{.*}}{internal}
// LOWER-DAG: simulation.code_unit.decl {{[0-9]+}} in {{[0-9]+}} port_input hierarchy "port_connections_inventory.generated[0].element.$port_connection_0"{{.*}}{internal}
// LOWER-DAG: simulation.code_unit.decl {{[0-9]+}} in {{[0-9]+}} port_input hierarchy "port_connections_inventory.generated[1].element.$port_connection_0"{{.*}}{internal}

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

module inventory_leaf(input reg defaulted = 1'b1, input logic input_value,
                      output logic output_value, inout wire net_value);
endmodule

module inventory_nonansi(a, .b(a), c);
  input a;
  output c;
  wire a, c;
endmodule

module inventory_multiport(.pair({right, left}));
  input right, left;
  wire right, left;
endmodule

program inventory_program(input logic value);
endprogram

module port_connections_inventory;
  logic defaulted, input_value;
  wire net_value, output_value;

  inventory_leaf ordered(defaulted, input_value, output_value, net_value);
  inventory_leaf named(.defaulted(defaulted), .input_value(input_value),
                       .output_value(output_value), .net_value(net_value));
  inventory_leaf implicit(.defaulted, .input_value, .output_value, .net_value);
  inventory_leaf wildcard_instance(.*);
  inventory_leaf opened(.defaulted(), .input_value(input_value),
                        .output_value(), .net_value());
  inventory_leaf omitted(input_value, input_value);
  inventory_leaf uses_default(.input_value(input_value),
                              .output_value(output_value),
                              .net_value(net_value));
  inventory_nonansi nonansi(input_value, defaulted, output_value);
  inventory_multiport multiport({defaulted, input_value});
  inventory_leaf arrayed[1:0](defaulted, input_value, output_value,
                              net_value);
  inventory_program program_instance(input_value);
  for (genvar i = 0; i < 2; ++i) begin : generated
    inventory_leaf element(defaulted, input_value, output_value, net_value);
  end
endmodule
