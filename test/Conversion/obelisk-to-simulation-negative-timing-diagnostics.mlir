// RUN: %split-file %s %t
// RUN: not obelisk-opt %t/invalid-window.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   2>&1 | FileCheck %s --check-prefix=WINDOW
// RUN: not obelisk-opt %t/optional-role.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   2>&1 | FileCheck %s --check-prefix=ROLE
// RUN: not obelisk-opt %t/explicit-delayed.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   2>&1 | FileCheck %s --check-prefix=DELAYED
// RUN: not obelisk-opt %t/cross-kind.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   2>&1 | FileCheck %s --check-prefix=CROSS
// RUN: not obelisk-opt %t/clause30.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   2>&1 | FileCheck %s --check-prefix=PATH
// RUN: not obelisk-opt %t/view-terminal.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   2>&1 | FileCheck %s --check-prefix=VIEW
// RUN: not obelisk-opt %t/overflow.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   2>&1 | FileCheck %s --check-prefix=OVERFLOW

// IEEE 1800-2017 31.9 requires an open negative window wider than one design
// precision tick, and 31.9.2's explicit delayed outputs and role conditions
// are deliberately outside the implicit-delayed-signal tranche.
// WINDOW: error: IEEE 1800-2017 31.9 requires the two negative timing-check limits to sum to more than one simulation precision unit
// ROLE: error: IEEE 1800-2017 31.9.2 timestamp/timecheck conditions and explicit delayed_reference/delayed_data are not executable in the implicit delayed-signal tranche
// DELAYED: error: IEEE 1800-2017 31.9.2 timestamp/timecheck conditions and explicit delayed_reference/delayed_data are not executable in the implicit delayed-signal tranche
// CROSS: error: IEEE 1800-2017 31.9.1 delayed terminal 'top.r' also participates in another timing-check kind; that cross-kind component is not executable yet
// PATH: error: IEEE 1800-2017 31.9.1 delayed terminal 'top.r' also sources a Clause 30 propagation path; path-delay flooring for that component is not executable yet
// VIEW: error: negative timing-check event must name one whole direct packed storage or net terminal
// OVERFLOW: error: negative timing-check lower delay bound exceeds the signed tick range

//--- invalid-window.mlir
module {
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.system_timing_check attributes {
        hierarchical_name = "top", node_id = 2 : i64, sym_name = "bad",
        obelisk.invalid_negative_timing_window,
        obelisk.negative_timing_check} {}
  }
}

//--- optional-role.mlir
module {
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.system_timing_check attributes {
        hierarchical_name = "top", node_id = 2 : i64, sym_name = "explicit",
        obelisk.negative_timing_check, timing_check_arg_count = 4 : i64,
        timing_check_arg_has_condition = array<i64: 1, 0, 0, 0>} {}
  }
}

//--- explicit-delayed.mlir
module {
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.system_timing_check attributes {
        hierarchical_name = "top", node_id = 2 : i64, sym_name = "explicit",
        obelisk.negative_timing_check, timing_check_arg_count = 7 : i64,
        timing_check_arg_has_expression = array<i64: 1, 1, 1, 1, 0, 1, 1>} {}
  }
}

//--- cross-kind.mlir
!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>
!int = !obelisk.integral<32, true, false, 31 : 0, int>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 2 : i64, sym_name = "body", time_precision_fs = 1000 : i64, time_unit_fs = 1000 : i64} {
      obelisk.sv.symbol.variable attributes {hierarchical_name = "top.r", lifetime = 1 : i32, name = "r", node_id = 3 : i64, semantic_type = !logic1, sym_name = "r"} {}
      obelisk.sv.symbol.variable attributes {hierarchical_name = "top.d", lifetime = 1 : i32, name = "d", node_id = 4 : i64, semantic_type = !logic1, sym_name = "d"} {}
      obelisk.sv.symbol.specify_block attributes {hierarchical_name = "top", node_id = 5 : i64, sym_name = "specify"} {
        obelisk.sv.symbol.system_timing_check attributes {hierarchical_name = "top", node_id = 10 : i64, sym_name = "negative", obelisk.basic_timing_check, obelisk.negative_timing_check, time_unit_fs = 1000 : i64, time_precision_fs = 1000 : i64, timing_check_kind = 3 : i32, timing_check_arg_count = 4 : i64, timing_check_arg_expression_children = array<i64: 0, 1, 2, 3>, timing_check_arg_condition_children = array<i64: -1, -1, -1, -1>, timing_check_arg_effective_edges = array<i32: 1, 0, 0, 0>, timing_check_arg_is_time = array<i64: 0, 0, 1, 1>, timing_check_arg_time_fs = array<i64: 0, 0, -2000, 4000>} {
          obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "top.r", referenced_symbol = @root::@body::@r, semantic_type = !logic1} {}
          obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "top.d", referenced_symbol = @root::@body::@d, semantic_type = !logic1} {}
          obelisk.sv.expression.integer_literal attributes {node_id = 13 : i64, constant_value = "-2", semantic_type = !int} {}
          obelisk.sv.expression.integer_literal attributes {node_id = 14 : i64, constant_value = "4", semantic_type = !int} {}
        }
        obelisk.sv.symbol.system_timing_check attributes {hierarchical_name = "top", node_id = 20 : i64, sym_name = "other", obelisk.basic_timing_check, time_unit_fs = 1000 : i64, time_precision_fs = 1000 : i64, timing_check_kind = 0 : i32, timing_check_arg_count = 3 : i64, timing_check_arg_expression_children = array<i64: 0, 1, 2>, timing_check_arg_condition_children = array<i64: -1, -1, -1>, timing_check_arg_effective_edges = array<i32: 1, 0, 0>, timing_check_arg_is_time = array<i64: 0, 0, 1>, timing_check_arg_time_fs = array<i64: 0, 0, 1000>} {
          obelisk.sv.expression.named_value attributes {node_id = 21 : i64, referenced_path = "top.r", referenced_symbol = @root::@body::@r, semantic_type = !logic1} {}
          obelisk.sv.expression.named_value attributes {node_id = 22 : i64, referenced_path = "top.d", referenced_symbol = @root::@body::@d, semantic_type = !logic1} {}
          obelisk.sv.expression.integer_literal attributes {node_id = 23 : i64, constant_value = "1", semantic_type = !int} {}
        }
      }
    }
  }
}

