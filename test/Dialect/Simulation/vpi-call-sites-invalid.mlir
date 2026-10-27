// RUN: talos-opt %s -split-input-file -verify-diagnostics -o /dev/null
module {
 simulation.design @zero {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.code_unit.decl 45 in 1 task hierarchy "top.task"
  simulation.code_unit.decl 46 in 1 function hierarchy "top.function"
  // expected-error @+1 {{call-site and code-unit IDs must be nonzero}}
  simulation.vpi_call.decl @x id 0 in 44 scope 1 type 19 name "f"
 }
}

// -----

module {
 simulation.design @kind {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.code_unit.decl 45 in 1 task hierarchy "top.task"
  simulation.code_unit.decl 46 in 1 function hierarchy "top.function"
  // expected-error @+1 {{kind must identify an exact task/function call}}
  simulation.vpi_call.decl @x id 1 in 44 scope 1 type 38 name "f"
 }
}

// -----

module {
 simulation.design @name {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.code_unit.decl 45 in 1 task hierarchy "top.task"
  simulation.code_unit.decl 46 in 1 function hierarchy "top.function"
  // expected-error @+1 {{requires the nonempty source callee name}}
  simulation.vpi_call.decl @x id 1 in 44 scope 1 type 19 name ""
 }
}

// -----

module {
 simulation.design @owner {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.code_unit.decl 45 in 1 task hierarchy "top.task"
  simulation.code_unit.decl 46 in 1 function hierarchy "top.function"
  // expected-error @+1 {{owner must name a code unit in its physical scope}}
  simulation.vpi_call.decl @x id 1 in 45 scope 0 type 19 name "f"
 }
}

// -----

module {
 simulation.design @alias {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.code_unit.decl 45 in 1 task hierarchy "top.task"
  simulation.code_unit.decl 46 in 1 function hierarchy "top.function"
  // expected-error @+1 {{statement alias must name the same call kind and owner}}
  simulation.vpi_call.decl @x id 1 in 44 scope 1 type 19 name "f" statement 999
 }
}

// -----

module {
 simulation.design @within {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.code_unit.decl 45 in 1 task hierarchy "top.task"
  simulation.code_unit.decl 46 in 1 function hierarchy "top.function"
  // expected-error @+1 {{enclosing statement must have the same lexical owner}}
  simulation.vpi_call.decl @x id 1 in 44 scope 1 type 19 name "f" within 999
 }
}

// -----

module {
 simulation.design @callee {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.code_unit.decl 45 in 1 task hierarchy "top.task"
  simulation.code_unit.decl 46 in 1 function hierarchy "top.function"
  // expected-error @+1 {{callee must name the corresponding task/function declaration}}
  simulation.vpi_call.decl @x id 1 in 44 scope 1 type 19 name "f" callee 45
 }
}

// -----

module {
 simulation.design @system {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.code_unit.decl 45 in 1 task hierarchy "top.task"
  simulation.code_unit.decl 46 in 1 function hierarchy "top.function"
  // expected-error @+1 {{system call cannot name a source code-unit callee}}
  simulation.vpi_call.decl @x id 1 in 44 scope 1 type 56 name "$f" callee 46
 }
}

// -----

module {
 simulation.design @indirect {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.code_unit.decl 45 in 1 task hierarchy "top.task"
  simulation.code_unit.decl 46 in 1 function hierarchy "top.function"
  // expected-error @+1 {{only a user task/function call may use indirect dispatch}}
  simulation.vpi_call.decl @x id 1 in 44 scope 1 type 56 name "$f" {is_indirect}
 }
}

// -----

module {
 simulation.design @missing {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.code_unit.decl 45 in 1 task hierarchy "top.task"
  simulation.code_unit.decl 46 in 1 function hierarchy "top.function"

  simulation.func @caller(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 44 : i64, entry_kind = 1 : i32} {
   // expected-error @+1 {{VPI call site must name a simulation.vpi_call.decl}}
   simulation.call @callee(%ctx) {simulation.vpi_call_site = @missing} : (!simulation.context) -> ()
   simulation.return
  }
  simulation.func private @callee(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 46 : i64, entry_kind = 8 : i32} {
   simulation.return
  }
 }
}

// -----

module {
 simulation.design @mismatch {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.code_unit.decl 45 in 1 task hierarchy "top.task"
  simulation.code_unit.decl 46 in 1 function hierarchy "top.function"
  simulation.vpi_call.decl @task id 1 in 44 scope 1 type 60 name "task" callee 45
  simulation.func @caller(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 44 : i64, entry_kind = 1 : i32} {
   // expected-error @+1 {{VPI call site task/function kind disagrees with executable call}}
   simulation.call @callee(%ctx) {simulation.vpi_call_site = @task} : (!simulation.context) -> ()
   simulation.return
  }
  simulation.func private @callee(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 46 : i64, entry_kind = 8 : i32} {
   simulation.return
  }
 }
}

// -----

module {
  simulation.design @wrong_direct_target {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 1 function hierarchy "top.functionA"
    simulation.code_unit.decl 46 in 1 function hierarchy "top.functionB"
    simulation.vpi_call.decl @call id 1 in 44 scope 1 type 19 name "functionA" callee 45
    simulation.func @caller(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 44 : i64, entry_kind = 1 : i32} {
      // expected-error @+1 {{VPI direct call target disagrees with the executable code unit}}
      simulation.call @functionB(%ctx) {simulation.vpi_call_site = @call} : (!simulation.context) -> ()
      simulation.return
    }
    simulation.func private @functionB(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 46 : i64, entry_kind = 8 : i32} {
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @virtual_claimed_direct {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 46 in 1 function hierarchy "top.Base.get"
    simulation.class.decl @Base id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.method @Base_get of @Base slot 0 signature_id 17
        implemented_by @base_get :
        (!simulation.context, !simulation.class_handle<@Base>) -> i64 {
      is_final = false, is_pure = false, is_static = false,
      is_task = false, is_virtual = true
    }
    simulation.vpi_call.decl @call id 1 in 44 scope 1 type 648 name "get" callee 46
    simulation.func @caller(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 44 : i64, entry_kind = 1 : i32} {
      %object = simulation.class.alloc %ctx : !simulation.context -> !simulation.class_handle<@Base>
      // expected-error @+1 {{virtual executable call requires an indirect VPI call site}}
      %result = simulation.class.virtual_call %object[@Base_get] slot 0 signature_id 17() {simulation.vpi_call_site = @call} : (!simulation.class_handle<@Base>) -> i64
      simulation.return
    }
    simulation.func private @base_get(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@Base> {simulation.capture_kind = 1 : i32}) -> i64 attributes {code_unit_id = 46 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 1 : i64
      simulation.return %value : i64
    }
  }
}
