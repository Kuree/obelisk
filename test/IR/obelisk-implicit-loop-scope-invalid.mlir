// RUN: obelisk-opt %s --split-input-file --verify-diagnostics -o /dev/null

module {
  // expected-error @+1 {{implicit loop scope requires a sequential block and exact block symbol}}
  obelisk.sv.statement.block attributes {is_implicit_loop_scope, node_id = 1 : i64} {
    obelisk.sv.statement.for_loop attributes {initializer_count = 0 : i64, has_condition = false, step_count = 0 : i64, node_id = 2 : i64} {
      obelisk.sv.statement.empty attributes {node_id = 3 : i64} {}
    }
  }
}

// -----

module {
  // expected-error @+1 {{implicit loop scope requires one loop after its declarations}}
  obelisk.sv.statement.block attributes {is_implicit_loop_scope, block_symbol = @scope, node_id = 1 : i64} {

  }
}

// -----

module {
  // expected-error @+1 {{implicit for scope requires local loop declarations}}
  obelisk.sv.statement.block attributes {is_implicit_loop_scope, block_symbol = @scope, node_id = 1 : i64} {
    obelisk.sv.statement.for_loop attributes {initializer_count = 0 : i64, has_condition = false, step_count = 0 : i64, node_id = 2 : i64} {
      obelisk.sv.statement.empty attributes {node_id = 3 : i64} {}
    }
  }
}

// -----

module {
  // expected-error @+1 {{implicit for scope requires local loop declarations}}
  obelisk.sv.statement.block attributes {is_implicit_loop_scope, block_symbol = @scope, node_id = 1 : i64} {
    obelisk.sv.statement.for_loop attributes {has_local_variables = true, initializer_count = 0 : i64, has_condition = false, step_count = 0 : i64, node_id = 2 : i64} {
      obelisk.sv.statement.empty attributes {node_id = 3 : i64} {}
    }
  }
}

// -----

module {
  // expected-error @+1 {{implicit loop scope prefix must contain only variable declarations}}
  obelisk.sv.statement.block attributes {is_implicit_loop_scope, block_symbol = @scope, node_id = 1 : i64} {
    obelisk.sv.statement.for_loop attributes {initializer_count = 0 : i64, has_condition = false, step_count = 0 : i64, node_id = 2 : i64} {
      obelisk.sv.statement.empty attributes {node_id = 3 : i64} {}
    }
    obelisk.sv.statement.for_loop attributes {initializer_count = 0 : i64, has_condition = false, step_count = 0 : i64, node_id = 4 : i64} {
      obelisk.sv.statement.empty attributes {node_id = 5 : i64} {}
    }
  }
}

// -----

module {
  // expected-error @+1 {{local loop declarations and expression initializers are mutually exclusive}}
  obelisk.sv.statement.for_loop attributes {has_local_variables = true, initializer_count = 1 : i64, has_condition = false, step_count = 0 : i64, node_id = 1 : i64} {
    obelisk.sv.statement.empty attributes {node_id = 2 : i64} {}
    obelisk.sv.statement.empty attributes {node_id = 3 : i64} {}
  }
}
