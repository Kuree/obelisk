// RUN: obelisk-opt %s --split-input-file '--lower-obelisk-to-sim=opt-level=0' --verify-diagnostics -o /dev/null

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32,
      hierarchical_name = "leaf", name = "leaf", node_id = 0 : i64,
      sym_name = "leaf_def"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.instance_array attributes {array_range = array<i64: 1, 0>,
        hierarchical_name = "bad", name = "bad", node_id = 2 : i64,
        sym_name = "bad"} {
      obelisk.sv.symbol.instance_array attributes {
          array_range = array<i64: 0, 1>, hierarchical_name = "bad",
          node_id = 3 : i64, sym_name = "left"} {
        obelisk.sv.symbol.instance attributes {hierarchical_name = "bad[0][0]",
            is_uninstantiated = false, node_id = 4 : i64,
            referenced_path = "leaf", referenced_symbol = @leaf_def,
            sym_name = "left_0"} {}
        obelisk.sv.symbol.instance attributes {hierarchical_name = "bad[0][1]",
            is_uninstantiated = false, node_id = 5 : i64,
            referenced_path = "leaf", referenced_symbol = @leaf_def,
            sym_name = "left_1"} {}
      }
      // expected-error @+1 {{instance-array branches have mismatched dimension ranges}}
      obelisk.sv.symbol.instance_array attributes {
          array_range = array<i64: 1, 0>, hierarchical_name = "bad",
          node_id = 6 : i64, sym_name = "right"} {
        obelisk.sv.symbol.instance attributes {hierarchical_name = "bad[1][0]",
            is_uninstantiated = false, node_id = 7 : i64,
            referenced_path = "leaf", referenced_symbol = @leaf_def,
            sym_name = "right_0"} {}
        obelisk.sv.symbol.instance attributes {hierarchical_name = "bad[1][1]",
            is_uninstantiated = false, node_id = 8 : i64,
            referenced_path = "leaf", referenced_symbol = @leaf_def,
            sym_name = "right_1"} {}
      }
    }
  }
}

// -----

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32,
      hierarchical_name = "leaf", name = "leaf", node_id = 0 : i64,
      sym_name = "leaf_def"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64, sym_name = "root"} {
    // expected-error @+1 {{instance-array dimension has 1 elements but its source range requires 2}}
    obelisk.sv.symbol.instance_array attributes {array_range = array<i64: 1, 0>,
        hierarchical_name = "short", name = "short", node_id = 2 : i64,
        sym_name = "short"} {
      obelisk.sv.symbol.instance attributes {hierarchical_name = "short[0]",
          is_uninstantiated = false, node_id = 3 : i64,
          referenced_path = "leaf", referenced_symbol = @leaf_def,
          sym_name = "only"} {}
    }
  }
}

// -----

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32,
      hierarchical_name = "leaf", name = "leaf", node_id = 0 : i64,
      sym_name = "leaf_def"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64, sym_name = "root"} {
    // expected-error @+1 {{instance-array dimension mixes nested arrays and leaves}}
    obelisk.sv.symbol.instance_array attributes {array_range = array<i64: 1, 0>,
        hierarchical_name = "mixed", name = "mixed", node_id = 2 : i64,
        sym_name = "mixed"} {
      obelisk.sv.symbol.instance_array attributes {
          array_range = array<i64: 0, 0>, hierarchical_name = "mixed",
          node_id = 3 : i64, sym_name = "nested"} {
        obelisk.sv.symbol.instance attributes {hierarchical_name = "mixed[0][0]",
            is_uninstantiated = false, node_id = 4 : i64,
            referenced_path = "leaf", referenced_symbol = @leaf_def,
            sym_name = "nested_leaf"} {}
      }
      obelisk.sv.symbol.instance attributes {hierarchical_name = "mixed[1]",
          is_uninstantiated = false, node_id = 5 : i64,
          referenced_path = "leaf", referenced_symbol = @leaf_def,
          sym_name = "direct_leaf"} {}
    }
  }
}

// -----

