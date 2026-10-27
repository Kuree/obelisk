// RUN: talos-opt %s \
// RUN:   --pass-pipeline='builtin.module(talos-sim-prepare-coverage,simulation.design(symbol-dce))' \
// RUN:   | FileCheck %s

!int = !talos.integral<32, true, false, 31 : 0, int>

module attributes {coverage.metrics = ["line"]} {
  simulation.design @coverage {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 7 in 0 function hierarchy "top.f"
    simulation.func private @covered(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 7 : i64, entry_kind = 8 : i32} {
      talos.sv.statement.expression_statement attributes {
          node_id = 10 : i64,
          source_range = !talos.source_range<"dce.sv", 4, 3,
              "dce.sv", 4, 7, "">} {
        talos.sv.expression.integer_literal attributes {
            constant_value = "1", node_id = 11 : i64,
            semantic_type = !int} {
        }
      }
      simulation.return
    }
  }
}

// The explicit SymbolUser edge keeps an included post-inventory obligation
// executable through every later SymbolDCE pass without changing visibility.
// CHECK: simulation.design @coverage
// CHECK: simulation.coverage.keepalive @covered
// CHECK: simulation.func private @covered
// CHECK: coverage.line_point_index
