// RUN: talos-opt %s '--lower-talos-to-sim=vpi=read' -o %t
// RUN: FileCheck %s < %t
// RUN: FileCheck %s --check-prefix=COUNT < %t
// RUN: talos-opt %s '--lower-talos-to-sim=vpi=off' | FileCheck %s --check-prefix=OFF --implicit-check-not=class_method --implicit-check-not=simulation.class.method --implicit-check-not='simulation.func private @unit_'

// VPI-only executable bindings must not retain unused methods in off mode.
// Immutable source declarations remain valid after their code is pruned.
// OFF: simulation.design
// OFF: simulation.vpi_object.anchor {{.*}}hierarchy "Base::ping"
// OFF: simulation.func @__talos_root

// Class definitions have one lexical identity. Generic specializations are
// canonical class typespecs, not additional vpiClassDefn declarations.
module {
  talos.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 0 : i64} {
    talos.sv.symbol.compilation_unit @cu attributes {hierarchical_name = "$unit", node_id = 1 : i64} {
      talos.sv.type.class_type @base attributes {bitstream_width = 0 : i64,
          declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [],
          has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Base",
          implemented_interfaces = [], is_abstract = true, is_final = false,
          is_interface = false, is_uninstantiated = false, name = "Base",
          node_id = 2 : i64, semantic_type = !talos.class_handle<@root::@cu::@base>} {
        talos.sv.symbol.class_property @value attributes {hierarchical_name = "Base::value", name = "value",
            node_id = 10 : i64, semantic_type = !talos.integral<4, false, true, 3 : 0, logic>} {}
        talos.sv.symbol.class_property @count attributes {hierarchical_name = "Base::count", name = "count",
            lifetime = 1 : i32, node_id = 11 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
        talos.sv.symbol.class_property @ready attributes {hierarchical_name = "Base::ready", name = "ready",
            lifetime = 1 : i32, node_id = 12 : i64, semantic_type = !talos.event} {}
        talos.sv.symbol.subroutine @ping attributes {hierarchical_name = "Base::ping", is_static, name = "ping",
            node_id = 13 : i64, semantic_type = !talos.subroutine<() -> !talos.void, false>,
            subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {}
      }
      talos.sv.type.class_type @derived attributes {base_class = !talos.class_handle<@root::@cu::@base>, bitstream_width = 0 : i64,
          declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [],
          has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Derived",
          implemented_interfaces = [], is_abstract = false, is_final = false,
          is_interface = false, is_uninstantiated = false, name = "Derived",
          node_id = 3 : i64, semantic_type = !talos.class_handle<@root::@cu::@derived>} {}
      talos.sv.type.class_type @deep attributes {base_class = !talos.class_handle<@root::@cu::@derived>, bitstream_width = 0 : i64,
          declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [],
          has_base_constructor_call = false, has_cycles = false, hierarchical_name = "ZDeep",
          implemented_interfaces = [], is_abstract = false, is_final = false,
          is_interface = false, is_uninstantiated = false, name = "ZDeep",
          node_id = 4 : i64, semantic_type = !talos.class_handle<@root::@cu::@deep>} {}
      talos.sv.symbol.generic_class_def @generic attributes {
          hierarchical_name = "Generic", name = "Generic", node_id = 5 : i64,
          is_interface = false, specialization_count = 2 : i64} {
      talos.sv.type.class_type @special_a attributes {base_class = !talos.class_handle<@root::@cu::@base>, bitstream_width = 0 : i64,
          declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [],
          has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Generic#(int)",
          implemented_interfaces = [], is_abstract = false, is_final = false,
          is_interface = false, is_uninstantiated = false, name = "Generic",
          node_id = 6 : i64, semantic_type = !talos.class_handle<@root::@cu::@special_a>} {
        talos.sv.symbol.class_property @item attributes {hierarchical_name = "Generic#(int)::item", name = "item",
            node_id = 14 : i64, semantic_type = !talos.integral<8, false, false, 7 : 0, bit>} {}
        talos.sv.type.type_alias @word attributes {hierarchical_name = "Generic#(int)::word",
            name = "word", node_id = 8 : i64, semantic_type = !talos.integral<8, false, false, 7 : 0, bit>} {}
        talos.sv.symbol.subroutine @method attributes {hierarchical_name = "Generic#(int)::method",
            is_static, name = "method", node_id = 9 : i64,
            semantic_type = !talos.subroutine<() -> !talos.void, false>, subroutine_kind = 0 : i32,
            time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {}
      }
      talos.sv.type.class_type @special_b attributes {base_class = !talos.class_handle<@root::@cu::@base>, bitstream_width = 0 : i64,
          declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [],
          has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Generic#(bit)",
          implemented_interfaces = [], is_abstract = false, is_final = false,
          is_interface = false, is_uninstantiated = false, name = "Generic",
          node_id = 7 : i64, semantic_type = !talos.class_handle<@root::@cu::@special_b>} {}
      }
    }
  }
}

// CHECK-DAG: simulation.vpi_object.anchor @[[BASE:[^ ]+]] {{.*}}type 652 {{.*}}hierarchy "Base"
// CHECK-DAG: simulation.vpi_object.anchor @[[DERIVED:[^ ]+]] {{.*}}type 652 {{.*}}hierarchy "Derived"
// CHECK-DAG: simulation.vpi_object.anchor @[[GENERIC:[^ ]+]] {{.*}}type 652 {{.*}}hierarchy "Generic"
// CHECK-DAG: simulation.vpi_typespec.decl @[[BASE_TS:[^ ]+]] {{.*}}owner @[[BASE]] hierarchy "Base"{{.*}}origin = 3 : i32
// CHECK-DAG: simulation.vpi_typespec.decl @[[DERIVED_TS:[^ ]+]] {{.*}}owner @[[DERIVED]] hierarchy "Derived"{{.*}}origin = 3 : i32
// CHECK-DAG: simulation.vpi_typespec.decl @[[SPEC_A:[^ ]+]] {{.*}}owner @[[GENERIC]] hierarchy "Generic#(int)"{{.*}}origin = 3 : i32
// CHECK-DAG: simulation.vpi_typespec.decl @[[SPEC_B:[^ ]+]] {{.*}}owner @[[GENERIC]] hierarchy "Generic#(bit)"{{.*}}origin = 3 : i32
// CHECK-DAG: simulation.vpi_relation.decl <kind = symbol, symbol = @[[DERIVED_TS]]> selector 677 handle ordinal 0 to <kind = symbol, symbol = @[[BASE_TS]]>
// CHECK-DAG: simulation.vpi_relation.decl <kind = symbol, symbol = @[[SPEC_A]]> selector 677 handle ordinal 0 to <kind = symbol, symbol = @[[BASE_TS]]>
// CHECK-DAG: simulation.vpi_relation.decl <kind = symbol, symbol = @[[SPEC_B]]> selector 677 handle ordinal 0 to <kind = symbol, symbol = @[[BASE_TS]]>
// CHECK-DAG: simulation.vpi_object.anchor @[[EXTENDS:[^ ]+]] {{.*}}type 677 {{.*}}parent @[[DERIVED]] {{.*}}hierarchy "Derived::extends"
// CHECK-DAG: simulation.vpi_relation.decl <kind = symbol, symbol = @[[DERIVED]]> selector 677 handle ordinal 0 to <kind = symbol, symbol = @[[EXTENDS]]>
// CHECK-DAG: simulation.vpi_relation.decl <kind = symbol, symbol = @[[EXTENDS]]> selector 652 handle ordinal 0 to <kind = symbol, symbol = @[[DERIVED]]>
// CHECK-DAG: simulation.vpi_relation.decl <kind = symbol, symbol = @[[EXTENDS]]> selector 630 handle ordinal 0 to <kind = symbol, symbol = @[[BASE_TS]]>
// CHECK-DAG: simulation.vpi_relation.decl <kind = symbol, symbol = @[[BASE]]> selector 727 iterate ordinal 0 to <kind = symbol, symbol = @[[DERIVED]]>
// CHECK-DAG: simulation.vpi_relation.decl <kind = symbol, symbol = @[[BASE]]> selector 727 iterate ordinal 1 to <kind = symbol, symbol = @[[GENERIC]]>
// CHECK-DAG: simulation.vpi_relation.decl <kind = symbol, symbol = @[[BASE]]> selector 727 iterate ordinal 2 to <kind = symbol, symbol = @[[DEEP:__talos_vpi_anchor_[0-9]+]]>
// CHECK-DAG: simulation.vpi_relation.decl <kind = symbol, symbol = @[[DERIVED]]> selector 727 iterate ordinal 0 to <kind = symbol, symbol = @[[DEEP]]>
// CHECK-DAG: simulation.vpi_typespec.decl @{{[^ ]+}} {{.*}}owner @[[SPEC_A]] hierarchy "Generic#(int)::word"
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 20 {{.*}}parent @[[SPEC_A]] {{.*}}hierarchy "Generic#(int)::method" {{.*}}class_method = @{{[^ ,}]+}}
// CHECK-DAG: simulation.class.field @[[VALUE:[^ ]+]] {{.*}}debug_name = "value"
// CHECK-DAG: simulation.class.field @[[COUNT_FIELD:[^ ]+]] {{.*}}debug_name = "count"{{.*}}is_static = true
// CHECK-DAG: simulation.class.field @[[READY:[^ ]+]] {{.*}}debug_name = "ready"{{.*}}is_static = true
// CHECK-DAG: simulation.vpi_class_member.decl @{{[^ ]+}} {{.*}}owner @[[BASE_TS]] field @[[VALUE]] hierarchy "Base::value"
// CHECK-DAG: simulation.vpi_class_member.decl @{{[^ ]+}} {{.*}}owner @[[BASE]] field @[[VALUE]] hierarchy "Base::value"
// CHECK-DAG: simulation.vpi_class_member.decl @{{[^ ]+}} {{.*}}owner @[[BASE_TS]] field @[[COUNT_FIELD]] hierarchy "Base::count"{{.*}}static_storage = [[COUNT_ID:[0-9]+]] : i64
// CHECK-DAG: simulation.vpi_class_member.decl @{{[^ ]+}} {{.*}}owner @[[BASE]] field @[[COUNT_FIELD]] hierarchy "Base::count"{{.*}}static_storage = [[COUNT_ID]] : i64
// CHECK-DAG: simulation.vpi_class_member.decl @{{[^ ]+}} {{.*}}owner @[[BASE_TS]] field @[[READY]] hierarchy "Base::ready"{{.*}}static_event = [[EVENT_ID:[0-9]+]] : i64
// CHECK-DAG: simulation.vpi_class_member.decl @{{[^ ]+}} {{.*}}owner @[[BASE]] field @[[READY]] hierarchy "Base::ready"{{.*}}static_event = [[EVENT_ID]] : i64
// CHECK-DAG: simulation.vpi_class_member.decl @{{[^ ]+}} {{.*}}owner @[[SPEC_A]] field @{{[^ ]+}} hierarchy "Generic#(int)::item"
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 20 {{.*}}parent @[[BASE]] {{.*}}hierarchy "Base::ping" {{.*}}class_method = @[[PING:[^ ,}]+]]
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 20 {{.*}}parent @[[BASE_TS]] {{.*}}hierarchy "Base::ping" {{.*}}backing = #simulation.vpi_backing<kind = code_unit{{.*}}class_method = @[[PING]]

// COUNT-COUNT-4: simulation.vpi_object.anchor {{.*}}type 652
// COUNT-NOT: simulation.vpi_object.anchor {{.*}}type 652
