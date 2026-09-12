// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' > %t.mlir
// RUN: %python %S/Inputs/dump-coverage-schema.py < %t.mlir | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "m", name = "m", node_id = 0 : i64, sym_name = "m"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.instance attributes {hierarchical_name = "m", is_uninstantiated = false, name = "m", node_id = 3 : i64, referenced_path = "m", referenced_symbol = @m, sym_name = "i"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "m", name = "m", node_id = 4 : i64, sym_name = "body"} {
        obelisk.sv.type.covergroup_type attributes {constructor_argument_count = 0 : i64, constructor_formals = [], coverage_event_kind = 0 : i32, has_coverage_event = false, hierarchical_name = "m.cg", name = "cg", node_id = 8 : i64, sample_formal_count = 0 : i64, sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@body::@cg>, sym_name = "cg"} {
          obelisk.sv.symbol.covergroup_body attributes {hierarchical_name = "m.cg", node_id = 9 : i64, option_count = 0 : i64, sym_name = "s9"} {
            obelisk.sv.symbol.coverpoint attributes {expression_roles = [0 : i32], has_iff = false, hierarchical_name = "m.cg.cp", name = "cp", node_id = 33 : i64, option_count = 0 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, sym_name = "cp"} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 999 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
              }
              obelisk.sv.symbol.coverage_bin attributes {bins_kind = 0 : i32, child_roles = array<i64: 6, 7, 6, 7, 6, 7>, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "m.cg.cp.transition", is_array = false, is_default = false, is_default_sequence = false, is_wildcard = false, name = "transition", node_id = 54 : i64, sym_name = "transition", transition_range_has_repeat_from = array<i64: 1, 1, 1>, transition_range_has_repeat_to = array<i64: 0, 0, 0>, transition_range_item_counts = array<i64: 1, 1, 1>, transition_range_repeat_kinds = array<i64: 1, 3, 2>, transition_set_count = 3 : i64, transition_set_range_counts = array<i64: 1, 1, 1>, value_count = 0 : i64} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 55 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "3", node_id = 56 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 57 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 58 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 59 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 60 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.symbol.coverage_bin attributes {bins_kind = 0 : i32, child_roles = array<i64: 6, 7, 6>, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "m.cg.cp.multiple", is_array = true, is_default = false, is_default_sequence = false, is_wildcard = false, name = "multiple", node_id = 62 : i64, sym_name = "multiple", transition_range_has_repeat_from = array<i64: 1, 0>, transition_range_has_repeat_to = array<i64: 0, 0>, transition_range_item_counts = array<i64: 1, 1>, transition_range_repeat_kinds = array<i64: 1, 0>, transition_set_count = 1 : i64, transition_set_range_counts = array<i64: 2>, value_count = 0 : i64} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 63 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 65 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 64 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.symbol.coverage_bin attributes {bins_kind = 0 : i32, child_roles = array<i64>, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "m.cg.cp.default_sequence", is_array = false, is_default = false, is_default_sequence = true, is_wildcard = false, name = "default_sequence", node_id = 61 : i64, sym_name = "default_sequence", transition_range_has_repeat_from = array<i64>, transition_range_has_repeat_to = array<i64>, transition_range_item_counts = array<i64>, transition_range_repeat_kinds = array<i64>, transition_set_count = 0 : i64, transition_set_range_counts = array<i64>, value_count = 0 : i64} {
              }
            }
          }
        }
      }
    }
  }
}

// CHECK-DAG: functional_bin id=[[BIN:[1-9][0-9]*]] item=[[ITEM:[1-9][0-9]*]] name=transition kind=2
// CHECK-DAG: functional_bin id=[[MULTIPLE:[1-9][0-9]*]] item=[[ITEM]] name=multiple kind=2 flags=0 ordinal=1
// CHECK-DAG: functional_bin id=[[DEFAULT:[1-9][0-9]*]] item=[[ITEM]] name=default_sequence kind=2 flags=2 ordinal=2
// CHECK-DAG: transition_program bin=[[BIN]] item=[[ITEM]] first_alternative={{[0-9]+}} alternative_count=3 flags=0
// CHECK-DAG: transition_alternative bin=[[BIN]] terminal_value_set=[[CONSEC:[1-9][0-9]*]] first_step={{[0-9]+}} step_count=1 ordinal=0 flags=0
// CHECK-DAG: transition_alternative bin=[[BIN]] terminal_value_set=[[GOTO:[1-9][0-9]*]] first_step={{[0-9]+}} step_count=1 ordinal=1 flags=0
// CHECK-DAG: transition_alternative bin=[[BIN]] terminal_value_set=[[NONCONSEC:[1-9][0-9]*]] first_step={{[0-9]+}} step_count=1 ordinal=2 flags=0
// CHECK-DAG: transition_step bin=[[BIN]] value_set=[[CONSEC]] lower_expression={{[1-9][0-9]*}} upper_expression=0 lower_bound=1 upper_bound=1 alternative_ordinal=0 ordinal=0 repetition=2 flags=1
// CHECK-DAG: transition_step bin=[[BIN]] value_set=[[GOTO]] lower_expression={{[1-9][0-9]*}} upper_expression=0 lower_bound=1 upper_bound=1 alternative_ordinal=1 ordinal=0 repetition=3 flags=1
// CHECK-DAG: transition_step bin=[[BIN]] value_set=[[NONCONSEC]] lower_expression={{[1-9][0-9]*}} upper_expression=0 lower_bound=1 upper_bound=1 alternative_ordinal=2 ordinal=0 repetition=4 flags=1
// CHECK-DAG: functional_bin_plan bin=[[BIN]] value_set=0 iff_expression=0 cardinality_expression=0 array_cardinality=0 array_mode=1 distribution=1 flags=0
// CHECK-DAG: transition_program bin=[[MULTIPLE]] item=[[ITEM]] first_alternative={{[0-9]+}} alternative_count=1 flags=0
// CHECK-DAG: transition_alternative bin=[[MULTIPLE]] terminal_value_set=[[MULTI_LAST:[1-9][0-9]*]] first_step={{[0-9]+}} step_count=2 ordinal=0 flags=0
// CHECK-DAG: transition_step bin=[[MULTIPLE]] value_set={{[1-9][0-9]*}} lower_expression={{[1-9][0-9]*}} upper_expression=0 lower_bound=1 upper_bound=1 alternative_ordinal=0 ordinal=0 repetition=2 flags=1
// CHECK-DAG: transition_step bin=[[MULTIPLE]] value_set=[[MULTI_LAST]] lower_expression=0 upper_expression=0 lower_bound=1 upper_bound=1 alternative_ordinal=0 ordinal=1 repetition=1 flags=0
// CHECK-DAG: functional_bin_plan bin=[[MULTIPLE]] value_set=0 iff_expression=0 cardinality_expression=0 array_cardinality=0 array_mode=2 distribution=2 flags=0
// CHECK-DAG: transition_program bin=[[DEFAULT]] item=[[ITEM]] first_alternative={{[0-9]+}} alternative_count=0 flags=0
// CHECK-DAG: functional_bin_plan bin=[[DEFAULT]] value_set=0 iff_expression=0 cardinality_expression=0 array_cardinality=0 array_mode=1 distribution=1 flags=0
