// RUN: obelisk-opt %s -split-input-file -verify-diagnostics -o /dev/null

module {
  simulation.design @not_mailbox {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @std id 0 type 600 in 0 ordinal 0 hierarchy "std" debug "std"
    // expected-error @+1 {{message_type requires a canonical builtin mailbox typespec}}
    simulation.vpi_typespec.decl @type id 0 in 0 owner @std hierarchy "std::semaphore" debug "semaphore" {
      origin = 4 : i32, message_type = i32,
      target_type = #simulation.vpi_type<kind = semaphore, isSigned = false,
          isFourState = false, range = [], children = [], childNames = []>}
  }
}

// -----

module {
  simulation.design @alias_is_not_canonical {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @std id 0 type 600 in 0 ordinal 0 hierarchy "std" debug "std"
    // expected-error @+1 {{message_type requires a canonical builtin mailbox typespec}}
    simulation.vpi_typespec.decl @type id 0 in 0 owner @std hierarchy "std::box_t" debug "box_t" {
      origin = 0 : i32, message_type = i32,
      target_type = #simulation.vpi_type<kind = mailbox, isSigned = false,
          isFourState = false, range = [], children = [
            #simulation.vpi_type<kind = int, isSigned = true, isFourState = false,
                range = [31, 0], children = [], childNames = []>], childNames = []>}
  }
}

// -----

module {
  simulation.design @wrong_storage_width {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @std id 0 type 600 in 0 ordinal 0 hierarchy "std" debug "std"
    // expected-error @+1 {{does not normalize to executable type 'i64'}}
    simulation.vpi_typespec.decl @type id 0 in 0 owner @std hierarchy "std::mailbox" debug "mailbox" {
      origin = 4 : i32, message_type = i64,
      target_type = #simulation.vpi_type<kind = mailbox, isSigned = false,
          isFourState = false, range = [], children = [
            #simulation.vpi_type<kind = int, isSigned = true, isFourState = false,
                range = [31, 0], children = [], childNames = []>], childNames = []>}
  }
}

// -----

module {
  simulation.design @wrong_storage_domain {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @std id 0 type 600 in 0 ordinal 0 hierarchy "std" debug "std"
    // expected-error @+1 {{does not normalize to executable type 'i8'}}
    simulation.vpi_typespec.decl @type id 0 in 0 owner @std hierarchy "std::mailbox" debug "mailbox" {
      origin = 4 : i32, message_type = i8,
      target_type = #simulation.vpi_type<kind = mailbox, isSigned = false,
          isFourState = false, range = [], children = [
            #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true,
                range = [7, 0], children = [], childNames = []>], childNames = []>}
  }
}
