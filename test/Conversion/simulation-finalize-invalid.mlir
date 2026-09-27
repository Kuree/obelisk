// RUN: obelisk-opt %s --split-input-file --verify-diagnostics \
// RUN:   --pass-pipeline='builtin.module(obelisk-sim-finalize)'

module {
  // expected-error @+1 {{operation from dialect 'func' survived simulation finalization}}
  func.func @unexpected() {
    // expected-error @+1 {{operation from dialect 'func' survived simulation finalization}}
    return
  }
}

// -----

module {
  simulation.design @illegal_reference {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.illegal_reference.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %zero = arith.constant 0 : i8
      // expected-error @+1 {{operation from dialect 'builtin' survived simulation finalization}}
      %illegal = builtin.unrealized_conversion_cast %zero : i8 to i16
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @forbidden_type {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.forbidden_type.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %float = arith.constant 0.0 : f32
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @forbidden_block_argument {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.forbidden_block_argument.bad.9000001"
    simulation.scope.decl 0
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      simulation.return
    ^unreachable(%bad_argument: f32):
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @illegal_reference {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.illegal_reference.bad.9000001"
    simulation.scope.decl 0
    // expected-error @+1 {{disallowed symbol reference @semantic_path}}
    simulation.func @bad(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {bad_metadata = @semantic_path, entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      simulation.return
    }
  }
}
