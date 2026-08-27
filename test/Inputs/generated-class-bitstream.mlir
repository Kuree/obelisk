module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.feature.class_bitstream_source
} {
  obelisk_sim.design @generated_class_bitstream {
    obelisk_sim.code_unit.decl 9100001 in 0 root_initializer hierarchy "test.generated_class_bitstream.root"
    obelisk_sim.scope.decl 0

    obelisk_sim.class.decl @__obelisk_class_C id 1 {
      is_abstract = false, is_final = true, is_interface = false
    }
    obelisk_sim.class.field @__obelisk_class_C_value of @__obelisk_class_C at 0 : i8 {
      is_static = false, is_weak = false,
      obelisk_sim.class_bitstream_member,
      obelisk_sim.class_bitstream_visibility = 0 : i32
    }

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9100001 : i64} {
      %object = obelisk_sim.class.alloc %ctx :
          !obelisk_sim.context -> !obelisk_sim.class_handle<@__obelisk_class_C>
      %field = obelisk_sim.class.field_ref %object[@__obelisk_class_C_value] :
          !obelisk_sim.class_handle<@__obelisk_class_C> ->
          !obelisk_sim.managed_ref<i8, @__obelisk_class_C>
      %value = arith.constant 165 : i8
      obelisk_sim.managed.store %value to %field :
          i8, !obelisk_sim.managed_ref<i8, @__obelisk_class_C>
      %result, %matched, %watch =
          obelisk_sim.recursive.export_bitstream %object {
            class_allow_hidden_root,
            plan = array<i64: 9702691408, 1, 64, 0,
                5, 0, 4256129838436814268, 0, 64, 0>
          } : (!obelisk_sim.class_handle<@__obelisk_class_C>) ->
              (i8, i1, !obelisk_sim.managed_watch)
      %observed_result, %observed_matched, %observed_watch =
          obelisk_sim.recursive.export_bitstream %object {
            class_allow_hidden_root, observe,
            plan = array<i64: 9702691408, 1, 64, 0,
                5, 0, 4256129838436814268, 0, 64, 0>
          } : (!obelisk_sim.class_handle<@__obelisk_class_C>) ->
              (i8, i1, !obelisk_sim.managed_watch)
      %expected = arith.constant 165 : i8
      %value_ok = arith.cmpi eq, %result, %expected : i8
      %observed_value_ok = arith.cmpi eq, %observed_result, %expected : i8
      %values_ok = arith.andi %value_ok, %observed_value_ok : i1
      %matches_ok = arith.andi %matched, %observed_matched : i1
      %ok = arith.andi %values_ok, %matches_ok : i1
      cf.cond_br %ok, ^pass, ^fail
    ^pass:
      obelisk_sim.return
    ^fail:
      obelisk_sim.error %ctx
      obelisk_sim.return
    }
  }
}
