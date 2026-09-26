// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// Hand-authored simulation IR verifies that exact unpacked-array element views
// address disjoint flat net windows for static force, dynamic RHS updates, and
// release in both execution tiers.
// CHECK: static 00 01
// CHECK: dynamic 10 00
// CHECK: released 00 00

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @array_net_force_runtime {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.unpacked_array<2 : 1 x !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>> design hierarchy "top.n"
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.unpacked_array<2 : 1 x !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>> design {
      driven_low = 0 : i64, driven_width = 2 : i64
    }
    obelisk_sim.driver.decl 1 in 0 drives 0 : !obelisk_sim.unpacked_array<2 : 1 x !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>> design {
      driven_low = 2 : i64, driven_width = 2 : i64
    }
    obelisk_sim.code_unit.decl 9983000 in 0 root_initializer hierarchy "top.root"
    obelisk_sim.code_unit.decl 9983001 in 0 initial hierarchy "top.check"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9983000 : i64} {
      %net = obelisk_sim.context.net %ctx[0] : !obelisk_sim.net<!obelisk_sim.unpacked_array<2 : 1 x !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>>
      %d2 = obelisk_sim.context.driver %ctx[0] : !obelisk_sim.driver<!obelisk_sim.unpacked_array<2 : 1 x !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>>
      %d1 = obelisk_sim.context.driver %ctx[1] : !obelisk_sim.driver<!obelisk_sim.unpacked_array<2 : 1 x !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>>
      %process = obelisk_sim.spawn @check(%ctx, %net, %d2, %d1) :
          !obelisk_sim.context,
          !obelisk_sim.net<!obelisk_sim.unpacked_array<2 : 1 x !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>>,
          !obelisk_sim.driver<!obelisk_sim.unpacked_array<2 : 1 x !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>>,
          !obelisk_sim.driver<!obelisk_sim.unpacked_array<2 : 1 x !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @check(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %net: !obelisk_sim.net<!obelisk_sim.unpacked_array<2 : 1 x !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %d2: !obelisk_sim.driver<!obelisk_sim.unpacked_array<2 : 1 x !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %d1: !obelisk_sim.driver<!obelisk_sim.unpacked_array<2 : 1 x !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9983001 : i64} {
      %zero_bits = obelisk_sim.logic.constant 0 : i2, 0 : i2 : !obelisk_sim.logic<2>
      %one_bits = obelisk_sim.logic.constant 1 : i2, 0 : i2 : !obelisk_sim.logic<2>
      %two_bits = obelisk_sim.logic.constant 2 : i2, 0 : i2 : !obelisk_sim.logic<2>
      %zero = obelisk_sim.packed.unflatten %zero_bits : (!obelisk_sim.logic<2>) -> !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>
      %one = obelisk_sim.packed.unflatten %one_bits : (!obelisk_sim.logic<2>) -> !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>
      %two = obelisk_sim.packed.unflatten %two_bits : (!obelisk_sim.logic<2>) -> !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>
      %n2 = obelisk_sim.net.extract %net from 0 : !obelisk_sim.net<!obelisk_sim.unpacked_array<2 : 1 x !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>> -> !obelisk_sim.net<!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>
      %n1 = obelisk_sim.net.extract %net from 2 : !obelisk_sim.net<!obelisk_sim.unpacked_array<2 : 1 x !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>> -> !obelisk_sim.net<!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>
      %d2e = obelisk_sim.driver.subelement %d2[[0]] : !obelisk_sim.driver<!obelisk_sim.unpacked_array<2 : 1 x !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>> -> !obelisk_sim.driver<!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>
      %d1e = obelisk_sim.driver.subelement %d1[[1]] : !obelisk_sim.driver<!obelisk_sim.unpacked_array<2 : 1 x !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>> -> !obelisk_sim.driver<!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>
      obelisk_sim.driver.drive %d2e = %zero : !obelisk_sim.driver<!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>, !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>
      obelisk_sim.driver.drive %d1e = %zero : !obelisk_sim.driver<!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>, !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>

      obelisk_sim.override %n1 = %one assign false : !obelisk_sim.net<!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>, !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>
      %static2 = obelisk_sim.net.read %n2 : !obelisk_sim.net<!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>> -> !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>
      %static1 = obelisk_sim.net.read %n1 : !obelisk_sim.net<!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>> -> !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>
      %static2_bits = obelisk_sim.packed.flatten %static2 : (!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>) -> !obelisk_sim.logic<2>
      %static1_bits = obelisk_sim.packed.flatten %static1 : (!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>) -> !obelisk_sim.logic<2>
      %static_format = obelisk_sim.bytes.constant "static %b %b"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%static_format, %static2_bits, %static1_bits) newline = true radix = 10 flags = [0, 0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<2>, !obelisk_sim.logic<2>
      obelisk_sim.release_override %n1 assign false : !obelisk_sim.net<!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>

      %owner = obelisk_sim.process.current
      obelisk_sim.dynamic_override %n2 = %one owner %owner assign false claim true : !obelisk_sim.net<!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>, !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>
      obelisk_sim.dynamic_override %n2 = %two owner %owner assign false claim false : !obelisk_sim.net<!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>, !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>
      %dynamic2 = obelisk_sim.net.read %n2 : !obelisk_sim.net<!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>> -> !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>
      %dynamic1 = obelisk_sim.net.read %n1 : !obelisk_sim.net<!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>> -> !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>
      %dynamic2_bits = obelisk_sim.packed.flatten %dynamic2 : (!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>) -> !obelisk_sim.logic<2>
      %dynamic1_bits = obelisk_sim.packed.flatten %dynamic1 : (!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>) -> !obelisk_sim.logic<2>
      %dynamic_format = obelisk_sim.bytes.constant "dynamic %b %b"
      obelisk_sim.display %ctx to %stdout(%dynamic_format, %dynamic2_bits, %dynamic1_bits) newline = true radix = 10 flags = [0, 0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<2>, !obelisk_sim.logic<2>
      obelisk_sim.release_override %n2 assign false : !obelisk_sim.net<!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>

      %released2 = obelisk_sim.net.read %n2 : !obelisk_sim.net<!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>> -> !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>
      %released1 = obelisk_sim.net.read %n1 : !obelisk_sim.net<!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>> -> !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>
      %released2_bits = obelisk_sim.packed.flatten %released2 : (!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>) -> !obelisk_sim.logic<2>
      %released1_bits = obelisk_sim.packed.flatten %released1 : (!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>) -> !obelisk_sim.logic<2>
      %released_format = obelisk_sim.bytes.constant "released %b %b"
      obelisk_sim.display %ctx to %stdout(%released_format, %released2_bits, %released1_bits) newline = true radix = 10 flags = [0, 0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<2>, !obelisk_sim.logic<2>
      obelisk_sim.return
    }
  }
}
