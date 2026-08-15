// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32,
    hierarchical_name = "nested_container_methods",
    name = "nested_container_methods",
    node_id = 0 : i64,
    sym_name = "s0.nested_container_methods"
  } {
  }
  obelisk.sv.symbol.root attributes {
    hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64,
    sym_name = "s1.$root"
  } {
    obelisk.sv.symbol.compilation_unit attributes {
      hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"
    } {
    }
    obelisk.sv.symbol.instance attributes {
      hierarchical_name = "nested_container_methods",
      is_uninstantiated = false,
      name = "nested_container_methods",
      node_id = 3 : i64,
      referenced_path = "nested_container_methods",
      referenced_symbol = @s0.nested_container_methods,
      sym_name = "s3.nested_container_methods"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "nested_container_methods",
        name = "nested_container_methods",
        node_id = 4 : i64,
        sym_name = "s4.nested_container_methods"
      } {
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "nested_container_methods.all",
          lifetime = 1 : i32,
          name = "all",
          node_id = 5 : i64,
          semantic_type = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>, false>,
          sym_name = "s5.all"
        } {
        }
        obelisk.sv.symbol.statement_block attributes {
          block_kind = 0 : i32,
          hierarchical_name = "nested_container_methods",
          node_id = 20 : i64,
          sym_name = "s20"
        } {
          obelisk.sv.symbol.iterator attributes {
            array_type = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>, false>,
            hierarchical_name = "nested_container_methods.assoc_index",
            index_method_name = "",
            is_const,
            name = "assoc_index",
            node_id = 21 : i64,
            semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
            sym_name = "s21.assoc_index"
          } {
          }
          obelisk.sv.symbol.iterator attributes {
            array_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>,
            hierarchical_name = "nested_container_methods.queue_index",
            index_method_name = "",
            is_const,
            name = "queue_index",
            node_id = 22 : i64,
            semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
            sym_name = "s22.queue_index"
          } {
          }
        }

        // UVM's resource pool uses all[precedence].push_front(resource).
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "nested_container_methods",
          node_id = 6 : i64,
          procedure_kind = 0 : i32,
          sym_name = "s6",
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 7 : i64
          } {
            obelisk.sv.expression.call attributes {
              argument_count = 2 : i64,
              callee_name = "push_front",
              constraint_restrictions = [],
              has_inline_constraints = false,
              has_iterator_expression = false,
              has_output_arguments = false,
              has_this_class = false,
              is_super_class = false,
              is_system_call = true,
              node_id = 8 : i64,
              semantic_type = !obelisk.void,
              subroutine_kind = 0 : i32
            } {
              obelisk.sv.expression.element_select attributes {
                node_id = 9 : i64,
                semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>
              } {
                obelisk.sv.expression.named_value attributes {
                  node_id = 10 : i64,
                  referenced_path = "nested_container_methods.all",
                  referenced_symbol = @s1.$root::@s3.nested_container_methods::@s4.nested_container_methods::@s5.all,
                  semantic_type = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>, false>
                } {
                }
                obelisk.sv.expression.integer_literal attributes {
                  constant_value = "7",
                  is_signed = true,
                  node_id = 11 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {
                }
              }
              obelisk.sv.expression.integer_literal attributes {
                constant_value = "42",
                is_signed = true,
                node_id = 12 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
              } {
              }
            }
          }
        }

        // The companion UVM path uses all[precedence].push_back(resource).
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "nested_container_methods",
          node_id = 13 : i64,
          procedure_kind = 0 : i32,
          sym_name = "s13",
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 14 : i64
          } {
            obelisk.sv.expression.call attributes {
              argument_count = 2 : i64,
              callee_name = "push_back",
              constraint_restrictions = [],
              has_inline_constraints = false,
              has_iterator_expression = false,
              has_output_arguments = false,
              has_this_class = false,
              is_super_class = false,
              is_system_call = true,
              node_id = 15 : i64,
              semantic_type = !obelisk.void,
              subroutine_kind = 0 : i32
            } {
              obelisk.sv.expression.element_select attributes {
                node_id = 16 : i64,
                semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>
              } {
                obelisk.sv.expression.named_value attributes {
                  node_id = 17 : i64,
                  referenced_path = "nested_container_methods.all",
                  referenced_symbol = @s1.$root::@s3.nested_container_methods::@s4.nested_container_methods::@s5.all,
                  semantic_type = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>, false>
                } {
                }
                obelisk.sv.expression.integer_literal attributes {
                  constant_value = "7",
                  is_signed = true,
                  node_id = 18 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {
                }
              }
              obelisk.sv.expression.integer_literal attributes {
                constant_value = "43",
                is_signed = true,
                node_id = 19 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
              } {
              }
            }
          }
        }

        // When the inner queue traversal finishes, its exit edge must carry
        // the current associative key to the outer traversal step.
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "nested_container_methods",
          node_id = 23 : i64,
          procedure_kind = 0 : i32,
          sym_name = "s23",
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.foreach_loop attributes {
            loop_dimensions = [
              {
                has_iterator = true,
                has_static_range = false,
                iterator_path = "nested_container_methods.assoc_index",
                iterator_symbol = @s1.$root::@s4.nested_container_methods::@s20::@s21.assoc_index,
                iterator_type = !obelisk.integral<32, true, false, 31 : 0, int>
              },
              {
                has_iterator = true,
                has_static_range = false,
                iterator_path = "nested_container_methods.queue_index",
                iterator_symbol = @s1.$root::@s4.nested_container_methods::@s20::@s22.queue_index,
                iterator_type = !obelisk.integral<32, true, false, 31 : 0, int>
              }
            ],
            node_id = 24 : i64
          } {
            obelisk.sv.expression.named_value attributes {
              node_id = 25 : i64,
              referenced_path = "nested_container_methods.all",
              referenced_symbol = @s1.$root::@s3.nested_container_methods::@s4.nested_container_methods::@s5.all,
              semantic_type = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>, false>
            } {
            }
            obelisk.sv.statement.empty attributes {node_id = 26 : i64} {
            }
          }
        }
      }
    }
  }
}

// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK: %[[ORIGINAL_PARENT:.*]] = obelisk_sim.ref.load %[[PARENT_REF:arg[0-9]+]]
// CHECK: %[[ORIGINAL_QUEUE:.*]] = obelisk_sim.assoc.read %[[ORIGINAL_PARENT]],
// CHECK: obelisk_sim.container.clone %{{.*}}
// CHECK: ^{{bb[0-9]+}}(%[[MUTATED_QUEUE:.*]]: !obelisk_sim.queue<i32, 0>):
// CHECK: obelisk_sim.queue.insert {{.*}} into %[[MUTATED_QUEUE]]
// The parent is reloaded after mutation so argument/with-clause side effects
// to sibling elements are retained by the recursive writeback.
// CHECK: %[[FRESH_PARENT:.*]] = obelisk_sim.ref.load %[[PARENT_REF]]
// CHECK: %[[UPDATED_PARENT:.*]] = obelisk_sim.container.clone %[[FRESH_PARENT]]
// CHECK: obelisk_sim.assoc.write %[[UPDATED_PARENT]], {{.*}}, %[[MUTATED_QUEUE]]
// CHECK: %[[PUBLISHED_PARENT:.*]] = obelisk_sim.container.clone %[[UPDATED_PARENT]]
// CHECK: obelisk_sim.ref.store %[[PUBLISHED_PARENT]] to %[[PARENT_REF]]

// CHECK-LABEL: obelisk_sim.func private @unit_1
// CHECK: %[[BACK_ORIGINAL_PARENT:.*]] = obelisk_sim.ref.load %[[BACK_PARENT_REF:arg[0-9]+]]
// CHECK: %[[BACK_ORIGINAL_QUEUE:.*]] = obelisk_sim.assoc.read %[[BACK_ORIGINAL_PARENT]],
// CHECK: obelisk_sim.container.clone %{{.*}}
// CHECK: ^{{bb[0-9]+}}(%[[BACK_MUTATED_QUEUE:.*]]: !obelisk_sim.queue<i32, 0>):
// CHECK: obelisk_sim.container.write %[[BACK_MUTATED_QUEUE]],
// CHECK: %[[BACK_FRESH_PARENT:.*]] = obelisk_sim.ref.load %[[BACK_PARENT_REF]]
// CHECK: %[[BACK_UPDATED_PARENT:.*]] = obelisk_sim.container.clone %[[BACK_FRESH_PARENT]]
// CHECK: obelisk_sim.assoc.write %[[BACK_UPDATED_PARENT]], {{.*}}, %[[BACK_MUTATED_QUEUE]]
// CHECK: %[[BACK_PUBLISHED_PARENT:.*]] = obelisk_sim.container.clone %[[BACK_UPDATED_PARENT]]
// CHECK: obelisk_sim.ref.store %[[BACK_PUBLISHED_PARENT]] to %[[BACK_PARENT_REF]]

// CHECK-LABEL: obelisk_sim.func private @unit_2
// CHECK: obelisk_sim.assoc.traverse
// CHECK: obelisk_sim.container.size
// CHECK: ^[[OUTER_STEP:bb[0-9]+]](%[[STEP_KEY:.*]]: i32):
// CHECK: cf.cond_br %{{.*}}, ^{{.*}}, ^[[OUTER_STEP]](%{{.*}} : i32)
// CHECK: obelisk_sim.assoc.traverse {{.*}}, %[[STEP_KEY]]
// CHECK-NOT: obelisk.sv.
