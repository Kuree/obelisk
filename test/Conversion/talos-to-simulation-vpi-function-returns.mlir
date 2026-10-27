// RUN: talos-opt %s '--lower-talos-to-sim=vpi=read opt-level=0' -o %t.mlir
// RUN: env TALOS_TEST_INPUT=%t.mlir %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.FunctionReturns
// RUN: talos-opt %s '--lower-talos-to-sim=vpi=full opt-level=3' -o %t.opt.mlir
// RUN: env TALOS_TEST_INPUT=%t.opt.mlir %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.FunctionReturns
// RUN: talos-opt %t.mlir --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null
!signed = !talos.integral<9, true, true, 8 : 0, logic>
!word = !talos.integral<32, true, false, 31 : 0, int>
!vector = !talos.ranged_packed_array<2 : 5 x !talos.integral<1, false, false, 0 : 0, bit>>
!matrix = !talos.ranged_unpacked_array<2 : 3 x !talos.ranged_unpacked_array<4 : 6 x !word>>
!record = !talos.source_aggregate<"record", false, false, false, false, false, false, 0, 41, 41, 0, [{name = "first", ordinal = 0 : i32, packed_offset = 0 : i64, type = !word}, {name = "second", ordinal = 1 : i32, packed_offset = 0 : i64, type = !signed}]>
!packed_record = !talos.source_aggregate<"packed_record", true, false, false, false, true, false, 41, 41, 41, 0, [{name = "first", ordinal = 0 : i32, packed_offset = 9 : i64, type = !word}, {name = "second", ordinal = 1 : i32, packed_offset = 0 : i64, type = !signed}]>
!state = !talos.enum<"state_t", !signed>
module attributes {llvm.data_layout = "e-p:64:64-i64:64", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  talos.sv.symbol.definition @def attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {}
  talos.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.instance @top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 2 : i64, referenced_path = "top", referenced_symbol = @def} {
      talos.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", node_id = 3 : i64} {
        talos.sv.symbol.subroutine @signed_result attributes {default_lifetime = 0 : i32, hierarchical_name = "top.signed_result", name = "signed_result", node_id = 10 : i64, semantic_type = !talos.subroutine<() -> !signed, false>, subroutine_kind = 0 : i32, return_variable_path = "top.signed_result.signed_result", return_variable_symbol = @root::@top::@body::@signed_result::@result} {
          talos.sv.statement.empty attributes {node_id = 11 : i64} {}
          talos.sv.symbol.variable @result attributes {hierarchical_name = "top.signed_result.signed_result", is_compiler_generated, name = "signed_result", node_id = 12 : i64, semantic_type = !signed} {}
        }
        talos.sv.symbol.subroutine @static_result attributes {default_lifetime = 1 : i32, hierarchical_name = "top.static_result", name = "static_result", node_id = 13 : i64, semantic_type = !talos.subroutine<() -> !word, false>, subroutine_kind = 0 : i32, return_variable_path = "top.static_result.static_result", return_variable_symbol = @root::@top::@body::@static_result::@result} {
          talos.sv.statement.empty attributes {node_id = 14 : i64} {}
          talos.sv.symbol.variable @result attributes {hierarchical_name = "top.static_result.static_result", is_compiler_generated, name = "static_result", node_id = 15 : i64, semantic_type = !word} {}
        }
        talos.sv.symbol.subroutine @vector_result attributes {default_lifetime = 0 : i32, hierarchical_name = "top.vector_result", name = "vector_result", node_id = 16 : i64, semantic_type = !talos.subroutine<() -> !vector, false>, subroutine_kind = 0 : i32, return_variable_path = "top.vector_result.vector_result", return_variable_symbol = @root::@top::@body::@vector_result::@result} {
          talos.sv.statement.empty attributes {node_id = 17 : i64} {}
          talos.sv.symbol.variable @result attributes {hierarchical_name = "top.vector_result.vector_result", is_compiler_generated, name = "vector_result", node_id = 18 : i64, semantic_type = !vector} {}
        }
        talos.sv.symbol.subroutine @text_result attributes {default_lifetime = 0 : i32, hierarchical_name = "top.text_result", name = "text_result", node_id = 19 : i64, semantic_type = !talos.subroutine<() -> !talos.string, false>, subroutine_kind = 0 : i32, return_variable_path = "top.text_result.text_result", return_variable_symbol = @root::@top::@body::@text_result::@result} {
          talos.sv.statement.empty attributes {node_id = 20 : i64} {}
          talos.sv.symbol.variable @result attributes {hierarchical_name = "top.text_result.text_result", is_compiler_generated, name = "text_result", node_id = 21 : i64, semantic_type = !talos.string} {}
        }
        talos.sv.symbol.subroutine @void_result attributes {default_lifetime = 0 : i32, hierarchical_name = "top.void_result", name = "void_result", node_id = 22 : i64, semantic_type = !talos.subroutine<() -> !talos.void, false>, subroutine_kind = 0 : i32} {
          talos.sv.statement.empty attributes {node_id = 23 : i64} {}
        }
        talos.sv.symbol.subroutine @unretained_result attributes {default_lifetime = 0 : i32, hierarchical_name = "top.unretained_result", name = "unretained_result", node_id = 24 : i64, semantic_type = !talos.subroutine<() -> !word, false>, subroutine_kind = 0 : i32} {
          talos.sv.statement.empty attributes {node_id = 25 : i64} {}
        }
        talos.sv.symbol.subroutine @enum_result attributes {default_lifetime = 0 : i32, hierarchical_name = "top.enum_result", name = "enum_result", node_id = 30 : i64, semantic_type = !talos.subroutine<() -> !state, false>, subroutine_kind = 0 : i32, return_variable_symbol = @root::@top::@body::@enum_result::@result} {
          talos.sv.statement.empty attributes {node_id = 31 : i64} {}
          talos.sv.type.type_alias @state_t attributes {hierarchical_name = "top.enum_result.state_t", name = "state_t", node_id = 32 : i64, semantic_type = !state, vpi_source_type_identity = 200 : i64, vpi_typedef_layers = [{aliases = [@root::@top::@body::@enum_result::@state_t], path = array<i64>}]} {}
          talos.sv.symbol.enum_value @negative attributes {constant_value = "9'sb111111110", hierarchical_name = "top.enum_result.NEGATIVE", name = "NEGATIVE", node_id = 33 : i64, semantic_type = !state, vpi_source_type_identity = 200 : i64} {}
          talos.sv.symbol.variable @result attributes {hierarchical_name = "top.enum_result.enum_result", is_compiler_generated, name = "enum_result", node_id = 34 : i64, semantic_type = !state, vpi_source_type_identity = 200 : i64, vpi_typedef_layers = [{aliases = [@root::@top::@body::@enum_result::@state_t], path = array<i64>}]} {}
        }
        talos.sv.symbol.subroutine @array_result attributes {default_lifetime = 0 : i32, hierarchical_name = "top.array_result", name = "array_result", node_id = 40 : i64, semantic_type = !talos.subroutine<() -> !matrix, false>, subroutine_kind = 0 : i32, return_variable_symbol = @root::@top::@body::@array_result::@result} {
          talos.sv.statement.empty attributes {node_id = 41 : i64} {}
          talos.sv.symbol.variable @result attributes {hierarchical_name = "top.array_result.array_result", is_compiler_generated, name = "array_result", node_id = 42 : i64, semantic_type = !matrix} {}
        }
        talos.sv.symbol.subroutine @record_result attributes {default_lifetime = 0 : i32, hierarchical_name = "top.record_result", name = "record_result", node_id = 43 : i64, semantic_type = !talos.subroutine<() -> !record, false>, subroutine_kind = 0 : i32, return_variable_symbol = @root::@top::@body::@record_result::@result} {
          talos.sv.statement.empty attributes {node_id = 44 : i64} {}
          talos.sv.symbol.variable @result attributes {hierarchical_name = "top.record_result.record_result", is_compiler_generated, name = "record_result", node_id = 45 : i64, semantic_type = !record} {}
        }
        talos.sv.symbol.subroutine @packed_result attributes {default_lifetime = 0 : i32, hierarchical_name = "top.packed_result", name = "packed_result", node_id = 46 : i64, semantic_type = !talos.subroutine<() -> !packed_record, false>, subroutine_kind = 0 : i32, return_variable_symbol = @root::@top::@body::@packed_result::@result} {
          talos.sv.statement.empty attributes {node_id = 47 : i64} {}
          talos.sv.symbol.variable @result attributes {hierarchical_name = "top.packed_result.packed_result", is_compiler_generated, name = "packed_result", node_id = 48 : i64, semantic_type = !packed_record} {}
        }
        talos.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", node_id = 100 : i64, procedure_kind = 0 : i32} {
          talos.sv.statement.empty attributes {node_id = 101 : i64} {}
        }
      }
    }
  }
}
