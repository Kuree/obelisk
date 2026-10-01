// RUN: obelisk-opt %s --obelisk-sim-prepare | FileCheck %s
// RUN: obelisk-opt %s '--obelisk-sim-prepare=prune-unused-coverage=false' \
// RUN:   | FileCheck %s --check-prefix=PRESERVE

module {
  obelisk.sv.symbol.definition @definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.instance @instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 2 : i64, referenced_path = "top", referenced_symbol = @definition} {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", node_id = 3 : i64} {
        obelisk.sv.type.class_type @class attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "top.C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 4 : i64, semantic_type = !obelisk.class_handle<@root::@body::@class>, this_variable_path = "top.C::this", this_variable_symbol = @root::@instance::@body::@class::@this} {
          obelisk.sv.type.covergroup_type @covergroup attributes {constructor_argument_count = 0 : i64, constructor_formals = [], coverage_event_kind = 0 : i32, has_coverage_event = false, hierarchical_name = "top.C", node_id = 5 : i64, sample_formal_count = 0 : i64, sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@body::@class::@covergroup>} {
            obelisk.sv.symbol.covergroup_body @covergroup_body attributes {hierarchical_name = "top.C", node_id = 6 : i64, option_count = 1 : i64} {
              obelisk.sv.coverage.option attributes {node_id = 60 : i64, option_kind = 9 : i32, owner_kind = 0 : i32, owner_symbol = @covergroup, scope_kind = 0 : i32} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 61 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
          obelisk.sv.symbol.class_property @cg attributes {hierarchical_name = "top.C::cg", is_const, name = "cg", node_id = 7 : i64, semantic_type = !obelisk.covergroup_handle<@root::@body::@class::@covergroup>} {
          }
          obelisk.sv.symbol.variable @this attributes {hierarchical_name = "top.C::this", is_compiler_generated, is_const, name = "this", node_id = 8 : i64, semantic_type = !obelisk.class_handle<@root::@body::@class>} {
          }
        }
      }
    }
  }
}

// CHECK: simulation.class.decl
// CHECK-NOT: simulation.covergroup.decl
// CHECK-NOT: debug_name = "cg"
// PRESERVE: simulation.covergroup.decl
// PRESERVE: simulation.class.field
// PRESERVE-SAME: debug_name = "cg"
