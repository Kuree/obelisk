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
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32,
      hierarchical_name = "top", name = "top", node_id = 0 : i64,
      sym_name = "top_def"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 1 : i32,
      hierarchical_name = "iface", name = "iface", node_id = 35 : i64,
      sym_name = "iface_def"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 2 : i32,
      hierarchical_name = "prog", name = "prog", node_id = 36 : i64,
      sym_name = "prog_def"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top",
        is_uninstantiated = false, name = "top", node_id = 2 : i64,
        referenced_path = "top", referenced_symbol = @top_def,
        sym_name = "top_i"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top",
          name = "top", node_id = 3 : i64, sym_name = "top_b",
          time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
        obelisk.sv.symbol.net attributes {hierarchical_name = "top.a",
            is_implicit = false, name = "a", net_kind = 1 : i32,
            node_id = 4 : i64,
            semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>,
            sym_name = "a"} {}
        obelisk.sv.symbol.net attributes {hierarchical_name = "top.b",
            is_implicit = false, name = "b", net_kind = 1 : i32,
            node_id = 5 : i64,
            semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>,
            sym_name = "b"} {}
        obelisk.sv.symbol.net attributes {hierarchical_name = "top.c",
            is_implicit = false, name = "c", net_kind = 1 : i32,
            node_id = 31 : i64,
            semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>,
            sym_name = "c"} {}
        obelisk.sv.symbol.net attributes {hierarchical_name = "top.d",
            is_implicit = false, name = "d", net_kind = 1 : i32,
            node_id = 32 : i64,
            semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>,
            sym_name = "d"} {}
        obelisk.sv.symbol.continuous_assign attributes {
            hierarchical_name = "top", node_id = 6 : i64,
            sym_name = "direct_assign", time_precision_fs = 1 : i64,
            time_unit_fs = 1 : i64} {
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32,
              is_signed = false, node_id = 7 : i64,
              semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.named_value attributes {is_signed = false,
                node_id = 8 : i64, referenced_path = "top.a",
                referenced_symbol = @root::@top_i::@top_b::@a,
                semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "1'b1", is_signed = false, node_id = 9 : i64,
                semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
          }
        } loc("scope_owned.sv":10:3)
        obelisk.sv.symbol.net_alias attributes {hierarchical_name = "top",
            node_id = 10 : i64, sym_name = "direct_alias"} {
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
        obelisk.sv.symbol.generate_block attributes {
            hierarchical_name = "top.g", node_id = 13 : i64,
            sym_name = "generated"} {
          obelisk.sv.symbol.net attributes {hierarchical_name = "top.g.a",
              is_implicit = false, name = "a", net_kind = 1 : i32,
              node_id = 14 : i64,
              semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>,
              sym_name = "ga"} {}
          obelisk.sv.symbol.net attributes {hierarchical_name = "top.g.b",
              is_implicit = false, name = "b", net_kind = 1 : i32,
              node_id = 15 : i64,
              semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>,
              sym_name = "gb"} {}
          obelisk.sv.symbol.continuous_assign attributes {
              hierarchical_name = "top.g", node_id = 16 : i64,
              sym_name = "generated_assign", time_precision_fs = 1 : i64,
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
          obelisk.sv.symbol.net_alias attributes {hierarchical_name = "top.g",
              node_id = 20 : i64, sym_name = "generated_alias"} {
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
        obelisk.sv.symbol.generate_block attributes {
            hierarchical_name = "top.dead", is_uninstantiated = true,
            node_id = 23 : i64, sym_name = "uninstantiated"} {
          obelisk.sv.symbol.continuous_assign attributes {
              hierarchical_name = "top.dead", node_id = 24 : i64,
              sym_name = "dead_assign", time_precision_fs = 1 : i64,
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
          obelisk.sv.symbol.net_alias attributes {
              hierarchical_name = "top.dead", node_id = 28 : i64,
              sym_name = "dead_alias"} {
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
    obelisk.sv.symbol.instance attributes {hierarchical_name = "iface",
        is_uninstantiated = false, name = "iface", node_id = 37 : i64,
        referenced_path = "iface", referenced_symbol = @iface_def,
        sym_name = "iface_i"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "iface",
          name = "iface", node_id = 38 : i64, sym_name = "iface_b",
          time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
        obelisk.sv.symbol.net attributes {hierarchical_name = "iface.a",
            is_implicit = false, name = "a", net_kind = 1 : i32,
            node_id = 39 : i64,
            semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>,
            sym_name = "iface_a"} {}
        obelisk.sv.symbol.continuous_assign attributes {
            hierarchical_name = "iface", node_id = 40 : i64,
            sym_name = "iface_assign", time_precision_fs = 1 : i64,
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
    obelisk.sv.symbol.instance attributes {hierarchical_name = "prog",
        is_uninstantiated = false, name = "prog", node_id = 44 : i64,
        referenced_path = "prog", referenced_symbol = @prog_def,
        sym_name = "prog_i"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "prog",
          name = "prog", node_id = 45 : i64, sym_name = "prog_b",
          time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
        obelisk.sv.symbol.net attributes {hierarchical_name = "prog.a",
            is_implicit = false, name = "a", net_kind = 1 : i32,
            node_id = 46 : i64,
            semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>,
            sym_name = "prog_a"} {}
        obelisk.sv.symbol.continuous_assign attributes {
            hierarchical_name = "prog", node_id = 47 : i64,
            sym_name = "prog_assign", time_precision_fs = 1 : i64,
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

// IR: obelisk_sim.scope.decl 1 {{.*}} hierarchy "top" {{.*}} vpi_kind 32
// IR: obelisk_sim.scope.decl 2 {{.*}} hierarchy "iface" {{.*}} vpi_kind 601
// IR: obelisk_sim.scope.decl 3 {{.*}} hierarchy "prog" {{.*}} vpi_kind 602
// IR: obelisk_sim.vpi_object.anchor @[[TOP:[^ ]+]] id [[TOP_ID:[0-9]+]] type 32
// IR: obelisk_sim.vpi_object.anchor @[[GEN:[^ ]+]] id [[GEN_ID:[0-9]+]] type 134 {{.*}} parent @[[TOP]] {{.*}} hierarchy "top.g"
// IR: obelisk_sim.vpi_object.anchor @[[IFACE:[^ ]+]] id [[IFACE_ID:[0-9]+]] type 601
// IR: obelisk_sim.vpi_object.anchor @[[PROG:[^ ]+]] id [[PROG_ID:[0-9]+]] type 602
// IR: obelisk_sim.statement.decl 1 scope 1 type 8
// IR: obelisk_sim.vpi_statement_relation.decl scope 1 type 32 selector 8 ordinal 0 modes 2 to 1
// IR: obelisk_sim.statement.decl 2 scope 1 type 646
// IR: obelisk_sim.vpi_statement_relation.decl scope 1 type 32 selector 646 ordinal 0 modes 2 to 2
// IR: obelisk_sim.statement.decl 3 scope 1 type 646
// IR: obelisk_sim.vpi_statement_relation.decl scope 1 type 32 selector 646 ordinal 1 modes 2 to 3
// IR: obelisk_sim.statement.decl 4 scope 1 type 646
// IR: obelisk_sim.vpi_statement_relation.decl scope 1 type 32 selector 646 ordinal 2 modes 2 to 4
// IR: obelisk_sim.statement.decl 5 scope 1 type 8
// IR: obelisk_sim.vpi_statement_relation.decl anchor [[GEN_ID]] type 134 selector 8 ordinal 0 modes 2 to 5
// IR: obelisk_sim.statement.decl 6 scope 1 type 646
// IR: obelisk_sim.vpi_statement_relation.decl anchor [[GEN_ID]] type 134 selector 646 ordinal 0 modes 2 to 6
// IR: obelisk_sim.statement.decl 7 scope 2 type 8
// IR: obelisk_sim.vpi_statement_relation.decl scope 2 type 601 selector 8 ordinal 0 modes 2 to 7
// IR: obelisk_sim.statement.decl 8 scope 3 type 8
// IR: obelisk_sim.vpi_statement_relation.decl scope 3 type 602 selector 8 ordinal 0 modes 2 to 8
// IR-NOT: obelisk_sim.statement.decl 9
// IR-NOT: obelisk.sv.
