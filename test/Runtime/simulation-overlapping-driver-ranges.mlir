// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
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
  obelisk_sim.design @overlapping_driver_ranges {
    obelisk_sim.scope.decl 0 hierarchy "overlapping_driver_ranges"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<2> design
        hierarchy "overlapping_driver_ranges.value"
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<2> design
        {driven_low = 0 : i64, driven_width = 2 : i64}
    obelisk_sim.driver.decl 1 in 0 drives 0 : !obelisk_sim.logic<2> design
        {driven_low = 0 : i64, driven_width = 1 : i64}
    obelisk_sim.code_unit.decl 9960000 in 0 root_initializer
        hierarchy "overlapping_driver_ranges.root"
    obelisk_sim.code_unit.decl 9960001 in 0 continuous
        hierarchy "overlapping_driver_ranges.whole"
    obelisk_sim.code_unit.decl 9960002 in 0 continuous
        hierarchy "overlapping_driver_ranges.member"
    obelisk_sim.code_unit.decl 9960003 in 0 initial
        hierarchy "overlapping_driver_ranges.check"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9960000 : i64} {
      %whole = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<2>>
      %member = obelisk_sim.context.driver %ctx[1] :
          !obelisk_sim.driver<!obelisk_sim.logic<2>>
      %net = obelisk_sim.context.net %ctx[0] :
          !obelisk_sim.net<!obelisk_sim.logic<2>>
      %p0 = obelisk_sim.spawn @drive_whole(%ctx, %whole) :
          !obelisk_sim.context, !obelisk_sim.driver<!obelisk_sim.logic<2>>
          -> !obelisk_sim.process
      %p1 = obelisk_sim.spawn @drive_member(%ctx, %member) :
          !obelisk_sim.context, !obelisk_sim.driver<!obelisk_sim.logic<2>>
          -> !obelisk_sim.process
      %p2 = obelisk_sim.spawn @check(%ctx, %net) :
          !obelisk_sim.context, !obelisk_sim.net<!obelisk_sim.logic<2>>
          -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @drive_whole(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %driver: !obelisk_sim.driver<!obelisk_sim.logic<2>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9960001 : i64} {
      %value = obelisk_sim.logic.constant 2 : i2, 0 : i2 :
          !obelisk_sim.logic<2>
      obelisk_sim.driver.drive %driver = %value :
          !obelisk_sim.driver<!obelisk_sim.logic<2>>,
          !obelisk_sim.logic<2>
      obelisk_sim.return
    }

    obelisk_sim.func private @drive_member(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %driver: !obelisk_sim.driver<!obelisk_sim.logic<2>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9960002 : i64} {
      %bit = obelisk_sim.driver.extract %driver from 0 :
          !obelisk_sim.driver<!obelisk_sim.logic<2>> ->
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %zero = obelisk_sim.logic.constant false, false :
          !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %bit = %zero :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>
      obelisk_sim.return
    }

    obelisk_sim.func private @check(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %net: !obelisk_sim.net<!obelisk_sim.logic<2>>
            {obelisk_sim.capture_kind = 4 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9960003 : i64} {
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^resume
    ^resume:
      %actual = obelisk_sim.net.read %net :
          !obelisk_sim.net<!obelisk_sim.logic<2>> -> !obelisk_sim.logic<2>
      %expected = obelisk_sim.logic.constant 2 : i2, 0 : i2 :
          !obelisk_sim.logic<2>
      %ok = obelisk_sim.logic.compare case_eq %actual, %expected :
          (!obelisk_sim.logic<2>, !obelisk_sim.logic<2>) -> i1
      %format = obelisk_sim.bytes.constant "%0d"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%format, %ok)
          newline = true radix = 10 flags = [0, 0] :
          !obelisk_sim.bytes, i1
      obelisk_sim.return
    }
  }
}
