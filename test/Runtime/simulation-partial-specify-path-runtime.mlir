// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir > %t.ll
// RUN: %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O0>' %t.ll \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o0.o
// RUN: %llvm_dist/bin/clang++ %t.o0.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.o0.exe
// RUN: %t.o0.exe --execution-tier=native | FileCheck %s
// RUN: %t.o0.exe --execution-tier=bytecode | FileCheck %s
// RUN: %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' %t.ll \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o3.o
// RUN: %llvm_dist/bin/clang++ %t.o3.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.o3.exe
// RUN: %t.o3.exe --execution-tier=native | FileCheck %s
// RUN: %t.o3.exe --execution-tier=bytecode | FileCheck %s

// This MLIR-first test drives the path ABI directly. Bits outside the dynamic
// path mask update immediately, overlapping rules arbitrate independently per
// destination bit, an unrelated bit activation preserves another bit's pending
// deadline, and a short pulse is rejected without scanning the scheduler queue.
// CHECK: base 0000
// CHECK-NEXT: outside 1100
// CHECK-NEXT: overlap 1110
// CHECK-NEXT: independent-fall 1100
// CHECK-NEXT: independent-pending 1100
// CHECK-NEXT: independent-done 1101
// CHECK-NEXT: pulse-rejected 1101

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @partial_specify_path_runtime {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<4> design hierarchy "top.output"
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<4> design
    obelisk_sim.code_unit.decl 9931000 in 0 root_initializer hierarchy "top.root"
    obelisk_sim.code_unit.decl 9931001 in 0 initial hierarchy "top.test"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9931000 : i64,
                    obelisk_sim.lowered} {
      %driver = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<4>>
      %output = obelisk_sim.context.net %ctx[0] :
          !obelisk_sim.net<!obelisk_sim.logic<4>>
      %test = obelisk_sim.spawn @test(%ctx, %driver, %output) :
          !obelisk_sim.context,
          !obelisk_sim.driver<!obelisk_sim.logic<4>>,
          !obelisk_sim.net<!obelisk_sim.logic<4>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @test(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %driver: !obelisk_sim.driver<!obelisk_sim.logic<4>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %output: !obelisk_sim.net<!obelisk_sim.logic<4>>
            {obelisk_sim.capture_kind = 4 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9931001 : i64,
                    obelisk_sim.lowered} {
      %v0 = obelisk_sim.logic.constant 0 : i4, 0 : i4 : !obelisk_sim.logic<4>
      %m0 = arith.constant 0 : i4
      %d0 = obelisk_sim.time.constant 0
      obelisk_sim.driver.drive_inertial_path %driver = %v0 active %m0
          masks[%m0, %m0, %m0] after[%d0, %d0, %d0]
          site 9931001 : 0 group 0 of 1 :
          !obelisk_sim.driver<!obelisk_sim.logic<4>>,
          !obelisk_sim.logic<4>, i4
      %one0 = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %one0 to ^base

    ^base:
      %base_v = obelisk_sim.net.read %output :
          !obelisk_sim.net<!obelisk_sim.logic<4>> -> !obelisk_sim.logic<4>
      %base_f = obelisk_sim.bytes.constant "base %b"
      %stdout0 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout0(%base_f, %base_v)
          newline = true radix = 10 flags = [0, 0] :
          !obelisk_sim.bytes, !obelisk_sim.logic<4>
      %v15 = obelisk_sim.logic.constant -1 : i4, 0 : i4 : !obelisk_sim.logic<4>
      %m3 = arith.constant 3 : i4
      %m2 = arith.constant 2 : i4
      %d2 = obelisk_sim.time.constant 2
      %d5 = obelisk_sim.time.constant 5
      obelisk_sim.driver.drive_inertial_path %driver = %v15 active %m3
          masks[%m3, %m3, %m3] after[%d5, %d5, %d5]
          site 9931001 : 0 group 0 of 2 :
          !obelisk_sim.driver<!obelisk_sim.logic<4>>,
          !obelisk_sim.logic<4>, i4
      obelisk_sim.driver.drive_inertial_path %driver = %v15 active %m3
          masks[%m2, %m2, %m2] after[%d2, %d2, %d2]
          site 9931001 : 0 group 1 of 2 :
          !obelisk_sim.driver<!obelisk_sim.logic<4>>,
          !obelisk_sim.logic<4>, i4
      %one1 = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %one1 to ^outside

    ^outside:
      %outside_v = obelisk_sim.net.read %output :
          !obelisk_sim.net<!obelisk_sim.logic<4>> -> !obelisk_sim.logic<4>
      %outside_f = obelisk_sim.bytes.constant "outside %b"
      %stdout1 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout1(%outside_f, %outside_v)
          newline = true radix = 10 flags = [0, 0] :
          !obelisk_sim.bytes, !obelisk_sim.logic<4>
      %one2 = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %one2 to ^overlap

    ^overlap:
      %overlap_v = obelisk_sim.net.read %output :
          !obelisk_sim.net<!obelisk_sim.logic<4>> -> !obelisk_sim.logic<4>
      %overlap_f = obelisk_sim.bytes.constant "overlap %b"
      %stdout2 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout2(%overlap_f, %overlap_v)
          newline = true radix = 10 flags = [0, 0] :
          !obelisk_sim.bytes, !obelisk_sim.logic<4>
      %v13 = obelisk_sim.logic.constant -3 : i4, 0 : i4 : !obelisk_sim.logic<4>
      %m2_overlap = arith.constant 2 : i4
      %d1 = obelisk_sim.time.constant 1
      obelisk_sim.driver.drive_inertial_path %driver = %v13 active %m2_overlap
          masks[%m2_overlap, %m2_overlap, %m2_overlap] after[%d1, %d1, %d1]
          site 9931001 : 0 group 0 of 1 :
          !obelisk_sim.driver<!obelisk_sim.logic<4>>,
          !obelisk_sim.logic<4>, i4
      %one3 = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %one3 to ^independent_fall

    ^independent_fall:
      %fall_v = obelisk_sim.net.read %output :
          !obelisk_sim.net<!obelisk_sim.logic<4>> -> !obelisk_sim.logic<4>
      %fall_f = obelisk_sim.bytes.constant "independent-fall %b"
      %stdout3 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout3(%fall_f, %fall_v)
          newline = true radix = 10 flags = [0, 0] :
          !obelisk_sim.bytes, !obelisk_sim.logic<4>
      %one4 = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %one4 to ^independent_pending

    ^independent_pending:
      %pending_v = obelisk_sim.net.read %output :
          !obelisk_sim.net<!obelisk_sim.logic<4>> -> !obelisk_sim.logic<4>
      %pending_f = obelisk_sim.bytes.constant "independent-pending %b"
      %stdout4 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout4(%pending_f, %pending_v)
          newline = true radix = 10 flags = [0, 0] :
          !obelisk_sim.bytes, !obelisk_sim.logic<4>
      %one5 = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %one5 to ^independent_done

    ^independent_done:
      %done_v = obelisk_sim.net.read %output :
          !obelisk_sim.net<!obelisk_sim.logic<4>> -> !obelisk_sim.logic<4>
      %done_f = obelisk_sim.bytes.constant "independent-done %b"
      %stdout5 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout5(%done_f, %done_v)
          newline = true radix = 10 flags = [0, 0] :
          !obelisk_sim.bytes, !obelisk_sim.logic<4>
      %v15_done = obelisk_sim.logic.constant -1 : i4, 0 : i4 : !obelisk_sim.logic<4>
      %m2_done = arith.constant 2 : i4
      %d2_done = obelisk_sim.time.constant 2
      obelisk_sim.driver.drive_inertial_path %driver = %v15_done active %m2_done
          masks[%m2_done, %m2_done, %m2_done]
          after[%d2_done, %d2_done, %d2_done]
          site 9931001 : 0 group 0 of 1 :
          !obelisk_sim.driver<!obelisk_sim.logic<4>>,
          !obelisk_sim.logic<4>, i4
      %one6 = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %one6 to ^pulse_fall

    ^pulse_fall:
      %v13_pulse = obelisk_sim.logic.constant -3 : i4, 0 : i4 : !obelisk_sim.logic<4>
      %m2_pulse = arith.constant 2 : i4
      %d2_pulse = obelisk_sim.time.constant 2
      obelisk_sim.driver.drive_inertial_path %driver = %v13_pulse active %m2_pulse
          masks[%m2_pulse, %m2_pulse, %m2_pulse]
          after[%d2_pulse, %d2_pulse, %d2_pulse]
          site 9931001 : 0 group 0 of 1 :
          !obelisk_sim.driver<!obelisk_sim.logic<4>>,
          !obelisk_sim.logic<4>, i4
      %two = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %two to ^pulse_rejected

    ^pulse_rejected:
      %pulse_v = obelisk_sim.net.read %output :
          !obelisk_sim.net<!obelisk_sim.logic<4>> -> !obelisk_sim.logic<4>
      %pulse_f = obelisk_sim.bytes.constant "pulse-rejected %b"
      %stdout6 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout6(%pulse_f, %pulse_v)
          newline = true radix = 10 flags = [0, 0] :
          !obelisk_sim.bytes, !obelisk_sim.logic<4>
      %status = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %status
      obelisk_sim.return
    }
  }
}
