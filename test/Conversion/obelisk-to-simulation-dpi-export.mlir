// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s

module {
  obelisk.sv.symbol.definition @s0.dpi_export attributes {definition_kind = 0 : i32, hierarchical_name = "dpi_export", name = "dpi_export", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.dpi_export attributes {hierarchical_name = "dpi_export", is_uninstantiated = false, name = "dpi_export", node_id = 3 : i64, referenced_path = "dpi_export", referenced_symbol = @s0.dpi_export} {
      obelisk.sv.symbol.instance_body @s4.dpi_export attributes {hierarchical_name = "dpi_export", name = "dpi_export", node_id = 4 : i64} {
        obelisk.sv.symbol.subroutine @s5.exported attributes {default_lifetime = 1 : i32, dpi_export_c_identifier = "exported_c", hierarchical_name = "dpi_export.exported", name = "exported", node_id = 5 : i64, return_variable_path = "dpi_export.exported.exported", return_variable_symbol = @s1.$root::@s3.dpi_export::@s4.dpi_export::@s5.exported::@s7.exported, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.return attributes {node_id = 6 : i64} {
            obelisk.sv.expression.named_value attributes {node_id = 7 : i64, referenced_path = "dpi_export.exported.value", referenced_symbol = @s1.$root::@s3.dpi_export::@s4.dpi_export::@s5.exported::@s6.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
          obelisk.sv.symbol.formal_argument @s6.value attributes {direction = 0 : i32, hierarchical_name = "dpi_export.exported.value", lifetime = 1 : i32, name = "value", node_id = 8 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
          obelisk.sv.symbol.formal_argument @s15.copy attributes {direction = 1 : i32, hierarchical_name = "dpi_export.exported.copy", lifetime = 1 : i32, name = "copy", node_id = 17 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
          obelisk.sv.symbol.variable @s7.exported attributes {hierarchical_name = "dpi_export.exported.exported", is_compiler_generated, name = "exported", node_id = 9 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
      }
    }
    obelisk.sv.symbol.instance @s10.dpi_export_other attributes {hierarchical_name = "dpi_export_other", is_uninstantiated = false, name = "dpi_export_other", node_id = 10 : i64, referenced_path = "dpi_export", referenced_symbol = @s0.dpi_export} {
      obelisk.sv.symbol.instance_body @s11.dpi_export_other attributes {hierarchical_name = "dpi_export_other", name = "dpi_export", node_id = 11 : i64} {
        obelisk.sv.symbol.subroutine @s12.exported attributes {default_lifetime = 1 : i32, dpi_export_c_identifier = "exported_c", hierarchical_name = "dpi_export_other.exported", name = "exported", node_id = 12 : i64, return_variable_path = "dpi_export_other.exported.exported", return_variable_symbol = @s1.$root::@s10.dpi_export_other::@s11.dpi_export_other::@s12.exported::@s14.exported, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.return attributes {node_id = 13 : i64} {
            obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "dpi_export_other.exported.value", referenced_symbol = @s1.$root::@s10.dpi_export_other::@s11.dpi_export_other::@s12.exported::@s13.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
          obelisk.sv.symbol.formal_argument @s13.value attributes {direction = 0 : i32, hierarchical_name = "dpi_export_other.exported.value", lifetime = 1 : i32, name = "value", node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
          obelisk.sv.symbol.formal_argument @s16.copy attributes {direction = 1 : i32, hierarchical_name = "dpi_export_other.exported.copy", lifetime = 1 : i32, name = "copy", node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
          obelisk.sv.symbol.variable @s14.exported attributes {hierarchical_name = "dpi_export_other.exported.exported", is_compiler_generated, name = "exported", node_id = 16 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
      }
    }
  }
}

// CHECK: module attributes {simulation.has_dpi_exports}
// CHECK-DAG: simulation.code_unit.decl {{.*}} in 1 function hierarchy "dpi_export.exported" {{.*}}dpi_abi_signature = [{{.*}}direction = input{{.*}}direction = output{{.*}}direction = result{{.*}}direction = output{{.*}}]{{.*}}dpi_c_identifier = "exported_c"{{.*}}dpi_export_id = -2001470629 : i32{{.*}}dpi_logical_inputs = 2 : i32{{.*}}dpi_scope_id = 1 : i64
// CHECK-DAG: simulation.code_unit.decl {{.*}} in 2 function hierarchy "dpi_export_other.exported" {{.*}}dpi_abi_signature = [{{.*}}direction = input{{.*}}direction = output{{.*}}direction = result{{.*}}direction = output{{.*}}]{{.*}}dpi_c_identifier = "exported_c"{{.*}}dpi_export_id = -2001470629 : i32{{.*}}dpi_logical_inputs = 2 : i32{{.*}}dpi_scope_id = 2 : i64
// CHECK-DAG: simulation.func nested {{.*}}dpi_abi_signature = [{{.*}}direction = input{{.*}}direction = output{{.*}}direction = result{{.*}}direction = output{{.*}}]{{.*}}dpi_c_identifier = "exported_c"{{.*}}dpi_export_id = -2001470629 : i32{{.*}}dpi_logical_inputs = 2 : i32{{.*}}dpi_scope_id = 1 : i64{{.*}}simulation.hierarchical_name = "dpi_export.exported"
// CHECK-DAG: simulation.func nested {{.*}}dpi_abi_signature = [{{.*}}direction = input{{.*}}direction = output{{.*}}direction = result{{.*}}direction = output{{.*}}]{{.*}}dpi_c_identifier = "exported_c"{{.*}}dpi_export_id = -2001470629 : i32{{.*}}dpi_logical_inputs = 2 : i32{{.*}}dpi_scope_id = 2 : i64{{.*}}simulation.hierarchical_name = "dpi_export_other.exported"
// CHECK-NOT: error:
