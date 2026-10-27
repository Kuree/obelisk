// RUN: talos-opt --split-input-file --verify-diagnostics %s

// expected-error @below {{symbol VPI object reference requires only a symbol}}
module attributes {test.ref = #simulation.vpi_ref<kind = symbol, id = 0 : i64>} {}

// -----

// expected-error @below {{symbol VPI object reference requires only a symbol}}
module attributes {test.ref = #simulation.vpi_ref<kind = symbol, id = 0 : i64, symbol = @a>} {}

// -----

// expected-error @below {{numeric VPI object reference cannot carry a symbol}}
module attributes {test.ref = #simulation.vpi_ref<kind = storage, symbol = @a>} {}

// -----

// expected-error @below {{VPI object reference ID must be a signless i64}}
module attributes {test.ref = #simulation.vpi_ref<kind = storage>} {}

// -----

module {
  simulation.design @missing_symbol {
    simulation.scope.decl 0 hierarchy "$root"
    // expected-error @below {{references an unknown source VPI symbol @missing}}
    simulation.vpi_relation.decl <kind = symbol, symbol = @missing>
        selector 727 iterate ordinal 0 to <kind = symbol, symbol = @missing>
  }
}

// -----

module {
  simulation.design @missing_target {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @pkg id 99 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @base id 0 type 652 in 0 parent @pkg ordinal 0
        hierarchy "Base" debug "Base"
    // expected-error @below {{references an unknown target VPI symbol @missing}}
    simulation.vpi_relation.decl <kind = symbol, symbol = @base>
        selector 727 iterate ordinal 0 to <kind = symbol, symbol = @missing>
  }
}

// -----

module {
  simulation.design @wrong_symbol {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.class.decl @class id 1 {debug_name = "C",
      is_abstract = false, is_final = false, is_interface = false}
    // expected-error @below {{source VPI symbol @class must name an object anchor, typespec, or nettype}}
    simulation.vpi_relation.decl <kind = symbol, symbol = @class>
        selector 727 iterate ordinal 0 to <kind = symbol, symbol = @class>
  }
}

// -----

module {
  simulation.design @wrong_kind {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @pkg id 99 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @base id 0 type 652 in 0 parent @pkg ordinal 0
        hierarchy "Base" debug "Base"
    // vpiExtends from a class definition must return an extends object.
    // expected-error @below {{is not a legal non-containment traversal in the generated VPI model}}
    simulation.vpi_relation.decl <kind = symbol, symbol = @base>
        selector 677 handle ordinal 0 to <kind = symbol, symbol = @base>
  }
}

// -----

module {
  simulation.design @duplicate_target {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @pkg id 99 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @base id 0 type 652 in 0 parent @pkg ordinal 0
        hierarchy "Base" debug "Base"
    simulation.vpi_object.anchor @derived id 1 type 652 in 0 parent @pkg ordinal 1
        hierarchy "Derived" debug "Derived"
    simulation.vpi_relation.decl <kind = symbol, symbol = @base>
        selector 727 iterate ordinal 0 to <kind = symbol, symbol = @derived>
    // expected-error @below {{duplicates a target in the same source, selector, and access mode}}
    simulation.vpi_relation.decl <kind = symbol, symbol = @base>
        selector 727 iterate ordinal 1 to <kind = symbol, symbol = @derived>
  }
}

// -----

module {
  simulation.design @sparse_ordinals {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @pkg id 99 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @base id 0 type 652 in 0 parent @pkg ordinal 0
        hierarchy "Base" debug "Base"
    simulation.vpi_object.anchor @derived id 1 type 652 in 0 parent @pkg ordinal 1
        hierarchy "Derived" debug "Derived"
    // expected-error @below {{ordinals must be dense from zero for each source, selector, and access mode}}
    simulation.vpi_relation.decl <kind = symbol, symbol = @base>
        selector 727 iterate ordinal 1 to <kind = symbol, symbol = @derived>
  }
}

// -----

module {
  simulation.design @singular {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @pkg id 99 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @base id 0 type 652 in 0 parent @pkg ordinal 0
        hierarchy "Base" debug "Base"
    simulation.vpi_object.anchor @a id 1 type 677 in 0 parent @base ordinal 0
        hierarchy "Base.extendsA" debug "extendsA"
    simulation.vpi_object.anchor @b id 2 type 677 in 0 parent @base ordinal 1
        hierarchy "Base.extendsB" debug "extendsB"
    simulation.vpi_relation.decl <kind = symbol, symbol = @base>
        selector 677 handle ordinal 0 to <kind = symbol, symbol = @a>
    // expected-error @below {{vpi_handle relation may expose at most one target}}
    simulation.vpi_relation.decl <kind = symbol, symbol = @base>
        selector 677 handle ordinal 0 to <kind = symbol, symbol = @b>
  }
}

// -----

module {
  simulation.design @automatic_relation {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @pkg id 99 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @base id 0 type 652 in 0 parent @pkg ordinal 0
        hierarchy "Base" debug "Base"
    simulation.vpi_object.anchor @method id 1 type 20 in 0 parent @base ordinal 0
        hierarchy "Base.method" debug "method"
    // Method membership comes from the lexical anchor inventory.
    // expected-error @below {{is not a legal non-containment traversal in the generated VPI model}}
    simulation.vpi_relation.decl <kind = symbol, symbol = @base>
        selector 730 iterate ordinal 0 to <kind = symbol, symbol = @method>
  }
}

// -----

module {
  simulation.design @backed_alias {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    simulation.net.decl 0 in 1 : !simulation.logic<1> design
        hierarchy "top.n" debug "n" {
      vpi_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>,
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 16 : i32>
      ]>
    }
    simulation.vpi_object.anchor @net id 1 type 533 in 1 parent @top ordinal 0
        hierarchy "top.n" debug "n" {
      backing = #simulation.vpi_backing<kind = net, id = 0 : i64>
    }
    simulation.statement.decl 1 scope 1 type 8
    simulation.vpi_statement_relation.decl scope 1 type 32 selector 8
        ordinal 0 modes 2 to 1
    simulation.vpi_relation.decl <kind = symbol, symbol = @net>
        selector 93 iterate ordinal 0 to <kind = statement, id = 1 : i64>
    // Symbol and numeric references resolve to one physical object.
    // expected-error @below {{duplicates a target in the same source, selector, and access mode}}
    simulation.vpi_relation.decl <kind = net, id = 0 : i64>
        selector 93 iterate ordinal 1 to <kind = statement, id = 1 : i64>
  }
}
