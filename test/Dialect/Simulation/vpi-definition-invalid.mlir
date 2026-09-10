// RUN: obelisk-opt %s --split-input-file --verify-diagnostics -o /dev/null

module {
  obelisk_sim.design @unknown_definition {
    obelisk_sim.scope.decl 0 hierarchy "$root"
    // expected-error @+1 {{references an unknown VPI definition}}
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32 definition @missing
  }
}

// -----

module {
  obelisk_sim.design @mismatched_definition {
    obelisk_sim.vpi_definition.decl @iface type 601 name "iface"
    obelisk_sim.scope.decl 0 hierarchy "$root"
    // expected-error @+1 {{VPI definition kind disagrees with the scope kind}}
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32 definition @iface
  }
}

// -----

module {
  obelisk_sim.design @root_definition {
    obelisk_sim.vpi_definition.decl @cell type 32 name "cell"
    // expected-error @+1 {{root scope cannot reference a VPI definition}}
    obelisk_sim.scope.decl 0 hierarchy "$root" definition @cell
  }
}

// -----

module {
  obelisk_sim.design @invalid_definition_kind {
    // expected-error @+1 {{VPI definition kind must be a module, interface, or program}}
    obelisk_sim.vpi_definition.decl @bad type 600 name "bad"
    obelisk_sim.scope.decl 0 hierarchy "$root"
  }
}

// -----

module {
  obelisk_sim.design @empty_definition_name {
    // expected-error @+1 {{requires a nonempty source definition name}}
    obelisk_sim.vpi_definition.decl @bad type 32 name ""
    obelisk_sim.scope.decl 0 hierarchy "$root"
  }
}

// -----

module {
  obelisk_sim.design @nul_definition_name {
    // expected-error @+1 {{source definition name contains an embedded NUL}}
    obelisk_sim.vpi_definition.decl @bad type 32 name "bad\00tail"
    obelisk_sim.scope.decl 0 hierarchy "$root"
  }
}

// -----

module {
  obelisk_sim.design @nul_definition_filename {
    // expected-error @+1 {{definition_loc filename contains an embedded NUL}}
    obelisk_sim.vpi_definition.decl @bad type 32 name "bad" definition_loc loc("bad\00tail.sv":1:1)
    obelisk_sim.scope.decl 0 hierarchy "$root"
  }
}

module {
  obelisk_sim.design @oversized_definition_line {
    // expected-error @+1 {{definition_loc line exceeds the VPI 32-bit integer range}}
    obelisk_sim.vpi_definition.decl @bad type 32 name "bad" definition_loc loc("bad.sv":2147483648:1)
    obelisk_sim.scope.decl 0 hierarchy "$root"
  }
}
