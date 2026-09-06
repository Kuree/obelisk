// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

// A FixedImage property and an independent definition site are valid static
// inventory. Neither attribute changes executable scheduling.
module {
  obelisk_sim.design @valid {
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32 {
      is_protected,
      vpi_properties = #obelisk_sim.vpi_properties<[
        #obelisk_sim.vpi_property<selector = 74 : i32, value = true>
      ]>,
      definition_loc = loc("definition.sv":3:1)
    } loc("use.sv":19:7)
  }
}

// -----

module {
  obelisk_sim.design @invalid_net_type_domain {
    obelisk_sim.scope.decl 0 hierarchy "top"
    // expected-error @+1 {{VPI integer property value is outside its generated domain}}
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design {vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 22 : i32, value = 12 : i32>]>}
    // expected-error @-1 {{failed to parse SimVPIPropertySetAttr parameter}}
  }
}

// -----

module {
  obelisk_sim.design @invalid_charge_strength_domain {
    obelisk_sim.scope.decl 0 hierarchy "top"
    // expected-error @+1 {{VPI integer property value is outside its generated domain}}
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design {vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 27 : i32, value = 3 : i32>]>}
    // expected-error @-1 {{failed to parse SimVPIPropertySetAttr parameter}}
  }
}

// -----

module {
  obelisk_sim.design @derived_property {
    // expected-error @+1 {{VPI property 1 is not a FixedImage property}}
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32 {
      vpi_properties = #obelisk_sim.vpi_properties<[
        #obelisk_sim.vpi_property<selector = 1 : i32, value = 32 : i32>
      ]>
    }
  }
}

// -----

module {
  obelisk_sim.design @wrong_value_kind {
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32 {
      vpi_properties = #obelisk_sim.vpi_properties<[
        // expected-error @+1 {{VPI property value does not match its generated value kind}}
        #obelisk_sim.vpi_property<selector = 74 : i32, value = 1 : i32>
        // expected-error @+1 {{failed to parse SimVPIPropertySetAttr parameter}}
      ]>
    }
  }
}

// -----

module {
  obelisk_sim.design @duplicate_definition_file {
    // expected-error @+1 {{VPI definition file and line properties must use definition_loc as their canonical IR representation}}
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32 {
      vpi_properties = #obelisk_sim.vpi_properties<[
        #obelisk_sim.vpi_property<selector = 15 : i32, value = "other.sv">
      ]>,
      definition_loc = loc("definition.sv":3:1)
    }
  }
}

// -----

module {
  obelisk_sim.design @duplicate_definition_line {
    // expected-error @+1 {{VPI definition file and line properties must use definition_loc as their canonical IR representation}}
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32 {
      vpi_properties = #obelisk_sim.vpi_properties<[
        #obelisk_sim.vpi_property<selector = 16 : i32, value = 7 : i32>
      ]>,
      definition_loc = loc("definition.sv":3:1)
    }
  }
}

// -----

module {
  obelisk_sim.design @duplicate_property {
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32 {
      // expected-error @+1 {{VPI property set must be sorted and unique}}
      vpi_properties = #obelisk_sim.vpi_properties<[
        #obelisk_sim.vpi_property<selector = 74 : i32, value = false>,
        #obelisk_sim.vpi_property<selector = 74 : i32, value = true>
      ]>
    }
  }
}

// -----

module {
  obelisk_sim.design @unsorted_property {
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32 {
      // expected-error @+1 {{VPI property set must be sorted and unique}}
      vpi_properties = #obelisk_sim.vpi_properties<[
        #obelisk_sim.vpi_property<selector = 74 : i32, value = true>,
        #obelisk_sim.vpi_property<selector = 15 : i32, value = "def.sv">
      ]>
    }
  }
}

// -----

module {
  obelisk_sim.design @definition_not_applicable {
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32
    // expected-error @+1 {{definition_loc is not applicable to this exact VPI object kind}}
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial" {
      definition_loc = loc("definition.sv":3:1)
    }
  }
}

// -----

module {
  obelisk_sim.design @property_not_applicable {
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32
    // expected-error @+1 {{VPI property 7 is not applicable to exact object kind 15}}
    obelisk_sim.statement.decl 1 in 1 scope 0 type 15 {
      vpi_properties = #obelisk_sim.vpi_properties<[
        #obelisk_sim.vpi_property<selector = 7 : i32, value = true>
      ]>
    }
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
  }
}

// -----

module {
  obelisk_sim.design @runtime_property {
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32
    // expected-error @+1 {{VPI property 608 is not a FixedImage property}}
    obelisk_sim.storage.decl 1 in 0 : i8 design {
      vpi_properties = #obelisk_sim.vpi_properties<[
        #obelisk_sim.vpi_property<selector = 608 : i32, value = false>
      ]>
    }
  }
}

// -----

module {
  obelisk_sim.design @conflicting_protection {
    // expected-error @+1 {{explicit vpiIsProtected value conflicts with is_protected}}
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32 {
      is_protected,
      vpi_properties = #obelisk_sim.vpi_properties<[
        #obelisk_sim.vpi_property<selector = 74 : i32, value = false>
      ]>
    }
  }
}

// -----

module {
  obelisk_sim.design @unknown_property {
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32 {
      vpi_properties = #obelisk_sim.vpi_properties<[
        // expected-error @+1 {{unknown VPI property selector 65535}}
        #obelisk_sim.vpi_property<selector = 65535 : i32, value = true>
        // expected-error @+1 {{failed to parse SimVPIPropertySetAttr parameter}}
      ]>
    }
  }
}

// -----

module {
  obelisk_sim.design @malformed_property_set {
    // expected-error @+1 {{vpi_properties must be a #obelisk_sim.vpi_properties attribute}}
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32 {
      vpi_properties = "not a property set"
    }
  }
}

// -----

module {
  obelisk_sim.design @malformed_definition_location {
    // expected-error @+1 {{definition_loc must be a location attribute}}
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32 {
      definition_loc = 3 : i32
    }
  }
}

// -----

module {
  obelisk_sim.design @malformed_protection {
    // expected-error @+1 {{is_protected must be a unit attribute}}
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32 {
      is_protected = true
    }
  }
}

// -----

module {
  obelisk_sim.design @protection_not_applicable {
    // expected-error @+1 {{is_protected is not applicable to this exact VPI object kind}}
    obelisk_sim.scope.decl 0 {is_protected}
  }
}

// -----

module {
  obelisk_sim.design @internal_code_unit_not_reflected {
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32
    // expected-error @+1 {{is_protected is not applicable to this exact VPI object kind}}
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.hidden" {
      internal,
      is_protected
    }
  }
}
