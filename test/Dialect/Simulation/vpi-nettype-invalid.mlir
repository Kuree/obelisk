// RUN: obelisk-opt %s --split-input-file --verify-diagnostics -o /dev/null

module {
  simulation.design @unknown_owner {
    simulation.scope.decl 0 hierarchy "$root"
    // expected-error @+1 {{nettype must be owned by a VPI anchor}}
    simulation.vpi_nettype.decl @nt id 0 in 0 owner @missing
        hierarchy "$root.nt" debug "nt" {
      target_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @wrong_owner_kind {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @root id 0 type 600 in 0 ordinal 0
        hierarchy "$root" debug "$root"
    simulation.vpi_object.anchor @property id 1 type 655 in 0 parent @root ordinal 0
        hierarchy "$root.p" debug "p"
    // expected-error @+1 {{owner kind does not support vpiNetTypedef traversal}}
    simulation.vpi_nettype.decl @nt id 0 in 0 owner @property
        hierarchy "$root.p.nt" debug "nt" {
      target_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @unknown_alias {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" {vpi_kind = 32 : i32}
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    // expected-error @+1 {{references an unknown direct nettype alias}}
    simulation.vpi_nettype.decl @nt id 0 in 1 owner @top
        hierarchy "top.nt" debug "nt" {
      direct_alias = @missing,
      target_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @alias_cycle {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" {vpi_kind = 32 : i32}
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    // expected-error @+1 {{direct nettype aliases contain a cycle}}
    simulation.vpi_nettype.decl @a id 0 in 1 owner @top
        hierarchy "top.a" debug "a" {
      direct_alias = @b,
      target_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
    simulation.vpi_nettype.decl @b id 1 in 1 owner @top
        hierarchy "top.b" debug "b" {
      direct_alias = @a,
      target_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @wrong_resolver_kind {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" {vpi_kind = 32 : i32}
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    simulation.vpi_object.anchor @task id 1 type 59 in 1 parent @top ordinal 0
        hierarchy "top.resolve" debug "resolve"
    // expected-error @+1 {{resolution function must reference a function VPI anchor}}
    simulation.vpi_nettype.decl @nt id 0 in 1 owner @top
        hierarchy "top.nt" debug "nt" {
      resolution_function = @task,
      target_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @unknown_net_nettype {
    simulation.scope.decl 0 hierarchy "$root"
    // expected-error @+1 {{references an unknown user-defined nettype}}
    simulation.net.decl 0 in 0 : !simulation.logic<1> design {
      nettype = @missing,
      vpi_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @alias_type_mismatch {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" {vpi_kind = 32 : i32}
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    simulation.vpi_nettype.decl @base id 0 in 1 owner @top
        hierarchy "top.base" debug "base" {
      target_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
    // expected-error @+1 {{direct nettype alias target has a different declared data type}}
    simulation.vpi_nettype.decl @alias id 1 in 1 owner @top
        hierarchy "top.alias" debug "alias" {
      direct_alias = @base,
      target_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [1, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @net_type_mismatch {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" {vpi_kind = 32 : i32}
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    simulation.vpi_nettype.decl @nt id 0 in 1 owner @top
        hierarchy "top.nt" debug "nt" {
      target_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
    // expected-error @+1 {{VPI type semantics do not match the referenced nettype}}
    simulation.net.decl 0 in 1 : !simulation.logic<2> design {
      nettype = @nt,
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 14 : i32>
      ]>,
      vpi_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [1, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @missing_nettype_subtype {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" {vpi_kind = 32 : i32}
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    simulation.vpi_nettype.decl @nt id 0 in 1 owner @top
        hierarchy "top.nt" debug "nt" {
      target_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
    // expected-error @+1 {{user-defined nettype reference requires vpiNettypeNet or vpiInterconnect subtype}}
    simulation.net.decl 0 in 1 : !simulation.logic<1> design {
      nettype = @nt,
      vpi_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @alias_missing_effective_resolver {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" {vpi_kind = 32 : i32}
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    simulation.vpi_object.anchor @resolve id 1 type 20 in 1 parent @top
        ordinal 0 hierarchy "top.resolve" debug "resolve"
    simulation.vpi_nettype.decl @base id 0 in 1 owner @top
        hierarchy "top.base" debug "base" {
      resolution_function = @resolve,
      target_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
    // expected-error @+1 {{direct nettype alias must preserve the effective resolution function}}
    simulation.vpi_nettype.decl @alias id 1 in 1 owner @top
        hierarchy "top.alias" debug "alias" {
      direct_alias = @base,
      target_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @alias_changed_effective_resolver {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" {vpi_kind = 32 : i32}
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    simulation.vpi_object.anchor @resolve id 1 type 20 in 1 parent @top
        ordinal 0 hierarchy "top.resolve" debug "resolve"
    simulation.vpi_object.anchor @other id 2 type 20 in 1 parent @top
        ordinal 1 hierarchy "top.other" debug "other"
    simulation.vpi_nettype.decl @base id 0 in 1 owner @top
        hierarchy "top.base" debug "base" {
      resolution_function = @resolve,
      target_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
    // expected-error @+1 {{direct nettype alias must preserve the effective resolution function}}
    simulation.vpi_nettype.decl @alias id 1 in 1 owner @top
        hierarchy "top.alias" debug "alias" {
      direct_alias = @base, resolution_function = @other,
      target_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @collapsed_net_type_mismatch {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" {vpi_kind = 32 : i32}
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    simulation.vpi_nettype.decl @nt id 0 in 1 owner @top
        hierarchy "top.nt" debug "nt" {
      target_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [1, 0], children = [], childNames = []>
    }
    simulation.net.decl 0 in 1 : !simulation.logic<1> design {
      vpi_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
    // expected-error @+1 {{VPI type semantics do not match the referenced nettype}}
    simulation.vpi_net_identity.decl 0 backed_by 0 in 1
        : !simulation.logic<1> hierarchy "top.alias" debug "alias" {
      nettype = @nt,
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 14 : i32>
      ]>,
      vpi_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
  }
}
