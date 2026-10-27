// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0 vpi=full' | FileCheck %s
// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=3 vpi=full' | FileCheck %s
// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0 vpi=off' | FileCheck %s --check-prefix=OFF
// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=3 vpi=full' -o %t.mlir
// RUN: env TALOS_TEST_INPUT=%t.mlir %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.ClassCallDispatchProducer
// The source occurrence keeps its typed receiver/method contract even after
// monomorphic dispatch becomes a direct executable call.
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  talos.sv.symbol.definition @definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {}
  talos.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @cu attributes {hierarchical_name = "$unit", name = "$unit", node_id = 2 : i64} {
      talos.sv.type.class_type @C attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !talos.class_handle<@root::@cu::@C>, this_variable_path = "C::this", this_variable_symbol = @root::@cu::@C::@this} {
        talos.sv.symbol.subroutine @f attributes {hierarchical_name = "C::f", is_declared_virtual, is_virtual, name = "f", node_id = 4 : i64, semantic_type = !talos.subroutine<() -> !talos.void, false>, subroutine_kind = 0 : i32, this_variable_path = "C::f.this", this_variable_symbol = @root::@cu::@C::@f::@this, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
          talos.sv.statement.list attributes {node_id = 5 : i64} {}
          talos.sv.symbol.variable @this attributes {hierarchical_name = "C::f.this", is_compiler_generated, is_const, name = "this", node_id = 6 : i64, semantic_type = !talos.class_handle<@root::@cu::@C>} {}
        }
        talos.sv.symbol.variable @this attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 7 : i64, semantic_type = !talos.class_handle<@root::@cu::@C>} {}
      }
    }
    talos.sv.symbol.instance @top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 8 : i64, referenced_path = "top", referenced_symbol = @definition} {
      talos.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", node_id = 9 : i64, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
        talos.sv.symbol.variable @c attributes {hierarchical_name = "top.c", lifetime = 1 : i32, name = "c", node_id = 10 : i64, semantic_type = !talos.class_handle<@root::@cu::@C>} {}
        talos.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", node_id = 11 : i64, procedure_kind = 0 : i32, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 12 : i64} {
            talos.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "f", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 13 : i64, referenced_path = "C::f", referenced_symbol = @root::@cu::@C::@f, semantic_type = !talos.void, subroutine_kind = 0 : i32} {
              talos.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "top.c", referenced_symbol = @root::@top::@body::@c, semantic_type = !talos.class_handle<@root::@cu::@C>} {}
            }
          }
        }
      }
    }
  }
}
// CHECK-DAG: simulation.class.method @[[METHOD:[^ ]+]] of @[[CLASS:[^ ]+]] slot {{[0-9]+}} signature_id {{[0-9]+}}
// CHECK-DAG: simulation.vpi_call.decl @[[CALL:[^ ]+]] {{.*}}type 648 name "f" {{.*}}is_indirect
// CHECK-DAG: simulation.vpi_class_call_dispatch @[[CALL]] on !simulation.class_handle<@[[CLASS]]> via @[[METHOD]]
// OFF-NOT: simulation.vpi_class_call_dispatch
