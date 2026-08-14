// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32,
    hierarchical_name = "queue_assignment_pattern",
    name = "queue_assignment_pattern", node_id = 0 : i64,
    sym_name = "s0.queue_assignment_pattern"
  } {}
  obelisk.sv.symbol.root attributes {
    hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64,
    sym_name = "s1.$root"
  } {
    obelisk.sv.symbol.compilation_unit attributes {
      hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"
    } {}
    obelisk.sv.symbol.instance attributes {
      hierarchical_name = "queue_assignment_pattern",
      is_uninstantiated = false, name = "queue_assignment_pattern",
      node_id = 3 : i64, referenced_path = "queue_assignment_pattern",
      referenced_symbol = @s0.queue_assignment_pattern,
      sym_name = "s3.queue_assignment_pattern"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "queue_assignment_pattern",
        name = "queue_assignment_pattern", node_id = 4 : i64,
        sym_name = "s4.queue_assignment_pattern"
      } {
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "queue_assignment_pattern.values",
          lifetime = 1 : i32, name = "values", node_id = 5 : i64,
          semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>,
          sym_name = "s5.values"
        } {
          obelisk.sv.expression.simple_assignment_pattern attributes {
            node_id = 6 : i64,
            semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>
          } {
            obelisk.sv.expression.integer_literal attributes {
              constant_value = "11", is_signed = true, node_id = 7 : i64,
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
            } {}
            obelisk.sv.expression.integer_literal attributes {
              constant_value = "22", is_signed = true, node_id = 8 : i64,
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
            } {}
          }
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "queue_assignment_pattern.empty",
          lifetime = 1 : i32, name = "empty", node_id = 9 : i64,
          semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>,
          sym_name = "s9.empty"
        } {
          obelisk.sv.expression.simple_assignment_pattern attributes {
            node_id = 10 : i64,
            semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>
          } {}
        }
      }
    }
  }
}

// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK-DAG: %[[INDEX_ONE:.*]] = arith.constant 1 : i64
// CHECK-DAG: %[[TWENTY_TWO:.*]] = arith.constant 22 : i32
// CHECK-DAG: %[[ELEVEN:.*]] = arith.constant 11 : i32
// CHECK-DAG: %[[ZERO:.*]] = arith.constant 0 : i64
// CHECK: %[[VALUES:.*]] = obelisk_sim.container.create %[[ZERO]]
// CHECK-SAME: bound = -1
// CHECK-SAME: container_kind = 2
// CHECK: obelisk_sim.container.write %[[VALUES]], %[[ZERO]], %[[ELEVEN]]
// CHECK: obelisk_sim.container.write %[[VALUES]], %[[INDEX_ONE]], %[[TWENTY_TWO]]
// CHECK: obelisk_sim.ref.store %[[VALUES]]

// CHECK-LABEL: obelisk_sim.func private @unit_1
// CHECK: %[[EMPTY_ZERO:.*]] = arith.constant 0 : i64
// CHECK: %[[EMPTY:.*]] = obelisk_sim.container.create %[[EMPTY_ZERO]]
// CHECK-SAME: container_kind = 2
// CHECK-NOT: obelisk_sim.container.write
// CHECK: obelisk_sim.ref.store %[[EMPTY]]
