// RUN: obelisk-opt %s | FileCheck %s
// RUN: obelisk-opt %s '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   -o /dev/null

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @class_handle_format {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "top.format"
    obelisk_sim.class.decl @Object id 1 {
      is_abstract = false, is_final = true, is_interface = false
    }

    obelisk_sim.func @format(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %object: !obelisk_sim.class_handle<@Object>
          {obelisk_sim.capture_kind = 2 : i32}) -> !obelisk_sim.string
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32,
                    obelisk_sim.hierarchical_name = "top.format"} {
      %format = obelisk_sim.bytes.constant "%p"
      %formatted = obelisk_sim.string.output_format %ctx(%format, %object)
          radix = 10 flags = [32, 64] {scope = "top.format"} :
          !obelisk_sim.bytes, !obelisk_sim.class_handle<@Object>
      %fd = arith.constant 1 : i32
      obelisk_sim.display %ctx to %fd(%format, %object) newline = false
          radix = 10 flags = [0, 64] {scope = "top.format"} :
          !obelisk_sim.bytes, !obelisk_sim.class_handle<@Object>
      obelisk_sim.display %ctx to %fd(%object) newline = false
          radix = 10 flags = [64] {scope = "top.format"} :
          !obelisk_sim.class_handle<@Object>
      obelisk_sim.return %formatted : !obelisk_sim.string
    }
  }
}

// CHECK: obelisk_sim.string.output_format
// CHECK-SAME: flags = [32, 64]
// CHECK-SAME: !obelisk_sim.class_handle<@Object>
// CHECK: obelisk_sim.display
// CHECK-SAME: flags = [0, 64]
// CHECK-SAME: !obelisk_sim.class_handle<@Object>
// CHECK: obelisk_sim.display
// CHECK-SAME: flags = [64]
// CHECK-SAME: !obelisk_sim.class_handle<@Object>
