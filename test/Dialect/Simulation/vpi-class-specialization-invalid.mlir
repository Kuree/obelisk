// RUN: obelisk-opt %s --split-input-file --verify-diagnostics -o /dev/null

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @package id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @definition id 1 type 652 in 0 parent @package
        ordinal 0 hierarchy "pkg.C" debug "C"
    simulation.class.decl @cls id 1 {
      is_abstract = false, is_interface = false, is_final = false}
    // expected-error @+1 {{class specialization references an unknown class}}
    simulation.vpi_typespec.decl @typespec id 0 in 0 owner @definition
        hierarchy "pkg.typespec" debug "typespec" {
      origin = 3 : i32,
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
        isFourState = false, symbol = @missing, range = [], children = [],
        childNames = []>
    }
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @package id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @definition id 1 type 652 in 0 parent @package
        ordinal 0 hierarchy "pkg.C" debug "C"
    simulation.class.decl @cls id 1 {
      is_abstract = false, is_interface = false, is_final = false}
    // expected-error @+1 {{class specialization requires a class-definition owner}}
    simulation.vpi_typespec.decl @typespec id 0 in 0 owner @package
        hierarchy "pkg.typespec" debug "typespec" {
      origin = 3 : i32,
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
        isFourState = false, symbol = @cls, range = [], children = [],
        childNames = []>
    }
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @package id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @definition id 1 type 652 in 0 parent @package
        ordinal 0 hierarchy "pkg.C" debug "C"
    simulation.class.decl @cls id 1 {
      is_abstract = false, is_interface = false, is_final = false}
    simulation.vpi_typespec.decl @first id 0 in 0 owner @definition
        hierarchy "pkg.first" debug "first" {
      origin = 3 : i32,
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
        isFourState = false, symbol = @cls, range = [], children = [],
        childNames = []>
    }
    // expected-error @+1 {{duplicates a canonical class specialization}}
    simulation.vpi_typespec.decl @second id 1 in 0 owner @definition
        hierarchy "pkg.second" debug "second" {
      origin = 3 : i32,
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
        isFourState = false, symbol = @cls, range = [], children = [],
        childNames = []>
    }
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @package id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @definition id 1 type 652 in 0 parent @package
        ordinal 0 hierarchy "pkg.C" debug "C"
    simulation.class.decl @cls id 1 {
      is_abstract = false, is_interface = false, is_final = false}
    // expected-error @+1 {{class-specialization origin requires an exact class symbol and no typedef aliases}}
    simulation.vpi_typespec.decl @typespec id 0 in 0 owner @definition
        hierarchy "pkg.typespec" debug "typespec" {
      origin = 3 : i32,
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
        isFourState = false, symbol = @cls, range = [], children = [],
        childNames = [], typedefAliases = [@typespec]>
    }
  }
}

// -----

module {
  simulation.design @conflicting_owner {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @package id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @original id 1 type 652 in 0 parent @package
        ordinal 0 hierarchy "pkg.C" debug "C" {
      backing = #simulation.vpi_backing<kind = class, symbol = @cls>
    }
    simulation.vpi_object.anchor @other id 2 type 652 in 0 parent @package
        ordinal 1 hierarchy "pkg.Other" debug "Other"
    simulation.class.decl @cls id 1 {
      is_abstract = false, is_interface = false, is_final = false}
    // expected-error @+1 {{class specialization owner disagrees with the class-backed anchor}}
    simulation.vpi_typespec.decl @typespec id 0 in 0 owner @other
        hierarchy "pkg.type" debug "type" {
      origin = 3 : i32,
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
        isFourState = false, symbol = @cls, range = [], children = [],
        childNames = []>
    }
  }
}

// -----

module {
  simulation.design @property_kind {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @package id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{VPI property 640 is not applicable to exact object kind 641}}
    simulation.vpi_typespec.decl @typespec id 0 in 0 owner @package
        hierarchy "pkg.type" debug "type" {
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 640 : i32, value = 3 : i32>
      ]>,
      target_type = #simulation.vpi_type<kind = logic, isSigned = false,
        isFourState = true, range = [0, 0], children = [], childNames = []>
    }
  }
}
