// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0 vpi=read' \
// RUN:   --mlir-print-debuginfo -o %t.mlir
// RUN: FileCheck %s --check-prefix=IR < %t.mlir
// RUN: env OBELISK_TEST_INPUT=%t.mlir \
// RUN:   %obj_root/test/obelisk-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.ScopeOwnedStatementQueries

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition @top_def attributes {definition_kind = 0 : i32,
      hierarchical_name = "top", name = "top", node_id = 0 : i64
  } {}
  obelisk.sv.symbol.definition @iface_def attributes {definition_kind = 1 : i32,
      hierarchical_name = "iface", name = "iface", node_id = 35 : i64
  } {}
  obelisk.sv.symbol.definition @prog_def attributes {definition_kind = 2 : i32,
      hierarchical_name = "prog", name = "prog", node_id = 36 : i64
  } {}
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.instance @top_i attributes {hierarchical_name = "top",
        is_uninstantiated = false, name = "top", node_id = 2 : i64,
        referenced_path = "top", referenced_symbol = @top_def
    } {
      obelisk.sv.symbol.instance_body @top_b attributes {hierarchical_name = "top",
          name = "top", node_id = 3 : i64,
          time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
        obelisk.sv.symbol.net @a attributes {hierarchical_name = "top.a",
            is_implicit = false, name = "a", net_kind = 1 : i32,
            node_id = 4 : i64,
            semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
        } {}
        obelisk.sv.symbol.net @b attributes {hierarchical_name = "top.b",
            is_implicit = false, name = "b", net_kind = 1 : i32,
            node_id = 5 : i64,
            semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
        } {}
        obelisk.sv.symbol.net @c attributes {hierarchical_name = "top.c",
            is_implicit = false, name = "c", net_kind = 1 : i32,
            node_id = 31 : i64,
            semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
        } {}
        obelisk.sv.symbol.net @d attributes {hierarchical_name = "top.d",
            is_implicit = false, name = "d", net_kind = 1 : i32,
            node_id = 32 : i64,
            semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
        } {}
        obelisk.sv.symbol.net @source attributes {hierarchical_name = "top.source",
            is_implicit = false, name = "source", net_kind = 1 : i32,
            node_id = 60 : i64,
            semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
        } {}
        obelisk.sv.symbol.net @direct_lhs attributes {hierarchical_name = "top.direct_lhs",
            is_implicit = false, name = "direct_lhs", net_kind = 1 : i32,
            node_id = 68 : i64,
            semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
        } {}
        obelisk.sv.symbol.net @vector_lhs attributes {hierarchical_name = "top.vector_lhs",
            is_implicit = false, name = "vector_lhs", net_kind = 1 : i32,
            node_id = 62 : i64,
            semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
        } {}
        obelisk.sv.symbol.net @vector_rhs attributes {hierarchical_name = "top.vector_rhs",
            is_implicit = false, name = "vector_rhs", net_kind = 1 : i32,
            node_id = 63 : i64,
            semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
        } {}
        obelisk.sv.symbol.variable @variable_lhs attributes {
            hierarchical_name = "top.variable_lhs", lifetime = 1 : i32,
            name = "variable_lhs", node_id = 69 : i64,
            semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
        } {}
        obelisk.sv.symbol.variable @variable_rhs attributes {
            hierarchical_name = "top.variable_rhs", lifetime = 1 : i32,
            name = "variable_rhs", node_id = 70 : i64,
            semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
        } {}
        obelisk.sv.symbol.continuous_assign @direct_assign attributes {
            hierarchical_name = "top", node_id = 6 : i64,
            time_precision_fs = 1 : i64,
            time_unit_fs = 1 : i64} {
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32,
              is_signed = false, node_id = 7 : i64,
              semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.named_value attributes {is_signed = false,
                node_id = 8 : i64, referenced_path = "top.direct_lhs",
                referenced_symbol = @root::@top_i::@top_b::@direct_lhs,
                semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
            obelisk.sv.expression.named_value attributes {is_signed = false,
                node_id = 61 : i64, referenced_path = "top.d",
                referenced_symbol = @root::@top_i::@top_b::@d,
                semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
          }
        } loc("scope_owned.sv":10:3)
        obelisk.sv.symbol.continuous_assign @vector_assign attributes {
            hierarchical_name = "top", node_id = 64 : i64,
            time_precision_fs = 1 : i64,
            time_unit_fs = 1 : i64} {
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32,
              is_signed = false, node_id = 65 : i64,
              semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
            obelisk.sv.expression.named_value attributes {is_signed = false,
                node_id = 66 : i64, referenced_path = "top.vector_lhs",
                referenced_symbol = @root::@top_i::@top_b::@vector_lhs,
                semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {}
            obelisk.sv.expression.named_value attributes {is_signed = false,
                node_id = 67 : i64, referenced_path = "top.vector_rhs",
                referenced_symbol = @root::@top_i::@top_b::@vector_rhs,
                semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {}
          }
        } loc("scope_owned.sv":12:3)
        obelisk.sv.symbol.net_alias @direct_alias attributes {hierarchical_name = "top",
            node_id = 10 : i64} {
          obelisk.sv.expression.named_value attributes {is_signed = false,
              node_id = 11 : i64, referenced_path = "top.a",
              referenced_symbol = @root::@top_i::@top_b::@a,
              semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
          obelisk.sv.expression.named_value attributes {is_signed = false,
              node_id = 12 : i64, referenced_path = "top.b",
              referenced_symbol = @root::@top_i::@top_b::@b,
              semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
          obelisk.sv.expression.named_value attributes {is_signed = false,
              node_id = 33 : i64, referenced_path = "top.c",
              referenced_symbol = @root::@top_i::@top_b::@c,
              semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
          obelisk.sv.expression.named_value attributes {is_signed = false,
              node_id = 34 : i64, referenced_path = "top.d",
              referenced_symbol = @root::@top_i::@top_b::@d,
              semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
        } loc("scope_owned.sv":11:3)
        obelisk.sv.symbol.generate_block @generated attributes {
            hierarchical_name = "top.g", node_id = 13 : i64
        } {
          obelisk.sv.symbol.net @ga attributes {hierarchical_name = "top.g.a",
              is_implicit = false, name = "a", net_kind = 1 : i32,
              node_id = 14 : i64,
              semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
          } {}
          obelisk.sv.symbol.net @gb attributes {hierarchical_name = "top.g.b",
              is_implicit = false, name = "b", net_kind = 1 : i32,
              node_id = 15 : i64,
              semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
          } {}
          obelisk.sv.symbol.continuous_assign @generated_assign attributes {
              hierarchical_name = "top.g", node_id = 16 : i64,
              time_precision_fs = 1 : i64,
              time_unit_fs = 1 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32,
                is_signed = false, node_id = 17 : i64,
                semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {is_signed = false,
                  node_id = 18 : i64, referenced_path = "top.g.a",
                  referenced_symbol = @root::@top_i::@top_b::@generated::@ga,
                  semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
              obelisk.sv.expression.integer_literal attributes {
                  constant_value = "1'b0", is_signed = false,
                  node_id = 19 : i64,
                  semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
            }
          } loc("scope_owned.sv":20:5)
          obelisk.sv.symbol.net_alias @generated_alias attributes {hierarchical_name = "top.g",
              node_id = 20 : i64} {
            obelisk.sv.expression.named_value attributes {is_signed = false,
                node_id = 21 : i64, referenced_path = "top.g.a",
                referenced_symbol = @root::@top_i::@top_b::@generated::@ga,
                semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
            obelisk.sv.expression.named_value attributes {is_signed = false,
                node_id = 22 : i64, referenced_path = "top.g.b",
                referenced_symbol = @root::@top_i::@top_b::@generated::@gb,
                semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
          } loc("scope_owned.sv":21:5)
        }
        obelisk.sv.symbol.continuous_assign @variable_assign attributes {
            hierarchical_name = "top", node_id = 71 : i64,
            time_precision_fs = 1 : i64,
            time_unit_fs = 1 : i64} {
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32,
              is_signed = false, node_id = 72 : i64,
              semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.named_value attributes {is_signed = false,
                node_id = 73 : i64, referenced_path = "top.variable_lhs",
                referenced_symbol = @root::@top_i::@top_b::@variable_lhs,
                semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
            obelisk.sv.expression.named_value attributes {is_signed = false,
                node_id = 74 : i64, referenced_path = "top.variable_rhs",
                referenced_symbol = @root::@top_i::@top_b::@variable_rhs,
                semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
          }
        } loc("scope_owned.sv":13:3)
        obelisk.sv.symbol.generate_block @uninstantiated attributes {
            hierarchical_name = "top.dead", is_uninstantiated = true,
            node_id = 23 : i64} {
          obelisk.sv.symbol.continuous_assign @dead_assign attributes {
              hierarchical_name = "top.dead", node_id = 24 : i64,
              time_precision_fs = 1 : i64,
              time_unit_fs = 1 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32,
                is_signed = false, node_id = 25 : i64,
                semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {is_signed = false,
                  node_id = 26 : i64, referenced_path = "top.a",
                  referenced_symbol = @root::@top_i::@top_b::@a,
                  semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
              obelisk.sv.expression.integer_literal attributes {
                  constant_value = "1'b0", is_signed = false,
                  node_id = 27 : i64,
                  semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
            }
          } loc("scope_owned.sv":30:5)
          obelisk.sv.symbol.net_alias @dead_alias attributes {
              hierarchical_name = "top.dead", node_id = 28 : i64
          } {
            obelisk.sv.expression.named_value attributes {is_signed = false,
                node_id = 29 : i64, referenced_path = "top.a",
                referenced_symbol = @root::@top_i::@top_b::@a,
                semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
            obelisk.sv.expression.named_value attributes {is_signed = false,
                node_id = 30 : i64, referenced_path = "top.b",
                referenced_symbol = @root::@top_i::@top_b::@b,
                semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
          } loc("scope_owned.sv":31:5)
        }
      }
    }
    obelisk.sv.symbol.instance @iface_i attributes {hierarchical_name = "iface",
        is_uninstantiated = false, name = "iface", node_id = 37 : i64,
        referenced_path = "iface", referenced_symbol = @iface_def
    } {
      obelisk.sv.symbol.instance_body @iface_b attributes {hierarchical_name = "iface",
          name = "iface", node_id = 38 : i64,
          time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
        obelisk.sv.symbol.net @iface_a attributes {hierarchical_name = "iface.a",
            is_implicit = false, name = "a", net_kind = 1 : i32,
            node_id = 39 : i64,
            semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
        } {}
        obelisk.sv.symbol.continuous_assign @iface_assign attributes {
            hierarchical_name = "iface", node_id = 40 : i64,
            time_precision_fs = 1 : i64,
            time_unit_fs = 1 : i64} {
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32,
              is_signed = false, node_id = 41 : i64,
              semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.named_value attributes {is_signed = false,
                node_id = 42 : i64, referenced_path = "iface.a",
                referenced_symbol = @root::@iface_i::@iface_b::@iface_a,
                semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "1'b1", is_signed = false, node_id = 43 : i64,
                semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
          }
        } loc("scope_owned.sv":40:3)
      }
    }
    obelisk.sv.symbol.instance @prog_i attributes {hierarchical_name = "prog",
        is_uninstantiated = false, name = "prog", node_id = 44 : i64,
        referenced_path = "prog", referenced_symbol = @prog_def
    } {
      obelisk.sv.symbol.instance_body @prog_b attributes {hierarchical_name = "prog",
          name = "prog", node_id = 45 : i64,
          time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
        obelisk.sv.symbol.net @prog_a attributes {hierarchical_name = "prog.a",
            is_implicit = false, name = "a", net_kind = 1 : i32,
            node_id = 46 : i64,
            semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
        } {}
        obelisk.sv.symbol.continuous_assign @prog_assign attributes {
            hierarchical_name = "prog", node_id = 47 : i64,
            time_precision_fs = 1 : i64,
            time_unit_fs = 1 : i64} {
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32,
              is_signed = false, node_id = 48 : i64,
              semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.named_value attributes {is_signed = false,
                node_id = 49 : i64, referenced_path = "prog.a",
                referenced_symbol = @root::@prog_i::@prog_b::@prog_a,
                semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "1'b0", is_signed = false, node_id = 50 : i64,
                semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
          }
        } loc("scope_owned.sv":50:3)
      }
    }
  }
}

// IR: simulation.scope.decl 1 {{.*}} hierarchy "top" {{.*}} vpi_kind 32
// IR: simulation.scope.decl 2 {{.*}} hierarchy "iface" {{.*}} vpi_kind 601
// IR: simulation.scope.decl 3 {{.*}} hierarchy "prog" {{.*}} vpi_kind 602
// IR: simulation.vpi_object.anchor @[[TOP:[^ ]+]] id [[TOP_ID:[0-9]+]] type 32
// IR: simulation.vpi_object.anchor @[[GEN:[^ ]+]] id [[GEN_ID:[0-9]+]] type 134 {{.*}} parent @[[TOP]] {{.*}} hierarchy "top.g"
// IR: simulation.vpi_object.anchor @[[IFACE:[^ ]+]] id [[IFACE_ID:[0-9]+]] type 601
// IR: simulation.vpi_object.anchor @[[PROG:[^ ]+]] id [[PROG_ID:[0-9]+]] type 602
// IR: simulation.statement.decl 1 scope 1 type 8
// IR: simulation.vpi_statement_relation.decl scope 1 type 32 selector 8 ordinal 0 modes 2 to 1
// IR: simulation.statement.decl 2 scope 1 type 8
// IR: simulation.vpi_statement_relation.decl scope 1 type 32 selector 8 ordinal 1 modes 2 to 2
// IR: simulation.statement.decl 3 scope 1 type 646
// IR: simulation.vpi_statement_relation.decl scope 1 type 32 selector 646 ordinal 0 modes 2 to 3
// IR: simulation.statement.decl 4 scope 1 type 646
// IR: simulation.vpi_statement_relation.decl scope 1 type 32 selector 646 ordinal 1 modes 2 to 4
// IR: simulation.statement.decl 5 scope 1 type 646
// IR: simulation.vpi_statement_relation.decl scope 1 type 32 selector 646 ordinal 2 modes 2 to 5
// IR: simulation.statement.decl 6 scope 1 type 8
// IR: simulation.vpi_statement_relation.decl anchor [[GEN_ID]] type 134 selector 8 ordinal 0 modes 2 to 6
// IR: simulation.statement.decl 7 scope 1 type 646
// IR: simulation.vpi_statement_relation.decl anchor [[GEN_ID]] type 134 selector 646 ordinal 0 modes 2 to 7
// IR: simulation.statement.decl 8 scope 1 type 8
// IR: simulation.vpi_statement_relation.decl scope 1 type 32 selector 8 ordinal 2 modes 2 to 8
// IR: simulation.statement.decl 9 scope 2 type 8
// IR: simulation.vpi_statement_relation.decl scope 2 type 601 selector 8 ordinal 0 modes 2 to 9
// IR: simulation.statement.decl 10 scope 3 type 8
// IR: simulation.vpi_statement_relation.decl scope 3 type 602 selector 8 ordinal 0 modes 2 to 10
// IR-NOT: simulation.statement.decl 11
// IR: simulation.net.decl [[SOURCE:[0-9]+]] {{.*}} hierarchy "top.source"
// IR: simulation.net.decl [[DIRECT_LHS:[0-9]+]] {{.*}} hierarchy "top.direct_lhs"
// IR: simulation.net.decl [[VECTOR_LHS:[0-9]+]] {{.*}} hierarchy "top.vector_lhs"
// IR: simulation.net.decl [[VECTOR_RHS:[0-9]+]] {{.*}} hierarchy "top.vector_rhs"
// IR: simulation.storage.decl [[VARIABLE_LHS:[0-9]+]] {{.*}} hierarchy "top.variable_lhs"
// IR: simulation.storage.decl [[VARIABLE_RHS:[0-9]+]] {{.*}} hierarchy "top.variable_rhs"
// IR: simulation.net.decl [[GENERATED_A:[0-9]+]] {{.*}} hierarchy "top.g.a"
// IR: simulation.vpi_net_identity.decl [[B_ID:[0-9]+]] backed_by [[A:[0-9]+]] in 1 {{.*}} hierarchy "top.b"
// IR: simulation.vpi_net_identity.decl [[C_ID:[0-9]+]] backed_by [[A]] in 1 {{.*}} hierarchy "top.c"
// IR: simulation.vpi_net_identity.decl [[D_ID:[0-9]+]] backed_by [[A]] in 1 {{.*}} hierarchy "top.d"
// IR: simulation.vpi_net_identity.decl [[GENERATED_B_ID:[0-9]+]] backed_by [[GENERATED_A]] in 1 {{.*}} hierarchy "top.g.b"
// IR-NOT: simulation.vpi_relation.decl <kind = net, id = [[A]] : i64> selector 126
// IR: simulation.vpi_relation.decl <kind = net_identity, id = [[B_ID]] : i64> selector 126 handle ordinal 0 to <kind = net, id = [[A]] : i64>
// IR: simulation.vpi_relation.decl <kind = net_identity, id = [[C_ID]] : i64> selector 126 handle ordinal 0 to <kind = net, id = [[A]] : i64>
// IR: simulation.vpi_relation.decl <kind = net_identity, id = [[D_ID]] : i64> selector 126 handle ordinal 0 to <kind = net, id = [[A]] : i64>
// IR: simulation.vpi_relation.decl <kind = statement, id = 1 : i64> selector 77 handle ordinal 0 to <kind = net, id = [[DIRECT_LHS]] : i64>
// IR: simulation.vpi_relation.decl <kind = statement, id = 1 : i64> selector 82 handle ordinal 0 to <kind = net_identity, id = [[D_ID]] : i64>
// IR: simulation.vpi_relation.decl <kind = statement, id = 2 : i64> selector 77 handle ordinal 0 to <kind = net, id = [[VECTOR_LHS]] : i64>
// IR: simulation.vpi_relation.decl <kind = statement, id = 2 : i64> selector 82 handle ordinal 0 to <kind = net, id = [[VECTOR_RHS]] : i64>
// IR: simulation.vpi_relation.decl <kind = statement, id = 3 : i64> selector 77 handle ordinal 0 to <kind = net, id = [[A]] : i64>
// IR: simulation.vpi_relation.decl <kind = statement, id = 3 : i64> selector 82 handle ordinal 0 to <kind = net_identity, id = [[D_ID]] : i64>
// IR: simulation.vpi_relation.decl <kind = statement, id = 4 : i64> selector 77 handle ordinal 0 to <kind = net_identity, id = [[B_ID]] : i64>
// IR: simulation.vpi_relation.decl <kind = statement, id = 4 : i64> selector 82 handle ordinal 0 to <kind = net_identity, id = [[D_ID]] : i64>
// IR: simulation.vpi_relation.decl <kind = statement, id = 5 : i64> selector 77 handle ordinal 0 to <kind = net_identity, id = [[C_ID]] : i64>
// IR: simulation.vpi_relation.decl <kind = statement, id = 5 : i64> selector 82 handle ordinal 0 to <kind = net_identity, id = [[D_ID]] : i64>
// IR: simulation.vpi_relation.decl <kind = statement, id = 6 : i64> selector 77 handle ordinal 0 to <kind = net, id = [[GENERATED_A]] : i64>
// IR: simulation.vpi_relation.decl <kind = statement, id = 7 : i64> selector 77 handle ordinal 0 to <kind = net, id = [[GENERATED_A]] : i64>
// IR: simulation.vpi_relation.decl <kind = statement, id = 7 : i64> selector 82 handle ordinal 0 to <kind = net_identity, id = [[GENERATED_B_ID]] : i64>
// IR: simulation.vpi_relation.decl <kind = statement, id = 8 : i64> selector 77 handle ordinal 0 to <kind = storage, id = [[VARIABLE_LHS]] : i64>
// IR: simulation.vpi_relation.decl <kind = statement, id = 8 : i64> selector 82 handle ordinal 0 to <kind = storage, id = [[VARIABLE_RHS]] : i64>
// IR: simulation.vpi_relation.decl <kind = storage, id = [[VARIABLE_LHS]] : i64> selector 8 iterate ordinal 0 to <kind = statement, id = 8 : i64>
// IR: simulation.vpi_relation.decl <kind = storage, id = [[VARIABLE_LHS]] : i64> selector 91 iterate ordinal 0 to <kind = statement, id = 8 : i64>
// IR: simulation.vpi_relation.decl <kind = storage, id = [[VARIABLE_LHS]] : i64> selector 101 iterate ordinal 0 to <kind = statement, id = 8 : i64>
// IR: simulation.vpi_relation.decl <kind = storage, id = [[VARIABLE_RHS]] : i64> selector 93 iterate ordinal 0 to <kind = statement, id = 8 : i64>
// IR: simulation.vpi_relation.decl <kind = storage, id = [[VARIABLE_RHS]] : i64> selector 101 iterate ordinal 0 to <kind = statement, id = 8 : i64>
// IR: simulation.vpi_relation.decl <kind = net, id = [[DIRECT_LHS]] : i64> selector 8 iterate ordinal 0 to <kind = statement, id = 1 : i64>
// IR: simulation.vpi_relation.decl <kind = net, id = [[DIRECT_LHS]] : i64> selector 91 iterate ordinal 0 to <kind = statement, id = 1 : i64>
// IR: simulation.vpi_relation.decl <kind = net, id = [[DIRECT_LHS]] : i64> selector 101 iterate ordinal 0 to <kind = statement, id = 1 : i64>
// IR: simulation.vpi_relation.decl <kind = net, id = [[DIRECT_LHS]] : i64> selector 122 iterate ordinal 0 to <kind = statement, id = 1 : i64>
// IR-NOT: simulation.vpi_relation.decl <kind = net, id = [[VECTOR_LHS]] : i64> selector 8
// IR: simulation.vpi_relation.decl <kind = net, id = [[VECTOR_LHS]] : i64> selector 91 iterate ordinal 0 to <kind = statement, id = 2 : i64>
// IR: simulation.vpi_relation.decl <kind = net, id = [[VECTOR_LHS]] : i64> selector 101 iterate ordinal 0 to <kind = statement, id = 2 : i64>
// IR: simulation.vpi_relation.decl <kind = net, id = [[VECTOR_LHS]] : i64> selector 122 iterate ordinal 0 to <kind = statement, id = 2 : i64>
// IR: simulation.vpi_relation.decl <kind = net, id = [[VECTOR_RHS]] : i64> selector 93 iterate ordinal 0 to <kind = statement, id = 2 : i64>
// IR: simulation.vpi_relation.decl <kind = net, id = [[VECTOR_RHS]] : i64> selector 101 iterate ordinal 0 to <kind = statement, id = 2 : i64>
// IR: simulation.vpi_relation.decl <kind = net, id = [[VECTOR_RHS]] : i64> selector 123 iterate ordinal 0 to <kind = statement, id = 2 : i64>
// IR-NOT: obelisk.sv.
