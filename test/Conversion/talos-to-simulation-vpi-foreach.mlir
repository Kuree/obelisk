// RUN: talos-opt %s '--lower-talos-to-sim=vpi=read opt-level=0' --mlir-print-debuginfo -o %t.mlir
// RUN: env TALOS_TEST_INPUT=%t.mlir %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.FixedArrayForeach
// RUN: talos-opt %s '--lower-talos-to-sim=vpi=read opt-level=3' --mlir-print-debuginfo -o %t.o3.mlir
// RUN: env TALOS_TEST_INPUT=%t.o3.mlir %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.FixedArrayForeach
// RUN: talos-opt %s '--lower-talos-to-sim=vpi=read opt-level=0' --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null

// RUN: talos-opt %s '--lower-talos-to-sim=vpi=off opt-level=0' | FileCheck %s --check-prefix=OFF
// OFF-NOT: simulation.vpi_foreach.decl
// OFF-NOT: simulation.vpi_local.decl
// OFF: simulation.func

!bit = !talos.integral<1, false, false, 0 : 0, bit>
!packed = !talos.ranged_packed_array<3 : 0 x !bit>
!int = !talos.integral<32, true, false, 31 : 0, int>
!array = !talos.ranged_unpacked_array<1 : 0 x !talos.ranged_unpacked_array<3 : 4 x !talos.ranged_unpacked_array<0 : 1 x !int>>>
module attributes {llvm.target_triple = "x86_64-unknown-linux-gnu", llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"} {
  talos.sv.symbol.definition @def attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {}
  talos.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.instance @top attributes {hierarchical_name = "top", name = "top", is_uninstantiated = false, referenced_path = "top", referenced_symbol = @def, node_id = 2 : i64} {
      talos.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", node_id = 3 : i64} {
        talos.sv.symbol.variable @a attributes {hierarchical_name = "top.a", name = "a", lifetime = 1 : i32, semantic_type = !array, node_id = 4 : i64} {}
        talos.sv.symbol.variable @packed attributes {hierarchical_name = "top.packed", name = "packed", lifetime = 1 : i32, semantic_type = !packed, node_id = 30 : i64} {}
        talos.sv.symbol.statement_block @indices attributes {block_kind = 0 : i32, name = "sweep", hierarchical_name = "top.sweep", node_id = 5 : i64} {
          talos.sv.symbol.iterator @i attributes {array_type = !array, hierarchical_name = "top.sweep.i", index_method_name = "", is_const, name = "i", semantic_type = !int, node_id = 6 : i64} {}
          talos.sv.symbol.iterator @j attributes {array_type = !array, hierarchical_name = "top.sweep.j", index_method_name = "", is_const, name = "j", semantic_type = !int, node_id = 7 : i64} {}
        }
        talos.sv.symbol.statement_block @prefix attributes {block_kind = 0 : i32, hierarchical_name = "top", node_id = 50 : i64} {
          talos.sv.symbol.iterator @k attributes {array_type = !array, hierarchical_name = "top.k", index_method_name = "", is_const, name = "k", semantic_type = !int, node_id = 8 : i64} {}
        }
        talos.sv.symbol.statement_block @skipped attributes {block_kind = 0 : i32, hierarchical_name = "top", node_id = 51 : i64} {}
        talos.sv.symbol.statement_block @packed_indices attributes {block_kind = 0 : i32, name = "bits", hierarchical_name = "top.bits", node_id = 52 : i64} {
          talos.sv.symbol.iterator @n attributes {array_type = !packed, hierarchical_name = "top.bits.n", index_method_name = "", is_const, name = "n", semantic_type = !int, node_id = 31 : i64} {}
        }
        talos.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 9 : i64} {
          talos.sv.statement.block attributes {node_id = 10 : i64} {
            talos.sv.statement.list attributes {node_id = 11 : i64} {
              talos.sv.statement.block attributes {block_symbol = @root::@top::@body::@indices, block_path = "top.sweep", is_implicit_loop_scope, node_id = 60 : i64} {
                talos.sv.statement.foreach_loop attributes {loop_dimensions = [{has_static_range = true, left = 1 : i64, right = 0 : i64, has_iterator = true, iterator_path = "top.sweep.i", iterator_symbol = @root::@top::@body::@indices::@i, iterator_type = !int}, {has_static_range = true, left = 3 : i64, right = 4 : i64, has_iterator = false}, {has_static_range = true, left = 0 : i64, right = 1 : i64, has_iterator = true, iterator_path = "top.sweep.j", iterator_symbol = @root::@top::@body::@indices::@j, iterator_type = !int}], node_id = 12 : i64} {
                  talos.sv.expression.named_value attributes {referenced_path = "top.a", referenced_symbol = @root::@top::@body::@a, semantic_type = !array, node_id = 13 : i64} {}
                  talos.sv.statement.conditional attributes {has_else = false, check_kind = 0 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, node_id = 14 : i64} {
                    talos.sv.expression.named_value attributes {referenced_path = "top.sweep.i", referenced_symbol = @root::@top::@body::@indices::@i, semantic_type = !int, node_id = 15 : i64} {}
                    talos.sv.statement.empty attributes {node_id = 16 : i64} {}
                  }
                } loc("rtl-foreach.sv":10:1)
              } loc("rtl-foreach.sv":10:1)
              talos.sv.statement.block attributes {block_symbol = @root::@top::@body::@prefix, is_implicit_loop_scope, node_id = 61 : i64} {
                talos.sv.statement.foreach_loop attributes {loop_dimensions = [{has_static_range = true, left = 1 : i64, right = 0 : i64, has_iterator = true, iterator_path = "top.k", iterator_symbol = @root::@top::@body::@prefix::@k, iterator_type = !int}], node_id = 17 : i64} {
                  talos.sv.expression.named_value attributes {referenced_path = "top.a", referenced_symbol = @root::@top::@body::@a, semantic_type = !array, node_id = 18 : i64} {}
                  talos.sv.statement.empty attributes {node_id = 19 : i64} {}
                } loc("rtl-foreach.sv":20:1)
              } loc("rtl-foreach.sv":20:1)
              talos.sv.statement.block attributes {block_symbol = @root::@top::@body::@skipped, is_implicit_loop_scope, node_id = 62 : i64} {
                talos.sv.statement.foreach_loop attributes {loop_dimensions = [{has_static_range = true, left = 1 : i64, right = 0 : i64, has_iterator = false}, {has_static_range = true, left = 3 : i64, right = 4 : i64, has_iterator = false}, {has_static_range = true, left = 0 : i64, right = 1 : i64, has_iterator = false}], node_id = 20 : i64} {
                  talos.sv.expression.named_value attributes {referenced_path = "top.a", referenced_symbol = @root::@top::@body::@a, semantic_type = !array, node_id = 21 : i64} {}
                  talos.sv.statement.empty attributes {node_id = 22 : i64} {}
                } loc("rtl-foreach.sv":30:1)
              } loc("rtl-foreach.sv":30:1)
              talos.sv.statement.block attributes {block_symbol = @root::@top::@body::@packed_indices, block_path = "top.bits", is_implicit_loop_scope, node_id = 63 : i64} {
                talos.sv.statement.foreach_loop attributes {loop_dimensions = [{has_static_range = true, left = 3 : i64, right = 0 : i64, has_iterator = true, iterator_path = "top.bits.n", iterator_symbol = @root::@top::@body::@packed_indices::@n, iterator_type = !int}], node_id = 32 : i64} {
                  talos.sv.expression.named_value attributes {referenced_path = "top.packed", referenced_symbol = @root::@top::@body::@packed, semantic_type = !packed, node_id = 33 : i64} {}
                  talos.sv.statement.empty attributes {node_id = 34 : i64} {}
                } loc("rtl-foreach.sv":40:1)
              } loc("rtl-foreach.sv":40:1)
            }
          }
        }
      }
    }
  }
}
