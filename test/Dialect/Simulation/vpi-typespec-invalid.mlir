// RUN: obelisk-opt %s --split-input-file --verify-diagnostics -o /dev/null

module {
  obelisk_sim.design @duplicate_anchor_id {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @first id 0 type 600 in 0 ordinal 0 hierarchy "top" debug "top"
    // expected-error @+1 {{duplicate VPI object anchor ID 0}}
    obelisk_sim.vpi_object.anchor @second id 0 type 600 in 0 ordinal 1 hierarchy "other" debug "other"
  }
}

// -----

module {
  // expected-error @+1 {{VPI object anchor IDs must be dense from zero; missing 0}}
  obelisk_sim.design @sparse_anchor_id {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @root id 1 type 600 in 0 ordinal 0 hierarchy "top" debug "top"
  }
}

// -----

module {
  obelisk_sim.design @unknown_anchor_parent {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{references an unknown VPI parent anchor}}
    obelisk_sim.vpi_object.anchor @child id 0 type 600 in 0 parent @missing ordinal 0 hierarchy "pkg" debug "pkg"
  }
}

// -----

module {
  obelisk_sim.design @anchor_cycle {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{lexical parent relation contains a cycle}}
    obelisk_sim.vpi_object.anchor @a id 0 type 652 in 0 parent @b ordinal 0 hierarchy "a" debug "a"
    obelisk_sim.vpi_object.anchor @b id 1 type 652 in 0 parent @a ordinal 0 hierarchy "b" debug "b"
  }
}

// -----

module {
  // expected-error @+1 {{VPI anchor ordinals must be dense under each lexical parent}}
  obelisk_sim.design @sparse_anchor_ordinal {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @root id 0 type 600 in 0 ordinal 0 hierarchy "top" debug "top"
    obelisk_sim.vpi_object.anchor @child id 1 type 652 in 0 parent @root ordinal 1 hierarchy "child" debug "child"
  }
}

// -----

