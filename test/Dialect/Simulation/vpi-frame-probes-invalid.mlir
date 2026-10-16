// RUN: obelisk-opt %s -split-input-file -verify-diagnostics

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 1 function hierarchy "top.worker"
    simulation.statement.decl 11 in 44 scope 1 type 19
    simulation.statement.decl 12 in 45 scope 1 type 38
    simulation.vpi_call.decl @call id 20 in 44 scope 1 type 19 name "worker" statement 11 within 11 callee 45
    simulation.func @body(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} {
      // expected-error @+1 {{call must name a simulation.vpi_call.decl}}
      %t = simulation.vpi.call.prepare %ctx from @missing
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 1 function hierarchy "top.worker"
    simulation.statement.decl 11 in 44 scope 1 type 19
    simulation.statement.decl 12 in 45 scope 1 type 38
    simulation.vpi_call.decl @call id 20 in 44 scope 1 type 19 name "worker" statement 11 within 11 callee 45
    simulation.func @body(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 45 : i64} {
      // expected-error @+1 {{call belongs to a different lexical code unit}}
      %t = simulation.vpi.call.prepare %ctx from @call
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 1 function hierarchy "top.worker"
    simulation.statement.decl 11 in 44 scope 1 type 56
    simulation.statement.decl 12 in 45 scope 1 type 38
    simulation.vpi_call.decl @call id 20 in 44 scope 1 type 56 name "worker" statement 11 within 11
    simulation.func @body(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} {
      // expected-error @+1 {{call kind cannot be a procedural frame origin}}
      %t = simulation.vpi.call.prepare %ctx from @call
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 1 function hierarchy "top.worker"
    simulation.statement.decl 11 in 44 scope 1 type 19
    simulation.statement.decl 12 in 45 scope 1 type 38
    simulation.vpi_call.decl @call id 20 in 44 scope 1 type 19 name "worker" statement 11 within 11 callee 45
    simulation.func @body(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} {
      // expected-error @+1 {{entry must name its exact executable code unit}}
      %f = simulation.vpi.frame.enter %ctx unit 45
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 1 function hierarchy "top.worker"
    simulation.statement.decl 11 in 44 scope 1 type 19
    simulation.statement.decl 12 in 45 scope 1 type 38
    simulation.vpi_call.decl @call id 20 in 44 scope 1 type 19 name "worker" statement 11 within 11 callee 45
    simulation.func @body(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 45 : i64} {
      // expected-error @+1 {{entry task/function kind disagrees with its code unit}}
      %f = simulation.vpi.frame.enter_prepared %ctx unit 45 {is_task = true}
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 1 function hierarchy "top.worker"
    simulation.statement.decl 11 in 44 scope 1 type 19
    simulation.statement.decl 12 in 45 scope 1 type 38
    simulation.vpi_call.decl @call id 20 in 44 scope 1 type 19 name "worker" statement 11 within 11 callee 45
    simulation.func @body(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} {
      // expected-error @+1 {{references an unknown statement ID}}
      simulation.vpi.frame.checkpoint %ctx statement 99
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 1 function hierarchy "top.worker"
    simulation.statement.decl 11 in 44 scope 1 type 19
    simulation.statement.decl 12 in 45 scope 1 type 38
    simulation.vpi_call.decl @call id 20 in 44 scope 1 type 19 name "worker" statement 11 within 11 callee 45
    simulation.func @body(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} {
      // expected-error @+1 {{statement belongs to a different lexical code unit}}
      simulation.vpi.frame.checkpoint %ctx statement 12
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 1 function hierarchy "top.worker"
    simulation.statement.decl 11 in 44 scope 1 type 19
    simulation.statement.decl 12 in 45 scope 1 type 38
    simulation.vpi_call.decl @call id 20 in 44 scope 1 type 19 name "worker" statement 11 within 11 callee 45
    simulation.func @body(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} {
      // expected-error @+1 {{frame origin must name a statement scope}}
      %f = simulation.vpi.frame.enter %ctx unit 44 within 11
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 1 function hierarchy "top.worker"
    simulation.statement.decl 11 in 44 scope 1 type 19
    simulation.statement.decl 12 in 45 scope 1 type 38
    simulation.vpi_call.decl @call id 20 in 44 scope 1 type 19 name "worker" statement 11 within 11 callee 45
    simulation.func @body(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} {
      // expected-error @+1 {{code-unit ID must be positive}}
      %f = simulation.vpi.frame.enter %ctx unit 0
      simulation.return
    }
  }
}
