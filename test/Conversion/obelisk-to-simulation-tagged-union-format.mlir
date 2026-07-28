// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

!bits4 = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
!tagged = !obelisk.source_aggregate<"top", false, true, true, false, false,
    false, 0, 4, 4, 0, [
      {name = "invalid", ordinal = 0 : i32, packed_offset = 0 : i64,
       type = !obelisk.void},
      {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64,
       type = !bits4}
    ]>
!sim_tagged = !obelisk_sim.unpacked_union<fields = [
    #obelisk_sim.field<name = "invalid", type = i1, ordinal = 0,
        packedOffset = 0>,
    #obelisk_sim.field<name = "valid",
        type = !obelisk_sim.packed_array<3 : 0 x i1>, ordinal = 1,
        packedOffset = 0>
  ], isTagged = true>

module {
  obelisk_sim.design @tagged_union_format {
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !sim_tagged design
        hierarchy "top.value"

    // CHECK-LABEL: obelisk_sim.func @unit
    // CHECK: %[[VALUE:.*]] = obelisk_sim.ref.load %arg1
    // CHECK: %[[VALID:.*]] = obelisk_sim.union.extract %[[VALUE]][1]
    // CHECK: %[[FLAT:.*]] = obelisk_sim.packed.flatten %[[VALID]]
    // CHECK: %[[TEXT:.*]] = obelisk_sim.string.format_integer
    // CHECK: %[[PREFIX:.*]] = obelisk_sim.string.literal "'{valid:"
    // CHECK: %[[PATTERN:.*]] = obelisk_sim.string.concat %[[PREFIX]], %[[TEXT]]
    // CHECK: %[[ACTIVE:.*]] = obelisk_sim.union.is_active %[[VALUE]][1]
    // CHECK: arith.select %[[ACTIVE]], %[[PATTERN]]
    // CHECK: obelisk_sim.display {{.*}}({{.*}}, {{.*}}) newline = true radix = 10 flags = [0, 8]
    obelisk_sim.func @unit(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.ref<!sim_tagged>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {
          entry_kind = 1 : i32,
          obelisk_sim.delay_scale = 1 : i64,
          obelisk_sim.hierarchical_name = "top",
          obelisk_sim.bindings = [
            #obelisk_sim.argument_binding<path = "top.value", argument = 1,
                kind = direct, copyOut = false>
          ],
          code_unit_id = 1 : i64
        } {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        obelisk.sv.expression.call attributes {
            argument_count = 2 : i64,
            callee_name = "$display",
            constraint_restrictions = [],
            has_inline_constraints = false,
            has_iterator_expression = false,
            has_output_arguments = false,
            has_this_class = false,
            is_super_class = false,
            is_system_call = true,
            node_id = 2 : i64,
            semantic_type = !obelisk.void,
            subroutine_kind = 1 : i32,
            system_library_cell = "work.top",
            system_scope_path = "top"} {
          obelisk.sv.expression.string_literal attributes {
              constant_value = "%p",
              node_id = 3 : i64,
              semantic_type = !obelisk.ranged_packed_array<15 : 0 x
                  !obelisk.integral<1, false, false, 0 : 0, bit>>} {
          }
          obelisk.sv.expression.named_value attributes {
              node_id = 4 : i64,
              referenced_path = "top.value",
              referenced_symbol = @value,
              semantic_type = !tagged} {
          }
        }
      }
      obelisk_sim.return
    }
  }
}
