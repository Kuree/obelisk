// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 6.24.3 gives the first unbounded target member all bits left
// after later fixed members. Later unbounded members are empty. This 32-bit
// source therefore becomes a four-bit head, five four-bit middle elements, an
// eight-bit tail, and an empty final array.

!target = !talos.source_aggregate<"top", false, false, false, false, false, false, 0, 14, 12, 0, [
  {name = "head", ordinal = 0 : i32, packed_offset = 0 : i64, type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>},
  {name = "middle", ordinal = 1 : i32, packed_offset = 0 : i64, type = !talos.dynarray<!talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>>},
  {name = "tail", ordinal = 2 : i32, packed_offset = 0 : i64, type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>},
  {name = "later", ordinal = 3 : i32, packed_offset = 0 : i64, type = !talos.dynarray<!talos.integral<8, true, false, 7 : 0, byte>>}
]>
!source = !talos.ranged_packed_array<31 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>

module {
  talos.sv.symbol.definition @top_def attributes {
    definition_kind = 0 : i32, hierarchical_name = "top", name = "top",
    node_id = 0 : i64
  } {}
  talos.sv.symbol.root @root attributes {
    hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64
  } {
    talos.sv.symbol.instance @top attributes {
      hierarchical_name = "top", is_uninstantiated = false, name = "top",
      node_id = 2 : i64, referenced_path = "top",
      referenced_symbol = @top_def
    } {
      talos.sv.symbol.instance_body @body attributes {
        hierarchical_name = "top", name = "top", node_id = 3 : i64
      } {
        talos.sv.symbol.variable @source attributes {
          hierarchical_name = "top.source", lifetime = 1 : i32,
          name = "source", node_id = 4 : i64, semantic_type = !source
        } {}
        talos.sv.symbol.variable @result attributes {
          hierarchical_name = "top.result", lifetime = 1 : i32,
          name = "result", node_id = 5 : i64, semantic_type = !target
        } {
          talos.sv.expression.conversion attributes {
            is_implicit = false, is_signed = false, node_id = 6 : i64,
            semantic_type = !target
          } {
            talos.sv.expression.named_value attributes {
              is_signed = false, node_id = 7 : i64,
              referenced_path = "top.source",
              referenced_symbol = @root::@top::@body::@source,
              semantic_type = !source
            } {}
          }
        }
      }
    }
  }
}

// CHECK: %[[FIVE:.*]] = arith.constant 5 : i64
// CHECK: simulation.container.create %[[FIVE]]
// CHECK-SAME: -> !simulation.dynamic_array<!simulation.packed_array<3 : 0 x i1>>
// One dynamic extraction in the counted element loop, independent of the
// middle array's runtime size.
// CHECK-COUNT-1: simulation.logic.dyn_extract
// CHECK: %[[ZERO:.*]] = arith.constant {{.*}} 0 : i64
// CHECK: simulation.container.create %[[ZERO]]
// CHECK-SAME: -> !simulation.dynamic_array<i8>
// CHECK: simulation.aggregate.construct
