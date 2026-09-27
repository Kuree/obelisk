// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

// A FixedImage property and an independent definition site are valid static
// inventory. Neither attribute changes executable scheduling.
module {
  simulation.design @valid {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32 {
      is_protected,
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 74 : i32, value = true>
      ]>,
      definition_loc = loc("definition.sv":3:1)
    } loc("use.sv":19:7)
  }
}

// -----

module {
  simulation.design @invalid_net_type_domain {
    simulation.scope.decl 0 hierarchy "top"
    // expected-error @+1 {{VPI integer property value is outside its generated domain}}
    simulation.net.decl 0 in 0 : !simulation.logic<1> design {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 22 : i32, value = 12 : i32>]>}
    // expected-error @-1 {{failed to parse SimVPIPropertySetAttr parameter}}
  }
}

// -----

module {
  simulation.design @invalid_charge_strength_domain {
    simulation.scope.decl 0 hierarchy "top"
    // expected-error @+1 {{VPI integer property value is outside its generated domain}}
    simulation.net.decl 0 in 0 : !simulation.logic<1> design {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 27 : i32, value = 3 : i32>]>}
    // expected-error @-1 {{failed to parse SimVPIPropertySetAttr parameter}}
  }
}

// -----

module {
  simulation.design @derived_property {
    // expected-error @+1 {{VPI property 1 is not a FixedImage property}}
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32 {
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 1 : i32, value = 32 : i32>
      ]>
    }
  }
}

// -----

module {
  simulation.design @wrong_value_kind {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32 {
      vpi_properties = #simulation.vpi_properties<[
        // expected-error @+1 {{VPI property value does not match its generated value kind}}
        #simulation.vpi_property<selector = 74 : i32, value = 1 : i32>
        // expected-error @+1 {{failed to parse SimVPIPropertySetAttr parameter}}
      ]>
    }
  }
}

// -----

module {
  simulation.design @duplicate_definition_file {
    // expected-error @+1 {{VPI definition file and line properties must use definition_loc as their canonical IR representation}}
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32 {
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 15 : i32, value = "other.sv">
      ]>,
      definition_loc = loc("definition.sv":3:1)
    }
  }
}

// -----

module {
  simulation.design @duplicate_definition_line {
    // expected-error @+1 {{VPI definition file and line properties must use definition_loc as their canonical IR representation}}
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32 {
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 16 : i32, value = 7 : i32>
      ]>,
      definition_loc = loc("definition.sv":3:1)
    }
  }
}

// -----

module {
  simulation.design @duplicate_property {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32 {
      // expected-error @+1 {{VPI property set must be sorted and unique}}
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 74 : i32, value = false>,
        #simulation.vpi_property<selector = 74 : i32, value = true>
      ]>
    }
  }
}

// -----

module {
  simulation.design @unsorted_property {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32 {
      // expected-error @+1 {{VPI property set must be sorted and unique}}
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 74 : i32, value = true>,
        #simulation.vpi_property<selector = 15 : i32, value = "def.sv">
      ]>
    }
  }
}

// -----

module {
  simulation.design @definition_not_applicable {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    // expected-error @+1 {{definition_loc is not applicable to this exact VPI object kind}}
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial" {
      definition_loc = loc("definition.sv":3:1)
    }
  }
}

// -----

module {
  simulation.design @authored_always_type {
    simulation.scope.decl 0 hierarchy "top"
    // expected-error @+1 {{vpiAlwaysType must use code_unit_kind as its canonical IR representation}}
    simulation.code_unit.decl 1 in 0 always hierarchy "top.always" {
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 624 : i32, value = 2 : i32>
      ]>
    }
  }
}

// -----

module {
  simulation.design @property_not_applicable {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    // expected-error @+1 {{VPI property 7 is not applicable to exact object kind 15}}
    simulation.statement.decl 1 in 1 scope 0 type 15 {
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 7 : i32, value = true>
      ]>
    }
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
  }
}

// -----

module {
  simulation.design @runtime_property {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    // expected-error @+1 {{VPI property 608 is not a FixedImage property}}
    simulation.storage.decl 1 in 0 : i8 design {
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 608 : i32, value = false>
      ]>
    }
  }
}

// -----

module {
  simulation.design @conflicting_protection {
    // expected-error @+1 {{explicit vpiIsProtected value conflicts with is_protected}}
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32 {
      is_protected,
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 74 : i32, value = false>
      ]>
    }
  }
}

// -----

module {
  simulation.design @unknown_property {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32 {
      vpi_properties = #simulation.vpi_properties<[
        // expected-error @+1 {{unknown VPI property selector 65535}}
        #simulation.vpi_property<selector = 65535 : i32, value = true>
        // expected-error @+1 {{failed to parse SimVPIPropertySetAttr parameter}}
      ]>
    }
  }
}

// -----

module {
  simulation.design @malformed_property_set {
    // expected-error @+1 {{vpi_properties must be a #simulation.vpi_properties attribute}}
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32 {
      vpi_properties = "not a property set"
    }
  }
}

// -----

module {
  simulation.design @malformed_definition_location {
    // expected-error @+1 {{definition_loc must be a location attribute}}
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32 {
      definition_loc = 3 : i32
    }
  }
}

// -----

module {
  simulation.design @malformed_protection {
    // expected-error @+1 {{is_protected must be a unit attribute}}
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32 {
      is_protected = true
    }
  }
}

// -----

module {
  simulation.design @protection_not_applicable {
    // expected-error @+1 {{is_protected is not applicable to this exact VPI object kind}}
    simulation.scope.decl 0 {is_protected}
  }
}

// -----

module {
  simulation.design @internal_code_unit_not_reflected {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    // expected-error @+1 {{is_protected is not applicable to this exact VPI object kind}}
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.hidden" {
      internal,
      is_protected
    }
  }
}
