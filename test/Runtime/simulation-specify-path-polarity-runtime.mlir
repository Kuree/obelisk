// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir > %t.ll
// RUN: %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O0>' %t.ll \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.o0.o
// RUN: %llvm_dist/bin/clang++ %t.o0.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.o0.exe
// RUN: %t.o0.exe --execution-tier=native | FileCheck %s
// RUN: %t.o0.exe --execution-tier=bytecode | FileCheck %s
// RUN: %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' %t.ll \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.o3.o
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
  simulation.design @specify_path_polarity_runtime {
    simulation.scope.decl 0 hierarchy "specify_path_polarity_runtime"
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 1 in 0 : !simulation.logic<1> design
    simulation.net.decl 2 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design
    simulation.driver.decl 1 in 0 drives 1 : !simulation.logic<1> design
    simulation.driver.decl 2 in 0 drives 2 : !simulation.logic<1> design
    simulation.code_unit.decl 9915000 in 0 root_initializer
        hierarchy "specify_path_polarity_runtime.root"
    simulation.code_unit.decl 9915001 in 0 initial
        hierarchy "specify_path_polarity_runtime.initial"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9915000 : i64,
                    simulation.lowered} {
      %positive_driver = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<1>>
      %negative_driver = simulation.context.driver %ctx[1] :
          !simulation.driver<!simulation.logic<1>>
      %arbitrated_driver = simulation.context.driver %ctx[2] :
          !simulation.driver<!simulation.logic<1>>
      %positive_net = simulation.context.net %ctx[0] :
          !simulation.net<!simulation.logic<1>>
      %negative_net = simulation.context.net %ctx[1] :
          !simulation.net<!simulation.logic<1>>
      %arbitrated_net = simulation.context.net %ctx[2] :
          !simulation.net<!simulation.logic<1>>
      %process = simulation.spawn @initial(
          %ctx, %positive_driver, %negative_driver, %arbitrated_driver,
          %positive_net, %negative_net, %arbitrated_net) :
          !simulation.context, !simulation.driver<!simulation.logic<1>>,
          !simulation.driver<!simulation.logic<1>>,
          !simulation.driver<!simulation.logic<1>>,
          !simulation.net<!simulation.logic<1>>,
          !simulation.net<!simulation.logic<1>>,
          !simulation.net<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }

    simulation.func private @initial(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %positive_driver: !simulation.driver<!simulation.logic<1>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 0 : i64},
        %negative_driver: !simulation.driver<!simulation.logic<1>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 1 : i64},
        %arbitrated_driver: !simulation.driver<!simulation.logic<1>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 2 : i64},
        %positive_net: !simulation.net<!simulation.logic<1>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 0 : i64},
        %negative_net: !simulation.net<!simulation.logic<1>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 1 : i64},
        %arbitrated_net: !simulation.net<!simulation.logic<1>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9915001 : i64,
                    simulation.lowered} {
      %zero = simulation.logic.constant false, false :
          !simulation.logic<1>
      %one = simulation.logic.constant true, false :
          !simulation.logic<1>
      simulation.driver.drive %positive_driver = %zero :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>
      simulation.driver.drive %negative_driver = %one :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>
      simulation.driver.drive %arbitrated_driver = %zero :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>
      %settle = simulation.time.constant 1
      simulation.suspend.delay %settle to ^base

    ^base:
      %p0 = simulation.net.read %positive_net :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %n0 = simulation.net.read %negative_net :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %a0 = simulation.net.read %arbitrated_net :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %base_format = simulation.bytes.constant "base %b%b%b"
      %stdout0 = arith.constant 1 : i32
      simulation.display %ctx to %stdout0(%base_format, %p0, %n0, %a0)
          newline = true radix = <decimal> flags = [0, 0, 0, 0] :
          !simulation.bytes, !simulation.logic<1>, !simulation.logic<1>,
          !simulation.logic<1>

      %one1 = simulation.logic.constant true, false :
          !simulation.logic<1>
      %zero1 = simulation.logic.constant false, false :
          !simulation.logic<1>
      %positive_rise = simulation.time.constant 2
      %positive_fall = simulation.time.constant 3
      %positive_off = simulation.time.constant 4
      simulation.driver.drive_inertial %positive_driver = %one1
          after[%positive_rise, %positive_fall, %positive_off]
          site 9915001 : 0 vector = false :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>
      %negative_rise = simulation.time.constant 5
      %negative_fall = simulation.time.constant 6
      %negative_off = simulation.time.constant 7
      // The modeled inverter output falls, so the source rise uses tfall=6.
      simulation.driver.drive_inertial %negative_driver = %zero1
          after[%negative_rise, %negative_fall, %negative_off]
          site 9915001 : 1 vector = false :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>

      %fast_changed = arith.constant true
      %slow_rise_ticks = arith.constant 9 : i64
      %fast_rise_ticks = arith.constant 3 : i64
      %selected_rise_ticks = arith.select %fast_changed,
          %fast_rise_ticks, %slow_rise_ticks : i64
      %selected_rise = simulation.time.scale %selected_rise_ticks by 1
          signed = false : i64
      %slow_fall_ticks = arith.constant 10 : i64
      %fast_fall_ticks = arith.constant 4 : i64
      %selected_fall_ticks = arith.select %fast_changed,
          %fast_fall_ticks, %slow_fall_ticks : i64
      %selected_fall = simulation.time.scale %selected_fall_ticks by 1
          signed = false : i64
      %slow_off_ticks = arith.constant 11 : i64
      %fast_off_ticks = arith.constant 5 : i64
      %selected_off_ticks = arith.select %fast_changed,
          %fast_off_ticks, %slow_off_ticks : i64
      %selected_off = simulation.time.scale %selected_off_ticks by 1
          signed = false : i64
      simulation.driver.drive_inertial %arbitrated_driver = %one1
          after[%selected_rise, %selected_fall, %selected_off]
          site 9915001 : 2 vector = false :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>
      %one_tick = simulation.time.constant 1
      simulation.suspend.delay %one_tick to ^early

    ^early:
      %p1 = simulation.net.read %positive_net :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %n1 = simulation.net.read %negative_net :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %a1 = simulation.net.read %arbitrated_net :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %early_format = simulation.bytes.constant "early %b%b%b"
      %stdout1 = arith.constant 1 : i32
      simulation.display %ctx to %stdout1(%early_format, %p1, %n1, %a1)
          newline = true radix = <decimal> flags = [0, 0, 0, 0] :
          !simulation.bytes, !simulation.logic<1>, !simulation.logic<1>,
          !simulation.logic<1>
      %three_ticks = simulation.time.constant 3
      simulation.suspend.delay %three_ticks to ^simultaneous

    ^simultaneous:
      %p2 = simulation.net.read %positive_net :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %n2 = simulation.net.read %negative_net :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %a2 = simulation.net.read %arbitrated_net :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %simultaneous_format = simulation.bytes.constant "simultaneous %b%b%b"
      %stdout2 = arith.constant 1 : i32
      simulation.display %ctx to %stdout2(
          %simultaneous_format, %p2, %n2, %a2)
          newline = true radix = <decimal> flags = [0, 0, 0, 0] :
          !simulation.bytes, !simulation.logic<1>, !simulation.logic<1>,
          !simulation.logic<1>
      %three_more = simulation.time.constant 3
      simulation.suspend.delay %three_more to ^negative_fall

    ^negative_fall:
      %p3 = simulation.net.read %positive_net :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %n3 = simulation.net.read %negative_net :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %a3 = simulation.net.read %arbitrated_net :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %fall_format = simulation.bytes.constant "negative-fall %b%b%b"
      %stdout3 = arith.constant 1 : i32
      simulation.display %ctx to %stdout3(%fall_format, %p3, %n3, %a3)
          newline = true radix = <decimal> flags = [0, 0, 0, 0] :
          !simulation.bytes, !simulation.logic<1>, !simulation.logic<1>,
          !simulation.logic<1>

      %z = simulation.logic.constant true, true : !simulation.logic<1>
      %one2 = simulation.logic.constant true, false :
          !simulation.logic<1>
      %positive_rise2 = simulation.time.constant 2
      %positive_fall2 = simulation.time.constant 3
      %positive_off2 = simulation.time.constant 4
      simulation.driver.drive_inertial %positive_driver = %z
          after[%positive_rise2, %positive_fall2, %positive_off2]
          site 9915001 : 0 vector = false :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>
      %negative_rise2 = simulation.time.constant 5
      %negative_fall2 = simulation.time.constant 6
      %negative_off2 = simulation.time.constant 7
      // The modeled inverter output rises, so the source fall uses trise=5.
      simulation.driver.drive_inertial %negative_driver = %one2
          after[%negative_rise2, %negative_fall2, %negative_off2]
          site 9915001 : 1 vector = false :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>

      %fast_unchanged = arith.constant false
      %slow_rise_ticks2 = arith.constant 9 : i64
      %fast_rise_ticks2 = arith.constant 3 : i64
      %selected_rise_ticks2 = arith.select %fast_unchanged,
          %fast_rise_ticks2, %slow_rise_ticks2 : i64
      %selected_rise2 = simulation.time.scale %selected_rise_ticks2 by 1
          signed = false : i64
      %slow_fall_ticks2 = arith.constant 10 : i64
      %fast_fall_ticks2 = arith.constant 4 : i64
      %selected_fall_ticks2 = arith.select %fast_unchanged,
          %fast_fall_ticks2, %slow_fall_ticks2 : i64
      %selected_fall2 = simulation.time.scale %selected_fall_ticks2 by 1
          signed = false : i64
      %slow_off_ticks2 = arith.constant 11 : i64
      %fast_off_ticks2 = arith.constant 5 : i64
      %selected_off_ticks2 = arith.select %fast_unchanged,
          %fast_off_ticks2, %slow_off_ticks2 : i64
      %selected_off2 = simulation.time.scale %selected_off_ticks2 by 1
          signed = false : i64
      %zero2 = simulation.logic.constant false, false :
          !simulation.logic<1>
      simulation.driver.drive_inertial %arbitrated_driver = %zero2
          after[%selected_rise2, %selected_fall2, %selected_off2]
          site 9915001 : 2 vector = false :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>
      %later_three = simulation.time.constant 3
      simulation.suspend.delay %later_three to ^later_early

    ^later_early:
      %p4 = simulation.net.read %positive_net :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %n4 = simulation.net.read %negative_net :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %a4 = simulation.net.read %arbitrated_net :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %later_early_format = simulation.bytes.constant "later-early %b%b%b"
      %stdout4 = arith.constant 1 : i32
      simulation.display %ctx to %stdout4(
          %later_early_format, %p4, %n4, %a4)
          newline = true radix = <decimal> flags = [0, 0, 0, 0] :
          !simulation.bytes, !simulation.logic<1>, !simulation.logic<1>,
          !simulation.logic<1>
      %later_three2 = simulation.time.constant 3
      simulation.suspend.delay %later_three2 to ^destination_banks

    ^destination_banks:
      %p5 = simulation.net.read %positive_net :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %n5 = simulation.net.read %negative_net :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %a5 = simulation.net.read %arbitrated_net :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %banks_format = simulation.bytes.constant "destination-banks %b%b%b"
      %stdout5 = arith.constant 1 : i32
      simulation.display %ctx to %stdout5(%banks_format, %p5, %n5, %a5)
          newline = true radix = <decimal> flags = [0, 0, 0, 0] :
          !simulation.bytes, !simulation.logic<1>, !simulation.logic<1>,
          !simulation.logic<1>
      %later_three3 = simulation.time.constant 3
      simulation.suspend.delay %later_three3 to ^slow_path_early

    ^slow_path_early:
      %p6 = simulation.net.read %positive_net :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %n6 = simulation.net.read %negative_net :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %a6 = simulation.net.read %arbitrated_net :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %slow_early_format = simulation.bytes.constant "slow-path-early %b%b%b"
      %stdout6 = arith.constant 1 : i32
      simulation.display %ctx to %stdout6(
          %slow_early_format, %p6, %n6, %a6)
          newline = true radix = <decimal> flags = [0, 0, 0, 0] :
          !simulation.bytes, !simulation.logic<1>, !simulation.logic<1>,
          !simulation.logic<1>
      %later_two = simulation.time.constant 2
      simulation.suspend.delay %later_two to ^slow_path_done

    ^slow_path_done:
      %p7 = simulation.net.read %positive_net :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %n7 = simulation.net.read %negative_net :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %a7 = simulation.net.read %arbitrated_net :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %done_format = simulation.bytes.constant "slow-path-done %b%b%b"
      %stdout7 = arith.constant 1 : i32
      simulation.display %ctx to %stdout7(%done_format, %p7, %n7, %a7)
          newline = true radix = <decimal> flags = [0, 0, 0, 0] :
          !simulation.bytes, !simulation.logic<1>, !simulation.logic<1>,
          !simulation.logic<1>
      simulation.return
    }
  }
}
