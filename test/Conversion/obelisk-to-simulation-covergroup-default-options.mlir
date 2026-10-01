// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' > %t.mlir
// RUN: %python %S/Inputs/dump-coverage-schema.py < %t.mlir \
// RUN:   | FileCheck %s --check-prefix=SCHEMA

module {
  obelisk.sv.symbol.definition @m attributes {definition_kind = 0 : i32, hierarchical_name = "m", name = "m", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.instance @i attributes {hierarchical_name = "m", is_uninstantiated = false, name = "m", node_id = 3 : i64, referenced_path = "m", referenced_symbol = @m} {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "m", name = "m", node_id = 4 : i64} {
        obelisk.sv.type.covergroup_type @cg attributes {constructor_argument_count = 0 : i64, constructor_formals = [], coverage_event_kind = 0 : i32, has_coverage_event = false, hierarchical_name = "m.cg", name = "cg", node_id = 8 : i64, sample_formal_count = 0 : i64, sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@body::@cg>} {
          obelisk.sv.symbol.covergroup_body @s9 attributes {hierarchical_name = "m.cg", node_id = 9 : i64, option_count = 5 : i64} {
            obelisk.sv.coverage.option attributes {node_id = 24 : i64, option_kind = 0 : i32, owner_kind = 0 : i32, owner_symbol = @cg, scope_kind = 0 : i32} {
              obelisk.sv.expression.string_literal attributes {constant_value = "named", node_id = 25 : i64, semantic_type = !obelisk.string} {
              }
            }
            obelisk.sv.coverage.option attributes {node_id = 10 : i64, option_kind = 5 : i32, owner_kind = 0 : i32, owner_symbol = @cg, scope_kind = 0 : i32} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 11 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
            obelisk.sv.coverage.option attributes {node_id = 12 : i64, option_kind = 4 : i32, owner_kind = 0 : i32, owner_symbol = @cg, scope_kind = 0 : i32} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "3", node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
            obelisk.sv.coverage.option attributes {node_id = 20 : i64, option_kind = 1 : i32, owner_kind = 0 : i32, owner_symbol = @cg, scope_kind = 0 : i32} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "5", node_id = 21 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
            obelisk.sv.coverage.option attributes {node_id = 22 : i64, option_kind = 2 : i32, owner_kind = 0 : i32, owner_symbol = @cg, scope_kind = 0 : i32} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "75", node_id = 23 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
            obelisk.sv.symbol.coverpoint @cp attributes {expression_roles = [0 : i32], has_iff = false, hierarchical_name = "m.cg.cp", name = "cp", node_id = 14 : i64, option_count = 2 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 15 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
              }
              obelisk.sv.coverage.option attributes {node_id = 16 : i64, option_kind = 5 : i32, owner_kind = 1 : i32, owner_symbol = @cp, scope_kind = 0 : i32} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "4", node_id = 17 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.coverage.option attributes {node_id = 18 : i64, option_kind = 4 : i32, owner_kind = 1 : i32, owner_symbol = @cp, scope_kind = 0 : i32} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// Group properties and defaults are owned by the functional type. Item
// overrides retain their item owner, and option result ordinals preserve
// definition order.
// SCHEMA-DAG: functional_type id=[[TYPE:[1-9][0-9]*]] name={{.*}} language={{2017|2023}} hierarchy=m.cg
// SCHEMA-DAG: functional_item id=[[ITEM:[1-9][0-9]*]] type=[[TYPE]] name=cp kind=1 ordinal=0 hierarchy=m.cg.cp
// SCHEMA-DAG: functional_expression id=[[GROUP_NAME:[1-9][0-9]*]] owner=[[TYPE]] owner_kind=1 role=13 result_kind=6 width=0 signedness=3 owner_ordinal=13 owner_subordinal=1 phase=4 result_ordinal=0
// SCHEMA-DAG: functional_expression id=[[GROUP_AUTO:[1-9][0-9]*]] owner=[[TYPE]] owner_kind=1 role=13 result_kind=2 width=32 signedness=2 owner_ordinal=4 owner_subordinal=1 phase=4 result_ordinal=1
// SCHEMA-DAG: functional_expression id=[[GROUP_HITS:[1-9][0-9]*]] owner=[[TYPE]] owner_kind=1 role=13 result_kind=2 width=32 signedness=2 owner_ordinal=3 owner_subordinal=1 phase=4 result_ordinal=2
// SCHEMA-DAG: functional_expression id=[[GROUP_WEIGHT:[1-9][0-9]*]] owner=[[TYPE]] owner_kind=1 role=13 result_kind=2 width=32 signedness=2 owner_ordinal=2 owner_subordinal=1 phase=4 result_ordinal=3
// SCHEMA-DAG: functional_expression id=[[GROUP_GOAL:[1-9][0-9]*]] owner=[[TYPE]] owner_kind=1 role=13 result_kind=2 width=32 signedness=2 owner_ordinal=1 owner_subordinal=1 phase=4 result_ordinal=4
// SCHEMA-DAG: functional_expression id=[[POINT_AUTO:[1-9][0-9]*]] owner=[[ITEM]] owner_kind=2 role=13 result_kind=2 width=32 signedness=2 owner_ordinal=4 owner_subordinal=1 phase=4 result_ordinal=5
// SCHEMA-DAG: functional_expression id=[[POINT_HITS:[1-9][0-9]*]] owner=[[ITEM]] owner_kind=2 role=13 result_kind=2 width=32 signedness=2 owner_ordinal=3 owner_subordinal=1 phase=4 result_ordinal=6
// SCHEMA-DAG: functional_option_plan owner=[[TYPE]] expression=[[GROUP_NAME]] owner_kind=1 scope=1 option=13 ordinal=13 flags=0
// SCHEMA-DAG: functional_option_plan owner=[[TYPE]] expression=[[GROUP_GOAL]] owner_kind=1 scope=1 option=1 ordinal=1 flags=0
// SCHEMA-DAG: functional_option_plan owner=[[TYPE]] expression=[[GROUP_WEIGHT]] owner_kind=1 scope=1 option=2 ordinal=2 flags=0
// SCHEMA-DAG: functional_option_plan owner=[[TYPE]] expression=[[GROUP_HITS]] owner_kind=1 scope=1 option=3 ordinal=3 flags=0
// SCHEMA-DAG: functional_option_plan owner=[[TYPE]] expression=[[GROUP_AUTO]] owner_kind=1 scope=1 option=4 ordinal=4 flags=0
// SCHEMA-DAG: functional_option_plan owner=[[ITEM]] expression=[[POINT_HITS]] owner_kind=2 scope=1 option=3 ordinal=3 flags=0
// SCHEMA-DAG: functional_option_plan owner=[[ITEM]] expression=[[POINT_AUTO]] owner_kind=2 scope=1 option=4 ordinal=4 flags=0
