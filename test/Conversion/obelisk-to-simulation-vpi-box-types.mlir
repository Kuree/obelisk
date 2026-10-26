// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=full opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=off opt-level=0' | FileCheck %s --check-prefix=OFF
!word = !obelisk.integral<32, true, false, 31 : 0, int>
!mailbox = !obelisk.class_handle<@std::@mailbox>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition @top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {}
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @unit attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
      obelisk.sv.type.type_alias @word attributes {hierarchical_name = "$unit::Word", name = "Word", node_id = 3 : i64, semantic_type = !word,
        vpi_typedef_layers = [{aliases = [@root::@unit::@word], path = array<i64>}]} {}
    }
    obelisk.sv.symbol.instance @instance attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, is_uninstantiated = false, referenced_path = "top", referenced_symbol = @top} {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", node_id = 5 : i64} {
        obelisk.sv.symbol.variable @box attributes {hierarchical_name = "top.box", name = "box", node_id = 6 : i64, lifetime = 1 : i32, semantic_type = !mailbox} {}
        obelisk.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", node_id = 7 : i64, procedure_kind = 0 : i32, time_unit_fs = 1000000 : i64, time_precision_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 8 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "put", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 9 : i64, referenced_path = "std::mailbox#(dynamic_singular_type)::put", referenced_symbol = @std::@mailbox::@put, semantic_type = !obelisk.void, subroutine_kind = 1 : i32} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 10 : i64, referenced_path = "top.box", referenced_symbol = @root::@instance::@body::@box, semantic_type = !mailbox} {}
              obelisk.sv.expression.integer_literal attributes {constant_value = "-3", is_declared_unsized = true, is_signed = true, node_id = 11 : i64, semantic_type = !word,
                vpi_typedef_layers = [{aliases = [@root::@unit::@word], path = array<i64>}]} {}
            }
          }
        }
      }
    }
  }
  obelisk.sv.symbol.package @std attributes {hierarchical_name = "std", name = "std", node_id = 12 : i64} {
    obelisk.sv.type.class_type @mailbox attributes {hierarchical_name = "std::mailbox#(dynamic_singular_type)", name = "mailbox", node_id = 13 : i64, semantic_type = !mailbox,
      is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, has_base_constructor_call = false, has_cycles = false, bitstream_width = 0 : i64,
      declared_interfaces = [], implemented_interfaces = [], generic_parameter_paths = ["std::mailbox#(dynamic_singular_type)::T"], generic_parameter_symbols = [@std::@mailbox::@T]} {
      obelisk.sv.symbol.type_parameter @T attributes {hierarchical_name = "std::mailbox#(dynamic_singular_type)::T", name = "T", node_id = 14 : i64, semantic_type = !obelisk.untyped} {}
      obelisk.sv.symbol.subroutine @put attributes {hierarchical_name = "std::mailbox#(dynamic_singular_type)::put", is_builtin, name = "put", node_id = 15 : i64,
        semantic_type = !obelisk.subroutine<(!obelisk.untyped) -> (), true>, subroutine_kind = 1 : i32, time_unit_fs = 1000000 : i64, time_precision_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 16 : i64} {}
        obelisk.sv.symbol.formal_argument @message attributes {direction = 0 : i32, hierarchical_name = "std::mailbox#(dynamic_singular_type)::put.message", name = "message", node_id = 17 : i64, semantic_type = !obelisk.untyped} {}
      }
    }
  }
}
// CHECK: simulation.vpi_box_type.decl @[[BOX:__obelisk_vpi_box_type_[0-9]+]] id 1 : i32
// CHECK-SAME: typedefAliases = [@{{.*}}]
// CHECK: simulation.box.bind_type %{{.*}} to @[[BOX]] : !simulation.dynamic_array<i32>
// OFF-NOT: simulation.vpi_box_type.decl
// OFF-NOT: simulation.box.bind_type
// OFF: simulation.box.pack
