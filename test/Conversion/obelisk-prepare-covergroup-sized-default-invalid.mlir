// RUN: obelisk-opt %s --obelisk-sim-prepare --verify-diagnostics -o /dev/null

module {
  obelisk.sv.symbol.root @root attributes {name = "$root",
      node_id = 0 : i64} {
    // expected-error @+1 {{a default coverage bin array must be unsized}}
    obelisk.sv.symbol.coverage_bin @s32.bad attributes {bins_kind = 0 : i32,
        child_roles = array<i64: 1>,
        has_iff = false,
        has_number_of_bins = true,
        has_set_coverage = false,
        has_with = false,
        hierarchical_name = "sized_default.cg.cp.bad",
        is_array = true,
        is_default = true,
        is_default_sequence = false,
        is_wildcard = false,
        name = "bad",
        node_id = 52 : i64,
        transition_range_has_repeat_from = array<i64>,
        transition_range_has_repeat_to = array<i64>,
        transition_range_item_counts = array<i64>,
        transition_range_repeat_kinds = array<i64>,
        transition_set_count = 0 : i64,
        transition_set_range_counts = array<i64>,
        value_count = 0 : i64} {
      obelisk.sv.expression.integer_literal attributes {constant_value = "2",
          is_declared_unsized = true,
          is_signed = true,
          node_id = 999999 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
    }
  }
}
