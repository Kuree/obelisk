// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// A whole-aggregate driver and a low-order member driver legally have the
// same target start with different widths. Image validation must retain both;
// the resolver combines them bitwise in both execution tiers.
// CHECK: 1

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @overlapping_driver_ranges {
    simulation.scope.decl 0 hierarchy "overlapping_driver_ranges"
    simulation.net.decl 0 in 0 : !simulation.logic<2> design
        hierarchy "overlapping_driver_ranges.value"
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<2> design
        {driven_low = 0 : i64, driven_width = 2 : i64}
    simulation.driver.decl 1 in 0 drives 0 : !simulation.logic<2> design
        {driven_low = 0 : i64, driven_width = 1 : i64}
    simulation.code_unit.decl 9960000 in 0 root_initializer
        hierarchy "overlapping_driver_ranges.root"
    simulation.code_unit.decl 9960001 in 0 continuous
        hierarchy "overlapping_driver_ranges.whole"
    simulation.code_unit.decl 9960002 in 0 continuous
        hierarchy "overlapping_driver_ranges.member"
    simulation.code_unit.decl 9960003 in 0 initial
        hierarchy "overlapping_driver_ranges.check"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9960000 : i64} {
      %whole = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<2>>
      %member = simulation.context.driver %ctx[1] :
          !simulation.driver<!simulation.logic<2>>
      %net = simulation.context.net %ctx[0] :
          !simulation.net<!simulation.logic<2>>
      %p0 = simulation.spawn @drive_whole(%ctx, %whole) :
          !simulation.context, !simulation.driver<!simulation.logic<2>>
          -> !simulation.process
      %p1 = simulation.spawn @drive_member(%ctx, %member) :
          !simulation.context, !simulation.driver<!simulation.logic<2>>
          -> !simulation.process
      %p2 = simulation.spawn @check(%ctx, %net) :
          !simulation.context, !simulation.net<!simulation.logic<2>>
          -> !simulation.process
      simulation.return
    }

    simulation.func private @drive_whole(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %driver: !simulation.driver<!simulation.logic<2>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9960001 : i64} {
      %value = simulation.logic.constant 2 : i2, 0 : i2 :
          !simulation.logic<2>
      simulation.driver.drive %driver = %value :
          !simulation.driver<!simulation.logic<2>>,
          !simulation.logic<2>
      simulation.return
    }

    simulation.func private @drive_member(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %driver: !simulation.driver<!simulation.logic<2>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9960002 : i64} {
      %bit = simulation.driver.extract %driver from 0 :
          !simulation.driver<!simulation.logic<2>> ->
          !simulation.driver<!simulation.logic<1>>
      %zero = simulation.logic.constant false, false :
          !simulation.logic<1>
      simulation.driver.drive %bit = %zero :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>
      simulation.return
    }

    simulation.func private @check(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %net: !simulation.net<!simulation.logic<2>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9960003 : i64} {
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^resume
    ^resume:
      %actual = simulation.net.read %net :
          !simulation.net<!simulation.logic<2>> -> !simulation.logic<2>
      %expected = simulation.logic.constant 2 : i2, 0 : i2 :
          !simulation.logic<2>
      %ok = simulation.logic.compare case_eq %actual, %expected :
          (!simulation.logic<2>, !simulation.logic<2>) -> i1
      %format = simulation.bytes.constant "%0d"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%format, %ok)
          newline = true radix = <decimal> flags = [0, 0] :
          !simulation.bytes, i1
      simulation.return
    }
  }
}
