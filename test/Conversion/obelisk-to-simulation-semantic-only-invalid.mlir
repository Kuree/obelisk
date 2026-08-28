// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' -o /dev/null 2>&1 | FileCheck %s

// IEEE 1800-2017 Clauses 17, 30, and 31 define executable checker, specify,
// and timing-check behavior. Until their roadmap chunks land, preparation must
// reject the retained semantic nodes instead of silently erasing them.

module {
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "unit"} {
      obelisk.sv.symbol.checker attributes {hierarchical_name = "check", name = "check", node_id = 3 : i64, port_count = 0 : i64, port_paths = [], port_symbols = [], sym_name = "checker"} {
      }
    }
    obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "body"} {
      obelisk.sv.symbol.checker_instance attributes {connection_actual_kinds = array<i64>, connection_attribute_counts = array<i64>, connection_count = 0 : i64, connection_formal_paths = [], connection_formal_symbols = [], connection_has_actual = array<i64>, connection_has_output_initial = array<i64>, hierarchical_name = "top.c", is_procedural = false, name = "c", node_id = 5 : i64, referenced_checker_path = "check", referenced_checker_symbol = @root::@unit::@checker, sym_name = "checker_instance"} {
      }
      obelisk.sv.symbol.specify_block attributes {hierarchical_name = "top", node_id = 6 : i64, sym_name = "specify"} {
        obelisk.sv.symbol.timing_path attributes {hierarchical_name = "top", node_id = 7 : i64, sym_name = "path"} {
        }
        obelisk.sv.symbol.pulse_style attributes {hierarchical_name = "top", node_id = 8 : i64, sym_name = "pulse"} {
        }
        obelisk.sv.symbol.system_timing_check attributes {hierarchical_name = "top", node_id = 9 : i64, sym_name = "timing_check"} {
        }
      }
    }
  }
}

// CHECK: error: IEEE 1800-2017 Clause 17 checker instances are retained in semantic IR but are not executable yet
// CHECK: error: IEEE 1800-2017 Clause 30 specify timing paths are not executable yet for this form
// CHECK: error: IEEE 1800-2017 Clause 31 system timing checks are retained in semantic IR but are not executable yet
