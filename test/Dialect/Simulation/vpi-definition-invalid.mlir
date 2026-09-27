// RUN: obelisk-opt %s --split-input-file --verify-diagnostics -o /dev/null

module {
  simulation.design @unknown_definition {
    simulation.scope.decl 0 hierarchy "$root"
    // expected-error @+1 {{references an unknown VPI definition}}
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32 definition @missing
  }
}

// -----

module {
  simulation.design @mismatched_definition {
    simulation.vpi_definition.decl @iface type 601 name "iface"
    simulation.scope.decl 0 hierarchy "$root"
    // expected-error @+1 {{VPI definition kind disagrees with the scope kind}}
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32 definition @iface
  }
}

// -----

module {
  simulation.design @root_definition {
    simulation.vpi_definition.decl @cell type 32 name "cell"
    // expected-error @+1 {{root scope cannot reference a VPI definition}}
    simulation.scope.decl 0 hierarchy "$root" definition @cell
  }
}

// -----

module {
  simulation.design @invalid_definition_kind {
    // expected-error @+1 {{VPI definition kind must be a module, interface, or program}}
    simulation.vpi_definition.decl @bad type 600 name "bad"
    simulation.scope.decl 0 hierarchy "$root"
  }
}

// -----

module {
  simulation.design @empty_definition_name {
    // expected-error @+1 {{requires a nonempty source definition name}}
    simulation.vpi_definition.decl @bad type 32 name ""
    simulation.scope.decl 0 hierarchy "$root"
  }
}

// -----

module {
  simulation.design @nul_definition_name {
    // expected-error @+1 {{source definition name contains an embedded NUL}}
    simulation.vpi_definition.decl @bad type 32 name "bad\00tail"
    simulation.scope.decl 0 hierarchy "$root"
  }
}

// -----

module {
  simulation.design @nul_definition_filename {
    // expected-error @+1 {{definition_loc filename contains an embedded NUL}}
    simulation.vpi_definition.decl @bad type 32 name "bad" definition_loc loc("bad\00tail.sv":1:1)
    simulation.scope.decl 0 hierarchy "$root"
  }
}

module {
  simulation.design @oversized_definition_line {
    // expected-error @+1 {{definition_loc line exceeds the VPI 32-bit integer range}}
    simulation.vpi_definition.decl @bad type 32 name "bad" definition_loc loc("bad.sv":2147483648:1)
    simulation.scope.decl 0 hierarchy "$root"
  }
}
