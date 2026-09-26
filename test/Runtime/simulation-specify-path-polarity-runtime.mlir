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

// IEEE 1800-2017 30.4.7: polarity describes the source/destination
// relationship but does not change modeled data propagation. In particular,
// transition delays are selected from the destination transition: an
// inverting source rise that makes the destination fall uses tfall, and an
// inverting source fall that makes it rise uses trise. The third driver models
// 30.5.3 overlapping-path arbitration: simultaneous source changes select the
// smaller destination-transition delay, while a later change on only the
// slower path retains that path's delay. All selection is explicit SSA; no
// runtime path table or scan is involved.
// CHECK: base 010
// CHECK-NEXT: early 010
// CHECK-NEXT: simultaneous 111
// CHECK-NEXT: negative-fall 101
// CHECK-NEXT: later-early 101
// CHECK-NEXT: destination-banks z11
// CHECK-NEXT: slow-path-early z11
// CHECK-NEXT: slow-path-done z10

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @specify_path_polarity_runtime {
    obelisk_sim.scope.decl 0 hierarchy "specify_path_polarity_runtime"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 2 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 1 in 0 drives 1 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 2 in 0 drives 2 : !obelisk_sim.logic<1> design
    obelisk_sim.code_unit.decl 9915000 in 0 root_initializer
        hierarchy "specify_path_polarity_runtime.root"
    obelisk_sim.code_unit.decl 9915001 in 0 initial
        hierarchy "specify_path_polarity_runtime.initial"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9915000 : i64,
                    obelisk_sim.lowered} {
      %positive_driver = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %negative_driver = obelisk_sim.context.driver %ctx[1] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %arbitrated_driver = obelisk_sim.context.driver %ctx[2] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %positive_net = obelisk_sim.context.net %ctx[0] :
          !obelisk_sim.net<!obelisk_sim.logic<1>>
      %negative_net = obelisk_sim.context.net %ctx[1] :
          !obelisk_sim.net<!obelisk_sim.logic<1>>
      %arbitrated_net = obelisk_sim.context.net %ctx[2] :
          !obelisk_sim.net<!obelisk_sim.logic<1>>
      %process = obelisk_sim.spawn @initial(
          %ctx, %positive_driver, %negative_driver, %arbitrated_driver,
          %positive_net, %negative_net, %arbitrated_net) :
          !obelisk_sim.context, !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @initial(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %positive_driver: !obelisk_sim.driver<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %negative_driver: !obelisk_sim.driver<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 1 : i64},
        %arbitrated_driver: !obelisk_sim.driver<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 2 : i64},
        %positive_net: !obelisk_sim.net<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 4 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %negative_net: !obelisk_sim.net<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 4 : i32,
             obelisk_sim.descriptor_id = 1 : i64},
        %arbitrated_net: !obelisk_sim.net<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 4 : i32,
             obelisk_sim.descriptor_id = 2 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9915001 : i64,
                    obelisk_sim.lowered} {
      %zero = obelisk_sim.logic.constant false, false :
          !obelisk_sim.logic<1>
      %one = obelisk_sim.logic.constant true, false :
          !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %positive_driver = %zero :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %negative_driver = %one :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %arbitrated_driver = %zero :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>
      %settle = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %settle to ^base

    ^base:
      %p0 = obelisk_sim.net.read %positive_net :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %n0 = obelisk_sim.net.read %negative_net :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %a0 = obelisk_sim.net.read %arbitrated_net :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %base_format = obelisk_sim.bytes.constant "base %b%b%b"
      %stdout0 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout0(%base_format, %p0, %n0, %a0)
          newline = true radix = 10 flags = [0, 0, 0, 0] :
          !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>,
          !obelisk_sim.logic<1>

      %one1 = obelisk_sim.logic.constant true, false :
          !obelisk_sim.logic<1>
      %zero1 = obelisk_sim.logic.constant false, false :
          !obelisk_sim.logic<1>
      %positive_rise = obelisk_sim.time.constant 2
      %positive_fall = obelisk_sim.time.constant 3
      %positive_off = obelisk_sim.time.constant 4
      obelisk_sim.driver.drive_inertial %positive_driver = %one1
          after[%positive_rise, %positive_fall, %positive_off]
          site 9915001 : 0 vector = false :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>
      %negative_rise = obelisk_sim.time.constant 5
      %negative_fall = obelisk_sim.time.constant 6
      %negative_off = obelisk_sim.time.constant 7
      // The modeled inverter output falls, so the source rise uses tfall=6.
      obelisk_sim.driver.drive_inertial %negative_driver = %zero1
          after[%negative_rise, %negative_fall, %negative_off]
          site 9915001 : 1 vector = false :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>

      %fast_changed = arith.constant true
      %slow_rise_ticks = arith.constant 9 : i64
      %fast_rise_ticks = arith.constant 3 : i64
      %selected_rise_ticks = arith.select %fast_changed,
          %fast_rise_ticks, %slow_rise_ticks : i64
      %selected_rise = obelisk_sim.time.scale %selected_rise_ticks by 1
          signed = false : i64
      %slow_fall_ticks = arith.constant 10 : i64
      %fast_fall_ticks = arith.constant 4 : i64
      %selected_fall_ticks = arith.select %fast_changed,
          %fast_fall_ticks, %slow_fall_ticks : i64
      %selected_fall = obelisk_sim.time.scale %selected_fall_ticks by 1
          signed = false : i64
      %slow_off_ticks = arith.constant 11 : i64
      %fast_off_ticks = arith.constant 5 : i64
      %selected_off_ticks = arith.select %fast_changed,
          %fast_off_ticks, %slow_off_ticks : i64
      %selected_off = obelisk_sim.time.scale %selected_off_ticks by 1
          signed = false : i64
      obelisk_sim.driver.drive_inertial %arbitrated_driver = %one1
          after[%selected_rise, %selected_fall, %selected_off]
          site 9915001 : 2 vector = false :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>
      %one_tick = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %one_tick to ^early

    ^early:
      %p1 = obelisk_sim.net.read %positive_net :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %n1 = obelisk_sim.net.read %negative_net :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %a1 = obelisk_sim.net.read %arbitrated_net :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %early_format = obelisk_sim.bytes.constant "early %b%b%b"
      %stdout1 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout1(%early_format, %p1, %n1, %a1)
          newline = true radix = 10 flags = [0, 0, 0, 0] :
          !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>,
          !obelisk_sim.logic<1>
      %three_ticks = obelisk_sim.time.constant 3
      obelisk_sim.suspend.delay %three_ticks to ^simultaneous

    ^simultaneous:
      %p2 = obelisk_sim.net.read %positive_net :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %n2 = obelisk_sim.net.read %negative_net :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %a2 = obelisk_sim.net.read %arbitrated_net :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %simultaneous_format = obelisk_sim.bytes.constant "simultaneous %b%b%b"
      %stdout2 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout2(
          %simultaneous_format, %p2, %n2, %a2)
          newline = true radix = 10 flags = [0, 0, 0, 0] :
          !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>,
          !obelisk_sim.logic<1>
      %three_more = obelisk_sim.time.constant 3
      obelisk_sim.suspend.delay %three_more to ^negative_fall

    ^negative_fall:
      %p3 = obelisk_sim.net.read %positive_net :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %n3 = obelisk_sim.net.read %negative_net :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %a3 = obelisk_sim.net.read %arbitrated_net :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %fall_format = obelisk_sim.bytes.constant "negative-fall %b%b%b"
      %stdout3 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout3(%fall_format, %p3, %n3, %a3)
          newline = true radix = 10 flags = [0, 0, 0, 0] :
          !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>,
          !obelisk_sim.logic<1>

      %z = obelisk_sim.logic.constant true, true : !obelisk_sim.logic<1>
      %one2 = obelisk_sim.logic.constant true, false :
          !obelisk_sim.logic<1>
      %positive_rise2 = obelisk_sim.time.constant 2
      %positive_fall2 = obelisk_sim.time.constant 3
      %positive_off2 = obelisk_sim.time.constant 4
      obelisk_sim.driver.drive_inertial %positive_driver = %z
          after[%positive_rise2, %positive_fall2, %positive_off2]
          site 9915001 : 0 vector = false :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>
      %negative_rise2 = obelisk_sim.time.constant 5
      %negative_fall2 = obelisk_sim.time.constant 6
      %negative_off2 = obelisk_sim.time.constant 7
      // The modeled inverter output rises, so the source fall uses trise=5.
      obelisk_sim.driver.drive_inertial %negative_driver = %one2
          after[%negative_rise2, %negative_fall2, %negative_off2]
          site 9915001 : 1 vector = false :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>

      %fast_unchanged = arith.constant false
      %slow_rise_ticks2 = arith.constant 9 : i64
      %fast_rise_ticks2 = arith.constant 3 : i64
      %selected_rise_ticks2 = arith.select %fast_unchanged,
          %fast_rise_ticks2, %slow_rise_ticks2 : i64
      %selected_rise2 = obelisk_sim.time.scale %selected_rise_ticks2 by 1
          signed = false : i64
      %slow_fall_ticks2 = arith.constant 10 : i64
      %fast_fall_ticks2 = arith.constant 4 : i64
      %selected_fall_ticks2 = arith.select %fast_unchanged,
          %fast_fall_ticks2, %slow_fall_ticks2 : i64
      %selected_fall2 = obelisk_sim.time.scale %selected_fall_ticks2 by 1
          signed = false : i64
      %slow_off_ticks2 = arith.constant 11 : i64
      %fast_off_ticks2 = arith.constant 5 : i64
      %selected_off_ticks2 = arith.select %fast_unchanged,
          %fast_off_ticks2, %slow_off_ticks2 : i64
      %selected_off2 = obelisk_sim.time.scale %selected_off_ticks2 by 1
          signed = false : i64
      %zero2 = obelisk_sim.logic.constant false, false :
          !obelisk_sim.logic<1>
      obelisk_sim.driver.drive_inertial %arbitrated_driver = %zero2
          after[%selected_rise2, %selected_fall2, %selected_off2]
          site 9915001 : 2 vector = false :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>
      %later_three = obelisk_sim.time.constant 3
      obelisk_sim.suspend.delay %later_three to ^later_early

    ^later_early:
      %p4 = obelisk_sim.net.read %positive_net :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %n4 = obelisk_sim.net.read %negative_net :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %a4 = obelisk_sim.net.read %arbitrated_net :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %later_early_format = obelisk_sim.bytes.constant "later-early %b%b%b"
      %stdout4 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout4(
          %later_early_format, %p4, %n4, %a4)
          newline = true radix = 10 flags = [0, 0, 0, 0] :
          !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>,
          !obelisk_sim.logic<1>
      %later_three2 = obelisk_sim.time.constant 3
      obelisk_sim.suspend.delay %later_three2 to ^destination_banks

    ^destination_banks:
      %p5 = obelisk_sim.net.read %positive_net :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %n5 = obelisk_sim.net.read %negative_net :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %a5 = obelisk_sim.net.read %arbitrated_net :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %banks_format = obelisk_sim.bytes.constant "destination-banks %b%b%b"
      %stdout5 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout5(%banks_format, %p5, %n5, %a5)
          newline = true radix = 10 flags = [0, 0, 0, 0] :
          !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>,
          !obelisk_sim.logic<1>
      %later_three3 = obelisk_sim.time.constant 3
      obelisk_sim.suspend.delay %later_three3 to ^slow_path_early

    ^slow_path_early:
      %p6 = obelisk_sim.net.read %positive_net :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %n6 = obelisk_sim.net.read %negative_net :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %a6 = obelisk_sim.net.read %arbitrated_net :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %slow_early_format = obelisk_sim.bytes.constant "slow-path-early %b%b%b"
      %stdout6 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout6(
          %slow_early_format, %p6, %n6, %a6)
          newline = true radix = 10 flags = [0, 0, 0, 0] :
          !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>,
          !obelisk_sim.logic<1>
      %later_two = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %later_two to ^slow_path_done

    ^slow_path_done:
      %p7 = obelisk_sim.net.read %positive_net :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %n7 = obelisk_sim.net.read %negative_net :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %a7 = obelisk_sim.net.read %arbitrated_net :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %done_format = obelisk_sim.bytes.constant "slow-path-done %b%b%b"
      %stdout7 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout7(%done_format, %p7, %n7, %a7)
          newline = true radix = 10 flags = [0, 0, 0, 0] :
          !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>,
          !obelisk_sim.logic<1>
      obelisk_sim.return
    }
  }
}
