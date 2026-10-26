// RUN: obelisk-opt %s --split-input-file --verify-diagnostics -o /dev/null
#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @binding {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 1 in 1 task hierarchy "top.task"
    simulation.code_unit.decl 2 in 1 task hierarchy "top.other"
    simulation.vpi_local.decl @local id 19 in 1 scope 1 ordinal 0 hierarchy "top.task.value" debug "value" : i32 {vpi_type = #word}
    simulation.func @test(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 12 : i32, code_unit_id = 1 : i64} {
      %value = arith.constant 23 : i32
      %local = simulation.ref.alloc %value : i32 -> !simulation.ref<i32>
      // expected-error @+1 {{declaration must name a simulation.vpi_local.decl}}
      simulation.vpi.frame.bind %ctx, %local to @missing : !simulation.ref<i32>
      simulation.return
    }
  }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @binding {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 1 in 1 task hierarchy "top.task"
    simulation.code_unit.decl 2 in 1 task hierarchy "top.other"
    simulation.vpi_local.decl @local id 19 in 1 scope 1 ordinal 0 hierarchy "top.task.value" debug "value" : i32 {vpi_type = #word}
    simulation.func @test(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 12 : i32, code_unit_id = 1 : i64} {
      %value = arith.constant 23 : i64
      %local = simulation.ref.alloc %value : i64 -> !simulation.ref<i64>
      // expected-error @+1 {{automatic reference type must match its local declaration}}
      simulation.vpi.frame.bind %ctx, %local to @local : !simulation.ref<i64>
      simulation.return
    }
  }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @binding {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 1 in 1 task hierarchy "top.task"
    simulation.code_unit.decl 2 in 1 task hierarchy "top.other"
    simulation.vpi_local.decl @local id 19 in 1 scope 1 ordinal 0 hierarchy "top.task.value" debug "value" : i32 {vpi_type = #word}
    simulation.func @test(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 12 : i32, code_unit_id = 2 : i64} {
      %value = arith.constant 23 : i32
      %local = simulation.ref.alloc %value : i32 -> !simulation.ref<i32>
      // expected-error @+1 {{local declaration belongs to a different lexical code unit}}
      simulation.vpi.frame.bind %ctx, %local to @local : !simulation.ref<i32>
      simulation.return
    }
  }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @binding {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 1 in 1 task hierarchy "top.task"
    simulation.code_unit.decl 2 in 1 task hierarchy "top.other"
    simulation.vpi_local.decl @local id 19 in 1 scope 1 ordinal 0 hierarchy "top.task.value" debug "value" : i32 {vpi_type = #word}
    simulation.func @test(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 12 : i32} {
      %value = arith.constant 23 : i32
      %local = simulation.ref.alloc %value : i32 -> !simulation.ref<i32>
      // expected-error @+1 {{requires an exact executable code-unit owner}}
      simulation.vpi.frame.bind %ctx, %local to @local : !simulation.ref<i32>
      simulation.return
    }
  }
}
