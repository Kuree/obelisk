// RUN: talos-opt %s '--lower-talos-to-sim=vpi=full opt-level=0' -o %t
// RUN: FileCheck %s < %t
// RUN: env TALOS_TEST_INPUT=%t %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.StaticProceduralTypes
// Exact declaration ownership distinguishes repeated typedef spellings in a
// function and nested unnamed blocks. No block_path is needed for a scope.
!word = !talos.integral<32, true, false, 31 : 0, int>
!bit = !talos.integral<2, false, false, 1 : 0, bit>
!signed_base = !talos.integral<9, true, true, 8 : 0, logic>
!state = !talos.enum<"state_t", !signed_base>
!anon = !talos.enum<"anonymous", !bit>
module attributes {llvm.target_triple = "x86_64-unknown-linux-gnu", llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"} {
  talos.sv.symbol.definition @def attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {}
  talos.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @cu attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
      // Both alias representations share the same numeric typespec namespace.
      // The deferred omitted type below must not renumber around this alias.
      talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 2000 : i64, is_interface = false, specialization_count = 0 : i64} {
        talos.sv.source.type_parameter @t attributes {hierarchical_name = "G::T", name = "T", node_id = 2001 : i64} {}
        talos.sv.source.class_type_alias @a attributes {hierarchical_name = "G::A", name = "A", node_id = 2002 : i64, member_visibility = 0 : i32} {
          talos.sv.source.declared_type_reference attributes {node_id = 2003 : i64, declaration = @t} {}
        }
        talos.sv.source.class_type_alias @word attributes {hierarchical_name = "G::Word", name = "Word", node_id = 2004 : i64, member_visibility = 0 : i32} {
          talos.sv.source.declared_type attributes {node_id = 2005 : i64, semantic_type = !word} {}
        }
      }
      talos.sv.symbol.generic_class_def @derived attributes {hierarchical_name = "Derived", name = "Derived", node_id = 2010 : i64, is_interface = false, specialization_count = 0 : i64} {
        talos.sv.source.class_extends attributes {node_id = 2011 : i64, base_definition = @root::@cu::@g} {}
      }
    }
    talos.sv.symbol.instance @top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @def} {
      talos.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64} {
        talos.sv.symbol.subroutine @work attributes {hierarchical_name = "top.work", name = "work", node_id = 5 : i64, semantic_type = !talos.subroutine<() -> !talos.void, false>, subroutine_kind = 0 : i32} {
          talos.sv.statement.block attributes {block_symbol = @root::@top::@body::@work::@block, node_id = 6 : i64} {
            talos.sv.statement.list attributes {node_id = 7 : i64} {
              talos.sv.statement.variable_declaration attributes {node_id = 8 : i64, referenced_path = "top.work.state", referenced_symbol = @root::@top::@body::@work::@block::@state} {}
              talos.sv.statement.block attributes {block_symbol = @root::@top::@body::@work::@block::@block, node_id = 9 : i64} {
                talos.sv.statement.list attributes {node_id = 10 : i64} {
                  talos.sv.statement.variable_declaration attributes {node_id = 11 : i64, referenced_path = "top.work.value", referenced_symbol = @root::@top::@body::@work::@block::@block::@value} {}
                  talos.sv.statement.variable_declaration attributes {node_id = 12 : i64, referenced_path = "top.work.anonymous", referenced_symbol = @root::@top::@body::@work::@block::@block::@anonymous} {}
                }
              }
            }
          }
          // A lexical block retained after an unsupported/eliminated statement
          // has no statement identity. Its static storage must still lower,
          // without a dangling reference to a deferred block typedef.
          talos.sv.symbol.statement_block @unretained attributes {block_kind = 0 : i32, hierarchical_name = "top.work", node_id = 1000 : i64} {
            talos.sv.type.type_alias @hidden_t attributes {hierarchical_name = "top.work.hidden_t", name = "hidden_t", node_id = 1001 : i64, semantic_type = !word, vpi_typedef_layers = [{aliases = [@root::@top::@body::@work::@unretained::@hidden_t], path = array<i64>}]} {}
            talos.sv.symbol.variable @hidden attributes {hierarchical_name = "top.work.hidden", name = "hidden", node_id = 1002 : i64, lifetime = 1 : i32, semantic_type = !word, vpi_typedef_layers = [{aliases = [@root::@top::@body::@work::@unretained::@hidden_t], path = array<i64>}]} {}
          }
          talos.sv.type.type_alias @same attributes {hierarchical_name = "top.work.same_t", name = "same_t", node_id = 13 : i64, semantic_type = !word, vpi_typedef_layers = [{aliases = [@root::@top::@body::@work::@same], path = array<i64>}]} {}
          talos.sv.symbol.statement_block @block attributes {block_kind = 0 : i32, hierarchical_name = "top.work", node_id = 14 : i64} {
            talos.sv.type.type_alias @same attributes {hierarchical_name = "top.work.same_t", name = "same_t", node_id = 15 : i64, semantic_type = !state, vpi_source_type_identity = 100 : i64, vpi_typedef_layers = [{aliases = [@root::@top::@body::@work::@block::@same], path = array<i64>}]} {}
            talos.sv.symbol.enum_value @a attributes {constant_value = "9'sb111111110", hierarchical_name = "top.work.A.dot", name = "A.dot", node_id = 16 : i64, semantic_type = !state, vpi_source_type_identity = 100 : i64} {}
            talos.sv.symbol.variable @state attributes {lifetime = 1 : i32, hierarchical_name = "top.work.state", name = "state", node_id = 17 : i64, semantic_type = !state, vpi_source_type_identity = 100 : i64, vpi_typedef_layers = [{aliases = [@root::@top::@body::@work::@block::@same], path = array<i64>}]} {}
            talos.sv.symbol.statement_block @block attributes {block_kind = 0 : i32, hierarchical_name = "top.work", node_id = 18 : i64} {
              talos.sv.type.type_alias @same attributes {hierarchical_name = "top.work.same_t", name = "same_t", node_id = 19 : i64, semantic_type = !word, vpi_typedef_layers = [{aliases = [@root::@top::@body::@work::@block::@block::@same], path = array<i64>}]} {}
              talos.sv.symbol.variable @value attributes {lifetime = 1 : i32, hierarchical_name = "top.work.value", name = "value", node_id = 20 : i64, semantic_type = !word, vpi_typedef_layers = [{aliases = [@root::@top::@body::@work::@block::@block::@same], path = array<i64>}]} {}
              talos.sv.symbol.enum_value @b attributes {constant_value = "2'b10", hierarchical_name = "top.work.B", name = "B", node_id = 21 : i64, semantic_type = !anon, vpi_source_type_identity = 101 : i64} {}
              talos.sv.symbol.variable @anonymous attributes {lifetime = 1 : i32, hierarchical_name = "top.work.anonymous", name = "anonymous", node_id = 22 : i64, semantic_type = !anon, vpi_source_type_identity = 101 : i64} {}
            }
          }
        }
        talos.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", node_id = 23 : i64, procedure_kind = 0 : i32} {
          talos.sv.statement.expression_statement attributes {node_id = 24 : i64} {
            talos.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "work", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, node_id = 25 : i64, referenced_path = "top.work", referenced_symbol = @root::@top::@body::@work, semantic_type = !talos.void, subroutine_kind = 0 : i32} {}
          }
        }
      }
    }
  }
}