//--- clause30.mlir
!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>
!int = !obelisk.integral<32, true, false, 31 : 0, int>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 2 : i64, sym_name = "body", time_precision_fs = 1000 : i64, time_unit_fs = 1000 : i64} {
      obelisk.sv.symbol.variable attributes {hierarchical_name = "top.r", lifetime = 1 : i32, name = "r", node_id = 3 : i64, semantic_type = !logic1, sym_name = "r"} {}
      obelisk.sv.symbol.variable attributes {hierarchical_name = "top.d", lifetime = 1 : i32, name = "d", node_id = 4 : i64, semantic_type = !logic1, sym_name = "d"} {}
      obelisk.sv.symbol.variable attributes {hierarchical_name = "top.out", lifetime = 1 : i32, name = "out", node_id = 5 : i64, semantic_type = !logic1, sym_name = "out"} {}
      obelisk.sv.symbol.specify_block attributes {hierarchical_name = "top", node_id = 6 : i64, sym_name = "specify"} {
        obelisk.sv.symbol.system_timing_check attributes {hierarchical_name = "top", node_id = 10 : i64, sym_name = "negative", obelisk.basic_timing_check, obelisk.negative_timing_check, time_unit_fs = 1000 : i64, time_precision_fs = 1000 : i64, timing_check_kind = 3 : i32, timing_check_arg_count = 4 : i64, timing_check_arg_expression_children = array<i64: 0, 1, 2, 3>, timing_check_arg_condition_children = array<i64: -1, -1, -1, -1>, timing_check_arg_effective_edges = array<i32: 1, 0, 0, 0>, timing_check_arg_is_time = array<i64: 0, 0, 1, 1>, timing_check_arg_time_fs = array<i64: 0, 0, -2000, 4000>} {
          obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "top.r", referenced_symbol = @root::@body::@r, semantic_type = !logic1} {}
          obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "top.d", referenced_symbol = @root::@body::@d, semantic_type = !logic1} {}
          obelisk.sv.expression.integer_literal attributes {node_id = 13 : i64, constant_value = "-2", semantic_type = !int} {}
          obelisk.sv.expression.integer_literal attributes {node_id = 14 : i64, constant_value = "4", semantic_type = !int} {}
        }
        obelisk.sv.symbol.timing_path attributes {hierarchical_name = "top", node_id = 20 : i64, obelisk.simple_timing_path, sym_name = "path", timing_connection_full = false, timing_delay_fs = array<i64: 1000>, timing_input_terminals = [{low = 0 : i64, path = "top.r", root_width = 1 : i64, width = 1 : i64}], timing_output_terminal = {low = 0 : i64, path = "top.out", root_width = 1 : i64, width = 1 : i64}, timing_polarity = 0 : i32} {}
      }
    }
  }
}

