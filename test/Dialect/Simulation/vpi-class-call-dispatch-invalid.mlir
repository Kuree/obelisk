// RUN: talos-opt %s --split-input-file --verify-diagnostics

module {
  simulation.design @invalid {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "initial"
    simulation.class.decl @C id 1 {is_abstract = true, is_final = false, is_interface = false}
    simulation.class.decl @D id 2 {is_abstract = true, is_final = false, is_interface = false}
    simulation.class.method @method of @C slot 0 signature_id 17 : (!simulation.context, !simulation.class_handle<@C>) -> i32 {is_final = false, is_pure = true, is_static = false, is_task = false, is_virtual = true}
    simulation.vpi_call.decl @call id 1 in 1 scope 0 type 648 name "f" {}

    // expected-error @+1 {{must name an indirect VPI call declaration}}
    simulation.vpi_class_call_dispatch @call on !simulation.class_handle<@C> via @method
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "initial"
    simulation.class.decl @C id 1 {is_abstract = true, is_final = false, is_interface = false}
    simulation.class.decl @D id 2 {is_abstract = true, is_final = false, is_interface = false}
    simulation.class.method @method of @C slot 0 signature_id 17 : (!simulation.context, !simulation.class_handle<@C>) -> i32 {is_final = false, is_pure = true, is_static = false, is_task = false, is_virtual = true}
    simulation.vpi_call.decl @call id 1 in 1 scope 0 type 648 name "f" {is_indirect}

    // expected-error @+1 {{must name an indirect VPI call declaration}}
    simulation.vpi_class_call_dispatch @missing on !simulation.class_handle<@C> via @method
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "initial"
    simulation.class.decl @C id 1 {is_abstract = true, is_final = false, is_interface = false}
    simulation.class.decl @D id 2 {is_abstract = true, is_final = false, is_interface = false}
    simulation.class.method @method of @C slot 0 signature_id 17 : (!simulation.context, !simulation.class_handle<@C>) -> i32 {is_final = false, is_pure = true, is_static = false, is_task = false, is_virtual = true}
    simulation.vpi_call.decl @call id 1 in 1 scope 0 type 648 name "f" {is_indirect}

    // expected-error @+1 {{receiver type disagrees with the virtual method ABI}}
    simulation.vpi_class_call_dispatch @call on !simulation.class_handle<@D> via @method
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "initial"
    simulation.class.decl @C id 1 {is_abstract = true, is_final = false, is_interface = false}
    simulation.class.decl @D id 2 {is_abstract = true, is_final = false, is_interface = false}
    simulation.class.method @method of @C slot 0 signature_id 17 : (!simulation.context, !simulation.class_handle<@C>) -> i32 {is_final = false, is_pure = true, is_static = false, is_task = false, is_virtual = true}
    simulation.vpi_call.decl @call id 1 in 1 scope 0 type 649 name "f" {is_indirect}

    // expected-error @+1 {{method task/function kind disagrees with the call}}
    simulation.vpi_class_call_dispatch @call on !simulation.class_handle<@C> via @method
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "initial"
    simulation.class.decl @C id 1 {is_abstract = true, is_final = false, is_interface = false}
    simulation.class.decl @D id 2 {is_abstract = true, is_final = false, is_interface = false}
    simulation.class.method @method of @C slot 0 signature_id 17 : (!simulation.context, !simulation.class_handle<@C>) -> i32 {is_final = false, is_pure = true, is_static = false, is_task = false, is_virtual = true}
    simulation.vpi_call.decl @call id 1 in 1 scope 0 type 648 name "f" {is_indirect}
    simulation.vpi_class_call_dispatch @call on !simulation.class_handle<@C> via @method
    // expected-error @+1 {{duplicates a class call dispatch contract}}
    simulation.vpi_class_call_dispatch @call on !simulation.class_handle<@C> via @method
  }
}

// -----
module {
  simulation.design @invalid {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "initial"
    simulation.class.decl @C id 1 {is_abstract = true, is_final = false, is_interface = false}
    simulation.class.method @method of @C slot 0 signature_id 17 : (!simulation.context, !simulation.class_handle<@C>) -> i32 {is_final = false, is_pure = true, is_static = false, is_task = false, is_virtual = true}
    simulation.class.method @other of @C slot 1 signature_id 17 : (!simulation.context, !simulation.class_handle<@C>) -> i32 {is_final = false, is_pure = true, is_static = false, is_task = false, is_virtual = true}
    simulation.vpi_call.decl @call id 1 in 1 scope 0 type 648 name "f" {is_indirect}
    simulation.vpi_class_call_dispatch @call on !simulation.class_handle<@C> via @method
    simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@C> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      // expected-error @+1 {{executable dispatch disagrees with its VPI class call contract}}
      %value = simulation.class.virtual_call %this [@other] slot 1 signature_id 17() {simulation.vpi_call_site = @call} : (!simulation.class_handle<@C>) -> i32
      simulation.return
    }
  }
}
