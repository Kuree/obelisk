// RUN: obelisk-opt %s --obelisk-sim-prepare-coverage | FileCheck %s --check-prefix=INVENTORY
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk-sim-prepare-coverage,obelisk-sim-prepare-unit-lowering,simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s --check-prefix=LOWER
// RUN: obelisk-opt %s --obelisk-sim-prepare-coverage \
// RUN:   | %python %S/Inputs/dump-coverage-schema.py \
// RUN:   | FileCheck %s --check-prefix=SCHEMA

!int = !obelisk.integral<32, true, false, 31 : 0, int>

module attributes {
  obelisk.coverage.metrics = ["line"]
} {
  simulation.design @coverage {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" source_definition "DUT"
    simulation.code_unit.decl 7 in 1 initial hierarchy "top.initial"

    simulation.func @unit(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 7 : i64, entry_kind = 1 : i32} {
      obelisk.sv.statement.expression_statement attributes {
          node_id = 10 : i64,
          source_range = !obelisk.source_range<"coverage.sv", 4, 3,
              "coverage.sv", 4, 7, "">} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "1", node_id = 11 : i64,
            semantic_type = !int} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {
          macro_expansion_stack = [{
            definition = !obelisk.source_range<"nested-a.svh", 2, 1,
                "nested-a.svh", 2, 1, "">,
            invocation = !obelisk.source_range<"macro-a.svh", 1, 1,
                "macro-a.svh", 1, 5, "">,
            name = "M"
          }],
          node_id = 12 : i64,
          original_source_range = !obelisk.source_range<"macro-a.svh", 1, 1,
              "macro-a.svh", 1, 5, "M">,
          source_range = !obelisk.source_range<"coverage.sv", 4, 9,
              "coverage.sv", 4, 13, "M">} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "2", node_id = 13 : i64,
            semantic_type = !int} {
        }
      }
      // This deliberately shares the expansion range and frontend node ID.
      // Its distinct definition range must still produce a distinct stable
      // point rather than a hash collision.
      obelisk.sv.statement.expression_statement attributes {
          macro_expansion_stack = [{
            definition = !obelisk.source_range<"nested-b.svh", 2, 1,
                "nested-b.svh", 2, 1, "">,
            invocation = !obelisk.source_range<"macro-a.svh", 1, 1,
                "macro-a.svh", 1, 5, "">,
            name = "M"
          }],
          node_id = 12 : i64,
          original_source_range = !obelisk.source_range<"macro-a.svh", 1, 1,
              "macro-a.svh", 1, 5, "M">,
          source_range = !obelisk.source_range<"coverage.sv", 4, 9,
              "coverage.sv", 4, 13, "M">} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "3", node_id = 14 : i64,
            semantic_type = !int} {
        }
      }
      // This shares the invocation and expansion stack with the preceding
      // point but has a distinct fully original range. Its expansion stack is
      // intentionally attached only to the nested expression.
      obelisk.sv.statement.expression_statement attributes {
          node_id = 12 : i64,
          original_source_range = !obelisk.source_range<"macro-b.svh", 1, 1,
              "macro-b.svh", 1, 5, "M">,
          source_range = !obelisk.source_range<"coverage.sv", 4, 9,
              "coverage.sv", 4, 13, "M">} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "4",
            macro_expansion_stack = [{
              definition = !obelisk.source_range<"nested-b.svh", 2, 1,
                  "nested-b.svh", 2, 1, "">,
              invocation = !obelisk.source_range<"macro-a.svh", 1, 1,
                  "macro-a.svh", 1, 5, "">,
              name = "M"
            }],
            node_id = 15 : i64,
            semantic_type = !int} {
        }
      }
      simulation.return
    }
  }
}

// INVENTORY: module attributes {
// INVENTORY-SAME: obelisk.coverage.line_point_count = 4 : i64
// INVENTORY-SAME: obelisk.execution.coverage_schema_blob = array<i8:
// INVENTORY: simulation.scope.decl 1 parent 0 hierarchy "top" source_definition "DUT" coverage_id {{-?[1-9][0-9]*}}
// INVENTORY-COUNT-4: obelisk.coverage.line_point_index = {{[0-3]}} : i64

// SCHEMA: scope id={{[1-9][0-9]*}} parent={{[1-9][0-9]*}} name=top kind=0 definition=DUT

// LOWER-LABEL: simulation.func @unit
// LOWER-COUNT-4: simulation.coverage.point_hit %arg0 if {{.*}}[{{[0-3]}}]
// LOWER-NOT: obelisk.sv.statement
