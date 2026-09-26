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
  obelisk_sim.design @conditional_specify_runtime {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design hierarchy "top.source"
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design hierarchy "top.c0"
    obelisk_sim.net.decl 2 in 0 : !obelisk_sim.logic<1> design hierarchy "top.c1"
    obelisk_sim.net.decl 3 in 0 : !obelisk_sim.logic<1> design hierarchy "top.output"
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 1 in 0 drives 1 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 2 in 0 drives 2 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 3 in 0 drives 3 : !obelisk_sim.logic<1> design
    obelisk_sim.code_unit.decl 9921000 in 0 root_initializer hierarchy "top.root"
    obelisk_sim.code_unit.decl 9921001 in 0 continuous hierarchy "top.path"
    obelisk_sim.code_unit.decl 9921002 in 0 initial hierarchy "top.test"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9921000 : i64,
                    obelisk_sim.lowered} {
      %source_driver = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %c0_driver = obelisk_sim.context.driver %ctx[1] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %c1_driver = obelisk_sim.context.driver %ctx[2] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %output_driver = obelisk_sim.context.driver %ctx[3] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %source = obelisk_sim.context.net %ctx[0] :
          !obelisk_sim.net<!obelisk_sim.logic<1>>
      %c0 = obelisk_sim.context.net %ctx[1] :
          !obelisk_sim.net<!obelisk_sim.logic<1>>
      %c1 = obelisk_sim.context.net %ctx[2] :
          !obelisk_sim.net<!obelisk_sim.logic<1>>
      %output = obelisk_sim.context.net %ctx[3] :
          !obelisk_sim.net<!obelisk_sim.logic<1>>
      %path = obelisk_sim.spawn @path(
          %ctx, %source, %c0, %c1, %output_driver) :
          !obelisk_sim.context, !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.driver<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %test = obelisk_sim.spawn @test(
          %ctx, %source_driver, %c0_driver, %c1_driver, %output) :
          !obelisk_sim.context,
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @path(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %source: !obelisk_sim.net<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %c0: !obelisk_sim.net<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 1 : i64},
        %c1: !obelisk_sim.net<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 2 : i64},
        %output: !obelisk_sim.driver<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 3 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9921001 : i64,
                    obelisk_sim.lowered} {
      cf.br ^evaluate
    ^evaluate:
      %value = obelisk_sim.net.read %source :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %c0_value = obelisk_sim.net.read %c0 :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %c1_value = obelisk_sim.net.read %c1 :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %condition_zero = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      %c0_true = obelisk_sim.logic.compare case_ne %c0_value, %condition_zero : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
      %c1_true = obelisk_sim.logic.compare case_ne %c1_value, %condition_zero : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> i1
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
      %delay = obelisk_sim.time.scale %ticks by 1 signed = false : i64
      obelisk_sim.driver.drive_inertial %output = %value
          after[%delay, %delay, %delay] site 9921001 : 0 vector = false :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.suspend.change %source to ^evaluate :
          !obelisk_sim.net<!obelisk_sim.logic<1>>
    }

    obelisk_sim.func private @test(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %source: !obelisk_sim.driver<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %c0: !obelisk_sim.driver<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 1 : i64},
        %c1: !obelisk_sim.driver<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 2 : i64},
        %output: !obelisk_sim.net<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 3 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9921002 : i64,
                    obelisk_sim.lowered} {
      %zero = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      %one = obelisk_sim.logic.constant true, false : !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %source = %zero :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %c0 = %zero :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %c1 = %zero :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      %eight = obelisk_sim.time.constant 8
      obelisk_sim.suspend.delay %eight to ^base
    ^base:
      %base_v = obelisk_sim.net.read %output : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %base_f = obelisk_sim.bytes.constant "base %b"
      %stdout0 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout0(%base_f, %base_v) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<1>
      %one0 = obelisk_sim.logic.constant true, false : !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %c0 = %one0 : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      %one_tick = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %one_tick to ^condition_only
    ^condition_only:
      %co_v = obelisk_sim.net.read %output : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %co_f = obelisk_sim.bytes.constant "condition-only %b"
      %stdout1 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout1(%co_f, %co_v) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<1>
      %one1 = obelisk_sim.logic.constant true, false : !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %source = %one1 : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      %one_wait = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %one_wait to ^clear_condition
    ^clear_condition:
      %zero0 = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %c0 = %zero0 : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      %three = obelisk_sim.time.constant 3
      obelisk_sim.suspend.delay %three to ^pending
    ^pending:
      %pending_v = obelisk_sim.net.read %output : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %pending_f = obelisk_sim.bytes.constant "pending-preserved %b"
      %stdout2 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout2(%pending_f, %pending_v) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<1>
      %one_wait2 = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %one_wait2 to ^if_done
    ^if_done:
      %if_v = obelisk_sim.net.read %output : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %if_f = obelisk_sim.bytes.constant "if-done %b"
      %stdout3 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout3(%if_f, %if_v) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<1>
      %zero1 = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %source = %zero1 : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      %six = obelisk_sim.time.constant 6
      obelisk_sim.suspend.delay %six to ^ifnone_early
    ^ifnone_early:
      %ine_v = obelisk_sim.net.read %output : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %ine_f = obelisk_sim.bytes.constant "ifnone-early %b"
      %stdout4 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout4(%ine_f, %ine_v) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<1>
      %one_wait3 = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %one_wait3 to ^ifnone_done
    ^ifnone_done:
      %ind_v = obelisk_sim.net.read %output : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %ind_f = obelisk_sim.bytes.constant "ifnone-done %b"
      %stdout5 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout5(%ind_f, %ind_v) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<1>
      %one2 = obelisk_sim.logic.constant true, false : !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %c0 = %one2 : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %c1 = %one2 : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %source = %one2 : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      %one_wait4 = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %one_wait4 to ^overlap_early
    ^overlap_early:
      %oe_v = obelisk_sim.net.read %output : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %oe_f = obelisk_sim.bytes.constant "overlap-early %b"
      %stdout6 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout6(%oe_f, %oe_v) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<1>
      %one_wait5 = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %one_wait5 to ^overlap_done
    ^overlap_done:
      %od_v = obelisk_sim.net.read %output : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %od_f = obelisk_sim.bytes.constant "overlap-min %b"
      %stdout7 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout7(%od_f, %od_v) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<1>
      %x = obelisk_sim.logic.constant false, true : !obelisk_sim.logic<1>
      %z = obelisk_sim.logic.constant true, true : !obelisk_sim.logic<1>
      %zero2 = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %c0 = %x : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %c1 = %z : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %source = %zero2 : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      %six2 = obelisk_sim.time.constant 6
      obelisk_sim.suspend.delay %six2 to ^xz_early
    ^xz_early:
      %xz_e_v = obelisk_sim.net.read %output : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %xz_e_f = obelisk_sim.bytes.constant "xz-early %b"
      %stdout8 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout8(%xz_e_f, %xz_e_v) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<1>
      %one_wait6 = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %one_wait6 to ^xz_done
    ^xz_done:
      %xz_d_v = obelisk_sim.net.read %output : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %xz_d_f = obelisk_sim.bytes.constant "xz-ifnone %b"
      %stdout9 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout9(%xz_d_f, %xz_d_v) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<1>
      %zero3 = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      %one3 = obelisk_sim.logic.constant true, false : !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %c0 = %zero3 : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %c1 = %one3 : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %source = %one3 : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      %one_wait7 = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %one_wait7 to ^reject_fall
    ^reject_fall:
      %zero4 = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %source = %zero4 : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      %one_wait8 = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %one_wait8 to ^reject_check
    ^reject_check:
      %r_v = obelisk_sim.net.read %output : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %r_f = obelisk_sim.bytes.constant "inertial-reject %b"
      %stdout10 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout10(%r_f, %r_v) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<1>
      %two_wait = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %two_wait to ^stable
    ^stable:
      %s_v = obelisk_sim.net.read %output : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %s_f = obelisk_sim.bytes.constant "inertial-stable %b"
      %stdout11 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout11(%s_f, %s_v) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<1>
      %status = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %status
      obelisk_sim.return
    }
  }
}
