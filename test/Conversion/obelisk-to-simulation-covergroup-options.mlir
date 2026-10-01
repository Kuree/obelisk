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
          obelisk.sv.symbol.covergroup_body @s9 attributes {hierarchical_name = "m.cg", node_id = 9 : i64, option_count = 0 : i64} {
            obelisk.sv.symbol.coverpoint @cp attributes {expression_roles = [0 : i32], has_iff = false, hierarchical_name = "m.cg.cp", name = "cp", node_id = 33 : i64, option_count = 4 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 34 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
              }
              obelisk.sv.coverage.option attributes {node_id = 35 : i64, option_kind = 5 : i32, owner_kind = 1 : i32, owner_symbol = @cp, scope_kind = 0 : i32} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "8", node_id = 36 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.coverage.option attributes {node_id = 37 : i64, option_kind = 4 : i32, owner_kind = 1 : i32, owner_symbol = @cp, scope_kind = 0 : i32} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 38 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.coverage.option attributes {node_id = 39 : i64, option_kind = 1 : i32, owner_kind = 1 : i32, owner_symbol = @cp, scope_kind = 0 : i32} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "3", node_id = 40 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.coverage.option attributes {node_id = 41 : i64, option_kind = 2 : i32, owner_kind = 1 : i32, owner_symbol = @cp, scope_kind = 0 : i32} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "50", node_id = 42 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// SCHEMA-DAG: functional_expression id=[[GOAL:[1-9][0-9]*]] owner=[[ITEM:[1-9][0-9]*]] owner_kind=2 role=13 result_kind=2 width=32 signedness=2 owner_ordinal=1 owner_subordinal=1 phase=4 result_ordinal=3
// SCHEMA-DAG: functional_expression id=[[WEIGHT:[1-9][0-9]*]] owner=[[ITEM]] owner_kind=2 role=13 result_kind=2 width=32 signedness=2 owner_ordinal=2 owner_subordinal=1 phase=4 result_ordinal=2
// SCHEMA-DAG: functional_expression id=[[ATLEAST:[1-9][0-9]*]] owner=[[ITEM]] owner_kind=2 role=13 result_kind=2 width=32 signedness=2 owner_ordinal=3 owner_subordinal=1 phase=4 result_ordinal=1
// SCHEMA-DAG: functional_expression id=[[AUTOMAX:[1-9][0-9]*]] owner=[[ITEM]] owner_kind=2 role=13 result_kind=2 width=32 signedness=2 owner_ordinal=4 owner_subordinal=1 phase=4 result_ordinal=0
// SCHEMA-DAG: functional_option_plan owner=[[ITEM]] expression=[[GOAL]] owner_kind=2 scope=1 option=1 ordinal=1 flags=0
// SCHEMA-DAG: functional_option_plan owner=[[ITEM]] expression=[[WEIGHT]] owner_kind=2 scope=1 option=2 ordinal=2 flags=0
// SCHEMA-DAG: functional_option_plan owner=[[ITEM]] expression=[[ATLEAST]] owner_kind=2 scope=1 option=3 ordinal=3 flags=0
// SCHEMA-DAG: functional_option_plan owner=[[ITEM]] expression=[[AUTOMAX]] owner_kind=2 scope=1 option=4 ordinal=4 flags=0
