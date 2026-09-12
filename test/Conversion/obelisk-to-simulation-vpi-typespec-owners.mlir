// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0 early-symbol-dce=false' | FileCheck %s

!bit = !obelisk.integral<1, false, false, 0 : 0, bit>
!state = !obelisk.enum<"state_t", !obelisk.integral<2, false, false, 1 : 0, bit>>

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 1 : i32,
      hierarchical_name = "iface_t", name = "iface_t", node_id = 0 : i64,
      sym_name = "iface_def"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32,
      hierarchical_name = "wrapper_t", name = "wrapper_t",
      node_id = 21 : i64, sym_name = "wrapper_def"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64, sym_name = "root"} {
    // The two compilation units deliberately have the same VPI display name.
    obelisk.sv.symbol.compilation_unit attributes {
        hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "cu_a"} {
      obelisk.sv.type.type_alias attributes {
          hierarchical_name = "$unit::same_t", name = "same_t",
          node_id = 3 : i64, semantic_type = !state, sym_name = "same",
          vpi_source_type_identity = 10 : i64,
          vpi_typedef_layers = [{aliases = [@root::@cu_a::@same], path = array<i64>}]} {}
      obelisk.sv.symbol.enum_value attributes {constant_value = "2'b00",
          hierarchical_name = "$unit::state_t.IDLE", name = "IDLE",
          node_id = 10 : i64, semantic_type = !state, sym_name = "idle_a",
          vpi_source_type_identity = 10 : i64} {}
    }
    obelisk.sv.symbol.compilation_unit attributes {
        hierarchical_name = "$unit", node_id = 4 : i64, sym_name = "cu_b"} {
      obelisk.sv.type.type_alias attributes {
          hierarchical_name = "$unit::same_t", name = "same_t",
          node_id = 5 : i64, semantic_type = !state, sym_name = "same",
          vpi_source_type_identity = 11 : i64,
          vpi_typedef_layers = [{aliases = [@root::@cu_b::@same], path = array<i64>}]} {}
      obelisk.sv.symbol.enum_value attributes {constant_value = "2'b01",
          hierarchical_name = "$unit::state_t.RUN", name = "RUN",
          node_id = 11 : i64, semantic_type = !state, sym_name = "run_b",
          vpi_source_type_identity = 11 : i64} {}
      obelisk.sv.symbol.sequence attributes {has_default_instance = false,
          hierarchical_name = "$unit::seq", name = "seq", node_id = 12 : i64,
          port_count = 1 : i64, port_paths = ["$unit::seq.arg"],
          port_symbols = [@root::@cu_b::@seq::@seq_arg], sym_name = "seq"} {
        obelisk.sv.symbol.assertion_port attributes {
            has_default_value = false, hierarchical_name = "$unit::seq.arg",
            is_local_variable = false, name = "arg", node_id = 13 : i64,
            semantic_type = !obelisk.sequence, sym_name = "seq_arg"} {}
      }
      obelisk.sv.symbol.property attributes {has_default_instance = false,
          hierarchical_name = "$unit::prop", name = "prop",
          node_id = 14 : i64, port_count = 1 : i64,
          port_paths = ["$unit::prop.arg"],
          port_symbols = [@root::@cu_b::@prop::@prop_arg], sym_name = "prop"} {
        obelisk.sv.symbol.assertion_port attributes {
            has_default_value = false, hierarchical_name = "$unit::prop.arg",
            is_local_variable = false, name = "arg", node_id = 15 : i64,
            semantic_type = !obelisk.property, sym_name = "prop_arg"} {}
        }
      // Statement-backed ownership is deliberately deferred as a unit: this
      // local enum must neither escape to the compilation unit nor make the
      // otherwise valid lowering fail.
      obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32,
          hierarchical_name = "$unit::local", name = "local",
          node_id = 16 : i64, sym_name = "local"} {
        obelisk.sv.type.type_alias attributes {
            hierarchical_name = "$unit::local::local_t", name = "local_t",
            node_id = 17 : i64, semantic_type = !state, sym_name = "same",
            vpi_source_type_identity = 12 : i64,
            vpi_typedef_layers = [{aliases = [@root::@cu_b::@local::@same], path = array<i64>}]} {}
        obelisk.sv.symbol.enum_value attributes {constant_value = "2'b10",
            hierarchical_name = "$unit::local::state_t.LOCAL", name = "LOCAL",
            node_id = 18 : i64, semantic_type = !state, sym_name = "local_value",
            vpi_source_type_identity = 12 : i64} {}
        obelisk.sv.symbol.instance attributes {
            hierarchical_name = "$unit::local::iface_t",
            is_uninstantiated = false, is_virtual_interface_type_instance = true,
            name = "iface_t", node_id = 19 : i64,
            referenced_path = "iface_t", referenced_symbol = @iface_def,
            sym_name = "vif_type"} {
          obelisk.sv.symbol.instance_body attributes {
              hierarchical_name = "$unit::local::iface_t",
              is_virtual_interface_type_instance = true, name = "iface_t",
              node_id = 20 : i64, sym_name = "vif_body",
              virtual_interface_identity = @root::@cu_b::@local::@vif_type} {}
        }
        // Static storage survives lowering, but its VPI type relation must be
        // deferred together with its statement-owned typespec.
        obelisk.sv.symbol.variable attributes {
            hierarchical_name = "$unit::local::local_value", lifetime = 1 : i32,
            name = "local_value", node_id = 22 : i64, semantic_type = !state,
            sym_name = "local_storage",
            vpi_typedef_layers = [{aliases = [@root::@cu_b::@local::@same], path = array<i64>}]} {}
        obelisk.sv.symbol.variable attributes {
            hierarchical_name = "$unit::local::local_vif", lifetime = 1 : i32,
            name = "local_vif", node_id = 23 : i64,
            semantic_type = !obelisk.virtual_interface<@root::@cu_b::@local::@vif_type, "">,
            sym_name = "local_vif"} {}
      }
    }
    // Exercise fallback lookup for both authored raw wrapper paths and the
    // frontend's collapsed instance-body paths. These bodies intentionally do
    // not carry virtual_interface_identity, so the primary body map cannot
    // satisfy either reference.
    obelisk.sv.symbol.instance attributes {hierarchical_name = "raw_top",
        is_uninstantiated = false, name = "raw_top", node_id = 24 : i64,
        referenced_path = "wrapper_t", referenced_symbol = @wrapper_def,
        sym_name = "raw_wrapper"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "raw_top",
          name = "wrapper_t", node_id = 25 : i64, sym_name = "raw_body"} {
        obelisk.sv.symbol.instance attributes {
            hierarchical_name = "raw_top.iface_t", is_uninstantiated = false,
            is_virtual_interface_type_instance = true, name = "iface_t",
            node_id = 26 : i64, referenced_path = "iface_t",
            referenced_symbol = @iface_def, sym_name = "raw_iface"} {
          obelisk.sv.symbol.instance_body attributes {
              hierarchical_name = "raw_top.iface_t", name = "iface_t",
              node_id = 27 : i64, sym_name = "raw_iface_body"} {}
        }
        obelisk.sv.symbol.variable attributes {
            hierarchical_name = "raw_top.raw_vif", lifetime = 1 : i32,
            name = "raw_vif", node_id = 28 : i64,
            semantic_type = !obelisk.virtual_interface<@root::@raw_wrapper::@raw_body::@raw_iface, "">,
            sym_name = "raw_vif"} {}
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "collapsed_top",
        is_uninstantiated = false, name = "collapsed_top", node_id = 29 : i64,
        referenced_path = "wrapper_t", referenced_symbol = @wrapper_def,
        sym_name = "collapsed_wrapper"} {
      obelisk.sv.symbol.instance_body attributes {
          hierarchical_name = "collapsed_top", name = "wrapper_t",
          node_id = 30 : i64, sym_name = "collapsed_body"} {
        obelisk.sv.symbol.instance attributes {
            hierarchical_name = "collapsed_top.iface_t",
            is_uninstantiated = false,
            is_virtual_interface_type_instance = true, name = "iface_t",
            node_id = 31 : i64, referenced_path = "iface_t",
            referenced_symbol = @iface_def, sym_name = "collapsed_iface"} {
          obelisk.sv.symbol.instance_body attributes {
              hierarchical_name = "collapsed_top.iface_t", name = "iface_t",
              node_id = 32 : i64, sym_name = "collapsed_iface_body"} {}
        }
        obelisk.sv.symbol.variable attributes {
            hierarchical_name = "collapsed_top.collapsed_vif",
            lifetime = 1 : i32, name = "collapsed_vif", node_id = 33 : i64,
            semantic_type = !obelisk.virtual_interface<@root::@collapsed_body::@collapsed_iface, "">,
            sym_name = "collapsed_vif"} {}
      }
    }
  }
  obelisk.sv.symbol.package attributes {hierarchical_name = "pkg",
      name = "pkg", node_id = 6 : i64, sym_name = "pkg"} {
    obelisk.sv.type.type_alias attributes {hierarchical_name = "pkg::unused_t",
        name = "unused_t", node_id = 7 : i64, semantic_type = !bit,
        sym_name = "pkg_unused"} {}
    obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64,
        declared_interfaces = [], generic_parameter_paths = [],
        generic_parameter_symbols = [], has_base_constructor_call = false,
        has_cycles = false, hierarchical_name = "pkg::C",
        implemented_interfaces = [], is_abstract = false, is_final = false,
        is_interface = false, is_uninstantiated = false, name = "C",
        node_id = 8 : i64,
        semantic_type = !obelisk.class_handle<@pkg::@class_c>,
        sym_name = "class_c"} {
      obelisk.sv.type.type_alias attributes {
          hierarchical_name = "pkg::C::local_t", name = "local_t",
          node_id = 9 : i64, semantic_type = !bit, sym_name = "class_local"} {}
    }
    obelisk.sv.symbol.generic_class_def attributes {
        hierarchical_name = "pkg::G", is_interface = false, name = "G",
        node_id = 34 : i64, specialization_count = 1 : i64,
        sym_name = "generic_def"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64,
          declared_interfaces = [], generic_parameter_paths = [],
          generic_parameter_symbols = [], has_base_constructor_call = false,
          has_cycles = false, hierarchical_name = "pkg::G#(bit)",
          implemented_interfaces = [], is_abstract = false, is_final = false,
          is_interface = false, is_uninstantiated = false, name = "G",
          node_id = 35 : i64,
          semantic_type = !obelisk.class_handle<@pkg::@generic_spec>,
          sym_name = "generic_spec"} {
        obelisk.sv.type.type_alias attributes {
            hierarchical_name = "pkg::G#(bit)::item_t", name = "item_t",
            node_id = 36 : i64, semantic_type = !bit,
            sym_name = "generic_alias",
            vpi_typedef_layers = [{aliases = [@pkg::@generic_spec::@generic_alias], path = array<i64>}]} {}
      }
    }
  }
}

