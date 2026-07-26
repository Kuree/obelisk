// RUN: obelisk-opt %s --split-input-file --verify-diagnostics \
// RUN:   --pass-pipeline='builtin.module(obelisk-sim-finalize)'

module {
  // expected-error @+1 {{operation from dialect 'func' survived obelisk_sim finalization}}
  func.func @unexpected() {
    // expected-error @+1 {{operation from dialect 'func' survived obelisk_sim finalization}}
    return
  }
}

// -----

module {
  obelisk_sim.design @illegal_reference {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.illegal_reference.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %zero = arith.constant 0 : i8
      // expected-error @+1 {{operation from dialect 'builtin' survived obelisk_sim finalization}}
      %illegal = builtin.unrealized_conversion_cast %zero : i8 to i16
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @forbidden_type {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.forbidden_type.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %float = arith.constant 0.0 : f32
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @forbidden_block_argument {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.forbidden_block_argument.bad.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      obelisk_sim.return
    ^unreachable(%bad_argument: f32):
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @illegal_reference {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.illegal_reference.bad.9000001"
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{disallowed symbol reference @semantic_path}}
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {bad_metadata = @semantic_path, entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      obelisk_sim.return
    }
  }
}
