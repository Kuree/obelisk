// RUN: not talos-opt %s '--lower-talos-to-sim=opt-level=0' 2>&1 | FileCheck %s

// An explicit extern declaration cannot silently become an empty block.
// CHECK: error: explicit extern constraint block has no resolved body

module {
  talos.sv.symbol.definition @top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {}
  talos.sv.symbol.root @root attributes {hierarchical_name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @unit attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
      talos.sv.type.class_type @C attributes {
        bitstream_width = 1 : i64, declared_interfaces = [],
        generic_parameter_paths = [], generic_parameter_symbols = [],
        has_base_constructor_call = false, has_cycles = false,
        hierarchical_name = "C", implemented_interfaces = [],
        is_abstract = false, is_final = false, is_interface = false,
        is_uninstantiated = false, name = "C", node_id = 3 : i64,
        semantic_type = !talos.class_handle<@root::@unit::@C>,
        this_variable_path = "C::this", this_variable_symbol = @root::@unit::@C::@this
      } {
        talos.sv.symbol.class_property @x attributes {hierarchical_name = "C::x", name = "x", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {}
        talos.sv.symbol.variable @this attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 5 : i64, semantic_type = !talos.class_handle<@root::@unit::@C>} {}
        talos.sv.symbol.constraint_block @external attributes {hierarchical_name = "C::external", is_extern, is_explicit_extern, name = "external", node_id = 6 : i64} {}
      }
    }
    talos.sv.symbol.instance @instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 7 : i64, referenced_path = "top", referenced_symbol = @top} {
      talos.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", node_id = 8 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        talos.sv.symbol.variable @object attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 9 : i64, semantic_type = !talos.class_handle<@root::@unit::@C>} {}
        talos.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", node_id = 10 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 11 : i64} {
            talos.sv.expression.call attributes {
              argument_count = 1 : i64, callee_name = "randomize",
              constraint_restrictions = [], defaulted_arguments = array<i64: 0>,
              has_inline_constraints = false, has_iterator_expression = false,
              has_output_arguments = false, has_this_class = false,
              is_super_class = false, is_system_call = true, node_id = 12 : i64,
              semantic_type = !talos.integral<32, true, false, 31 : 0, int>,
              subroutine_kind = 0 : i32, system_scope_path = "top",
              system_scope_symbol = @root::@instance::@body
            } {
              talos.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "top.object", referenced_symbol = @root::@instance::@body::@object, semantic_type = !talos.class_handle<@root::@unit::@C>} {}
            }
          }
        }
      }
    }
  }
}
