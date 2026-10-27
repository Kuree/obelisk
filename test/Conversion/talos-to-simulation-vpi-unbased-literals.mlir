// RUN: talos-opt %s '--lower-talos-to-sim=vpi=read opt-level=0' --mlir-print-debuginfo -o %t
// RUN: env TALOS_TEST_INPUT=%t %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.UnbasedLiterals
// RUN: talos-opt %s '--lower-talos-to-sim=vpi=full opt-level=3' --mlir-print-debuginfo -o %t.opt
// RUN: env TALOS_TEST_INPUT=%t.opt %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.UnbasedLiterals
// RUN: talos-opt %t.opt --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null
// Import provides the contextual width/value for unbased-unsized literals.
!u1 = !talos.integral<1, false, false, 0 : 0, bit>
!l1 = !talos.integral<1, false, true, 0 : 0, logic>
!u7 = !talos.ranged_packed_array<6 : 0 x !u1>
!u8 = !talos.ranged_packed_array<7 : 0 x !u1>
!l8 = !talos.ranged_packed_array<7 : 0 x !l1>
!l130 = !talos.ranged_packed_array<129 : 0 x !l1>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  talos.sv.symbol.definition @source attributes {node_id = 71 : i64, name = "source", hierarchical_name = "source", definition_kind = 0 : i32} {}
  talos.sv.symbol.root @root attributes {node_id = 70 : i64, name = "$root", hierarchical_name = "$root"} {
    talos.sv.symbol.instance @top attributes {node_id = 69 : i64, name = "top", hierarchical_name = "top", referenced_symbol = @source, referenced_path = "source", is_uninstantiated = false} {
      talos.sv.symbol.instance_body @body attributes {node_id = 68 : i64, name = "source", hierarchical_name = "top"} {
        talos.sv.symbol.variable @a attributes {node_id = 1 : i64, name = "a", hierarchical_name = "top.a", lifetime = 1 : i32, semantic_type = !l8} {} loc("unbased_literals.sv":1:1)
        talos.sv.symbol.variable @ones attributes {node_id = 2 : i64, name = "ones", hierarchical_name = "top.ones", lifetime = 1 : i32, semantic_type = !l8} {} loc("unbased_literals.sv":10:1)
        talos.sv.symbol.procedural_block @init_ones attributes {node_id = 7 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          talos.sv.statement.expression_statement attributes {node_id = 6 : i64} {
            talos.sv.expression.assignment attributes {node_id = 5 : i64, semantic_type = !l8, assignment_kind = 0 : i32} {
              talos.sv.expression.named_value attributes {node_id = 4 : i64, semantic_type = !l8, referenced_symbol = @root::@top::@body::@ones, referenced_path = "top.ones"} {} loc("unbased_literals.sv":10:1)
              talos.sv.expression.unbased_unsized_integer_literal attributes {node_id = 3 : i64, semantic_type = !l8, constant_value = "8'b11111111"} {} loc("unbased_literals.sv":10:1)
            } loc("unbased_literals.sv":10:1)
          } loc("unbased_literals.sv":10:1)
        } loc("unbased_literals.sv":10:1)
        talos.sv.symbol.variable @zeros attributes {node_id = 8 : i64, name = "zeros", hierarchical_name = "top.zeros", lifetime = 1 : i32, semantic_type = !l8} {} loc("unbased_literals.sv":20:1)
        talos.sv.symbol.procedural_block @init_zeros attributes {node_id = 13 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          talos.sv.statement.expression_statement attributes {node_id = 12 : i64} {
            talos.sv.expression.assignment attributes {node_id = 11 : i64, semantic_type = !l8, assignment_kind = 0 : i32} {
              talos.sv.expression.named_value attributes {node_id = 10 : i64, semantic_type = !l8, referenced_symbol = @root::@top::@body::@zeros, referenced_path = "top.zeros"} {} loc("unbased_literals.sv":20:1)
              talos.sv.expression.unbased_unsized_integer_literal attributes {node_id = 9 : i64, semantic_type = !l8, constant_value = "8'b00000000"} {} loc("unbased_literals.sv":20:1)
            } loc("unbased_literals.sv":20:1)
          } loc("unbased_literals.sv":20:1)
        } loc("unbased_literals.sv":20:1)
        talos.sv.symbol.variable @xs attributes {node_id = 14 : i64, name = "xs", hierarchical_name = "top.xs", lifetime = 1 : i32, semantic_type = !l8} {} loc("unbased_literals.sv":30:1)
        talos.sv.symbol.procedural_block @init_xs attributes {node_id = 19 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          talos.sv.statement.expression_statement attributes {node_id = 18 : i64} {
            talos.sv.expression.assignment attributes {node_id = 17 : i64, semantic_type = !l8, assignment_kind = 0 : i32} {
              talos.sv.expression.named_value attributes {node_id = 16 : i64, semantic_type = !l8, referenced_symbol = @root::@top::@body::@xs, referenced_path = "top.xs"} {} loc("unbased_literals.sv":30:1)
              talos.sv.expression.unbased_unsized_integer_literal attributes {node_id = 15 : i64, semantic_type = !l8, constant_value = "8'bxxxxxxxx"} {} loc("unbased_literals.sv":30:1)
            } loc("unbased_literals.sv":30:1)
          } loc("unbased_literals.sv":30:1)
        } loc("unbased_literals.sv":30:1)
        talos.sv.symbol.variable @zs attributes {node_id = 20 : i64, name = "zs", hierarchical_name = "top.zs", lifetime = 1 : i32, semantic_type = !l8} {} loc("unbased_literals.sv":40:1)
        talos.sv.symbol.procedural_block @init_zs attributes {node_id = 25 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          talos.sv.statement.expression_statement attributes {node_id = 24 : i64} {
            talos.sv.expression.assignment attributes {node_id = 23 : i64, semantic_type = !l8, assignment_kind = 0 : i32} {
              talos.sv.expression.named_value attributes {node_id = 22 : i64, semantic_type = !l8, referenced_symbol = @root::@top::@body::@zs, referenced_path = "top.zs"} {} loc("unbased_literals.sv":40:1)
              talos.sv.expression.unbased_unsized_integer_literal attributes {node_id = 21 : i64, semantic_type = !l8, constant_value = "8'bzzzzzzzz"} {} loc("unbased_literals.sv":40:1)
            } loc("unbased_literals.sv":40:1)
          } loc("unbased_literals.sv":40:1)
        } loc("unbased_literals.sv":40:1)
        talos.sv.symbol.variable @wide_ones attributes {node_id = 26 : i64, name = "wide_ones", hierarchical_name = "top.wide_ones", lifetime = 1 : i32, semantic_type = !l130} {} loc("unbased_literals.sv":80:1)
        talos.sv.symbol.procedural_block @init_wide_ones attributes {node_id = 31 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          talos.sv.statement.expression_statement attributes {node_id = 30 : i64} {
            talos.sv.expression.assignment attributes {node_id = 29 : i64, semantic_type = !l130, assignment_kind = 0 : i32} {
              talos.sv.expression.named_value attributes {node_id = 28 : i64, semantic_type = !l130, referenced_symbol = @root::@top::@body::@wide_ones, referenced_path = "top.wide_ones"} {} loc("unbased_literals.sv":80:1)
              talos.sv.expression.unbased_unsized_integer_literal attributes {node_id = 27 : i64, semantic_type = !l130, constant_value = "130'b1111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111"} {} loc("unbased_literals.sv":80:1)
            } loc("unbased_literals.sv":80:1)
          } loc("unbased_literals.sv":80:1)
        } loc("unbased_literals.sv":80:1)
        talos.sv.symbol.variable @wide_zs attributes {node_id = 32 : i64, name = "wide_zs", hierarchical_name = "top.wide_zs", lifetime = 1 : i32, semantic_type = !l130} {} loc("unbased_literals.sv":90:1)
        talos.sv.symbol.procedural_block @init_wide_zs attributes {node_id = 37 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          talos.sv.statement.expression_statement attributes {node_id = 36 : i64} {
            talos.sv.expression.assignment attributes {node_id = 35 : i64, semantic_type = !l130, assignment_kind = 0 : i32} {
              talos.sv.expression.named_value attributes {node_id = 34 : i64, semantic_type = !l130, referenced_symbol = @root::@top::@body::@wide_zs, referenced_path = "top.wide_zs"} {} loc("unbased_literals.sv":90:1)
              talos.sv.expression.unbased_unsized_integer_literal attributes {node_id = 33 : i64, semantic_type = !l130, constant_value = "130'bzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz"} {} loc("unbased_literals.sv":90:1)
            } loc("unbased_literals.sv":90:1)
          } loc("unbased_literals.sv":90:1)
        } loc("unbased_literals.sv":90:1)
        talos.sv.symbol.variable @again attributes {node_id = 38 : i64, name = "again", hierarchical_name = "top.again", lifetime = 1 : i32, semantic_type = !l8} {} loc("unbased_literals.sv":100:1)
        talos.sv.symbol.procedural_block @init_again attributes {node_id = 43 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          talos.sv.statement.expression_statement attributes {node_id = 42 : i64} {
            talos.sv.expression.assignment attributes {node_id = 41 : i64, semantic_type = !l8, assignment_kind = 0 : i32} {
              talos.sv.expression.named_value attributes {node_id = 40 : i64, semantic_type = !l8, referenced_symbol = @root::@top::@body::@again, referenced_path = "top.again"} {} loc("unbased_literals.sv":100:1)
              talos.sv.expression.unbased_unsized_integer_literal attributes {node_id = 39 : i64, semantic_type = !l8, constant_value = "8'b11111111"} {} loc("unbased_literals.sv":100:1)
            } loc("unbased_literals.sv":100:1)
          } loc("unbased_literals.sv":100:1)
        } loc("unbased_literals.sv":100:1)
        talos.sv.symbol.variable @concat attributes {node_id = 47 : i64, name = "concat", hierarchical_name = "top.concat", lifetime = 1 : i32, semantic_type = !l8} {} loc("unbased_literals.sv":50:1)
        talos.sv.symbol.procedural_block @init_concat attributes {node_id = 52 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          talos.sv.statement.expression_statement attributes {node_id = 51 : i64} {
            talos.sv.expression.assignment attributes {node_id = 50 : i64, semantic_type = !l8, assignment_kind = 0 : i32} {
              talos.sv.expression.named_value attributes {node_id = 49 : i64, semantic_type = !l8, referenced_symbol = @root::@top::@body::@concat, referenced_path = "top.concat"} {} loc("unbased_literals.sv":50:1)
              talos.sv.expression.conversion attributes {node_id = 48 : i64, semantic_type = !l8, is_implicit = true, is_const_cast = false, folded_constant = "8'b10000000"} {
                talos.sv.expression.concatenation attributes {node_id = 46 : i64, semantic_type = !u8, folded_constant = "8'b10000000"} {
                  talos.sv.expression.unbased_unsized_integer_literal attributes {node_id = 44 : i64, semantic_type = !u1, constant_value = "1'b1"} {} loc("unbased_literals.sv":50:1)
                  talos.sv.expression.integer_literal attributes {node_id = 45 : i64, semantic_type = !u7, constant_value = "7'b0"} {} loc("unbased_literals.sv":50:1)
                } loc("unbased_literals.sv":50:1)
              } loc("unbased_literals.sv":50:1)
            } loc("unbased_literals.sv":50:1)
          } loc("unbased_literals.sv":50:1)
        } loc("unbased_literals.sv":50:1)
        talos.sv.symbol.variable @and_value attributes {node_id = 53 : i64, name = "and_value", hierarchical_name = "top.and_value", lifetime = 1 : i32, semantic_type = !l8} {} loc("unbased_literals.sv":60:1)
        talos.sv.symbol.procedural_block @init_and_value attributes {node_id = 60 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          talos.sv.statement.expression_statement attributes {node_id = 59 : i64} {
            talos.sv.expression.assignment attributes {node_id = 58 : i64, semantic_type = !l8, assignment_kind = 0 : i32} {
              talos.sv.expression.named_value attributes {node_id = 57 : i64, semantic_type = !l8, referenced_symbol = @root::@top::@body::@and_value, referenced_path = "top.and_value"} {} loc("unbased_literals.sv":60:1)
              talos.sv.expression.binary_op attributes {node_id = 56 : i64, semantic_type = !l8, operator_kind = 5 : i32} {
                talos.sv.expression.named_value attributes {node_id = 54 : i64, semantic_type = !l8, referenced_symbol = @root::@top::@body::@a, referenced_path = "top.a"} {} loc("unbased_literals.sv":60:1)
                talos.sv.expression.unbased_unsized_integer_literal attributes {node_id = 55 : i64, semantic_type = !l8, constant_value = "8'b11111111"} {} loc("unbased_literals.sv":60:1)
              } loc("unbased_literals.sv":60:1)
            } loc("unbased_literals.sv":60:1)
          } loc("unbased_literals.sv":60:1)
        } loc("unbased_literals.sv":60:1)
        talos.sv.symbol.variable @to_two attributes {node_id = 61 : i64, name = "to_two", hierarchical_name = "top.to_two", lifetime = 1 : i32, semantic_type = !u8} {} loc("unbased_literals.sv":70:1)
        talos.sv.symbol.procedural_block @init_to_two attributes {node_id = 67 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          talos.sv.statement.expression_statement attributes {node_id = 66 : i64} {
            talos.sv.expression.assignment attributes {node_id = 65 : i64, semantic_type = !u8, assignment_kind = 0 : i32} {
              talos.sv.expression.named_value attributes {node_id = 64 : i64, semantic_type = !u8, referenced_symbol = @root::@top::@body::@to_two, referenced_path = "top.to_two"} {} loc("unbased_literals.sv":70:1)
              talos.sv.expression.conversion attributes {node_id = 63 : i64, semantic_type = !u8, is_implicit = true, is_const_cast = false, folded_constant = "8'b0"} {
                talos.sv.expression.unbased_unsized_integer_literal attributes {node_id = 62 : i64, semantic_type = !l8, constant_value = "8'bxxxxxxxx"} {} loc("unbased_literals.sv":70:1)
              } loc("unbased_literals.sv":70:1)
            } loc("unbased_literals.sv":70:1)
          } loc("unbased_literals.sv":70:1)
        } loc("unbased_literals.sv":70:1)
      }
    }
  }
}
