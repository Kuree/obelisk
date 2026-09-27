// RUN: obelisk-opt %s -split-input-file -verify-diagnostics

module {
  simulation.design @negative {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.func @unit(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %true = arith.constant true
      // expected-error@+1 {{attribute 'point' failed to satisfy constraint: 64-bit signless integer attribute whose value is non-negative}}
      simulation.coverage.point_hit %ctx if %true[-1] : !simulation.context
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @missing_inventory {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.func @unit(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %true = arith.constant true
      // expected-error@+1 {{requires the module line-point inventory attribute 'obelisk.coverage.line_point_count'}}
      simulation.coverage.point_hit %ctx if %true[0] : !simulation.context
      simulation.return
    }
  }
}

// -----

module attributes {obelisk.coverage.line_point_count = 1 : i64} {
  simulation.design @out_of_range {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.func @unit(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %true = arith.constant true
      // expected-error@+1 {{point index is outside the module inventory}}
      simulation.coverage.point_hit %ctx if %true[1] : !simulation.context
      simulation.return
    }
  }
}

// -----

module attributes {obelisk.coverage.line_point_count = 1 : i128} {
  simulation.design @wrong_inventory_width {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.func @unit(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %true = arith.constant true
      // expected-error@+1 {{requires a 64-bit module line-point inventory}}
      simulation.coverage.point_hit %ctx if %true[0] : !simulation.context
      simulation.return
    }
  }
}
