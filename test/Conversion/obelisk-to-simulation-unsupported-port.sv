// RUN: obelisk -O0 -emit-sim %s | FileCheck %s --implicit-check-not=obelisk.sv.

module sim_port_child(input logic [3:0] value,
                      output logic [3:0] copied);
  always_comb copied = value;
endmodule

module supported_port_connections;
  logic [7:0] source_value;
  logic [7:0] destination;
  sim_port_child child(source_value[3:0] + 1'b1,
                       {destination[7:6], destination[1:0]});
endmodule

// CHECK-DAG: obelisk_sim.code_unit.decl {{[0-9]+}} in {{[0-9]+}} port_input hierarchy "supported_port_connections.child.$port_connection_0"{{.*}}{internal}
// CHECK-DAG: obelisk_sim.code_unit.decl {{[0-9]+}} in {{[0-9]+}} port_output hierarchy "supported_port_connections.child.$port_connection_1"{{.*}}{internal}
// CHECK-DAG: entry_kind = 9 : i32{{.*}}internal
// CHECK-DAG: entry_kind = 10 : i32{{.*}}internal
// CHECK-DAG: #obelisk_sim.effect<effect = write, resource = storage{{.*}}low = 0, width = 2
// CHECK-DAG: #obelisk_sim.effect<effect = write, resource = storage{{.*}}low = 6, width = 2
