// RUN: obelisk-opt %s --obelisk-sim-prepare --verify-diagnostics -o /dev/null

module attributes {obelisk.coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.root @root attributes {name = "$root",
      node_id = 0 : i64} {
    obelisk.sv.symbol.coverpoint @s22.cp attributes {expression_roles = [0 : i32],
        has_iff = false,
        hierarchical_name = "real_coverage.cg.cp",
        name = "cp",
        node_id = 34 : i64,
        option_count = 0 : i64,
        semantic_type = !obelisk.real
    } {
      obelisk.sv.expression.real_literal attributes {constant_value = "1",
          node_id = 1 : i64,
          semantic_type = !obelisk.real} {}
      // expected-error @+1 {{a default bin for a real coverpoint cannot be an array}}
      obelisk.sv.symbol.coverage_bin @s41.other attributes {bins_kind = 0 : i32,
          child_roles = array<i64>,
          has_iff = false,
          has_number_of_bins = false,
          has_set_coverage = false,
          has_with = false,
          hierarchical_name = "real_coverage.cg.cp.other",
          is_array = true,
          is_default = true,
          is_default_sequence = false,
          is_wildcard = false,
          name = "other",
          node_id = 88 : i64,
          transition_range_has_repeat_from = array<i64>,
          transition_range_has_repeat_to = array<i64>,
          transition_range_item_counts = array<i64>,
          transition_range_repeat_kinds = array<i64>,
          transition_set_count = 0 : i64,
          transition_set_range_counts = array<i64>,
          value_count = 0 : i64} {
      }
    }
  }
}
