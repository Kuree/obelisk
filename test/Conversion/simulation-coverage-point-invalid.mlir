// RUN: obelisk-opt %s -split-input-file -verify-diagnostics

module {
  obelisk_sim.design @negative {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.func @unit(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %true = arith.constant true
      // expected-error@+1 {{attribute 'point' failed to satisfy constraint: 64-bit signless integer attribute whose value is non-negative}}
      obelisk_sim.coverage.point_hit %ctx if %true[-1] : !obelisk_sim.context
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @missing_inventory {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.func @unit(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %true = arith.constant true
      // expected-error@+1 {{requires the module line-point inventory attribute 'obelisk.coverage.line_point_count'}}
      obelisk_sim.coverage.point_hit %ctx if %true[0] : !obelisk_sim.context
      obelisk_sim.return
    }
  }
}

// -----

module attributes {obelisk.coverage.line_point_count = 1 : i64} {
  obelisk_sim.design @out_of_range {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.func @unit(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %true = arith.constant true
      // expected-error@+1 {{point index is outside the module inventory}}
      obelisk_sim.coverage.point_hit %ctx if %true[1] : !obelisk_sim.context
      obelisk_sim.return
    }
  }
}

// -----

module attributes {obelisk.coverage.line_point_count = 1 : i128} {
  obelisk_sim.design @wrong_inventory_width {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.func @unit(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %true = arith.constant true
      // expected-error@+1 {{requires a 64-bit module line-point inventory}}
      obelisk_sim.coverage.point_hit %ctx if %true[0] : !obelisk_sim.context
      obelisk_sim.return
    }
  }
}
