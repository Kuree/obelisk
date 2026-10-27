// RUN: talos-opt %s --split-input-file --verify-diagnostics -o /dev/null

module {
  simulation.design @invalid {
    // expected-error @+1 {{builtin base must name an own strong instance mailbox or semaphore field}}
    simulation.class.decl @S id 1 {builtin_base = @missing,
      is_abstract = false, is_final = false, is_interface = false}
  }
}

// -----

module {
  simulation.design @invalid {
    // expected-error @+1 {{builtin base must name an own strong instance mailbox or semaphore field}}
    simulation.class.decl @S id 1 {builtin_base = @payload,
      is_abstract = false, is_final = false, is_interface = false}
    simulation.class.decl @Other id 2 {
      is_abstract = false, is_final = false, is_interface = false}
    simulation.class.field @payload of @Other at 0 : !simulation.semaphore {
      is_static = false, is_weak = false}
  }
}

// -----

module {
  simulation.design @invalid {
    // expected-error @+1 {{builtin base must name an own strong instance mailbox or semaphore field}}
    simulation.class.decl @S id 1 {builtin_base = @payload,
      is_abstract = false, is_final = false, is_interface = false}
    simulation.class.field @payload of @S at 0 : !simulation.semaphore {
      is_static = true, is_weak = false}
  }
}

// -----

module {
  simulation.design @invalid {
    // expected-error @+1 {{builtin base must name an own strong instance mailbox or semaphore field}}
    simulation.class.decl @S id 1 {builtin_base = @payload,
      is_abstract = false, is_final = false, is_interface = false}
    simulation.class.field @payload of @S at 0 : i64 {
      is_static = false, is_weak = false}
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.class.decl @Base id 1 {
      is_abstract = false, is_final = false, is_interface = false}
    // expected-error @+1 {{builtin base requires a root class without a weak referent}}
    simulation.class.decl @S id 2 extends @Base {builtin_base = @payload,
      is_abstract = false, is_final = false, is_interface = false}
    simulation.class.field @payload of @S at 0 : !simulation.semaphore {
      is_static = false, is_weak = false}
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.class.decl @S id 1 {
      is_abstract = false, is_final = false, is_interface = false}
    simulation.func @wrong(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %object: !simulation.class_handle<@S> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32} {
      // expected-error @+1 {{operand class has no declared builtin base}}
      %base = simulation.class.builtin_upcast %object :
          !simulation.class_handle<@S> to !simulation.semaphore
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.class.decl @S id 1 {builtin_base = @payload,
      is_abstract = false, is_final = false, is_interface = false}
    simulation.class.field @payload of @S at 0 : !simulation.semaphore {
      is_static = false, is_weak = false}
    simulation.func @wrong(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %object: !simulation.class_handle<@S> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32} {
      // expected-error @+1 {{result does not match the declared builtin base type}}
      %base = simulation.class.builtin_upcast %object :
          !simulation.class_handle<@S> to !simulation.mailbox<i32>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.class.decl @M id 1 {builtin_base = @payload,
      is_abstract = false, is_final = false, is_interface = false}
    simulation.class.field @payload of @M at 0 : !simulation.mailbox<i32> {
      is_static = false, is_weak = false}
    simulation.func @wrong(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %object: !simulation.class_handle<@M> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32} {
      // expected-error @+1 {{result does not match the declared builtin base type}}
      %base = simulation.class.builtin_upcast %object :
          !simulation.class_handle<@M> to !simulation.mailbox<i64>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.class.decl @S id 1 {
      is_abstract = false, is_final = false, is_interface = false}
    simulation.func @wrong(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %base: !simulation.semaphore {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32} {
      // expected-error @+1 {{target class does not derive from the builtin source type}}
      %object = simulation.class.cast %base :
          !simulation.semaphore to !simulation.class_handle<@S>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.class.decl @M id 1 {builtin_base = @payload,
      is_abstract = false, is_final = false, is_interface = false}
    simulation.class.field @payload of @M at 0 : !simulation.mailbox<i32> {
      is_static = false, is_weak = false}
    simulation.func @wrong(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %base: !simulation.mailbox<i64> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32} {
      // expected-error @+1 {{target class does not derive from the builtin source type}}
      %object = simulation.class.cast %base :
          !simulation.mailbox<i64> to !simulation.class_handle<@M>
      simulation.return
    }
  }
}
