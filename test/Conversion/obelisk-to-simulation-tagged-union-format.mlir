// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk-sim-prepare-unit-lowering,simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

!logic4 = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
!tagged = !obelisk.source_aggregate<"top", false, true, true, false, false,
    false, 0, 4, 4, 0, [
      {name = "invalid", ordinal = 0 : i32, packed_offset = 0 : i64,
       type = !obelisk.void},
      {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64,
       type = !logic4}
    ]>
!sim_tagged = !simulation.unpacked_union<fields = [
    #simulation.field<name = "invalid", type = i1, ordinal = 0,
        packedOffset = 0>,
    #simulation.field<name = "valid",
        type = !simulation.packed_array<3 : 0 x !simulation.logic<1>>,
        ordinal = 1,
        packedOffset = 0>
  ], isTagged = true>

module {
  simulation.design @tagged_union_format {
    simulation.code_unit.decl 1 in 0 initial hierarchy "top"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !sim_tagged design
        hierarchy "top.value"

    // CHECK-LABEL: simulation.func @unit
    // CHECK: %[[VALUE:.*]] = simulation.ref.load %arg1
    // CHECK: %[[VALID:.*]] = simulation.union.extract %[[VALUE]][1]
    // CHECK: %[[FLAT:.*]] = simulation.packed.flatten %[[VALID]]
    // CHECK-SAME: !simulation.logic<4>
    // CHECK: %[[TEXT:.*]] = simulation.string.output_format
    // CHECK-SAME: %[[FLAT]]
    // CHECK-SAME: flags = [32, 0]
    // CHECK: %[[PREFIX:.*]] = simulation.string.literal "'{valid:"
    // CHECK: %[[PATTERN:.*]] = simulation.string.concat %[[PREFIX]], %[[TEXT]]
    // CHECK: %[[ACTIVE:.*]] = simulation.union.is_active %[[VALUE]][1]
    // CHECK: arith.select %[[ACTIVE]], %[[PATTERN]]
    // CHECK: simulation.display {{.*}}({{.*}}, {{.*}}) newline = true radix = <decimal> flags = [0, 8]
    simulation.func @unit(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %value: !simulation.ref<!sim_tagged>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {
          entry_kind = 1 : i32,
          simulation.delay_scale = 1 : i64,
          simulation.hierarchical_name = "top",
          simulation.bindings = [
            #simulation.argument_binding<path = "top.value", argument = 1,
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
      simulation.return
    }
  }
}
