// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
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
  simulation.design @array_net_force_runtime {
    simulation.scope.decl 0 hierarchy "top"
    simulation.net.decl 0 in 0 : !simulation.unpacked_array<2 : 1 x !simulation.packed_array<1 : 0 x !simulation.logic<1>>> design hierarchy "top.n"
    simulation.driver.decl 0 in 0 drives 0 : !simulation.unpacked_array<2 : 1 x !simulation.packed_array<1 : 0 x !simulation.logic<1>>> design {
      driven_low = 0 : i64, driven_width = 2 : i64
    }
    simulation.driver.decl 1 in 0 drives 0 : !simulation.unpacked_array<2 : 1 x !simulation.packed_array<1 : 0 x !simulation.logic<1>>> design {
      driven_low = 2 : i64, driven_width = 2 : i64
    }
    simulation.code_unit.decl 9983000 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 9983001 in 0 initial hierarchy "top.check"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9983000 : i64} {
      %net = simulation.context.net %ctx[0] : !simulation.net<!simulation.unpacked_array<2 : 1 x !simulation.packed_array<1 : 0 x !simulation.logic<1>>>>
      %d2 = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.unpacked_array<2 : 1 x !simulation.packed_array<1 : 0 x !simulation.logic<1>>>>
      %d1 = simulation.context.driver %ctx[1] : !simulation.driver<!simulation.unpacked_array<2 : 1 x !simulation.packed_array<1 : 0 x !simulation.logic<1>>>>
      %process = simulation.spawn @check(%ctx, %net, %d2, %d1) :
          !simulation.context,
          !simulation.net<!simulation.unpacked_array<2 : 1 x !simulation.packed_array<1 : 0 x !simulation.logic<1>>>>,
          !simulation.driver<!simulation.unpacked_array<2 : 1 x !simulation.packed_array<1 : 0 x !simulation.logic<1>>>>,
          !simulation.driver<!simulation.unpacked_array<2 : 1 x !simulation.packed_array<1 : 0 x !simulation.logic<1>>>> -> !simulation.process
      simulation.return
    }

    simulation.func private @check(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %net: !simulation.net<!simulation.unpacked_array<2 : 1 x !simulation.packed_array<1 : 0 x !simulation.logic<1>>>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64},
        %d2: !simulation.driver<!simulation.unpacked_array<2 : 1 x !simulation.packed_array<1 : 0 x !simulation.logic<1>>>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64},
        %d1: !simulation.driver<!simulation.unpacked_array<2 : 1 x !simulation.packed_array<1 : 0 x !simulation.logic<1>>>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9983001 : i64} {
      %zero_bits = simulation.logic.constant 0 : i2, 0 : i2 : !simulation.logic<2>
      %one_bits = simulation.logic.constant 1 : i2, 0 : i2 : !simulation.logic<2>
      %two_bits = simulation.logic.constant 2 : i2, 0 : i2 : !simulation.logic<2>
      %zero = simulation.packed.unflatten %zero_bits : (!simulation.logic<2>) -> !simulation.packed_array<1 : 0 x !simulation.logic<1>>
      %one = simulation.packed.unflatten %one_bits : (!simulation.logic<2>) -> !simulation.packed_array<1 : 0 x !simulation.logic<1>>
      %two = simulation.packed.unflatten %two_bits : (!simulation.logic<2>) -> !simulation.packed_array<1 : 0 x !simulation.logic<1>>
      %n2 = simulation.net.extract %net from 0 : !simulation.net<!simulation.unpacked_array<2 : 1 x !simulation.packed_array<1 : 0 x !simulation.logic<1>>>> -> !simulation.net<!simulation.packed_array<1 : 0 x !simulation.logic<1>>>
      %n1 = simulation.net.extract %net from 2 : !simulation.net<!simulation.unpacked_array<2 : 1 x !simulation.packed_array<1 : 0 x !simulation.logic<1>>>> -> !simulation.net<!simulation.packed_array<1 : 0 x !simulation.logic<1>>>
      %d2e = simulation.driver.subelement %d2[[0]] : !simulation.driver<!simulation.unpacked_array<2 : 1 x !simulation.packed_array<1 : 0 x !simulation.logic<1>>>> -> !simulation.driver<!simulation.packed_array<1 : 0 x !simulation.logic<1>>>
      %d1e = simulation.driver.subelement %d1[[1]] : !simulation.driver<!simulation.unpacked_array<2 : 1 x !simulation.packed_array<1 : 0 x !simulation.logic<1>>>> -> !simulation.driver<!simulation.packed_array<1 : 0 x !simulation.logic<1>>>
      simulation.driver.drive %d2e = %zero : !simulation.driver<!simulation.packed_array<1 : 0 x !simulation.logic<1>>>, !simulation.packed_array<1 : 0 x !simulation.logic<1>>
      simulation.driver.drive %d1e = %zero : !simulation.driver<!simulation.packed_array<1 : 0 x !simulation.logic<1>>>, !simulation.packed_array<1 : 0 x !simulation.logic<1>>

      simulation.override %n1 = %one assign false : !simulation.net<!simulation.packed_array<1 : 0 x !simulation.logic<1>>>, !simulation.packed_array<1 : 0 x !simulation.logic<1>>
      %static2 = simulation.net.read %n2 : !simulation.net<!simulation.packed_array<1 : 0 x !simulation.logic<1>>> -> !simulation.packed_array<1 : 0 x !simulation.logic<1>>
      %static1 = simulation.net.read %n1 : !simulation.net<!simulation.packed_array<1 : 0 x !simulation.logic<1>>> -> !simulation.packed_array<1 : 0 x !simulation.logic<1>>
      %static2_bits = simulation.packed.flatten %static2 : (!simulation.packed_array<1 : 0 x !simulation.logic<1>>) -> !simulation.logic<2>
      %static1_bits = simulation.packed.flatten %static1 : (!simulation.packed_array<1 : 0 x !simulation.logic<1>>) -> !simulation.logic<2>
      %static_format = simulation.bytes.constant "static %b %b"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%static_format, %static2_bits, %static1_bits) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, !simulation.logic<2>, !simulation.logic<2>
      simulation.release_override %n1 assign false : !simulation.net<!simulation.packed_array<1 : 0 x !simulation.logic<1>>>

      %owner = simulation.process.current
      simulation.dynamic_override %n2 = %one owner %owner assign false claim true : !simulation.net<!simulation.packed_array<1 : 0 x !simulation.logic<1>>>, !simulation.packed_array<1 : 0 x !simulation.logic<1>>
      simulation.dynamic_override %n2 = %two owner %owner assign false claim false : !simulation.net<!simulation.packed_array<1 : 0 x !simulation.logic<1>>>, !simulation.packed_array<1 : 0 x !simulation.logic<1>>
      %dynamic2 = simulation.net.read %n2 : !simulation.net<!simulation.packed_array<1 : 0 x !simulation.logic<1>>> -> !simulation.packed_array<1 : 0 x !simulation.logic<1>>
      %dynamic1 = simulation.net.read %n1 : !simulation.net<!simulation.packed_array<1 : 0 x !simulation.logic<1>>> -> !simulation.packed_array<1 : 0 x !simulation.logic<1>>
      %dynamic2_bits = simulation.packed.flatten %dynamic2 : (!simulation.packed_array<1 : 0 x !simulation.logic<1>>) -> !simulation.logic<2>
      %dynamic1_bits = simulation.packed.flatten %dynamic1 : (!simulation.packed_array<1 : 0 x !simulation.logic<1>>) -> !simulation.logic<2>
      %dynamic_format = simulation.bytes.constant "dynamic %b %b"
      simulation.display %ctx to %stdout(%dynamic_format, %dynamic2_bits, %dynamic1_bits) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, !simulation.logic<2>, !simulation.logic<2>
      simulation.release_override %n2 assign false : !simulation.net<!simulation.packed_array<1 : 0 x !simulation.logic<1>>>

      %released2 = simulation.net.read %n2 : !simulation.net<!simulation.packed_array<1 : 0 x !simulation.logic<1>>> -> !simulation.packed_array<1 : 0 x !simulation.logic<1>>
      %released1 = simulation.net.read %n1 : !simulation.net<!simulation.packed_array<1 : 0 x !simulation.logic<1>>> -> !simulation.packed_array<1 : 0 x !simulation.logic<1>>
      %released2_bits = simulation.packed.flatten %released2 : (!simulation.packed_array<1 : 0 x !simulation.logic<1>>) -> !simulation.logic<2>
      %released1_bits = simulation.packed.flatten %released1 : (!simulation.packed_array<1 : 0 x !simulation.logic<1>>) -> !simulation.logic<2>
      %released_format = simulation.bytes.constant "released %b %b"
      simulation.display %ctx to %stdout(%released_format, %released2_bits, %released1_bits) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, !simulation.logic<2>, !simulation.logic<2>
      simulation.return
    }
  }
}
