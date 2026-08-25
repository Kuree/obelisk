// RUN: obelisk -emit-slang %s 2>/dev/null | FileCheck %s --check-prefix=SLANG
// RUN: obelisk -emit-obelisk %s 2>/dev/null | FileCheck %s --check-prefix=OBELISK

primitive udp_nonansi (out, a, b);
  output out;
  input a, b;
  table
    0 0 : 0;
    0 ? : 0;
    1 b : 1;
    x 0 : 1;
  endtable
endprimitive

primitive udp_ansi(output out, input a, b);
  table
    0 ? : 1;
    1 ? : 0;
    x ? : x;
  endtable
endprimitive

primitive udp_sequential(output reg out = 1'b0, input in);
  table
    0 : ? : 0;
    1 : ? : 1;
  endtable
endprimitive

primitive udp_edge(output reg out = 1'b1, input in);
  table
    (b?) : ? : 0;
    p : ? : 0;
  endtable
endprimitive

primitive udp_initial_statement(out, in);
  output out;
  reg out;
  input in;
  initial out = 1'b1;
  table
    0 : ? : 0;
    1 : ? : 1;
  endtable
endprimitive

module combinational_udp_import(input logic [3:0] a, b,
                                output wire [3:0] y);
  udp_nonansi (weak0, strong1) #(2, 3) u[3:0] (y, a, b);
endmodule

// SLANG-DAG: slang.symbol.primitive attributes {{.*}}name = "udp_nonansi"{{.*}}udp_metadata = {is_edge_sensitive = false, is_sequential = false, name = "udp_nonansi", port_directions = array<i64: 1, 0, 0>, port_names = ["out", "a", "b"], table_edges = array<i64: 0, 0, 0, 0>, table_inputs = ["00", "0?", "1b", "x0"], table_outputs = array<i64: 48, 48, 49, 49>, table_states = array<i64: 0, 0, 0, 0>}

// SLANG-DAG: slang.symbol.primitive attributes {{.*}}name = "udp_ansi"{{.*}}udp_metadata = {is_edge_sensitive = false, is_sequential = false, name = "udp_ansi", port_directions = array<i64: 1, 0, 0>, port_names = ["out", "a", "b"], table_edges = array<i64: 0, 0, 0>, table_inputs = ["0?", "1?", "x?"], table_outputs = array<i64: 49, 48, 120>, table_states = array<i64: 0, 0, 0>}

// Sequential state and initialization use the same frozen declaration ABI.
// SLANG-DAG: slang.symbol.primitive attributes {{.*}}name = "udp_sequential"{{.*}}udp_metadata = {init_value = "1'b0", is_edge_sensitive = false, is_sequential = true, name = "udp_sequential", port_directions = array<i64: 2, 0>, port_names = ["out", "in"], table_edges = array<i64: 0, 0>, table_inputs = ["0", "1"], table_outputs = array<i64: 48, 49>, table_states = array<i64: 63, 63>}

// Explicit wildcard endpoints and symbolic edges are retained in their
// validated normalized spelling for compact G3 lowering.
// SLANG-DAG: slang.symbol.primitive attributes {{.*}}name = "udp_edge"{{.*}}udp_metadata = {init_value = "1'b1", is_edge_sensitive = true, is_sequential = true, name = "udp_edge", port_directions = array<i64: 2, 0>, port_names = ["out", "in"], table_edges = array<i64: 1, 1>, table_inputs = ["(b?)", "p"], table_outputs = array<i64: 48, 48>, table_states = array<i64: 63, 63>}

// The separate Clause 29.7 initial statement reaches the same frozen init ABI.
// SLANG-DAG: slang.symbol.primitive attributes {{.*}}name = "udp_initial_statement"{{.*}}udp_metadata = {init_value = "1'b1", is_edge_sensitive = false, is_sequential = true, name = "udp_initial_statement"

// Each primitive-array element is elaborated as an independent scalar actor,
// while the declaration dictionary remains an interned attribute value.
// SLANG-COUNT-4: slang.symbol.primitive_instance attributes {delay_fs = array<i64: 2000000, 3000000>, drive_strength0 = 3 : i32, drive_strength1 = 1 : i32, {{.*}}primitive_name = "udp_nonansi"{{.*}}udp_metadata = {

// OBELISK-DAG: obelisk.sv.symbol.primitive attributes {{.*}}name = "udp_nonansi"{{.*}}udp_metadata = {{.*}}table_inputs = ["00", "0?", "1b", "x0"]
// OBELISK-COUNT-4: obelisk.sv.symbol.primitive_instance attributes {delay_fs = array<i64: 2000000, 3000000>, drive_strength0 = 3 : i32, drive_strength1 = 1 : i32, {{.*}}primitive_name = "udp_nonansi"{{.*}}udp_metadata = {