module {
  obelisk_sim.design @duplicate_backing {
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32
    obelisk_sim.vpi_object.anchor @first id 0 type 32 in 0 ordinal 0 hierarchy "top" debug "top" {backing = #obelisk_sim.vpi_backing<kind = scope, id = 0 : i64>}
    // expected-error @+1 {{duplicates physical backing used by}}
    obelisk_sim.vpi_object.anchor @second id 1 type 32 in 0 ordinal 1 hierarchy "other" debug "other" {backing = #obelisk_sim.vpi_backing<kind = scope, id = 0 : i64>}
  }
}

// -----

module {
  obelisk_sim.design @wrong_backing_id_type {
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32
    // expected-error @+1 {{VPI backing ID must be a signless i64}}
    obelisk_sim.vpi_object.anchor @top id 0 type 32 in 0 ordinal 0 hierarchy "top" debug "top" {backing = #obelisk_sim.vpi_backing<kind = scope, id = 0 : i32>}
  }
}

// -----

module {
  obelisk_sim.design @wrong_scope_backing_hierarchy {
    obelisk_sim.scope.decl 0
    obelisk_sim.scope.decl 1 parent 0 hierarchy "physical" vpi_kind 32
    // expected-error @+1 {{backing scope hierarchy must equal the anchor hierarchy}}
    obelisk_sim.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "source" debug "top" {backing = #obelisk_sim.vpi_backing<kind = scope, id = 1 : i64>}
  }
}

// -----

module {
  obelisk_sim.design @bad_compilation_unit_kind {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{compilation-unit anchor must have vpiPackage kind}}
    obelisk_sim.vpi_object.anchor @root id 0 type 32 in 0 ordinal 0 hierarchy "$unit" debug "$unit" {is_compilation_unit}
  }
}

// -----

module {
  obelisk_sim.design @duplicate_typespec_id {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @root id 0 type 600 in 0 ordinal 0 hierarchy "top" debug "top"
    obelisk_sim.vpi_typespec.decl @first id 0 in 0 owner @root hierarchy "top.first_t" debug "first_t" {
      target_type = #obelisk_sim.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [0, 0], children = [], childNames = []>
    }
    // expected-error @+1 {{duplicate VPI typespec ID 0}}
    obelisk_sim.vpi_typespec.decl @second id 0 in 0 owner @root hierarchy "top.second_t" debug "second_t" {
      target_type = #obelisk_sim.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [0, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  obelisk_sim.design @unknown_scope {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{references an unknown enclosing scope ID}}
    obelisk_sim.vpi_object.anchor @root id 0 type 600 in 17 ordinal 0 hierarchy "top" debug "top"
    obelisk_sim.vpi_typespec.decl @orphan id 0 in 17 owner @root hierarchy "top.orphan_t" debug "orphan_t" {
      target_type = #obelisk_sim.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [0, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  obelisk_sim.design @unknown_owner {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @root id 0 type 600 in 0 ordinal 0 hierarchy "top" debug "top"
    // expected-error @+1 {{typespec must be owned by a VPI anchor}}
    obelisk_sim.vpi_typespec.decl @alias id 0 in 0 owner @missing hierarchy "top.alias_t" debug "alias_t" {
      target_type = #obelisk_sim.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [0, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  obelisk_sim.design @wrong_typedef_owner {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @root id 0 type 600 in 0 ordinal 0 hierarchy "top" debug "top"
    obelisk_sim.vpi_object.anchor @property id 1 type 655 in 0 parent @root ordinal 0 hierarchy "top.p" debug "p"
    // expected-error @+1 {{owner kind does not support vpiTypedef traversal}}
    obelisk_sim.vpi_typespec.decl @alias id 0 in 0 owner @property hierarchy "top.p.alias_t" debug "alias_t" {
      target_type = #obelisk_sim.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [0, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  obelisk_sim.design @unknown_alias {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @root id 0 type 600 in 0 ordinal 0 hierarchy "top" debug "top"
    // expected-error @+1 {{VPI typedef alias @missing does not reference a typedef typespec declaration}}
    obelisk_sim.vpi_typespec.decl @alias id 0 in 0 owner @root hierarchy "top.alias_t" debug "alias_t" {
      target_type = #obelisk_sim.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [0, 0], children = [], childNames = [], typedefAliases = [@missing]>
    }
  }
}

// -----

module {
  obelisk_sim.design @missing_interface_identity {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @root id 0 type 600 in 0 ordinal 0 hierarchy "top" debug "top"
    // expected-error @+1 {{VPI virtual-interface semantics require an exact typespec identity}}
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.virtual_interface<"@iface", ""> design {
      vpi_type = #obelisk_sim.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "@iface", modport = "", range = [], children = [], childNames = []>
    }
  }
}

// -----

module {
  obelisk_sim.design @dangling_interface {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @root id 0 type 600 in 0 ordinal 0 hierarchy "top" debug "top"
    obelisk_sim.vpi_object.anchor @iface_owner id 1 type 652 in 0 parent @root ordinal 0 hierarchy "top.iface" debug "iface"
    obelisk_sim.vpi_typespec.decl @iface id 0 in 0 owner @iface_owner hierarchy "@iface" debug "iface" {
      origin = 1 : i32,
      target_type = #obelisk_sim.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "@iface", symbol = @iface, modport = "", range = [], children = [], childNames = []>
    }
    // expected-error @+1 {{VPI virtual-interface identity @missing does not reference a typespec declaration}}
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.virtual_interface<"@iface", ""> design {
      vpi_type = #obelisk_sim.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "@iface", symbol = @missing, modport = "", range = [], children = [], childNames = []>
    }
  }
}

// -----

module {
  obelisk_sim.design @missing_interface_parent {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @root id 0 type 600 in 0 ordinal 0 hierarchy "top" debug "top"
    obelisk_sim.vpi_object.anchor @iface_owner id 1 type 652 in 0 parent @root ordinal 0 hierarchy "top.iface" debug "iface"
    // expected-error @+1 {{modport typespec requires its parent interface typespec}}
    obelisk_sim.vpi_typespec.decl @iface_mp id 0 in 0 owner @iface_owner hierarchy "@iface" debug "iface" {
      origin = 1 : i32,
      target_type = #obelisk_sim.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "@iface", symbol = @iface_mp, modport = "mp", range = [], children = [], childNames = []>
    }
  }
}

// -----

module {
  obelisk_sim.design @duplicate_interface_specialization {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @root id 0 type 600 in 0 ordinal 0 hierarchy "top" debug "top"
    obelisk_sim.vpi_object.anchor @iface_owner id 1 type 652 in 0 parent @root ordinal 0 hierarchy "top.iface" debug "iface"
    obelisk_sim.vpi_typespec.decl @iface_a id 0 in 0 owner @iface_owner hierarchy "@iface" debug "iface" {
      origin = 1 : i32,
      target_type = #obelisk_sim.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "@iface", symbol = @iface_a, modport = "", range = [], children = [], childNames = []>
    }
    // expected-error @+1 {{duplicates an interface/modport typespec specialization}}
    obelisk_sim.vpi_typespec.decl @iface_b id 1 in 0 owner @iface_owner hierarchy "@iface" debug "iface" {
      origin = 1 : i32,
      target_type = #obelisk_sim.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "@iface", symbol = @iface_b, modport = "", range = [], children = [], childNames = []>
    }
  }
}

// -----

module {
  obelisk_sim.design @duplicate_enum_ordinal {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @root id 0 type 600 in 0 ordinal 0 hierarchy "top" debug "top"
    obelisk_sim.vpi_typespec.decl @state id 0 in 0 owner @root hierarchy "top.state_t" debug "state_t" {
      target_type = #obelisk_sim.vpi_type<kind = enum, isSigned = false, isFourState = false, name = "state_t", range = [], children = [#obelisk_sim.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [1, 0], children = [], childNames = []>], childNames = []>
    }
    obelisk_sim.vpi_enum_const.decl 0 enum @state ordinal 0 name "IDLE" value "2'b00"
    // expected-error @+1 {{duplicates an ordinal in the same enum typespec}}
    obelisk_sim.vpi_enum_const.decl 1 enum @state ordinal 0 name "RUN" value "2'b01"
  }
}

// -----

module {
  obelisk_sim.design @illegal_anchor_kind {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{kind cannot be a persistent lexical source anchor}}
    obelisk_sim.vpi_object.anchor @value id 0 type 48 in 0 ordinal 0 hierarchy "top.value" debug "value"
  }
}

// -----

module {
  obelisk_sim.design @wrong_backing_category {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{class backing requires a class-definition anchor}}
    obelisk_sim.vpi_object.anchor @module id 0 type 32 in 0 ordinal 0 hierarchy "top" debug "top" {backing = #obelisk_sim.vpi_backing<kind = class, symbol = @C>}
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
  }
}

// -----

module {
  obelisk_sim.design @cross_owner_interface_parent {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @root id 0 type 600 in 0 ordinal 0 hierarchy "top" debug "top"
    obelisk_sim.vpi_object.anchor @iface_a_owner id 1 type 652 in 0 parent @root ordinal 0 hierarchy "top.a" debug "a"
    obelisk_sim.vpi_object.anchor @iface_b_owner id 2 type 652 in 0 parent @root ordinal 1 hierarchy "top.b" debug "b"
    obelisk_sim.vpi_typespec.decl @iface_a id 0 in 0 owner @iface_a_owner hierarchy "@iface" debug "iface" {
      origin = 1 : i32,
      target_type = #obelisk_sim.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "@iface", symbol = @iface_a, modport = "", range = [], children = [], childNames = []>
    }
    // expected-error @+1 {{modport typespec requires its parent interface typespec}}
    obelisk_sim.vpi_typespec.decl @iface_b_mp id 1 in 0 owner @iface_b_owner hierarchy "@iface" debug "iface" {
      origin = 1 : i32,
      target_type = #obelisk_sim.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "@iface", symbol = @iface_b_mp, modport = "mp", range = [], children = [], childNames = []>
    }
  }
}

// -----

module {
  // Property and sequence declarations are legal within a clocking block.
  obelisk_sim.design @clocking_declaration_owners {
    obelisk_sim.scope.decl 0
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top" debug "top" vpi_kind 32
    obelisk_sim.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #obelisk_sim.vpi_backing<kind = scope, id = 1 : i64>}
    obelisk_sim.vpi_object.anchor @cb id 1 type 650 in 1 parent @top ordinal 0 hierarchy "top.cb" debug "cb"
    obelisk_sim.vpi_object.anchor @seq id 2 type 661 in 1 parent @cb ordinal 0 hierarchy "top.cb.seq" debug "seq"
    obelisk_sim.vpi_object.anchor @prop id 3 type 655 in 1 parent @cb ordinal 1 hierarchy "top.cb.prop" debug "prop"
  }
}

// -----

module {
  obelisk_sim.design @nested_source_package {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @outer id 0 type 600 in 0 ordinal 0 hierarchy "outer" debug "outer"
    // expected-error @+1 {{has an illegal lexical parent kind}}
    obelisk_sim.vpi_object.anchor @inner id 1 type 600 in 0 parent @outer ordinal 0 hierarchy "outer::inner" debug "inner"
  }
}

// -----

module {
  obelisk_sim.design @missing_instance_backing {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{module, interface, and program anchors require scope backing}}
    obelisk_sim.vpi_object.anchor @top id 0 type 32 in 0 ordinal 0 hierarchy "top" debug "top"
  }
}

// -----

module {
  obelisk_sim.design @wrong_scope_backing_kind {
    obelisk_sim.scope.decl 0
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top"
    // expected-error @+1 {{VPI anchor kind does not match its backing scope kind}}
    obelisk_sim.vpi_object.anchor @iface id 0 type 601 in 1 ordinal 0 hierarchy "top" debug "iface" {backing = #obelisk_sim.vpi_backing<kind = scope, id = 1 : i64>}
  }
}

// -----

module {
  obelisk_sim.design @wrong_code_unit_backing_kind {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "pkg.t" debug "t"
    obelisk_sim.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{VPI anchor kind does not match its backing code-unit kind}}
    obelisk_sim.vpi_object.anchor @task id 1 type 59 in 0 parent @pkg ordinal 0 hierarchy "pkg.t" debug "t" {backing = #obelisk_sim.vpi_backing<kind = code_unit, id = 1 : i64>}
  }
}

// -----

module {
  obelisk_sim.design @dpi_import_code_unit_backing {
    obelisk_sim.scope.decl 0
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 1 function hierarchy "top.f" debug "f" {
      obelisk_sim.dpi_import
    }
    obelisk_sim.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #obelisk_sim.vpi_backing<kind = scope, id = 1 : i64>}
    // expected-error @+1 {{backing code unit must be a VPI-visible task or function}}
    obelisk_sim.vpi_object.anchor @function id 1 type 20 in 1 parent @top ordinal 0 hierarchy "top.f" debug "f" {backing = #obelisk_sim.vpi_backing<kind = code_unit, id = 1 : i64>}
  }
}

// -----

module {
  obelisk_sim.design @internal_code_unit_backing {
    obelisk_sim.scope.decl 0
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 1 function hierarchy "top.f" debug "f" {internal}
    obelisk_sim.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #obelisk_sim.vpi_backing<kind = scope, id = 1 : i64>}
    // expected-error @+1 {{backing code unit must be a VPI-visible task or function}}
    obelisk_sim.vpi_object.anchor @function id 1 type 20 in 1 parent @top ordinal 0 hierarchy "top.f" debug "f" {backing = #obelisk_sim.vpi_backing<kind = code_unit, id = 1 : i64>}
  }
}

// -----

module {
  obelisk_sim.design @wrong_code_unit_backing_scope {
    obelisk_sim.scope.decl 0
    obelisk_sim.scope.decl 1 parent 0 hierarchy "left"
    obelisk_sim.scope.decl 2 parent 0 hierarchy "right"
    obelisk_sim.code_unit.decl 1 in 2 function hierarchy "right.f" debug "f"
    obelisk_sim.vpi_object.anchor @left id 0 type 32 in 1 ordinal 0 hierarchy "left" debug "left" {backing = #obelisk_sim.vpi_backing<kind = scope, id = 1 : i64>}
    // expected-error @+1 {{backing code-unit scope must equal the anchor's enclosing scope}}
    obelisk_sim.vpi_object.anchor @function id 1 type 20 in 1 parent @left ordinal 0 hierarchy "left.f" debug "f" {backing = #obelisk_sim.vpi_backing<kind = code_unit, id = 1 : i64>}
  }
}

// -----

module {
  obelisk_sim.design @wrong_code_unit_backing_hierarchy {
    obelisk_sim.scope.decl 0
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 1 function hierarchy "top.other" debug "other"
    obelisk_sim.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #obelisk_sim.vpi_backing<kind = scope, id = 1 : i64>}
    // expected-error @+1 {{backing code-unit hierarchy must equal the anchor hierarchy}}
    obelisk_sim.vpi_object.anchor @function id 1 type 20 in 1 parent @top ordinal 0 hierarchy "top.f" debug "f" {backing = #obelisk_sim.vpi_backing<kind = code_unit, id = 1 : i64>}
  }
}
