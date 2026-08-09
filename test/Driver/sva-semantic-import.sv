// RUN: obelisk -emit-slang %s | FileCheck %s --check-prefix=SLANG
// RUN: obelisk -emit-obelisk %s | FileCheck %s --check-prefix=OBELISK

module sva_semantic_import(
    input logic clk,
    input logic rst_n,
    input logic req,
    input logic ack,
    input int initial_count);
  default clocking cb @(posedge clk);
  endclocking
  default disable iff (!rst_n);

  sequence response;
    ack;
  endsequence

  property typed(event sampling, logic request, sequence consequent);
    @sampling request |-> ##[1:3] consequent;
  endproperty

  property defaulted(logic request = req);
    request |-> ##1 ack;
  endproperty

  sequence with_local(logic request);
    int seen = 0;
    (request, seen = seen + 1) ##1 ack;
  endsequence

  property recursive(int countdown);
    @(posedge clk) countdown == 0 or (countdown > 0 |=> recursive(countdown));
  endproperty

  typed_a: assert property (typed(posedge clk, req, response));
  defaulted_a: assert property (defaulted());
  local_a: assert property (with_local(req));
  recursive_a: assert property (recursive(initial_count));
endmodule

// SLANG: slang.symbol.clocking_block
// SLANG-SAME: is_default = true
// SLANG-SAME: is_global = false
// SLANG-SAME: name = "cb"
// SLANG: slang.symbol.sequence
// SLANG-SAME: default_clocking_path = "sva_semantic_import.cb"
// SLANG-SAME: has_default_instance = true
// SLANG-SAME: name = "response"
// SLANG-SAME: port_count = 0 : i64
// SLANG: slang.symbol.property
// SLANG-SAME: default_clocking_path = "sva_semantic_import.cb"
// SLANG-SAME: has_default_instance = false
// SLANG-SAME: name = "typed"
// SLANG-SAME: port_count = 3 : i64
// SLANG-SAME: port_paths = ["sva_semantic_import.typed.sampling", "sva_semantic_import.typed.request", "sva_semantic_import.typed.consequent"]
// SLANG: slang.symbol.assertion_port
// SLANG-SAME: has_default_value = false
// SLANG-SAME: semantic_type = !slang.event
// SLANG: slang.symbol.assertion_port
// SLANG-SAME: has_default_value = false
// SLANG-SAME: semantic_type = !slang.integral<1, false, true
// SLANG: slang.symbol.assertion_port
// SLANG-SAME: has_default_value = false
// SLANG-SAME: semantic_type = !slang.sequence
// SLANG: slang.symbol.property
// SLANG-SAME: has_default_instance = true
// SLANG-SAME: name = "defaulted"
// SLANG: slang.symbol.assertion_port
// SLANG-SAME: has_default_value = true
// SLANG: slang.statement.concurrent_assertion
// SLANG-SAME: default_clocking_path = "sva_semantic_import.cb"
// SLANG-SAME: has_default_disable = true
// SLANG: slang.expression.unary_op
// SLANG: slang.expression.assertion_instance
// SLANG-SAME: argument_count = 3 : i64
// SLANG-SAME: argument_formal_paths = ["sva_semantic_import.typed.sampling", "sva_semantic_import.typed.request", "sva_semantic_import.typed.consequent"]
// SLANG-SAME: argument_kinds = array<i64: 2, 0, 1>
// SLANG-SAME: has_expanded_body = true
// SLANG-SAME: referenced_path = "sva_semantic_import.typed"
// SLANG: slang.assertion.clocking
// SLANG: slang.expression.assertion_instance attributes {{.*}}local_variable_count = 1 : i64
// SLANG-SAME: local_variable_has_initializer = array<i64: 1>
// SLANG-SAME: local_variable_paths = ["sva_semantic_import.with_local.seen"]
// SLANG-SAME: referenced_path = "sva_semantic_import.with_local"
// SLANG: slang.expression.assertion_instance attributes {{.*}}has_expanded_body = false
// SLANG-SAME: is_recursive_property = true
// SLANG-SAME: referenced_path = "sva_semantic_import.recursive"

// OBELISK: obelisk.sv.symbol.clocking_block
// OBELISK-SAME: is_default = true
// OBELISK-SAME: is_global = false
// OBELISK: obelisk.sv.symbol.property
// OBELISK-SAME: default_clocking_path = "sva_semantic_import.cb"
// OBELISK-SAME: name = "typed"
// OBELISK-SAME: port_count = 3 : i64
// OBELISK: obelisk.sv.statement.concurrent_assertion
// OBELISK-SAME: default_clocking_path = "sva_semantic_import.cb"
// OBELISK-SAME: has_default_disable = true
// OBELISK: obelisk.sv.expression.assertion_instance
// OBELISK-SAME: argument_count = 3 : i64
// OBELISK-SAME: argument_kinds = array<i64: 2, 0, 1>
// OBELISK-SAME: referenced_path = "sva_semantic_import.typed"
// OBELISK: obelisk.sv.expression.assertion_instance attributes {{.*}}has_expanded_body = false
// OBELISK-SAME: is_recursive_property = true
// OBELISK-SAME: referenced_path = "sva_semantic_import.recursive"
