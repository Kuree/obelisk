// RUN: obelisk-opt %s --split-input-file '--lower-obelisk-to-sim=opt-level=0' --verify-diagnostics -o /dev/null

module {
  obelisk.sv.symbol.definition @leaf_def attributes {definition_kind = 0 : i32,
      hierarchical_name = "leaf", name = "leaf", node_id = 0 : i64
  } {}
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.instance_array @bad attributes {array_range = array<i64: 1, 0>,
        hierarchical_name = "bad", name = "bad", node_id = 2 : i64
    } {
      obelisk.sv.symbol.instance_array @left attributes {
          array_range = array<i64: 0, 1>, hierarchical_name = "bad",
          node_id = 3 : i64} {
        obelisk.sv.symbol.instance @left_0 attributes {hierarchical_name = "bad[0][0]",
            is_uninstantiated = false, node_id = 4 : i64,
            referenced_path = "leaf", referenced_symbol = @leaf_def
        } {}
        obelisk.sv.symbol.instance @left_1 attributes {hierarchical_name = "bad[0][1]",
            is_uninstantiated = false, node_id = 5 : i64,
            referenced_path = "leaf", referenced_symbol = @leaf_def
        } {}
      }
      // expected-error @+1 {{instance-array branches have mismatched dimension ranges}}
      obelisk.sv.symbol.instance_array @right attributes {
          array_range = array<i64: 1, 0>, hierarchical_name = "bad",
          node_id = 6 : i64} {
        obelisk.sv.symbol.instance @right_0 attributes {hierarchical_name = "bad[1][0]",
            is_uninstantiated = false, node_id = 7 : i64,
            referenced_path = "leaf", referenced_symbol = @leaf_def
        } {}
        obelisk.sv.symbol.instance @right_1 attributes {hierarchical_name = "bad[1][1]",
            is_uninstantiated = false, node_id = 8 : i64,
            referenced_path = "leaf", referenced_symbol = @leaf_def
        } {}
      }
    }
  }
}

// -----

module {
  obelisk.sv.symbol.definition @leaf_def attributes {definition_kind = 0 : i32,
      hierarchical_name = "leaf", name = "leaf", node_id = 0 : i64
  } {}
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64} {
    // expected-error @+1 {{instance-array dimension has 1 elements but its source range requires 2}}
    obelisk.sv.symbol.instance_array @short attributes {array_range = array<i64: 1, 0>,
        hierarchical_name = "short", name = "short", node_id = 2 : i64
    } {
      obelisk.sv.symbol.instance @only attributes {hierarchical_name = "short[0]",
          is_uninstantiated = false, node_id = 3 : i64,
          referenced_path = "leaf", referenced_symbol = @leaf_def
      } {}
    }
  }
}

// -----

module {
  obelisk.sv.symbol.definition @leaf_def attributes {definition_kind = 0 : i32,
      hierarchical_name = "leaf", name = "leaf", node_id = 0 : i64
  } {}
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64} {
    // expected-error @+1 {{instance-array dimension mixes nested arrays and leaves}}
    obelisk.sv.symbol.instance_array @mixed attributes {array_range = array<i64: 1, 0>,
        hierarchical_name = "mixed", name = "mixed", node_id = 2 : i64
    } {
      obelisk.sv.symbol.instance_array @nested attributes {
          array_range = array<i64: 0, 0>, hierarchical_name = "mixed",
          node_id = 3 : i64} {
        obelisk.sv.symbol.instance @nested_leaf attributes {hierarchical_name = "mixed[0][0]",
            is_uninstantiated = false, node_id = 4 : i64,
            referenced_path = "leaf", referenced_symbol = @leaf_def
        } {}
      }
      obelisk.sv.symbol.instance @direct_leaf attributes {hierarchical_name = "mixed[1]",
          is_uninstantiated = false, node_id = 5 : i64,
          referenced_path = "leaf", referenced_symbol = @leaf_def
      } {}
    }
  }
}

// -----

