module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.feature.class_bitstream_source
} {
  simulation.design @generated_class_bitstream {
    simulation.code_unit.decl 9100001 in 0 root_initializer hierarchy "test.generated_class_bitstream.root"
    simulation.scope.decl 0

    simulation.class.decl @__obelisk_class_C id 1 {
      is_abstract = false, is_final = true, is_interface = false
    }
    simulation.class.field @__obelisk_class_C_value of @__obelisk_class_C at 0 : i8 {
      is_static = false, is_weak = false,
      simulation.class_bitstream_member,
      simulation.class_bitstream_visibility = #simulation.member_visibility<public>
    }

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9100001 : i64} {
      %object = simulation.class.alloc %ctx :
          !simulation.context -> !simulation.class_handle<@__obelisk_class_C>
      %field = simulation.class.field_ref %object[@__obelisk_class_C_value] :
          !simulation.class_handle<@__obelisk_class_C> ->
          !simulation.managed_ref<i8, @__obelisk_class_C>
      %value = arith.constant 165 : i8
      simulation.managed.store %value to %field :
          i8, !simulation.managed_ref<i8, @__obelisk_class_C>
      %result, %matched, %watch =
          simulation.recursive.export_bitstream %object {
            class_allow_hidden_root,
            plan = array<i64: 9702691408, 1, 64, 0,
                5, 0, 4256129838436814268, 0, 64, 0>
          } : (!simulation.class_handle<@__obelisk_class_C>) ->
              (i8, i1, !simulation.managed_watch)
      %observed_result, %observed_matched, %observed_watch =
          simulation.recursive.export_bitstream %object {
            class_allow_hidden_root, observe,
            plan = array<i64: 9702691408, 1, 64, 0,
                5, 0, 4256129838436814268, 0, 64, 0>
          } : (!simulation.class_handle<@__obelisk_class_C>) ->
              (i8, i1, !simulation.managed_watch)
      %expected = arith.constant 165 : i8
      %value_ok = arith.cmpi eq, %result, %expected : i8
      %observed_value_ok = arith.cmpi eq, %observed_result, %expected : i8
      %values_ok = arith.andi %value_ok, %observed_value_ok : i1
      %matches_ok = arith.andi %matched, %observed_matched : i1
      %ok = arith.andi %values_ok, %matches_ok : i1
      cf.cond_br %ok, ^pass, ^fail
    ^pass:
      simulation.return
    ^fail:
      simulation.error %ctx
      simulation.return
    }
  }
}
