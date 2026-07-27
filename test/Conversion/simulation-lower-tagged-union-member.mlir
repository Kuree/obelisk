// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

// Hand-authored semantic MLIR verifies that reading a tagged-union member
// guards the extraction and terminates the activation on an inactive tag.

!int = !obelisk.integral<32, true, false, 31 : 0, int>
!tagged = !obelisk.source_aggregate<"top", false, true, true, false, false, false,
    0, 32, 32, 0, [
      {name = "Invalid", ordinal = 0 : i32, packed_offset = 0 : i64,
       type = !obelisk.void},
      {name = "Valid", ordinal = 1 : i32, packed_offset = 0 : i64,
       type = !int}
    ]>
!sim_tagged = !obelisk_sim.unpacked_union<fields = [
    #obelisk_sim.field<name = "Invalid", type = i1, ordinal = 0,
        packedOffset = 0>,
    #obelisk_sim.field<name = "Valid", type = i32, ordinal = 1,
        packedOffset = 0>
  ], isTagged = true>

module {
  obelisk_sim.design @tagged_member {
    obelisk_sim.code_unit.decl 9200001 in 0 initial
        hierarchy "test.tagged_member.9200001"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !sim_tagged
        design hierarchy "top.value"
    obelisk_sim.storage.decl 1 in 0 : i32
        design hierarchy "top.result"

    // CHECK-LABEL: obelisk_sim.func @unit
    // CHECK: %[[UNION:.*]] = obelisk_sim.ref.load %arg1
    // CHECK: %[[ACTIVE:.*]] = obelisk_sim.union.is_active %[[UNION]][1]
    // CHECK: cf.cond_br %[[ACTIVE]], ^[[VALID:.*]], ^[[INVALID:.*]]
    // CHECK: ^[[VALID]]:
    // CHECK: %[[MEMBER:.*]] = obelisk_sim.union.extract %[[UNION]][1]
    // CHECK: obelisk_sim.ref.store %[[MEMBER]] to %arg2
    // CHECK: obelisk_sim.return
    // CHECK: ^[[INVALID]]:
    // CHECK: obelisk_sim.bytes.constant "FATAL: {{.*}}simulation-lower-tagged-union-member.mlir:{{[0-9]+}}: tagged union member access selected an inactive member."
    // CHECK: obelisk_sim.display
    // CHECK: obelisk_sim.fatal
    // CHECK: obelisk_sim.return
    obelisk_sim.func @unit(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.ref<!sim_tagged>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %result: !obelisk_sim.ref<i32>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64})
        attributes {
          entry_kind = 1 : i32,
          obelisk_sim.delay_scale = 1 : i64,
          obelisk_sim.hierarchical_name = "top",
          obelisk_sim.bindings = [
            #obelisk_sim.argument_binding<path = "top.value", argument = 1,
                kind = direct, copyOut = false>,
            #obelisk_sim.argument_binding<path = "top.result", argument = 2,
                kind = direct, copyOut = false>
          ],
          code_unit_id = 9200001 : i64
        } {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        obelisk.sv.expression.assignment attributes {
            node_id = 2 : i64, assignment_kind = 0 : i32,
            semantic_type = !int} {
          obelisk.sv.expression.named_value attributes {
              node_id = 3 : i64, referenced_path = "top.result",
              referenced_symbol = @result, semantic_type = !int} {
          }
          obelisk.sv.expression.member_access attributes {
              node_id = 4 : i64, field_ordinal = 1 : i64,
              referenced_path = "top.Valid", referenced_symbol = @Valid,
              semantic_type = !int} {
            obelisk.sv.expression.named_value attributes {
                node_id = 5 : i64, referenced_path = "top.value",
                referenced_symbol = @value, semantic_type = !tagged} {
            }
          }
        }
      }
      obelisk_sim.return
    }
  }
}
