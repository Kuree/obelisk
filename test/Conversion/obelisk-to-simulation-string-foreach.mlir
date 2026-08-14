// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32, hierarchical_name = "string_foreach",
    name = "string_foreach", node_id = 0 : i64,
    sym_name = "s0.string_foreach"
  } {}
  obelisk.sv.symbol.root attributes {
    hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64,
    sym_name = "s1.$root"
  } {
    obelisk.sv.symbol.compilation_unit attributes {
      hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"
    } {}
    obelisk.sv.symbol.instance attributes {
      hierarchical_name = "string_foreach", is_uninstantiated = false,
      name = "string_foreach", node_id = 3 : i64,
      referenced_path = "string_foreach",
      referenced_symbol = @s0.string_foreach,
      sym_name = "s3.string_foreach"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "string_foreach", name = "string_foreach",
        node_id = 4 : i64, sym_name = "s4.string_foreach"
      } {
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "string_foreach.text", lifetime = 1 : i32,
          name = "text", node_id = 5 : i64,
          semantic_type = !obelisk.string, sym_name = "s5.text"
        } {}
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "string_foreach.last_index", lifetime = 1 : i32,
          name = "last_index", node_id = 6 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
          sym_name = "s6.last_index"
        } {}
        obelisk.sv.symbol.statement_block attributes {
          block_kind = 0 : i32, hierarchical_name = "string_foreach",
          node_id = 7 : i64, sym_name = "s7"
        } {
          obelisk.sv.symbol.iterator attributes {
            array_type = !obelisk.string,
            hierarchical_name = "string_foreach.index",
            index_method_name = "", is_const, name = "index",
            node_id = 8 : i64,
            semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
            sym_name = "s8.index"
          } {}
        }
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "string_foreach", node_id = 9 : i64,
          procedure_kind = 0 : i32, sym_name = "s9",
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.block attributes {node_id = 10 : i64} {
            obelisk.sv.statement.foreach_loop attributes {
              loop_dimensions = [{has_iterator = true, has_static_range = false, iterator_path = "string_foreach.index", iterator_symbol = @s1.$root::@s4.string_foreach::@s7::@s8.index, iterator_type = !obelisk.integral<32, true, false, 31 : 0, int>}],
              node_id = 11 : i64
            } {
              obelisk.sv.expression.named_value attributes {
                node_id = 12 : i64, referenced_path = "string_foreach.text",
                referenced_symbol = @s1.$root::@s3.string_foreach::@s4.string_foreach::@s5.text,
                semantic_type = !obelisk.string
              } {}
              obelisk.sv.statement.expression_statement attributes {
                node_id = 13 : i64
              } {
                obelisk.sv.expression.assignment attributes {
                  assignment_kind = 0 : i32, node_id = 14 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {
                  obelisk.sv.expression.named_value attributes {
                    is_signed = true, node_id = 15 : i64,
                    referenced_path = "string_foreach.last_index",
                    referenced_symbol = @s1.$root::@s3.string_foreach::@s4.string_foreach::@s6.last_index,
                    semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                  } {}
                  obelisk.sv.expression.named_value attributes {
                    is_signed = true, node_id = 16 : i64,
                    referenced_path = "string_foreach.index",
                    referenced_symbol = @s1.$root::@s3.string_foreach::@s4.string_foreach::@s7::@s8.index,
                    semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                  } {}
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: %[[TEXT:.*]] = obelisk_sim.ref.load {{.*}} : !obelisk_sim.ref<!obelisk_sim.string> -> !obelisk_sim.string
// CHECK: %[[LENGTH:.*]] = obelisk_sim.string.length %[[TEXT]] : (!obelisk_sim.string) -> i64
// CHECK: cf.br ^[[HEADER:bb[0-9]+]](%{{.*}} : i64)
// CHECK: ^[[HEADER]](%[[INDEX:.*]]: i64):
// CHECK: %[[MORE:.*]] = arith.cmpi ult, %[[INDEX]], %[[LENGTH]] : i64
// CHECK: cf.cond_br %[[MORE]]
// CHECK: %[[NARROW_INDEX:.*]] = arith.trunci %[[INDEX]] : i64 to i32
// CHECK: obelisk_sim.ref.store %[[NARROW_INDEX]]
// CHECK-NOT: obelisk.sv.