// CHECK-NOT: obelisk_sim.vpi_object.anchor {{.*}}hierarchy "\\$root "
// CHECK-DAG: obelisk_sim.vpi_object.anchor @[[CU_A:__obelisk_vpi_anchor_0]] id 0 type 600 in 0 ordinal 0 hierarchy "$unit" debug "" {{\{.*}}is_compilation_unit, {{.*}}}
// CHECK-DAG: obelisk_sim.vpi_object.anchor @[[CU_B:__obelisk_vpi_anchor_1]] id 1 type 600 in 0 ordinal 1 hierarchy "$unit" debug "" {{\{.*}}is_compilation_unit, {{.*}}}
// CHECK-DAG: obelisk_sim.vpi_object.anchor @[[SEQ:__obelisk_vpi_anchor_2]] id 2 type 661 in 0 parent @[[CU_B]] ordinal 0 hierarchy "$unit::seq" debug "seq"
// CHECK-DAG: obelisk_sim.vpi_object.anchor @[[PROP:__obelisk_vpi_anchor_3]] id 3 type 655 in 0 parent @[[CU_B]] ordinal 1 hierarchy "$unit::prop" debug "prop"
// CHECK-DAG: obelisk_sim.vpi_object.anchor @[[RAW_TOP:__obelisk_vpi_anchor_4]] id 4 type 32 in 1 ordinal 2 hierarchy "raw_top" debug "wrapper_t" {{.*}}
// CHECK-DAG: obelisk_sim.vpi_object.anchor @[[COLLAPSED_TOP:__obelisk_vpi_anchor_5]] id 5 type 32 in 2 ordinal 3 hierarchy "collapsed_top" debug "wrapper_t" {{.*}}
// CHECK-DAG: obelisk_sim.vpi_object.anchor @[[PKG:__obelisk_vpi_anchor_6]] id 6 type 600 in 0 ordinal 4 hierarchy "pkg" debug "pkg" {{.*}}
// CHECK-DAG: obelisk_sim.vpi_object.anchor @[[CLASS:__obelisk_vpi_anchor_7]] id 7 type 652 in 0 parent @[[PKG]] ordinal 0 hierarchy "pkg::C" debug "C"
// CHECK-DAG: obelisk_sim.vpi_object.anchor @[[GENERIC_CLASS:__obelisk_vpi_anchor_8]] id 8 type 652 in 0 parent @[[PKG]] ordinal 1 hierarchy "pkg::G#(bit)" debug "G"
// CHECK-DAG: obelisk_sim.vpi_typespec.decl @[[CU_A_TS:__obelisk_vpi_typespec_0]] id 0 in 0 owner @[[CU_A]] hierarchy "$unit::same_t"{{.*}}typedefAliases = [@[[CU_A_TS]]]>
// CHECK-DAG: obelisk_sim.vpi_typespec.decl @[[CU_B_TS:__obelisk_vpi_typespec_1]] id 1 in 0 owner @[[CU_B]] hierarchy "$unit::same_t"{{.*}}typedefAliases = [@[[CU_B_TS]]]>
// CHECK-DAG: obelisk_sim.vpi_typespec.decl @{{__obelisk_vpi_typespec_[0-9]+}} id 2 in 0 owner @[[PKG]] hierarchy "pkg::unused_t"
// CHECK-DAG: obelisk_sim.vpi_typespec.decl @{{__obelisk_vpi_typespec_[0-9]+}} id 3 in 0 owner @[[CLASS]] hierarchy "pkg::C::local_t"
// CHECK-DAG: obelisk_sim.vpi_typespec.decl @[[GENERIC_TS:__obelisk_vpi_typespec_[0-9]+]] id 4 in 0 owner @[[GENERIC_CLASS]] hierarchy "pkg::G#(bit)::item_t"{{.*}}typedefAliases = [@[[GENERIC_TS]]]>
// CHECK-DAG: obelisk_sim.vpi_typespec.decl @[[RAW_VIF_TS:__obelisk_vpi_interface_typespec_[0-9A-F]+_modport_]] id {{[0-9]+}} in 1 owner @[[RAW_TOP]] hierarchy "@root::@raw_wrapper::@raw_body::@raw_iface"
// CHECK-DAG: obelisk_sim.vpi_typespec.decl @[[COLLAPSED_VIF_TS:__obelisk_vpi_interface_typespec_[0-9A-F]+_modport_]] id {{[0-9]+}} in 2 owner @[[COLLAPSED_TOP]] hierarchy "@root::@collapsed_body::@collapsed_iface"
// CHECK-DAG: obelisk_sim.vpi_enum_const.decl 0 enum @[[CU_A_TS]] ordinal 0 name "IDLE" value "2'b00"
// CHECK-DAG: obelisk_sim.vpi_enum_const.decl 1 enum @[[CU_B_TS]] ordinal 0 name "RUN" value "2'b01"
// CHECK-DAG: obelisk_sim.storage.decl {{[0-9]+}} in 1 : {{.*}} hierarchy "raw_top.raw_vif"{{.*}}symbol = @[[RAW_VIF_TS]]
// CHECK-DAG: obelisk_sim.storage.decl {{[0-9]+}} in 2 : {{.*}} hierarchy "collapsed_top.collapsed_vif"{{.*}}symbol = @[[COLLAPSED_VIF_TS]]
// CHECK-DAG: obelisk_sim.storage.decl {{[0-9]+}} in 0 : {{.*}} static hierarchy "$unit::local::local_value" debug "local_value" {{.*}}observability = 0 : i32
// CHECK-DAG: obelisk_sim.storage.decl {{[0-9]+}} in 0 : {{.*}} static hierarchy "$unit::local::local_vif" debug "local_vif" {{.*}}observability = 0 : i32
// CHECK-NOT: hierarchy "$unit::local::local_t"
// CHECK-NOT: name "LOCAL"
// CHECK-NOT: hierarchy "@root::@cu_b::@local::@vif_type"
// CHECK-NOT: obelisk.sv.
