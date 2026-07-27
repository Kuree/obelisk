// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

// Member-default expressions are ordinary semantic expressions annotated with
// the destination aggregate subelement by the prepare pass. Exercise that
// expression-to-reference lowering without involving the frontend.

!bit4 = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
!record = !obelisk_sim.unpacked_struct<[
  #obelisk_sim.field<name = "lo", type = !obelisk_sim.packed_array<3 : 0 x i1>, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "hi", type = !obelisk_sim.packed_array<3 : 0 x i1>, ordinal = 1, packedOffset = 0>
]>

module {
  obelisk_sim.design @aggregate_member_initializers {
    obelisk_sim.code_unit.decl 9600001 in 0 function
        hierarchy "top.value.$static_initializer"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !record
        design hierarchy "top.value"

    // CHECK-LABEL: obelisk_sim.func @initialize
    // CHECK: %[[FIELD:.*]] = obelisk_sim.ref.subelement %arg1{{.*}}0
    // CHECK: %[[FIVE:.*]] = arith.constant 5 : i4
    // CHECK: %[[VALUE:.*]] = obelisk_sim.packed.unflatten %[[FIVE]]
    // CHECK: obelisk_sim.ref.store %[[VALUE]] to %[[FIELD]]
    obelisk_sim.func @initialize(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.ref<!record>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {
          entry_kind = 8 : i32,
          obelisk_sim.bindings = [
            #obelisk_sim.argument_binding<path = "top.value", argument = 1,
                kind = direct, copyOut = false>
          ],
          code_unit_id = 9600001 : i64,
          obelisk_sim.void_function
        } {
      obelisk.sv.expression.integer_literal attributes {
          node_id = 1 : i64, constant_value = "4'h5",
          semantic_type = !bit4,
          obelisk_sim.initialize_static = "top.value",
          obelisk_sim.initialize_subelement = 0 : i64} {
      }
      obelisk_sim.return
    }
  }
}