//--- view-terminal.mlir
!logic1 = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
!logic2 = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
!int = !obelisk.integral<32, true, false, 31 : 0, int>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "child", name = "child", node_id = 0 : i64, sym_name = "child_def"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 1 : i64, sym_name = "top_def"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64, sym_name = "root"} {
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @top_def, sym_name = "top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "top_body", time_precision_fs = 1000 : i64, time_unit_fs = 1000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.bus", lifetime = 1 : i32, name = "bus", node_id = 5 : i64, semantic_type = !logic2, sym_name = "bus"} {}
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.c", is_uninstantiated = false, name = "c", node_id = 6 : i64, referenced_path = "child", referenced_symbol = @child_def, sym_name = "child"} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 3 : i32, formal_name = "slice", formal_ordinal = 0 : i64, formal_path = "top.c.slice", formal_symbol = @root::@top::@top_body::@child::@child_body::@slice_port, formal_type = !logic1, internal_path = "top.c.slice", internal_symbol = @root::@top::@top_body::@child::@child_body::@slice, is_ansi = true, is_net = false, node_id = 7 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.range_select attributes {node_id = 8 : i64, selection_kind = 0 : i32, semantic_type = !logic1} {
              obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "top.bus", referenced_symbol = @root::@top::@top_body::@bus, semantic_type = !logic2} {}
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 10 : i64, semantic_type = !int} {}
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 11 : i64, semantic_type = !int} {}
            }
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.c", name = "child", node_id = 12 : i64, sym_name = "child_body", time_precision_fs = 1000 : i64, time_unit_fs = 1000 : i64} {
            obelisk.sv.symbol.port attributes {direction = 3 : i32, hierarchical_name = "top.c.slice", name = "slice", node_id = 13 : i64, semantic_type = !logic1, sym_name = "slice_port"} {}
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.c.slice", lifetime = 1 : i32, name = "slice", node_id = 14 : i64, semantic_type = !logic1, sym_name = "slice"} {}
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.c.data", lifetime = 1 : i32, name = "data", node_id = 15 : i64, semantic_type = !logic1, sym_name = "data"} {}
            obelisk.sv.symbol.specify_block attributes {hierarchical_name = "top.c", node_id = 16 : i64, sym_name = "specify"} {
              obelisk.sv.symbol.system_timing_check attributes {hierarchical_name = "top.c", node_id = 20 : i64, sym_name = "negative", obelisk.basic_timing_check, obelisk.negative_timing_check, time_unit_fs = 1000 : i64, time_precision_fs = 1000 : i64, timing_check_kind = 3 : i32, timing_check_arg_count = 4 : i64, timing_check_arg_expression_children = array<i64: 0, 1, 2, 3>, timing_check_arg_condition_children = array<i64: -1, -1, -1, -1>, timing_check_arg_effective_edges = array<i32: 1, 0, 0, 0>, timing_check_arg_is_time = array<i64: 0, 0, 1, 1>, timing_check_arg_time_fs = array<i64: 0, 0, -2000, 4000>} {
                obelisk.sv.expression.named_value attributes {node_id = 21 : i64, referenced_path = "top.c.slice", referenced_symbol = @root::@top::@top_body::@child::@child_body::@slice, semantic_type = !logic1} {}
                obelisk.sv.expression.named_value attributes {node_id = 22 : i64, referenced_path = "top.c.data", referenced_symbol = @root::@top::@top_body::@child::@child_body::@data, semantic_type = !logic1} {}
                obelisk.sv.expression.integer_literal attributes {node_id = 23 : i64, constant_value = "-2", semantic_type = !int} {}
                obelisk.sv.expression.integer_literal attributes {node_id = 24 : i64, constant_value = "4", semantic_type = !int} {}
              }
            }
          }
        }
      }
    }
  }
}

//--- overflow.mlir
!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>
!int = !obelisk.integral<32, true, false, 31 : 0, int>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 2 : i64, sym_name = "body", time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
      obelisk.sv.symbol.variable attributes {hierarchical_name = "top.r", lifetime = 1 : i32, name = "r", node_id = 3 : i64, semantic_type = !logic1, sym_name = "r"} {}
      obelisk.sv.symbol.variable attributes {hierarchical_name = "top.d", lifetime = 1 : i32, name = "d", node_id = 4 : i64, semantic_type = !logic1, sym_name = "d"} {}
      obelisk.sv.symbol.specify_block attributes {hierarchical_name = "top", node_id = 5 : i64, sym_name = "specify"} {
        // Direct semantic IR exercises the checked i128-to-i64 boundary.  A
        // normal frontend producer rejects this window even earlier.
        obelisk.sv.symbol.system_timing_check attributes {hierarchical_name = "top", node_id = 10 : i64, sym_name = "overflow", obelisk.basic_timing_check, obelisk.negative_timing_check, time_unit_fs = 1 : i64, time_precision_fs = 1 : i64, timing_check_kind = 3 : i32, timing_check_arg_count = 4 : i64, timing_check_arg_expression_children = array<i64: 0, 1, 2, 3>, timing_check_arg_condition_children = array<i64: -1, -1, -1, -1>, timing_check_arg_effective_edges = array<i32: 1, 0, 0, 0>, timing_check_arg_is_time = array<i64: 0, 0, 1, 1>, timing_check_arg_time_fs = array<i64: 0, 0, -9223372036854775808, 9223372036854775807>} {
          obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "top.r", referenced_symbol = @root::@body::@r, semantic_type = !logic1} {}
          obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "top.d", referenced_symbol = @root::@body::@d, semantic_type = !logic1} {}
          obelisk.sv.expression.integer_literal attributes {node_id = 13 : i64, constant_value = "-9223372036854775808", semantic_type = !int} {}
          obelisk.sv.expression.integer_literal attributes {node_id = 14 : i64, constant_value = "9223372036854775807", semantic_type = !int} {}
        }
      }
    }
  }
}
