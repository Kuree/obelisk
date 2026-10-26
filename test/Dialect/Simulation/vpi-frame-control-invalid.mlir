// RUN: obelisk-opt %s --split-input-file --verify-diagnostics
module {
  simulation.design @invalid {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "f"
    simulation.func @f(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      // expected-error @+1 {{control target ID must be positive}}
      %selected = simulation.control.disable_selected 0
      simulation.return
    }
  }
}
// -----
module {
  simulation.design @invalid {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "f"
    simulation.func @f(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %activation = simulation.control.enter 1
      // expected-error @+1 {{hierarchical disable must not name one activation token}}
      %selected = simulation.control.disable_selected 1 activation %activation {hierarchical = true}
      simulation.return
    }
  }
}
// -----
module {
  simulation.design @invalid {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "f"
    simulation.func @f(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %activation = simulation.control.enter 1
      // expected-error @+1 {{requires a synchronous function entry}}
      simulation.control.local_boundary %activation exit ^exit body ^body
    ^body:
      cf.br ^exit
    ^exit:
      simulation.return
    }
  }
}
// -----
module {
  simulation.design @invalid {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "f"
    simulation.func @f(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %activation = simulation.control.enter 1
      // expected-error @+1 {{requires distinct argument-free local exit and body blocks}}
      simulation.control.local_boundary %activation exit ^exit body ^exit
    ^exit:
      simulation.return
    }
  }
}
