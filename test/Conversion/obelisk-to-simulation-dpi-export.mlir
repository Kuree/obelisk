// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "dpi_export", name = "dpi_export", node_id = 0 : i64, sym_name = "s0.dpi_export"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "dpi_export", is_uninstantiated = false, name = "dpi_export", node_id = 3 : i64, referenced_path = "dpi_export", referenced_symbol = @s0.dpi_export, sym_name = "s3.dpi_export"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "dpi_export", name = "dpi_export", node_id = 4 : i64, sym_name = "s4.dpi_export"} {
        obelisk.sv.symbol.subroutine attributes {default_lifetime = 1 : i32, dpi_export_c_identifier = "exported_c", hierarchical_name = "dpi_export.exported", name = "exported", node_id = 5 : i64, return_variable_path = "dpi_export.exported.exported", return_variable_symbol = @s1.$root::@s3.dpi_export::@s4.dpi_export::@s5.exported::@s7.exported, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s5.exported", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.return attributes {node_id = 6 : i64} {
            obelisk.sv.expression.named_value attributes {node_id = 7 : i64, referenced_path = "dpi_export.exported.value", referenced_symbol = @s1.$root::@s3.dpi_export::@s4.dpi_export::@s5.exported::@s6.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "dpi_export.exported.value", lifetime = 1 : i32, name = "value", node_id = 8 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s6.value"} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "dpi_export.exported.exported", is_compiler_generated, name = "exported", node_id = 9 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s7.exported"} {
          }
        }
      }
    }
  }
}

// CHECK: DPI export 'exported_c' is not supported by simulation lowering
