// RUN: talos-opt %s '--lower-talos-to-sim=vpi=full opt-level=0' -o %t
// RUN: env TALOS_TEST_INPUT=%t %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.WholePortEndpoints
// RUN: talos-opt %s '--lower-talos-to-sim=vpi=full opt-level=3' -o %t.opt
// RUN: env TALOS_TEST_INPUT=%t.opt %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.WholePortEndpoints
!word = !talos.integral<8, false, true, 7 : 0, logic>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  talos.sv.symbol.definition @Top attributes {node_id = 1 : i64, definition_kind = 0 : i32, hierarchical_name = "Top", name = "Top"} {}
  talos.sv.symbol.definition @Child attributes {node_id = 2 : i64, definition_kind = 0 : i32, hierarchical_name = "Child", name = "Child"} {}
  talos.sv.symbol.root @root attributes {node_id = 3 : i64, hierarchical_name = "$root", name = "$root"} {
    talos.sv.symbol.package @pkg attributes {node_id = 38 : i64, hierarchical_name = "pkg", name = "pkg"} {
      talos.sv.symbol.parameter @constant attributes {node_id = 33 : i64, hierarchical_name = "pkg::constant", name = "constant", constant_value = "8'h5a", semantic_type = !word} {}
    }
    talos.sv.symbol.compilation_unit @cu attributes {node_id = 4 : i64, hierarchical_name = "$unit"} {}
    talos.sv.symbol.instance @top attributes {node_id = 5 : i64, hierarchical_name = "top", is_uninstantiated = false, name = "top", referenced_path = "Top", referenced_symbol = @Top} {
      talos.sv.symbol.instance_body @body attributes {node_id = 6 : i64, hierarchical_name = "top", name = "Top"} {
        talos.sv.symbol.variable @source attributes {node_id = 7 : i64, hierarchical_name = "top.source", name = "source", lifetime = 1 : i32, semantic_type = !word} {}
        talos.sv.symbol.variable @destination attributes {node_id = 8 : i64, hierarchical_name = "top.destination", name = "destination", lifetime = 1 : i32, semantic_type = !word} {}
        talos.sv.symbol.variable @mirror attributes {node_id = 28 : i64, hierarchical_name = "top.mirror", name = "mirror", lifetime = 1 : i32, semantic_type = !word} {}
        talos.sv.symbol.continuous_assign @mirror_assign attributes {node_id = 29 : i64, hierarchical_name = "top", time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
          talos.sv.expression.assignment attributes {node_id = 30 : i64, assignment_kind = 0 : i32, semantic_type = !word} {
            talos.sv.expression.named_value attributes {node_id = 31 : i64, referenced_path = "top.mirror", referenced_symbol = @root::@top::@body::@mirror, semantic_type = !word} {}
            talos.sv.expression.named_value attributes {node_id = 32 : i64, referenced_path = "top.source", referenced_symbol = @root::@top::@body::@source, semantic_type = !word} {}
          }
        }
        talos.sv.symbol.instance @child attributes {node_id = 9 : i64, hierarchical_name = "top.child", is_uninstantiated = false, name = "child", referenced_path = "Child", referenced_symbol = @Child} {
          talos.sv.port.connection attributes {node_id = 10 : i64, actual_is_constant = false, direction = 0 : i32, formal_name = "input", formal_ordinal = 0 : i64, formal_path = "top.child.input", formal_symbol = @root::@top::@body::@child::@body::@input, formal_type = !word, internal_path = "top.child.inside_input", internal_symbol = @root::@top::@body::@child::@body::@inside_input, is_ansi = true, is_net = false, provenance = 0 : i32} {
          } {
            talos.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "top.source", referenced_symbol = @root::@top::@body::@source, semantic_type = !word} {}
          }
          talos.sv.port.connection attributes {node_id = 12 : i64, actual_is_constant = false, direction = 0 : i32, formal_name = "again", formal_ordinal = 1 : i64, formal_path = "top.child.again", formal_symbol = @root::@top::@body::@child::@body::@again, formal_type = !word, internal_path = "top.child.inside_again", internal_symbol = @root::@top::@body::@child::@body::@inside_again, is_ansi = true, is_net = false, provenance = 0 : i32} {
          } {
            talos.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "top.source", referenced_symbol = @root::@top::@body::@source, semantic_type = !word} {}
          }
          talos.sv.port.connection attributes {node_id = 14 : i64, actual_is_constant = false, direction = 1 : i32, formal_name = "output", formal_ordinal = 2 : i64, formal_path = "top.child.output", formal_symbol = @root::@top::@body::@child::@body::@output, formal_type = !word, internal_path = "top.child.inside_output", internal_symbol = @root::@top::@body::@child::@body::@inside_output, is_ansi = true, is_net = false, provenance = 0 : i32} {
          } {
            talos.sv.expression.assignment attributes {node_id = 15 : i64, assignment_kind = 0 : i32, semantic_type = !word} {
              talos.sv.expression.named_value attributes {node_id = 16 : i64, referenced_path = "top.destination", referenced_symbol = @root::@top::@body::@destination, semantic_type = !word} {}
              talos.sv.expression.empty_argument attributes {node_id = 17 : i64, semantic_type = !word} {}
            }
          }
          talos.sv.port.connection attributes {node_id = 18 : i64, actual_is_constant = false, direction = 0 : i32, formal_name = "open", formal_ordinal = 3 : i64, formal_path = "top.child.open", formal_symbol = @root::@top::@body::@child::@body::@open, formal_type = !word, internal_path = "top.child.inside_open", internal_symbol = @root::@top::@body::@child::@body::@inside_open, is_ansi = true, is_net = false, provenance = 0 : i32} {
          } {
          }
          talos.sv.port.connection attributes {node_id = 34 : i64, actual_is_constant = true, direction = 0 : i32, formal_name = "parameter", formal_ordinal = 4 : i64, formal_path = "top.child.parameter", formal_symbol = @root::@top::@body::@child::@body::@parameter, formal_type = !word, internal_path = "top.child.inside_parameter", internal_symbol = @root::@top::@body::@child::@body::@inside_parameter, is_ansi = true, is_net = false, provenance = 0 : i32} {
          } {
            talos.sv.expression.named_value attributes {node_id = 35 : i64, referenced_path = "pkg::constant", referenced_symbol = @root::@pkg::@constant, constant_value = "8'h5a", semantic_type = !word} {}
          }
          talos.sv.symbol.instance_body @body attributes {node_id = 19 : i64, hierarchical_name = "top.child", name = "Child"} {
            talos.sv.symbol.port @parameter attributes {node_id = 36 : i64, direction = 0 : i32, hierarchical_name = "top.child.parameter", name = "parameter", semantic_type = !word} {}
            talos.sv.symbol.variable @inside_parameter attributes {node_id = 37 : i64, hierarchical_name = "top.child.inside_parameter", name = "inside_parameter", lifetime = 1 : i32, semantic_type = !word} {}
            talos.sv.symbol.port @input attributes {node_id = 20 : i64, direction = 0 : i32, hierarchical_name = "top.child.input", name = "input", semantic_type = !word} {}
            talos.sv.symbol.variable @inside_input attributes {node_id = 21 : i64, hierarchical_name = "top.child.inside_input", name = "inside_input", lifetime = 1 : i32, semantic_type = !word} {}
            talos.sv.symbol.port @again attributes {node_id = 22 : i64, direction = 0 : i32, hierarchical_name = "top.child.again", name = "again", semantic_type = !word} {}
            talos.sv.symbol.variable @inside_again attributes {node_id = 23 : i64, hierarchical_name = "top.child.inside_again", name = "inside_again", lifetime = 1 : i32, semantic_type = !word} {}
            talos.sv.symbol.port @output attributes {node_id = 24 : i64, direction = 1 : i32, hierarchical_name = "top.child.output", name = "output", semantic_type = !word} {}
            talos.sv.symbol.variable @inside_output attributes {node_id = 25 : i64, hierarchical_name = "top.child.inside_output", name = "inside_output", lifetime = 1 : i32, semantic_type = !word} {}
            talos.sv.symbol.port @open attributes {node_id = 26 : i64, direction = 0 : i32, hierarchical_name = "top.child.open", name = "open", semantic_type = !word} {}
            talos.sv.symbol.variable @inside_open attributes {node_id = 27 : i64, hierarchical_name = "top.child.inside_open", name = "inside_open", lifetime = 1 : i32, semantic_type = !word} {}
          }
        }
      }
    }
  }
}
