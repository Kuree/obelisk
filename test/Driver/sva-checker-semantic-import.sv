// RUN: obelisk -emit-slang %s | FileCheck %s --check-prefix=SLANG
// RUN: obelisk -emit-obelisk %s | FileCheck %s --check-prefix=OBELISK

checker handshake_checker(
    event clock_event,
    logic reset_n,
    logic request,
    logic acknowledge,
    output bit completed = 0);
  default clocking cb @clock_event;
  endclocking
  default disable iff (!reset_n);

  property response;
    request |-> ##[1:2] acknowledge;
  endproperty

  always_comb completed = acknowledge;
  response_a: assert property (response);
endchecker

module sva_checker_semantic_import(
    input logic clk,
    input logic rst_n,
    input logic req,
    input logic ack);
  bit done;
  handshake_checker checker_i(posedge clk, rst_n, req, ack, done);

  initial begin
    automatic bit procedural_done;
    handshake_checker procedural_i(posedge clk, rst_n, req, ack,
                                   procedural_done);
  end
endmodule

// SLANG: slang.symbol.checker
// SLANG-SAME: name = "handshake_checker"
// SLANG-SAME: port_count = 5 : i64
// SLANG-SAME: port_paths = ["handshake_checker.clock_event", "handshake_checker.reset_n", "handshake_checker.request", "handshake_checker.acknowledge", "handshake_checker.completed"]
// SLANG-SAME: port_symbols = [
// SLANG: slang.symbol.checker_instance
// SLANG-SAME: connection_actual_kinds = array<i64: 2, 0, 0, 0, 0>
// SLANG-SAME: connection_attribute_counts = array<i64: 0, 0, 0, 0, 0>
// SLANG-SAME: connection_count = 5 : i64
// SLANG-SAME: connection_has_actual = array<i64: 1, 1, 1, 1, 1>
// SLANG-SAME: connection_has_output_initial = array<i64: 0, 0, 0, 0, 1>
// SLANG-SAME: is_procedural = false
// SLANG-SAME: name = "checker_i"
// SLANG-SAME: referenced_checker_path = "handshake_checker"
// SLANG-SAME: referenced_checker_symbol =
// SLANG: slang.symbol.checker_instance_body
// SLANG-SAME: instance_depth = 0 : i64
// SLANG-SAME: instance_flags = 0 : i64
// SLANG-SAME: is_procedural = false
// SLANG-SAME: parent_instance_path = "sva_checker_semantic_import.checker_i"
// SLANG-SAME: parent_instance_symbol =
// SLANG-SAME: referenced_checker_path = "handshake_checker"
// SLANG: slang.symbol.clocking_block
// SLANG-SAME: hierarchical_name = "sva_checker_semantic_import.checker_i.cb"
// SLANG-SAME: is_default = true
// SLANG: slang.symbol.property
// SLANG-SAME: default_clocking_path = "sva_checker_semantic_import.checker_i.cb"
// SLANG-SAME: name = "response"
// SLANG: slang.statement.concurrent_assertion
// SLANG-SAME: default_clocking_path = "sva_checker_semantic_import.checker_i.cb"
// SLANG-SAME: has_default_disable = true
// SLANG: slang.symbol.checker_instance
// SLANG-SAME: is_procedural = true
// SLANG-SAME: name = "procedural_i"
// SLANG-SAME: referenced_checker_path = "handshake_checker"
// SLANG: slang.symbol.checker_instance_body
// SLANG-SAME: is_procedural = true
// SLANG-SAME: parent_instance_path = "sva_checker_semantic_import.procedural_i"
// SLANG: slang.statement.procedural_checker
// SLANG-SAME: instance_count = 1 : i64
// SLANG-SAME: instance_paths = ["sva_checker_semantic_import.procedural_i"]
// SLANG-SAME: instance_symbols = [

// OBELISK: obelisk.sv.symbol.checker
// OBELISK-SAME: port_count = 5 : i64
// OBELISK-SAME: port_paths = ["handshake_checker.clock_event", "handshake_checker.reset_n", "handshake_checker.request", "handshake_checker.acknowledge", "handshake_checker.completed"]
// OBELISK: obelisk.sv.symbol.checker_instance
// OBELISK-SAME: connection_actual_kinds = array<i64: 2, 0, 0, 0, 0>
// OBELISK-SAME: connection_count = 5 : i64
// OBELISK-SAME: connection_has_output_initial = array<i64: 0, 0, 0, 0, 1>
// OBELISK-SAME: is_procedural = false
// OBELISK-SAME: referenced_checker_path = "handshake_checker"
// OBELISK: obelisk.sv.symbol.checker_instance_body
// OBELISK-SAME: is_procedural = false
// OBELISK-SAME: parent_instance_path = "sva_checker_semantic_import.checker_i"
// OBELISK-SAME: referenced_checker_path = "handshake_checker"
// OBELISK: obelisk.sv.symbol.clocking_block
// OBELISK-SAME: hierarchical_name = "sva_checker_semantic_import.checker_i.cb"
// OBELISK: obelisk.sv.symbol.property
// OBELISK-SAME: default_clocking_path = "sva_checker_semantic_import.checker_i.cb"
// OBELISK: obelisk.sv.statement.concurrent_assertion
// OBELISK-SAME: has_default_disable = true
// OBELISK: obelisk.sv.symbol.checker_instance
// OBELISK-SAME: is_procedural = true
// OBELISK-SAME: name = "procedural_i"
// OBELISK: obelisk.sv.statement.procedural_checker
// OBELISK-SAME: instance_count = 1 : i64
// OBELISK-SAME: instance_paths = ["sva_checker_semantic_import.procedural_i"]
