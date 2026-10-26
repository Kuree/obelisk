// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=full opt-level=0' --mlir-print-debuginfo -o %t
// RUN: env OBELISK_TEST_INPUT=%t %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.PortExpressions
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=full opt-level=3' --mlir-print-debuginfo -o %t.opt
// RUN: env OBELISK_TEST_INPUT=%t.opt %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.PortExpressions
!bit = !obelisk.integral<1, false, true, 0 : 0, logic>
!int = !obelisk.integral<32, true, false, 31 : 0, int>
!word = !obelisk.ranged_packed_array<7 : 0 x !bit>
!half = !obelisk.ranged_packed_array<3 : 0 x !bit>
!pair = !obelisk.ranged_packed_array<1 : 0 x !bit>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition @Top attributes {node_id = 43 : i64, definition_kind = 0 : i32, hierarchical_name = "Top", name = "Top"} {}
  obelisk.sv.symbol.definition @Child attributes {node_id = 44 : i64, definition_kind = 0 : i32, hierarchical_name = "Child", name = "Child"} {}
  obelisk.sv.symbol.root @root attributes {node_id = 42 : i64, hierarchical_name = "$root", name = "$root"} {
    obelisk.sv.symbol.instance @top attributes {node_id = 41 : i64, hierarchical_name = "top", name = "top", is_uninstantiated = false, referenced_path = "Top", referenced_symbol = @Top} {
      obelisk.sv.symbol.instance_body @body attributes {node_id = 40 : i64, hierarchical_name = "top", name = "Top"} {
        obelisk.sv.symbol.variable @source attributes {node_id = 38 : i64, hierarchical_name = "top.source", name = "source", lifetime = 1 : i32, semantic_type = !word} {}
        obelisk.sv.symbol.variable @destination attributes {node_id = 39 : i64, hierarchical_name = "top.destination", name = "destination", lifetime = 1 : i32, semantic_type = !word} {}
        obelisk.sv.symbol.instance @child attributes {node_id = 37 : i64, hierarchical_name = "top.child", name = "child", is_uninstantiated = false, referenced_path = "Child", referenced_symbol = @Child} {
          obelisk.sv.port.connection attributes {node_id = 5 : i64, actual_is_constant = false, direction = 0 : i32, formal_name = "first", formal_ordinal = 0 : i64, formal_path = "top.child.first", formal_symbol = @root::@top::@body::@child::@body::@first, formal_type = !half, internal_path = "top.child.inside_first", internal_symbol = @root::@top::@body::@child::@body::@inside_first, is_ansi = true, is_net = false, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.range_select attributes {node_id = 4 : i64, selection_kind = 0 : i32, semantic_type = !half, is_signed = false} {
              obelisk.sv.expression.named_value attributes {node_id = 1 : i64, referenced_path = "top.source", referenced_symbol = @root::@top::@body::@source, semantic_type = !word} {}
              obelisk.sv.expression.integer_literal attributes {node_id = 2 : i64, constant_value = "7", semantic_type = !int, is_signed = true} {}
              obelisk.sv.expression.integer_literal attributes {node_id = 3 : i64, constant_value = "4", semantic_type = !int, is_signed = true} {}
            }
          }
          obelisk.sv.port.connection attributes {node_id = 10 : i64, actual_is_constant = false, direction = 0 : i32, formal_name = "again", formal_ordinal = 1 : i64, formal_path = "top.child.again", formal_symbol = @root::@top::@body::@child::@body::@again, formal_type = !half, internal_path = "top.child.inside_again", internal_symbol = @root::@top::@body::@child::@body::@inside_again, is_ansi = true, is_net = false, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.range_select attributes {node_id = 9 : i64, selection_kind = 0 : i32, semantic_type = !half, is_signed = false} {
              obelisk.sv.expression.named_value attributes {node_id = 6 : i64, referenced_path = "top.source", referenced_symbol = @root::@top::@body::@source, semantic_type = !word} {}
              obelisk.sv.expression.integer_literal attributes {node_id = 7 : i64, constant_value = "7", semantic_type = !int, is_signed = true} {}
              obelisk.sv.expression.integer_literal attributes {node_id = 8 : i64, constant_value = "4", semantic_type = !int, is_signed = true} {}
            }
          }
          obelisk.sv.port.connection attributes {node_id = 20 : i64, actual_is_constant = false, direction = 0 : i32, formal_name = "concat", formal_ordinal = 2 : i64, formal_path = "top.child.concat", formal_symbol = @root::@top::@body::@child::@body::@concat, formal_type = !half, internal_path = "top.child.inside_concat", internal_symbol = @root::@top::@body::@child::@body::@inside_concat, is_ansi = true, is_net = false, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.concatenation attributes {node_id = 19 : i64, semantic_type = !half, is_signed = false} {
              obelisk.sv.expression.range_select attributes {node_id = 14 : i64, selection_kind = 0 : i32, semantic_type = !pair, is_signed = false} {
                obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "top.source", referenced_symbol = @root::@top::@body::@source, semantic_type = !word} {}
                obelisk.sv.expression.integer_literal attributes {node_id = 12 : i64, constant_value = "7", semantic_type = !int, is_signed = true} {}
                obelisk.sv.expression.integer_literal attributes {node_id = 13 : i64, constant_value = "6", semantic_type = !int, is_signed = true} {}
              }
              obelisk.sv.expression.range_select attributes {node_id = 18 : i64, selection_kind = 0 : i32, semantic_type = !pair, is_signed = false} {
                obelisk.sv.expression.named_value attributes {node_id = 15 : i64, referenced_path = "top.source", referenced_symbol = @root::@top::@body::@source, semantic_type = !word} {}
                obelisk.sv.expression.integer_literal attributes {node_id = 16 : i64, constant_value = "1", semantic_type = !int, is_signed = true} {}
                obelisk.sv.expression.integer_literal attributes {node_id = 17 : i64, constant_value = "0", semantic_type = !int, is_signed = true} {}
              }
            }
          }
          obelisk.sv.port.connection attributes {node_id = 27 : i64, actual_is_constant = false, direction = 1 : i32, formal_name = "output", formal_ordinal = 3 : i64, formal_path = "top.child.output", formal_symbol = @root::@top::@body::@child::@body::@output, formal_type = !half, internal_path = "top.child.inside_output", internal_symbol = @root::@top::@body::@child::@body::@inside_output, is_ansi = true, is_net = false, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.assignment attributes {node_id = 26 : i64, assignment_kind = 0 : i32, semantic_type = !half} {
              obelisk.sv.expression.range_select attributes {node_id = 24 : i64, selection_kind = 0 : i32, semantic_type = !half, is_signed = false} {
                obelisk.sv.expression.named_value attributes {node_id = 21 : i64, referenced_path = "top.destination", referenced_symbol = @root::@top::@body::@destination, semantic_type = !word} {}
                obelisk.sv.expression.integer_literal attributes {node_id = 22 : i64, constant_value = "3", semantic_type = !int, is_signed = true} {}
                obelisk.sv.expression.integer_literal attributes {node_id = 23 : i64, constant_value = "0", semantic_type = !int, is_signed = true} {}
              }
              obelisk.sv.expression.empty_argument attributes {node_id = 25 : i64, semantic_type = !half} {}
            }
          }
          obelisk.sv.port.connection attributes {node_id = 45 : i64, actual_is_constant = false, direction = 0 : i32, formal_name = "bit", formal_ordinal = 4 : i64, formal_path = "top.child.bit", formal_symbol = @root::@top::@body::@child::@body::@bit, formal_type = !bit, internal_path = "top.child.inside_bit", internal_symbol = @root::@top::@body::@child::@body::@inside_bit, is_ansi = true, is_net = false, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.element_select attributes {node_id = 46 : i64, semantic_type = !bit, is_signed = false} {
              obelisk.sv.expression.named_value attributes {node_id = 47 : i64, referenced_path = "top.source", referenced_symbol = @root::@top::@body::@source, semantic_type = !word} {}
              obelisk.sv.expression.integer_literal attributes {node_id = 48 : i64, constant_value = "7", semantic_type = !int, is_signed = true} {}
            }
          }
          obelisk.sv.symbol.instance_body @body attributes {node_id = 36 : i64, hierarchical_name = "top.child", name = "Child"} {
            obelisk.sv.symbol.port @first attributes {node_id = 28 : i64, direction = 0 : i32, hierarchical_name = "top.child.first", name = "first", semantic_type = !half} {}
            obelisk.sv.symbol.variable @inside_first attributes {node_id = 29 : i64, hierarchical_name = "top.child.inside_first", name = "inside_first", lifetime = 1 : i32, semantic_type = !half} {}
            obelisk.sv.symbol.port @again attributes {node_id = 30 : i64, direction = 0 : i32, hierarchical_name = "top.child.again", name = "again", semantic_type = !half} {}
            obelisk.sv.symbol.variable @inside_again attributes {node_id = 31 : i64, hierarchical_name = "top.child.inside_again", name = "inside_again", lifetime = 1 : i32, semantic_type = !half} {}
            obelisk.sv.symbol.port @concat attributes {node_id = 32 : i64, direction = 0 : i32, hierarchical_name = "top.child.concat", name = "concat", semantic_type = !half} {}
            obelisk.sv.symbol.variable @inside_concat attributes {node_id = 33 : i64, hierarchical_name = "top.child.inside_concat", name = "inside_concat", lifetime = 1 : i32, semantic_type = !half} {}
            obelisk.sv.symbol.port @output attributes {node_id = 34 : i64, direction = 1 : i32, hierarchical_name = "top.child.output", name = "output", semantic_type = !half} {}
            obelisk.sv.symbol.variable @inside_output attributes {node_id = 35 : i64, hierarchical_name = "top.child.inside_output", name = "inside_output", lifetime = 1 : i32, semantic_type = !half} {}
            obelisk.sv.symbol.port @bit attributes {node_id = 49 : i64, direction = 0 : i32, hierarchical_name = "top.child.bit", name = "bit", semantic_type = !bit} {}
            obelisk.sv.symbol.variable @inside_bit attributes {node_id = 50 : i64, hierarchical_name = "top.child.inside_bit", name = "inside_bit", lifetime = 1 : i32, semantic_type = !bit} {}
          }
        }
      }
    }
  }
}
