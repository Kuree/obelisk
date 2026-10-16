// RUN: obelisk-opt %s --split-input-file --verify-diagnostics
#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.code_unit.decl 45 in 0 function hierarchy "work"
    simulation.code_unit.decl 46 in 0 function hierarchy "other"
    simulation.storage.decl 0 in 0 : i32 static hierarchy "work.x" debug "x" {vpi_type = #word}

    // expected-error @+1 {{requires static storage in the exact source code unit's physical scope}}
    simulation.vpi_static_local.bind 9 in 45 ordinal 0
  }
}

// -----
#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.code_unit.decl 45 in 0 function hierarchy "work"
    simulation.code_unit.decl 46 in 0 function hierarchy "other"
    simulation.storage.decl 0 in 0 : i32 automatic hierarchy "work.x" debug "x" {vpi_type = #word}

    // expected-error @+1 {{requires static storage in the exact source code unit's physical scope}}
    simulation.vpi_static_local.bind 0 in 45 ordinal 0
  }
}

// -----
#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.code_unit.decl 45 in 0 function hierarchy "work"
    simulation.code_unit.decl 46 in 0 function hierarchy "other"
    simulation.storage.decl 0 in 0 : i32 static hierarchy "work.x" debug "x" {vpi_type = #word}
    simulation.statement.decl 11 in 46 scope 0 type 4 {is_scope}
    // expected-error @+1 {{statement scope must belong to the exact lexical code unit}}
    simulation.vpi_static_local.bind 0 in 45 within 11 ordinal 0
  }
}

// -----
#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.code_unit.decl 45 in 0 function hierarchy "work"
    simulation.code_unit.decl 46 in 0 function hierarchy "other"
    simulation.storage.decl 0 in 0 : i32 static hierarchy "work.x" debug "x" {vpi_type = #word}
    simulation.vpi_static_local.bind 0 in 45 ordinal 0
    // expected-error @+1 {{duplicates a static local storage binding}}
    simulation.vpi_static_local.bind 0 in 45 ordinal 1
  }
}

// -----
#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.code_unit.decl 45 in 0 function hierarchy "work"
    simulation.code_unit.decl 46 in 0 function hierarchy "other"
    simulation.storage.decl 0 in 0 : i32 static hierarchy "work.x" debug "x" {vpi_type = #word}

    // expected-error @+1 {{code-unit ID must be positive}}
    simulation.vpi_static_local.bind 0 in 0 ordinal 0
  }
}

// -----
#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  // expected-error @+1 {{procedural source declaration ordinals must be dense and unique within each scope}}
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.code_unit.decl 45 in 0 function hierarchy "work"
    simulation.code_unit.decl 46 in 0 function hierarchy "other"
    simulation.storage.decl 0 in 0 : i32 static hierarchy "work.x" debug "x" {vpi_type = #word}

    simulation.vpi_static_local.bind 0 in 45 ordinal 1
  }
}