module {
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 0 : i64, sym_name = "root"} {
    // expected-error @+1 {{generate-array source indices do not match its elements}}
    obelisk.sv.symbol.generate_block_array attributes {
        array_indices = array<i64: -3, 5>, hierarchical_name = "g",
        name = "g", node_id = 1 : i64, sym_name = "g"} {
      obelisk.sv.symbol.generate_block attributes {hierarchical_name = "g[-3]",
          node_id = 2 : i64, sym_name = "g_neg3"} {}
    }
  }
}

// -----

module {
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 0 : i64, sym_name = "root"} {
    obelisk.sv.symbol.generate_block_array attributes {
        array_indices = array<i64: -3, 5>, hierarchical_name = "inactive",
        name = "inactive", node_id = 1 : i64, sym_name = "inactive"} {
      // expected-error @+1 {{generate-array contains an uninstantiated indexed element}}
      obelisk.sv.symbol.generate_block attributes {
          hierarchical_name = "inactive[-3]", is_uninstantiated = true,
          node_id = 2 : i64, sym_name = "inactive_neg3"} {}
      obelisk.sv.symbol.generate_block attributes {
          hierarchical_name = "inactive[5]", is_uninstantiated = false,
          node_id = 3 : i64, sym_name = "inactive_5"} {}
    }
  }
}

// -----

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32,
      hierarchical_name = "leaf", name = "leaf", node_id = 0 : i64,
      sym_name = "leaf_def"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.instance_array attributes {array_range = array<i64: 0, 0>,
        hierarchical_name = "missing", name = "missing", node_id = 2 : i64,
        sym_name = "missing"} {
      // expected-error @+1 {{instance-array dimension requires an exact source range}}
      obelisk.sv.symbol.instance_array attributes {
          hierarchical_name = "missing", node_id = 3 : i64,
          sym_name = "inner"} {
        obelisk.sv.symbol.instance attributes {
            hierarchical_name = "missing[0][0]", is_uninstantiated = false,
            node_id = 4 : i64, referenced_path = "leaf",
            referenced_symbol = @leaf_def, sym_name = "leaf"} {}
      }
    }
  }
}

// -----

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32,
      hierarchical_name = "leaf", name = "leaf", node_id = 0 : i64,
      sym_name = "leaf_def"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.instance_array attributes {array_range = array<i64: 1, 0>,
        hierarchical_name = "ragged", name = "ragged", node_id = 2 : i64,
        sym_name = "ragged"} {
      obelisk.sv.symbol.instance_array attributes {
          array_range = array<i64: 0, 0>, hierarchical_name = "ragged",
          node_id = 3 : i64, sym_name = "shallow"} {
        obelisk.sv.symbol.instance attributes {
            hierarchical_name = "ragged[0][0]", is_uninstantiated = false,
            node_id = 4 : i64, referenced_path = "leaf",
            referenced_symbol = @leaf_def, sym_name = "shallow_leaf"} {}
      }
      obelisk.sv.symbol.instance_array attributes {
          array_range = array<i64: 0, 0>, hierarchical_name = "ragged",
          node_id = 5 : i64, sym_name = "deep"} {
        obelisk.sv.symbol.instance_array attributes {
            array_range = array<i64: 0, 0>, hierarchical_name = "ragged",
            node_id = 6 : i64, sym_name = "deeper"} {
          // expected-error @+1 {{instance-array branches have mismatched terminal ranks}}
          obelisk.sv.symbol.instance attributes {
              hierarchical_name = "ragged[1][0][0]",
              is_uninstantiated = false, node_id = 7 : i64,
              referenced_path = "leaf", referenced_symbol = @leaf_def,
              sym_name = "deep_leaf"} {}
        }
      }
    }
  }
}

// -----

module {
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 0 : i64, sym_name = "root"} {
    // expected-error @+1 {{generate-array source indices must be unique}}
    obelisk.sv.symbol.generate_block_array attributes {
        array_indices = array<i64: 5, 5>, hierarchical_name = "duplicate",
        name = "duplicate", node_id = 1 : i64, sym_name = "duplicate"} {
      obelisk.sv.symbol.generate_block attributes {
          hierarchical_name = "duplicate[5]", node_id = 2 : i64,
          sym_name = "first"} {}
      obelisk.sv.symbol.generate_block attributes {
          hierarchical_name = "duplicate[5]", node_id = 3 : i64,
          sym_name = "second"} {}
    }
  }
}
