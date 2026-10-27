// RUN: talos-opt %s -split-input-file -verify-diagnostics -o /dev/null

module {
  simulation.design @invalid_builtin_kind {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @std id 0 type 600 in 0 ordinal 0 hierarchy "std" debug "std"
    // expected-error @+1 {{builtin-class origin requires an unaliased process, semaphore, or mailbox type}}
    simulation.vpi_typespec.decl @type id 0 in 0 owner @std hierarchy "std::int" debug "int" {
      origin = 4 : i32,
      target_type = #simulation.vpi_type<kind = int, isSigned = true,
          isFourState = false, range = [31, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @aliased_builtin_element {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @std id 0 type 600 in 0 ordinal 0 hierarchy "std" debug "std"
    simulation.vpi_typespec.decl @alias id 0 in 0 owner @std hierarchy "std::T" debug "T" {
      origin = 0 : i32,
      target_type = #simulation.vpi_type<kind = int, isSigned = true,
          isFourState = false, range = [31, 0], children = [], childNames = []>
    }
    // expected-error @+1 {{builtin-class origin requires an unaliased process, semaphore, or mailbox type}}
    simulation.vpi_typespec.decl @type id 1 in 0 owner @std hierarchy "std::mailbox" debug "mailbox" {
      origin = 4 : i32,
      target_type = #simulation.vpi_type<kind = mailbox, isSigned = false,
          isFourState = false, range = [], children = [
            #simulation.vpi_type<kind = int, isSigned = true,
                isFourState = false, range = [31, 0], children = [], childNames = [],
                typedefAliases = [@alias]>
          ], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @duplicate_builtin_type {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @std id 0 type 600 in 0 ordinal 0 hierarchy "std" debug "std"
    simulation.vpi_typespec.decl @first id 0 in 0 owner @std hierarchy "std::semaphore" debug "semaphore" {
      origin = 4 : i32,
      target_type = #simulation.vpi_type<kind = semaphore, isSigned = false,
          isFourState = false, range = [], children = [], childNames = []>
    }
    // expected-error @+1 {{duplicates a canonical builtin class type}}
    simulation.vpi_typespec.decl @second id 1 in 0 owner @std hierarchy "std::semaphore" debug "semaphore" {
      origin = 4 : i32,
      target_type = #simulation.vpi_type<kind = semaphore, isSigned = false,
          isFourState = false, range = [], children = [], childNames = []>
    }
  }
}
