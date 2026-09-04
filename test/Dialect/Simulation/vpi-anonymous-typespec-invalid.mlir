// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

module {
  obelisk_sim.design @missing_typespec_identity {
    obelisk_sim.scope.decl 0 hierarchy "$root"
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top" {vpi_kind = 32 : i32}
    obelisk_sim.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #obelisk_sim.vpi_backing<kind = scope, id = 1 : i64>
    }
    // expected-error @+1 {{anonymous-enum origin requires an enum typespec and exact source type identity}}
    obelisk_sim.vpi_typespec.decl @anonymous_t id 0 in 1 owner @top
        hierarchy "top" debug "anonymous" {
      origin = 2 : i32,
      target_type = #obelisk_sim.vpi_type<kind = enum, isSigned = false,
          isFourState = false, name = "anonymous", range = [], children = [
            #obelisk_sim.vpi_type<kind = bit, isSigned = false,
                isFourState = false, range = [0, 0], children = [],
                childNames = []>
          ], childNames = []>
    }
  }
}

// -----

module {
  obelisk_sim.design @duplicate_typespec_identity {
    obelisk_sim.scope.decl 0 hierarchy "$root"
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top" {vpi_kind = 32 : i32}
    obelisk_sim.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #obelisk_sim.vpi_backing<kind = scope, id = 1 : i64>
    }
    obelisk_sim.vpi_typespec.decl @anonymous_a id 0 in 1 owner @top
        hierarchy "top" debug "anonymous_a" {
      origin = 2 : i32, source_type_identity = 8 : i64,
      target_type = #obelisk_sim.vpi_type<kind = enum, isSigned = false,
          isFourState = true, name = "anonymous_a", range = [], children = [
            #obelisk_sim.vpi_type<kind = logic, isSigned = false,
                isFourState = true, range = [0, 0], children = [],
                childNames = []>
          ], childNames = []>
    }
    // expected-error @+1 {{duplicates an anonymous enum source type identity}}
    obelisk_sim.vpi_typespec.decl @anonymous_b id 1 in 1 owner @top
        hierarchy "top" debug "anonymous_b" {
      origin = 2 : i32, source_type_identity = 8 : i64,
      target_type = #obelisk_sim.vpi_type<kind = enum, isSigned = false,
          isFourState = true, name = "anonymous_b", range = [], children = [
            #obelisk_sim.vpi_type<kind = logic, isSigned = false,
                isFourState = true, range = [0, 0], children = [],
                childNames = []>
          ], childNames = []>
    }
  }
}

// -----

module {
  obelisk_sim.design @unresolved_value_identity {
    obelisk_sim.scope.decl 0 hierarchy "$root"
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top" {vpi_kind = 32 : i32}
    obelisk_sim.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #obelisk_sim.vpi_backing<kind = scope, id = 1 : i64>
    }
    obelisk_sim.vpi_typespec.decl @anonymous_t id 0 in 1 owner @top
        hierarchy "top" debug "anonymous" {
      origin = 2 : i32, source_type_identity = 8 : i64,
      target_type = #obelisk_sim.vpi_type<kind = enum, isSigned = false,
          isFourState = true, name = "anonymous", range = [], children = [
            #obelisk_sim.vpi_type<kind = logic, isSigned = false,
                isFourState = true, range = [0, 0], children = [],
                childNames = []>
          ], childNames = []>
    }
    // expected-error @+1 {{anonymous enum VPI value has no matching typespec identity}}
    obelisk_sim.storage.decl 0 in 1 : i1 design hierarchy "top.value" {
      obelisk_sim.vpi_source_type_identity = 9 : i64,
      vpi_type = #obelisk_sim.vpi_type<kind = enum, isSigned = false,
          isFourState = false, name = "anonymous", range = [], children = [
            #obelisk_sim.vpi_type<kind = bit, isSigned = false,
                isFourState = false, range = [0, 0], children = [],
                childNames = []>
          ], childNames = []>
    }
  }
}

// -----

module {
  obelisk_sim.design @missing_value_identity {
    obelisk_sim.scope.decl 0 hierarchy "$root"
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top" {vpi_kind = 32 : i32}
    obelisk_sim.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #obelisk_sim.vpi_backing<kind = scope, id = 1 : i64>
    }
    obelisk_sim.vpi_typespec.decl @anonymous_t id 0 in 1 owner @top
        hierarchy "top" debug "anonymous" {
      origin = 2 : i32, source_type_identity = 8 : i64,
      target_type = #obelisk_sim.vpi_type<kind = enum, isSigned = false,
          isFourState = true, name = "anonymous", range = [], children = [
            #obelisk_sim.vpi_type<kind = logic, isSigned = false,
                isFourState = true, range = [0, 0], children = [],
                childNames = []>
          ], childNames = []>
    }
    // expected-error @+1 {{anonymous enum VPI value requires an exact source type identity}}
    obelisk_sim.storage.decl 0 in 1 : i1 design hierarchy "top.value" {
      vpi_type = #obelisk_sim.vpi_type<kind = enum, isSigned = false,
          isFourState = false, name = "anonymous", range = [], children = [
            #obelisk_sim.vpi_type<kind = bit, isSigned = false,
                isFourState = false, range = [0, 0], children = [],
                childNames = []>
          ], childNames = []>
    }
  }
}
