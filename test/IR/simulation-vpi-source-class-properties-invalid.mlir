// RUN: talos-opt %s -split-input-file -verify-diagnostics

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @package id 0 type 600 in 0 ordinal 0 hierarchy "$unit" debug "$unit"
    simulation.vpi_object.anchor @definition id 1 type 652 in 0 parent @package ordinal 0 hierarchy "G" debug "G"
    // expected-error @+1 {{a lexical property cannot have executable backing}}
    simulation.vpi_class_member.decl @member id 2 in 0 owner @definition hierarchy "G::x" debug "x" {vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>, static_storage = 0 : i64}
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @package id 0 type 600 in 0 ordinal 0 hierarchy "$unit" debug "$unit"
    simulation.vpi_object.anchor @definition id 1 type 652 in 0 parent @package ordinal 0 hierarchy "G" debug "G"
    // expected-error @+1 {{class property requires a concrete declared type}}
    simulation.vpi_class_member.decl @member id 2 in 0 owner @definition hierarchy "G::x" debug "x" {vpi_type = #simulation.vpi_type<kind = void, isSigned = false, isFourState = false, range = [], children = [], childNames = []>}
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @package id 0 type 600 in 0 ordinal 0 hierarchy "$unit" debug "$unit"
    simulation.vpi_object.anchor @definition id 1 type 652 in 0 parent @package ordinal 0 hierarchy "G" debug "G"
    // expected-error @+1 {{vpiName must match the exact property declaration name}}
    simulation.vpi_class_member.decl @member id 2 in 0 owner @definition hierarchy "G::x" debug "x" {vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 2 : i32, value = "wrong">]>}
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @package id 0 type 600 in 0 ordinal 0 hierarchy "$unit" debug "$unit"
    simulation.vpi_object.anchor @definition id 1 type 652 in 0 parent @package ordinal 0 hierarchy "G" debug "G"
    simulation.vpi_class_member.decl @member id 2 in 0 owner @definition hierarchy "G::x" debug "x" {vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>}
    // expected-error @+1 {{duplicates a property name in the same class owner}}
    simulation.vpi_class_member.decl @other id 3 in 0 owner @definition hierarchy "G::x" debug "x" {vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>}
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @package id 0 type 600 in 0 ordinal 0 hierarchy "$unit" debug "$unit"
    simulation.vpi_object.anchor @definition id 1 type 652 in 0 parent @package ordinal 0 hierarchy "G" debug "G"
    simulation.class.decl @cls id 1 {is_abstract = false, is_interface = false, is_final = false}
    simulation.vpi_typespec.decl @canonical id 0 in 0 owner @definition hierarchy "G$type" debug "G" {origin = 3 : i32, target_type = #simulation.vpi_type<kind = class, isSigned = false, isFourState = false, symbol = @cls, range = [], children = [], childNames = []>}
    // expected-error @+1 {{owner must be the field's canonical class specialization}}
    simulation.vpi_class_member.decl @member id 2 in 0 owner @canonical hierarchy "G::x" debug "x" {vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>}
  }
}
