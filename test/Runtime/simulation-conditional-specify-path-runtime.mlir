// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
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

// Hand-authored Simulation IR models the straight-line conditional-path actor
// produced by lowering. Conditions are sampled only when `source` changes.
// IEEE 1800-2017 30.4.4.1 treats X and Z path conditions as true;
// simultaneous true predicates select the shorter delay. One inertial site
// rejects a source pulse shorter than its selected delay.
// CHECK: base 0
// CHECK-NEXT: condition-only 0
// CHECK-NEXT: pending-preserved 0
// CHECK-NEXT: if-done 1
// CHECK-NEXT: ifnone-early 1
// CHECK-NEXT: ifnone-done 0
// CHECK-NEXT: overlap-early 0
// CHECK-NEXT: overlap-min 1
// CHECK-NEXT: xz-early 0
// CHECK-NEXT: xz-ifnone 0
// CHECK-NEXT: inertial-reject 0
// CHECK-NEXT: inertial-stable 0

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @conditional_specify_runtime {
    simulation.scope.decl 0 hierarchy "top"
    simulation.net.decl 0 in 0 : !simulation.logic<1> design hierarchy "top.source"
    simulation.net.decl 1 in 0 : !simulation.logic<1> design hierarchy "top.c0"
    simulation.net.decl 2 in 0 : !simulation.logic<1> design hierarchy "top.c1"
    simulation.net.decl 3 in 0 : !simulation.logic<1> design hierarchy "top.output"
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design
    simulation.driver.decl 1 in 0 drives 1 : !simulation.logic<1> design
    simulation.driver.decl 2 in 0 drives 2 : !simulation.logic<1> design
    simulation.driver.decl 3 in 0 drives 3 : !simulation.logic<1> design
    simulation.code_unit.decl 9921000 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 9921001 in 0 continuous hierarchy "top.path"
    simulation.code_unit.decl 9921002 in 0 initial hierarchy "top.test"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9921000 : i64,
                    simulation.lowered} {
      %source_driver = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<1>>
      %c0_driver = simulation.context.driver %ctx[1] :
          !simulation.driver<!simulation.logic<1>>
      %c1_driver = simulation.context.driver %ctx[2] :
          !simulation.driver<!simulation.logic<1>>
      %output_driver = simulation.context.driver %ctx[3] :
          !simulation.driver<!simulation.logic<1>>
      %source = simulation.context.net %ctx[0] :
          !simulation.net<!simulation.logic<1>>
      %c0 = simulation.context.net %ctx[1] :
          !simulation.net<!simulation.logic<1>>
      %c1 = simulation.context.net %ctx[2] :
          !simulation.net<!simulation.logic<1>>
      %output = simulation.context.net %ctx[3] :
          !simulation.net<!simulation.logic<1>>
      %path = simulation.spawn @path(
          %ctx, %source, %c0, %c1, %output_driver) :
          !simulation.context, !simulation.net<!simulation.logic<1>>,
          !simulation.net<!simulation.logic<1>>,
          !simulation.net<!simulation.logic<1>>,
          !simulation.driver<!simulation.logic<1>> -> !simulation.process
      %test = simulation.spawn @test(
          %ctx, %source_driver, %c0_driver, %c1_driver, %output) :
          !simulation.context,
          !simulation.driver<!simulation.logic<1>>,
          !simulation.driver<!simulation.logic<1>>,
          !simulation.driver<!simulation.logic<1>>,
          !simulation.net<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }

    simulation.func private @path(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !simulation.net<!simulation.logic<1>>
            {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64},
        %c0: !simulation.net<!simulation.logic<1>>
            {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 1 : i64},
        %c1: !simulation.net<!simulation.logic<1>>
            {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 2 : i64},
        %output: !simulation.driver<!simulation.logic<1>>
            {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 3 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9921001 : i64,
                    simulation.lowered} {
      cf.br ^evaluate
    ^evaluate:
      %value = simulation.net.read %source :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %c0_value = simulation.net.read %c0 :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %c1_value = simulation.net.read %c1 :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %condition_zero = simulation.logic.constant false, false : !simulation.logic<1>
      %c0_true = simulation.logic.compare case_ne %c0_value, %condition_zero : (!simulation.logic<1>, !simulation.logic<1>) -> i1
      %c1_true = simulation.logic.compare case_ne %c1_value, %condition_zero : (!simulation.logic<1>, !simulation.logic<1>) -> i1
      %any = arith.ori %c0_true, %c1_true : i1
      %true = arith.constant true
      %ifnone = arith.xori %any, %true : i1
      %zero_ticks = arith.constant 0 : i64
      %ifnone_ticks = arith.constant 7 : i64
      %ifnone_selected = arith.select %ifnone, %ifnone_ticks, %zero_ticks : i64
      %slow_ticks = arith.constant 5 : i64
      %slow_selected = arith.select %c0_true, %slow_ticks, %ifnone_selected : i64
      %fast_ticks = arith.constant 2 : i64
      %ticks = arith.select %c1_true, %fast_ticks, %slow_selected : i64
      %delay = simulation.time.scale %ticks by 1 signed = false : i64
      simulation.driver.drive_inertial %output = %value
          after[%delay, %delay, %delay] site 9921001 : 0 vector = false :
          !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.suspend.change %source to ^evaluate :
          !simulation.net<!simulation.logic<1>>
    }

    simulation.func private @test(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !simulation.driver<!simulation.logic<1>>
            {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64},
        %c0: !simulation.driver<!simulation.logic<1>>
            {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 1 : i64},
        %c1: !simulation.driver<!simulation.logic<1>>
            {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 2 : i64},
        %output: !simulation.net<!simulation.logic<1>>
            {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 3 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9921002 : i64,
                    simulation.lowered} {
      %zero = simulation.logic.constant false, false : !simulation.logic<1>
      %one = simulation.logic.constant true, false : !simulation.logic<1>
      simulation.driver.drive %source = %zero :
          !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.driver.drive %c0 = %zero :
          !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.driver.drive %c1 = %zero :
          !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %eight = simulation.time.constant 8
      simulation.suspend.delay %eight to ^base
    ^base:
      %base_v = simulation.net.read %output : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %base_f = simulation.bytes.constant "base %b"
      %stdout0 = arith.constant 1 : i32
      simulation.display %ctx to %stdout0(%base_f, %base_v) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !simulation.logic<1>
      %one0 = simulation.logic.constant true, false : !simulation.logic<1>
      simulation.driver.drive %c0 = %one0 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %one_tick = simulation.time.constant 1
      simulation.suspend.delay %one_tick to ^condition_only
    ^condition_only:
      %co_v = simulation.net.read %output : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %co_f = simulation.bytes.constant "condition-only %b"
      %stdout1 = arith.constant 1 : i32
      simulation.display %ctx to %stdout1(%co_f, %co_v) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !simulation.logic<1>
      %one1 = simulation.logic.constant true, false : !simulation.logic<1>
      simulation.driver.drive %source = %one1 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %one_wait = simulation.time.constant 1
      simulation.suspend.delay %one_wait to ^clear_condition
    ^clear_condition:
      %zero0 = simulation.logic.constant false, false : !simulation.logic<1>
      simulation.driver.drive %c0 = %zero0 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %three = simulation.time.constant 3
      simulation.suspend.delay %three to ^pending
    ^pending:
      %pending_v = simulation.net.read %output : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %pending_f = simulation.bytes.constant "pending-preserved %b"
      %stdout2 = arith.constant 1 : i32
      simulation.display %ctx to %stdout2(%pending_f, %pending_v) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !simulation.logic<1>
      %one_wait2 = simulation.time.constant 1
      simulation.suspend.delay %one_wait2 to ^if_done
    ^if_done:
      %if_v = simulation.net.read %output : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %if_f = simulation.bytes.constant "if-done %b"
      %stdout3 = arith.constant 1 : i32
      simulation.display %ctx to %stdout3(%if_f, %if_v) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !simulation.logic<1>
      %zero1 = simulation.logic.constant false, false : !simulation.logic<1>
      simulation.driver.drive %source = %zero1 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %six = simulation.time.constant 6
      simulation.suspend.delay %six to ^ifnone_early
    ^ifnone_early:
      %ine_v = simulation.net.read %output : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %ine_f = simulation.bytes.constant "ifnone-early %b"
      %stdout4 = arith.constant 1 : i32
      simulation.display %ctx to %stdout4(%ine_f, %ine_v) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !simulation.logic<1>
      %one_wait3 = simulation.time.constant 1
      simulation.suspend.delay %one_wait3 to ^ifnone_done
    ^ifnone_done:
      %ind_v = simulation.net.read %output : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %ind_f = simulation.bytes.constant "ifnone-done %b"
      %stdout5 = arith.constant 1 : i32
      simulation.display %ctx to %stdout5(%ind_f, %ind_v) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !simulation.logic<1>
      %one2 = simulation.logic.constant true, false : !simulation.logic<1>
      simulation.driver.drive %c0 = %one2 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.driver.drive %c1 = %one2 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.driver.drive %source = %one2 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %one_wait4 = simulation.time.constant 1
      simulation.suspend.delay %one_wait4 to ^overlap_early
    ^overlap_early:
      %oe_v = simulation.net.read %output : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %oe_f = simulation.bytes.constant "overlap-early %b"
      %stdout6 = arith.constant 1 : i32
      simulation.display %ctx to %stdout6(%oe_f, %oe_v) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !simulation.logic<1>
      %one_wait5 = simulation.time.constant 1
      simulation.suspend.delay %one_wait5 to ^overlap_done
    ^overlap_done:
      %od_v = simulation.net.read %output : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %od_f = simulation.bytes.constant "overlap-min %b"
      %stdout7 = arith.constant 1 : i32
      simulation.display %ctx to %stdout7(%od_f, %od_v) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !simulation.logic<1>
      %x = simulation.logic.constant false, true : !simulation.logic<1>
      %z = simulation.logic.constant true, true : !simulation.logic<1>
      %zero2 = simulation.logic.constant false, false : !simulation.logic<1>
      simulation.driver.drive %c0 = %x : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.driver.drive %c1 = %z : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.driver.drive %source = %zero2 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %six2 = simulation.time.constant 6
      simulation.suspend.delay %six2 to ^xz_early
    ^xz_early:
      %xz_e_v = simulation.net.read %output : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %xz_e_f = simulation.bytes.constant "xz-early %b"
      %stdout8 = arith.constant 1 : i32
      simulation.display %ctx to %stdout8(%xz_e_f, %xz_e_v) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !simulation.logic<1>
      %one_wait6 = simulation.time.constant 1
      simulation.suspend.delay %one_wait6 to ^xz_done
    ^xz_done:
      %xz_d_v = simulation.net.read %output : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %xz_d_f = simulation.bytes.constant "xz-ifnone %b"
      %stdout9 = arith.constant 1 : i32
      simulation.display %ctx to %stdout9(%xz_d_f, %xz_d_v) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !simulation.logic<1>
      %zero3 = simulation.logic.constant false, false : !simulation.logic<1>
      %one3 = simulation.logic.constant true, false : !simulation.logic<1>
      simulation.driver.drive %c0 = %zero3 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.driver.drive %c1 = %one3 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.driver.drive %source = %one3 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %one_wait7 = simulation.time.constant 1
      simulation.suspend.delay %one_wait7 to ^reject_fall
    ^reject_fall:
      %zero4 = simulation.logic.constant false, false : !simulation.logic<1>
      simulation.driver.drive %source = %zero4 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %one_wait8 = simulation.time.constant 1
      simulation.suspend.delay %one_wait8 to ^reject_check
    ^reject_check:
      %r_v = simulation.net.read %output : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %r_f = simulation.bytes.constant "inertial-reject %b"
      %stdout10 = arith.constant 1 : i32
      simulation.display %ctx to %stdout10(%r_f, %r_v) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !simulation.logic<1>
      %two_wait = simulation.time.constant 2
      simulation.suspend.delay %two_wait to ^stable
    ^stable:
      %s_v = simulation.net.read %output : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %s_f = simulation.bytes.constant "inertial-stable %b"
      %stdout11 = arith.constant 1 : i32
      simulation.display %ctx to %stdout11(%s_f, %s_v) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !simulation.logic<1>
      %status = arith.constant 0 : i32
      simulation.finish %ctx, %status
      simulation.return
    }
  }
}