// CHECK-DAG: simulation.statement.decl [[OUTER:[0-9]+]] in [[FUNCTION:[0-9]+]] scope 1 type 4 {is_scope}
// CHECK-DAG: simulation.statement.decl [[INNER:[0-9]+]] in [[FUNCTION]] scope 1 type 4 parent [[OUTER]] {is_scope}
// CHECK-DAG: simulation.storage.decl [[STATE:[0-9]+]] {{.*}}hierarchy "top.work.state" debug "state" {{.*}}typedefAliases = [@[[OUTER_TYPE:[a-zA-Z_0-9]+]]]
// CHECK-DAG: simulation.storage.decl [[VALUE:[0-9]+]] {{.*}}hierarchy "top.work.value" debug "value" {{.*}}typedefAliases = [@[[INNER_TYPE:[a-zA-Z_0-9]+]]]
// CHECK-DAG: simulation.storage.decl [[ANON:[0-9]+]] {{.*}}hierarchy "top.work.anonymous" debug "anonymous" {{.*}}simulation.vpi_source_type_identity = 101
// CHECK-DAG: simulation.vpi_static_local.bind [[STATE]] in [[FUNCTION]] within [[OUTER]] ordinal 0
// CHECK-DAG: simulation.vpi_static_local.bind [[VALUE]] in [[FUNCTION]] within [[INNER]] ordinal 0
// CHECK-DAG: simulation.vpi_static_local.bind [[ANON]] in [[FUNCTION]] within [[INNER]] ordinal 1
// CHECK-DAG: simulation.vpi_typespec.decl @[[OUTER_TYPE]] {{.*}}within [[OUTER]]
// CHECK-DAG: simulation.vpi_typespec.decl @[[INNER_TYPE]] {{.*}}within [[INNER]]
