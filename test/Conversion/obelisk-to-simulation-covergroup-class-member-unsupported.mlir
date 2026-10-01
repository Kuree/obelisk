// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 19.4 defines an embedded covergroup as an anonymous type and
// class member. The unused member is outside the executable closure, so it is
// removed before unsupported executable lowering while its owner class stays.

module {
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @unit attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
      obelisk.sv.type.class_type @covergroup_owner attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "covergroup_owner", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "covergroup_owner", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@root::@unit::@covergroup_owner>, this_variable_path = "covergroup_owner::this", this_variable_symbol = @root::@unit::@covergroup_owner::@this} {
        obelisk.sv.type.covergroup_type @s4 attributes {constructor_argument_count = 0 : i64, constructor_formals = [], coverage_event_kind = 0 : i32, has_coverage_event = false, hierarchical_name = "covergroup_owner", node_id = 4 : i64, sample_formal_count = 0 : i64, sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@unit::@covergroup_owner::@s4>} {
        }
        obelisk.sv.symbol.variable @this attributes {hierarchical_name = "covergroup_owner::this", is_compiler_generated, is_const, name = "this", node_id = 77 : i64, semantic_type = !obelisk.class_handle<@root::@unit::@covergroup_owner>} {
        }
      }
    }
  }
}

// CHECK: simulation.class.decl @__obelisk_class_covergroup_owner
// CHECK: simulation.func private @__obelisk_class_covergroup_owner_implicit_new
// CHECK-NOT: simulation.covergroup
// CHECK-NOT: obelisk.sv.