module {
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 0 : i64} {
    // expected-error @+1 {{generate-array source indices do not match its elements}}
    obelisk.sv.symbol.generate_block_array @g attributes {
        array_indices = array<i64: -3, 5>, hierarchical_name = "g",
        name = "g", node_id = 1 : i64} {
      obelisk.sv.symbol.generate_block @g_neg3 attributes {hierarchical_name = "g[-3]",
          node_id = 2 : i64} {}
    }
  }
}

// -----

module {
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 0 : i64} {
    obelisk.sv.symbol.generate_block_array @inactive attributes {
        array_indices = array<i64: -3, 5>, hierarchical_name = "inactive",
        name = "inactive", node_id = 1 : i64} {
      // expected-error @+1 {{generate-array contains an uninstantiated indexed element}}
      obelisk.sv.symbol.generate_block @inactive_neg3 attributes {
          hierarchical_name = "inactive[-3]", is_uninstantiated = true,
          node_id = 2 : i64} {}
      obelisk.sv.symbol.generate_block @inactive_5 attributes {
          hierarchical_name = "inactive[5]", is_uninstantiated = false,
          node_id = 3 : i64} {}
    }
  }
}

// -----

module {
  obelisk.sv.symbol.definition @leaf_def attributes {definition_kind = 0 : i32,
      hierarchical_name = "leaf", name = "leaf", node_id = 0 : i64
  } {}
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.instance_array @missing attributes {array_range = array<i64: 0, 0>,
        hierarchical_name = "missing", name = "missing", node_id = 2 : i64
    } {
      // expected-error @+1 {{instance-array dimension requires an exact source range}}
      obelisk.sv.symbol.instance_array @inner attributes {
          hierarchical_name = "missing", node_id = 3 : i64
      } {
        obelisk.sv.symbol.instance @leaf attributes {
            hierarchical_name = "missing[0][0]", is_uninstantiated = false,
            node_id = 4 : i64, referenced_path = "leaf",
            referenced_symbol = @leaf_def} {}
      }
    }
  }
}

// -----

module {
  obelisk.sv.symbol.definition @leaf_def attributes {definition_kind = 0 : i32,
      hierarchical_name = "leaf", name = "leaf", node_id = 0 : i64
  } {}
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.instance_array @ragged attributes {array_range = array<i64: 1, 0>,
        hierarchical_name = "ragged", name = "ragged", node_id = 2 : i64
    } {
      obelisk.sv.symbol.instance_array @shallow attributes {
          array_range = array<i64: 0, 0>, hierarchical_name = "ragged",
          node_id = 3 : i64} {
        obelisk.sv.symbol.instance @shallow_leaf attributes {
            hierarchical_name = "ragged[0][0]", is_uninstantiated = false,
            node_id = 4 : i64, referenced_path = "leaf",
            referenced_symbol = @leaf_def} {}
      }
      obelisk.sv.symbol.instance_array @deep attributes {
          array_range = array<i64: 0, 0>, hierarchical_name = "ragged",
          node_id = 5 : i64} {
        obelisk.sv.symbol.instance_array @deeper attributes {
            array_range = array<i64: 0, 0>, hierarchical_name = "ragged",
            node_id = 6 : i64} {
          // expected-error @+1 {{instance-array branches have mismatched terminal ranks}}
          obelisk.sv.symbol.instance @deep_leaf attributes {
              hierarchical_name = "ragged[1][0][0]",
              is_uninstantiated = false, node_id = 7 : i64,
              referenced_path = "leaf", referenced_symbol = @leaf_def
          } {}
        }
      }
    }
  }
}

// -----

module {
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 0 : i64} {
    // expected-error @+1 {{generate-array source indices must be unique}}
    obelisk.sv.symbol.generate_block_array @duplicate attributes {
        array_indices = array<i64: 5, 5>, hierarchical_name = "duplicate",
        name = "duplicate", node_id = 1 : i64} {
      obelisk.sv.symbol.generate_block @first attributes {
          hierarchical_name = "duplicate[5]", node_id = 2 : i64
      } {}
      obelisk.sv.symbol.generate_block @second attributes {
          hierarchical_name = "duplicate[5]", node_id = 3 : i64
      } {}
    }
  }
}
