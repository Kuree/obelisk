// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

// Member-default expressions are ordinary semantic expressions annotated with
// the destination aggregate subelement by the prepare pass. Exercise that
// expression-to-reference lowering without involving the frontend.

!bit4 = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
!record = !simulation.unpacked_struct<[
  #simulation.field<name = "lo", type = !simulation.packed_array<3 : 0 x i1>, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "hi", type = !simulation.packed_array<3 : 0 x i1>, ordinal = 1, packedOffset = 0>
]>

module {
  simulation.design @aggregate_member_initializers {
    simulation.code_unit.decl 9600001 in 0 function
        hierarchy "top.value.$static_initializer"
    simulation.code_unit.decl 9600002 in 0 function
        hierarchy "top.initialize_local"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !record
        design hierarchy "top.value"

    // CHECK-LABEL: simulation.func @initialize
    // CHECK: %[[FIELD:.*]] = simulation.ref.subelement %arg1{{.*}}0
    // CHECK: %[[FIVE:.*]] = arith.constant 5 : i4
    // CHECK: %[[VALUE:.*]] = simulation.packed.unflatten %[[FIVE]]
    // CHECK: simulation.ref.store %[[VALUE]] to %[[FIELD]]
    simulation.func @initialize(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %value: !simulation.ref<!record>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {
          entry_kind = 8 : i32,
          simulation.bindings = [
            #simulation.argument_binding<path = "top.value", argument = 1,
                kind = direct, copyOut = false>
          ],
          code_unit_id = 9600001 : i64,
          simulation.void_function
        } {
      obelisk.sv.expression.integer_literal attributes {
          node_id = 1 : i64, constant_value = "4'h5",
          semantic_type = !bit4,
          simulation.initialize_static = "top.value",
          simulation.initialize_subelement = 0 : i64} {
      }
      simulation.return
    }

    // Automatic aggregate locals are allocated first, then each declared
    // member initializer is stored through a subelement of that allocation.
    // CHECK-LABEL: simulation.func @initialize_local
    // CHECK: %[[DEFAULT:.*]] = simulation.aggregate.default
    // CHECK: %[[LOCAL:.*]] = simulation.ref.alloc %[[DEFAULT]]
    // CHECK: %[[LOCAL_FIVE:.*]] = arith.constant 5 : i4
    // CHECK: %[[LOCAL_VALUE:.*]] = simulation.packed.unflatten %[[LOCAL_FIVE]]
    // CHECK: %[[LOCAL_FIELD:.*]] = simulation.ref.subelement %[[LOCAL]]{{.*}}0
    // CHECK: simulation.ref.store %[[LOCAL_VALUE]] to %[[LOCAL_FIELD]]
    simulation.func @initialize_local(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {
          entry_kind = 8 : i32,
          simulation.bindings = [
            #simulation.local_binding<path = "local", type = !record,
                automatic = true, patternVariable = false, isReturn = false>
          ],
          code_unit_id = 9600002 : i64,
          simulation.void_function
        } {
      obelisk.sv.statement.variable_declaration attributes {
          node_id = 2 : i64, referenced_path = "local",
          referenced_symbol = @local,
          simulation.aggregate_member_initializers} {
        obelisk.sv.expression.integer_literal attributes {
            node_id = 3 : i64, constant_value = "4'h5",
            semantic_type = !bit4,
            simulation.initialize_subelement = 0 : i64} {
        }
      }
      simulation.return
    }
  }
}
