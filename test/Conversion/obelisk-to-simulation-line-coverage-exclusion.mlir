// RUN: obelisk-opt %s --obelisk-sim-prepare-coverage \
// RUN:   | FileCheck %s --check-prefix=INVENTORY
// RUN: obelisk-opt %s --obelisk-sim-prepare-coverage \
// RUN:   | %python %S/Inputs/dump-coverage-schema.py \
// RUN:   | FileCheck %s --check-prefix=SCHEMA
// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk-sim-prepare-coverage,obelisk-sim-prepare-unit-lowering,simulation.design(simulation.func(obelisk-sim-lower-unit)))' \
// RUN:   | FileCheck %s --check-prefix=LOWER

!int = !obelisk.integral<32, true, false, 31 : 0, int>

module attributes {
  obelisk.coverage.metrics = ["line"],
  obelisk.coverage.config = "{\22exclude\22:[{\22metrics\22:[\22line\22],\22file\22:\22excluded.sv\22,\22reason\22:\22generated wrapper\22}]}"
} {
  simulation.design @coverage {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 7 in 0 initial hierarchy "top.initial"
    simulation.func @unit(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 7 : i64, entry_kind = 1 : i32} {
      obelisk.sv.statement.expression_statement attributes {
          node_id = 10 : i64,
          source_range = !obelisk.source_range<"excluded.sv", 4, 3,
              "excluded.sv", 4, 7, "">} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "1", node_id = 11 : i64,
            semantic_type = !int} {
        }
      }
      simulation.return
    }
  }
}

// The excluded obligation remains in the schema denominator inventory but is
// not executable and therefore cannot accumulate misleading hit counts.
// INVENTORY: obelisk.coverage.line_point_count = 1 : i64
// INVENTORY-NOT: obelisk.coverage.line_point_index
// INVENTORY-NOT: simulation.coverage.keepalive
// SCHEMA: line_point id=[[POINT:[1-9][0-9]*]] file=excluded.sv end_file=excluded.sv scope={{[1-9][0-9]*}} macro= range=4:3-4:7 phase=0 flags=0
// SCHEMA: exclusion entity=[[POINT]] metric=1 reason=generated wrapper
// LOWER-NOT: simulation.coverage.point_hit
// LOWER-NOT: obelisk.sv.statement
