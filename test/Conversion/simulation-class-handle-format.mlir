// RUN: obelisk-opt %s | FileCheck %s
// RUN: obelisk-opt %s '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   -o /dev/null

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @class_handle_format {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 function hierarchy "top.format"
    simulation.class.decl @Object id 1 {
      is_abstract = false, is_final = true, is_interface = false
    }

    simulation.func @format(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %object: !simulation.class_handle<@Object>
          {simulation.capture_kind = 2 : i32}) -> !simulation.string
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32,
                    simulation.hierarchical_name = "top.format"} {
      %format = simulation.bytes.constant "%p"
      %formatted = simulation.string.output_format %ctx(%format, %object)
          radix = <decimal> flags = [32, 64] {scope = "top.format"} :
          !simulation.bytes, !simulation.class_handle<@Object>
      %fd = arith.constant 1 : i32
      simulation.display %ctx to %fd(%format, %object) newline = false
          radix = <decimal> flags = [0, 64] {scope = "top.format"} :
          !simulation.bytes, !simulation.class_handle<@Object>
      simulation.display %ctx to %fd(%object) newline = false
          radix = <decimal> flags = [64] {scope = "top.format"} :
          !simulation.class_handle<@Object>
      simulation.return %formatted : !simulation.string
    }
  }
}

// CHECK: simulation.string.output_format
// CHECK-SAME: flags = [32, 64]
// CHECK-SAME: !simulation.class_handle<@Object>
// CHECK: simulation.display
// CHECK-SAME: flags = [0, 64]
// CHECK-SAME: !simulation.class_handle<@Object>
// CHECK: simulation.display
// CHECK-SAME: flags = [64]
// CHECK-SAME: !simulation.class_handle<@Object>
