// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// IEEE 1800-2017 20.16. The legacy stochastic queue manager is shared design
// state, preserves four-state job/inform identifiers, implements FIFO and
// LIFO ordering, and derives its six statistics from scheduler time. A scale
// of two ticks per calling-scope unit also checks round-to-nearest conversion.
// CHECK: status 1 1 1 1 1 1 1 1 1
// CHECK-NEXT: fill 1 1 1 1 1 1 1 1 1 1 1 1 1 1
// CHECK-NEXT: fifo 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1
// CHECK-NEXT: lifo 1 1 1 1 1 1 1 1 1

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @stochastic_queue_runtime {
    obelisk_sim.scope.decl 0 hierarchy "stochastic_queue_runtime"
    obelisk_sim.code_unit.decl 9961000 in 0 root_initializer
        hierarchy "stochastic_queue_runtime.root"
    obelisk_sim.code_unit.decl 9961001 in 0 initial
        hierarchy "stochastic_queue_runtime.initial"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9961000 : i64} {
      %process = obelisk_sim.spawn @initial(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @initial(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9961001 : i64} {
      %zero32 = obelisk_sim.logic.constant 0 : i32, 0 : i32 :
          !obelisk_sim.logic<32>
      %one32 = obelisk_sim.logic.constant 1 : i32, 0 : i32 :
          !obelisk_sim.logic<32>
      %two32 = obelisk_sim.logic.constant 2 : i32, 0 : i32 :
          !obelisk_sim.logic<32>
      %three32 = obelisk_sim.logic.constant 3 : i32, 0 : i32 :
          !obelisk_sim.logic<32>
      %seven32 = obelisk_sim.logic.constant 7 : i32, 0 : i32 :
          !obelisk_sim.logic<32>
      %unknown32 = obelisk_sim.logic.constant -1 : i32, -1 : i32 :
          !obelisk_sim.logic<32>
      %unknown64 = obelisk_sim.logic.constant 0 : i64, -1 : i64 :
          !obelisk_sim.logic<64>
      %zero_i32 = arith.constant 0 : i32
      %one_i32 = arith.constant 1 : i32
      %two_i32 = arith.constant 2 : i32
      %three_i32 = arith.constant 3 : i32
      %four_i32 = arith.constant 4 : i32
      %five_i32 = arith.constant 5 : i32
      %six_i32 = arith.constant 6 : i32
      %eight_i32 = arith.constant 8 : i32
      %ten_i32 = arith.constant 10 : i32

      %u0, %u1, %undefined = obelisk_sim.stochastic_queue
          %ctx, %unknown32, %one32, %zero32
          {action = 1 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %bad_type0, %bad_type1, %bad_type = obelisk_sim.stochastic_queue
          %ctx, %one32, %three32, %two32
          {action = 0 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %bad_length0, %bad_length1, %bad_length =
          obelisk_sim.stochastic_queue %ctx, %one32, %one32, %zero32
          {action = 0 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %init0, %init1, %initialized = obelisk_sim.stochastic_queue
          %ctx, %one32, %one32, %two32
          {action = 0 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %dup0, %dup1, %duplicate = obelisk_sim.stochastic_queue
          %ctx, %one32, %one32, %two32
          {action = 0 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %invalid_stat, %invalid_stat1, %invalid_stat_status =
          obelisk_sim.stochastic_queue %ctx, %one32, %seven32, %zero32
          {action = 4 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %no_mean0, %no_mean1, %no_mean = obelisk_sim.stochastic_queue
          %ctx, %one32, %two32, %zero32
          {action = 4 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %empty0, %empty1, %empty = obelisk_sim.stochastic_queue
          %ctx, %one32, %zero32, %zero32
          {action = 2 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)

      %undefined_ok = arith.cmpi eq, %undefined, %two_i32 : i32
      %bad_type_ok = arith.cmpi eq, %bad_type, %four_i32 : i32
      %bad_length_ok = arith.cmpi eq, %bad_length, %five_i32 : i32
      %initialized_ok = arith.cmpi eq, %initialized, %zero_i32 : i32
      %duplicate_ok = arith.cmpi eq, %duplicate, %six_i32 : i32
      %invalid_stat_ok = arith.cmpi eq, %invalid_stat_status, %eight_i32 : i32
      %no_mean_ok = arith.cmpi eq, %no_mean, %ten_i32 : i32
      %empty_ok = arith.cmpi eq, %empty, %three_i32 : i32
      %empty_value_ok = obelisk_sim.logic.compare case_eq %empty0, %unknown64 :
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>) -> i1
      %status_format = obelisk_sim.bytes.constant
          "status %0d %0d %0d %0d %0d %0d %0d %0d %0d"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(
          %status_format, %undefined_ok, %bad_type_ok, %bad_length_ok,
          %initialized_ok, %duplicate_ok, %invalid_stat_ok, %no_mean_ok,
          %empty_ok, %empty_value_ok)
          newline = true radix = 10 flags = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0] :
          !obelisk_sim.bytes, i1, i1, i1, i1, i1, i1, i1, i1, i1

      %job_a = obelisk_sim.logic.constant -1515913215 : i32,
          15728880 : i32 : !obelisk_sim.logic<32>
      %info_a = obelisk_sim.logic.constant 1515847682 : i32,
          -268435216 : i32 : !obelisk_sim.logic<32>
      %job_b = obelisk_sim.logic.constant 305419896 : i32, 0 : i32 :
          !obelisk_sim.logic<32>
      %info_b = obelisk_sim.logic.constant -1698898192 : i32, 0 : i32 :
          !obelisk_sim.logic<32>
      %add10, %add11, %add1 = obelisk_sim.stochastic_queue
          %ctx, %one32, %job_a, %info_a
          {action = 1 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %not_full_value, %not_full1, %not_full = obelisk_sim.stochastic_queue
          %ctx, %one32, %zero32, %zero32
          {action = 3 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %add1_ok = arith.cmpi eq, %add1, %zero_i32 : i32
      %not_full_ok = arith.cmpi eq, %not_full, %zero_i32 : i32
      %zero64 = obelisk_sim.logic.constant 0 : i64, 0 : i64 :
          !obelisk_sim.logic<64>
      %not_full_value_ok = obelisk_sim.logic.compare case_eq
          %not_full_value, %zero64 :
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>) -> i1
      %delay5 = obelisk_sim.time.constant 5
      obelisk_sim.suspend.delay %delay5 to ^after5(
          %add1_ok, %not_full_ok, %not_full_value_ok : i1, i1, i1)

    ^after5(%add1_ok_5: i1, %not_full_ok_5: i1, %not_full_value_ok_5: i1):
      %zero32_5 = obelisk_sim.logic.constant 0 : i32, 0 : i32 :
          !obelisk_sim.logic<32>
      %one32_5 = obelisk_sim.logic.constant 1 : i32, 0 : i32 :
          !obelisk_sim.logic<32>
      %two32_5 = obelisk_sim.logic.constant 2 : i32, 0 : i32 :
          !obelisk_sim.logic<32>
      %three32_5 = obelisk_sim.logic.constant 3 : i32, 0 : i32 :
          !obelisk_sim.logic<32>
      %job_b_5 = obelisk_sim.logic.constant 305419896 : i32, 0 : i32 :
          !obelisk_sim.logic<32>
      %info_b_5 = obelisk_sim.logic.constant -1698898192 : i32, 0 : i32 :
          !obelisk_sim.logic<32>
      %zero_i32_5 = arith.constant 0 : i32
      %one_i32_5 = arith.constant 1 : i32
      %ten_i32_5 = arith.constant 10 : i32
      %stdout_5 = arith.constant 1 : i32
      %add20, %add21, %add2 = obelisk_sim.stochastic_queue
          %ctx, %one32_5, %job_b_5, %info_b_5
          {action = 1 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %full_value, %full1, %full = obelisk_sim.stochastic_queue
          %ctx, %one32_5, %zero32_5, %zero32_5
          {action = 3 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %add_full0, %add_full1, %add_full = obelisk_sim.stochastic_queue
          %ctx, %one32_5, %zero32_5, %zero32_5
          {action = 1 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %length, %length1, %length_status = obelisk_sim.stochastic_queue
          %ctx, %one32_5, %one32_5, %zero32_5
          {action = 4 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %mean, %mean1, %mean_status = obelisk_sim.stochastic_queue
          %ctx, %one32_5, %two32_5, %zero32_5
          {action = 4 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %maximum, %maximum1, %maximum_status = obelisk_sim.stochastic_queue
          %ctx, %one32_5, %three32_5, %zero32_5
          {action = 4 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %four32 = obelisk_sim.logic.constant 4 : i32, 0 : i32 :
          !obelisk_sim.logic<32>
      %five32 = obelisk_sim.logic.constant 5 : i32, 0 : i32 :
          !obelisk_sim.logic<32>
      %six32 = obelisk_sim.logic.constant 6 : i32, 0 : i32 :
          !obelisk_sim.logic<32>
      %short0, %short1, %short_status = obelisk_sim.stochastic_queue
          %ctx, %one32_5, %four32, %zero32_5
          {action = 4 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %longest, %longest1, %longest_status = obelisk_sim.stochastic_queue
          %ctx, %one32_5, %five32, %zero32_5
          {action = 4 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %average, %average1, %average_status = obelisk_sim.stochastic_queue
          %ctx, %one32_5, %six32, %zero32_5
          {action = 4 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %one64 = obelisk_sim.logic.constant 1 : i64, 0 : i64 :
          !obelisk_sim.logic<64>
      %two64 = obelisk_sim.logic.constant 2 : i64, 0 : i64 :
          !obelisk_sim.logic<64>
      %three64 = obelisk_sim.logic.constant 3 : i64, 0 : i64 :
          !obelisk_sim.logic<64>
      %add2_ok = arith.cmpi eq, %add2, %zero_i32_5 : i32
      %full_ok = arith.cmpi eq, %full, %zero_i32_5 : i32
      %full_value_ok = obelisk_sim.logic.compare case_eq %full_value, %one64 :
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>) -> i1
      %add_full_ok = arith.cmpi eq, %add_full, %one_i32_5 : i32
      %length_ok = obelisk_sim.logic.compare case_eq %length, %two64 :
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>) -> i1
      %mean_ok = obelisk_sim.logic.compare case_eq %mean, %three64 :
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>) -> i1
      %maximum_ok = obelisk_sim.logic.compare case_eq %maximum, %two64 :
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>) -> i1
      %short_ok = arith.cmpi eq, %short_status, %ten_i32_5 : i32
      %longest_ok = obelisk_sim.logic.compare case_eq %longest, %three64 :
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>) -> i1
      %average_ok = obelisk_sim.logic.compare case_eq %average, %one64 :
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>) -> i1
      %stat_status0 = arith.cmpi eq, %length_status, %zero_i32_5 : i32
      %stat_status1 = arith.cmpi eq, %mean_status, %zero_i32_5 : i32
      %stat_status2 = arith.cmpi eq, %maximum_status, %zero_i32_5 : i32
      %stat_status3 = arith.cmpi eq, %longest_status, %zero_i32_5 : i32
      %stat_status4 = arith.cmpi eq, %average_status, %zero_i32_5 : i32
      %all_stat_status0 = arith.andi %stat_status0, %stat_status1 : i1
      %all_stat_status1 = arith.andi %stat_status2, %stat_status3 : i1
      %all_stat_status2 = arith.andi %stat_status4, %all_stat_status0 : i1
      %all_stat_status = arith.andi %all_stat_status1, %all_stat_status2 : i1
      %fill_format = obelisk_sim.bytes.constant
          "fill %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d"
      obelisk_sim.display %ctx to %stdout_5(
          %fill_format, %add1_ok_5, %not_full_ok_5, %not_full_value_ok_5,
          %add2_ok, %full_ok, %full_value_ok, %add_full_ok, %length_ok,
          %mean_ok, %maximum_ok, %short_ok, %longest_ok, %average_ok,
          %all_stat_status)
          newline = true radix = 10
          flags = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0] :
          !obelisk_sim.bytes, i1, i1, i1, i1, i1, i1, i1, i1, i1, i1, i1,
          i1, i1, i1
      %delay3 = obelisk_sim.time.constant 3
      obelisk_sim.suspend.delay %delay3 to ^after8

    ^after8:
      %zero32_8 = obelisk_sim.logic.constant 0 : i32, 0 : i32 :
          !obelisk_sim.logic<32>
      %one32_8 = obelisk_sim.logic.constant 1 : i32, 0 : i32 :
          !obelisk_sim.logic<32>
      %two32_8 = obelisk_sim.logic.constant 2 : i32, 0 : i32 :
          !obelisk_sim.logic<32>
      %four32_8 = obelisk_sim.logic.constant 4 : i32, 0 : i32 :
          !obelisk_sim.logic<32>
      %five32_8 = obelisk_sim.logic.constant 5 : i32, 0 : i32 :
          !obelisk_sim.logic<32>
      %six32_8 = obelisk_sim.logic.constant 6 : i32, 0 : i32 :
          !obelisk_sim.logic<32>
      %job_a_8 = obelisk_sim.logic.constant -1515913215 : i32,
          15728880 : i32 : !obelisk_sim.logic<32>
      %info_a_8 = obelisk_sim.logic.constant 1515847682 : i32,
          -268435216 : i32 : !obelisk_sim.logic<32>
      %zero_i32_8 = arith.constant 0 : i32
      %three_i32_8 = arith.constant 3 : i32
      %stdout_8 = arith.constant 1 : i32
      %removed_a, %removed_info_a, %remove_a_status =
          obelisk_sim.stochastic_queue %ctx, %one32_8, %zero32_8, %zero32_8
          {action = 2 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %shortest, %shortest1, %shortest_status = obelisk_sim.stochastic_queue
          %ctx, %one32_8, %four32_8, %zero32_8
          {action = 4 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %longest8, %longest81, %longest8_status = obelisk_sim.stochastic_queue
          %ctx, %one32_8, %five32_8, %zero32_8
          {action = 4 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %average8, %average81, %average8_status = obelisk_sim.stochastic_queue
          %ctx, %one32_8, %six32_8, %zero32_8
          {action = 4 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      // With the FIFO head at slot one, this add wraps into slot zero. The
      // following removes must still return the older B entry before C.
      %job_c = obelisk_sim.logic.constant -889275714 : i32, 0 : i32 :
          !obelisk_sim.logic<32>
      %info_c = obelisk_sim.logic.constant 195948557 : i32, 0 : i32 :
          !obelisk_sim.logic<32>
      %add_c0, %add_c1, %add_c_status = obelisk_sim.stochastic_queue
          %ctx, %one32_8, %job_c, %info_c
          {action = 1 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %removed_b, %removed_info_b, %remove_b_status =
          obelisk_sim.stochastic_queue %ctx, %one32_8, %zero32_8, %zero32_8
          {action = 2 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %removed_c, %removed_info_c, %remove_c_status =
          obelisk_sim.stochastic_queue %ctx, %one32_8, %zero32_8, %zero32_8
          {action = 2 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %removed_empty, %removed_empty1, %removed_empty_status =
          obelisk_sim.stochastic_queue %ctx, %one32_8, %zero32_8, %zero32_8
          {action = 2 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %job_a64 = obelisk_sim.logic.constant 2779054081 : i64,
          15728880 : i64 : !obelisk_sim.logic<64>
      %info_a64 = obelisk_sim.logic.constant 1515847682 : i64,
          4026532080 : i64 : !obelisk_sim.logic<64>
      %job_b64 = obelisk_sim.logic.constant 305419896 : i64, 0 : i64 :
          !obelisk_sim.logic<64>
      %info_b64 = obelisk_sim.logic.constant 2596069104 : i64, 0 : i64 :
          !obelisk_sim.logic<64>
      %job_c64 = obelisk_sim.logic.constant 3405691582 : i64, 0 : i64 :
          !obelisk_sim.logic<64>
      %info_c64 = obelisk_sim.logic.constant 195948557 : i64, 0 : i64 :
          !obelisk_sim.logic<64>
      %four64 = obelisk_sim.logic.constant 4 : i64, 0 : i64 :
          !obelisk_sim.logic<64>
      %two64_8 = obelisk_sim.logic.constant 2 : i64, 0 : i64 :
          !obelisk_sim.logic<64>
      %three64_8 = obelisk_sim.logic.constant 3 : i64, 0 : i64 :
          !obelisk_sim.logic<64>
      %removed_a_ok = obelisk_sim.logic.compare case_eq %removed_a, %job_a64 :
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>) -> i1
      %removed_info_a_ok = obelisk_sim.logic.compare case_eq
          %removed_info_a, %info_a64 :
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>) -> i1
      %remove_a_status_ok = arith.cmpi eq, %remove_a_status, %zero_i32_8 : i32
      %shortest_ok = obelisk_sim.logic.compare case_eq %shortest, %four64 :
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>) -> i1
      %longest8_ok = obelisk_sim.logic.compare case_eq %longest8, %two64_8 :
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>) -> i1
      %average8_ok = obelisk_sim.logic.compare case_eq %average8, %three64_8 :
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>) -> i1
      %time_status0 = arith.cmpi eq, %shortest_status, %zero_i32_8 : i32
      %time_status1 = arith.cmpi eq, %longest8_status, %zero_i32_8 : i32
      %time_status2 = arith.cmpi eq, %average8_status, %zero_i32_8 : i32
      %time_status01 = arith.andi %time_status0, %time_status1 : i1
      %time_status_ok = arith.andi %time_status01, %time_status2 : i1
      %add_c_ok = arith.cmpi eq, %add_c_status, %zero_i32_8 : i32
      %removed_b_ok = obelisk_sim.logic.compare case_eq %removed_b, %job_b64 :
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>) -> i1
      %removed_info_b_ok = obelisk_sim.logic.compare case_eq
          %removed_info_b, %info_b64 :
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>) -> i1
      %remove_b_status_ok = arith.cmpi eq, %remove_b_status, %zero_i32_8 : i32
      %removed_c_ok = obelisk_sim.logic.compare case_eq %removed_c, %job_c64 :
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>) -> i1
      %removed_info_c_ok = obelisk_sim.logic.compare case_eq
          %removed_info_c, %info_c64 :
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>) -> i1
      %remove_c_status_ok = arith.cmpi eq, %remove_c_status, %zero_i32_8 : i32
      %removed_empty_ok =
          arith.cmpi eq, %removed_empty_status, %three_i32_8 : i32
      %fifo_format = obelisk_sim.bytes.constant
          "fifo %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d"
      obelisk_sim.display %ctx to %stdout_8(
          %fifo_format, %removed_a_ok, %removed_info_a_ok,
          %remove_a_status_ok, %shortest_ok, %longest8_ok, %average8_ok,
          %time_status_ok, %add_c_ok, %removed_b_ok, %removed_info_b_ok,
          %remove_b_status_ok, %removed_c_ok, %removed_info_c_ok,
          %remove_c_status_ok, %removed_empty_ok)
          newline = true radix = 10
          flags = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0] :
          !obelisk_sim.bytes, i1, i1, i1, i1, i1, i1, i1, i1, i1, i1, i1,
          i1, i1, i1, i1

      %lifo_init0, %lifo_init1, %lifo_init = obelisk_sim.stochastic_queue
          %ctx, %two32_8, %two32_8, %two32_8
          {action = 0 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %lifo_add_a0, %lifo_add_a1, %lifo_add_a = obelisk_sim.stochastic_queue
          %ctx, %two32_8, %job_a_8, %info_a_8
          {action = 1 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %lifo_init_ok_8 = arith.cmpi eq, %lifo_init, %zero_i32_8 : i32
      %lifo_add_a_ok_8 = arith.cmpi eq, %lifo_add_a, %zero_i32_8 : i32
      %delay1 = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay1 to ^after9(
          %lifo_init_ok_8, %lifo_add_a_ok_8 : i1, i1)

    ^after9(%lifo_init_ok_9: i1, %lifo_add_a_ok_9: i1):
      %zero32_9 = obelisk_sim.logic.constant 0 : i32, 0 : i32 :
          !obelisk_sim.logic<32>
      %two32_9 = obelisk_sim.logic.constant 2 : i32, 0 : i32 :
          !obelisk_sim.logic<32>
      %job_b_9 = obelisk_sim.logic.constant 305419896 : i32, 0 : i32 :
          !obelisk_sim.logic<32>
      %info_b_9 = obelisk_sim.logic.constant -1698898192 : i32, 0 : i32 :
          !obelisk_sim.logic<32>
      %zero_i32_9 = arith.constant 0 : i32
      %stdout_9 = arith.constant 1 : i32
      %lifo_add_b0, %lifo_add_b1, %lifo_add_b = obelisk_sim.stochastic_queue
          %ctx, %two32_9, %job_b_9, %info_b_9
          {action = 1 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %lifo_b, %lifo_info_b, %lifo_remove_b = obelisk_sim.stochastic_queue
          %ctx, %two32_9, %zero32_9, %zero32_9
          {action = 2 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %lifo_a, %lifo_info_a, %lifo_remove_a = obelisk_sim.stochastic_queue
          %ctx, %two32_9, %zero32_9, %zero32_9
          {action = 2 : i32, unit_scale = 2 : i64} :
          (!obelisk_sim.context, !obelisk_sim.logic<32>,
           !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
      %job_a64_9 = obelisk_sim.logic.constant 2779054081 : i64,
          15728880 : i64 : !obelisk_sim.logic<64>
      %info_a64_9 = obelisk_sim.logic.constant 1515847682 : i64,
          4026532080 : i64 : !obelisk_sim.logic<64>
      %job_b64_9 = obelisk_sim.logic.constant 305419896 : i64, 0 : i64 :
          !obelisk_sim.logic<64>
      %info_b64_9 = obelisk_sim.logic.constant 2596069104 : i64, 0 : i64 :
          !obelisk_sim.logic<64>
      %lifo_add_b_ok = arith.cmpi eq, %lifo_add_b, %zero_i32_9 : i32
      %lifo_b_ok = obelisk_sim.logic.compare case_eq %lifo_b, %job_b64_9 :
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>) -> i1
      %lifo_info_b_ok = obelisk_sim.logic.compare case_eq
          %lifo_info_b, %info_b64_9 :
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>) -> i1
      %lifo_remove_b_ok = arith.cmpi eq, %lifo_remove_b, %zero_i32_9 : i32
      %lifo_a_ok = obelisk_sim.logic.compare case_eq %lifo_a, %job_a64_9 :
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>) -> i1
      %lifo_info_a_ok = obelisk_sim.logic.compare case_eq
          %lifo_info_a, %info_a64_9 :
          (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>) -> i1
      %lifo_remove_a_ok = arith.cmpi eq, %lifo_remove_a, %zero_i32_9 : i32
      %lifo_format = obelisk_sim.bytes.constant
          "lifo %0d %0d %0d %0d %0d %0d %0d %0d %0d"
      obelisk_sim.display %ctx to %stdout_9(
          %lifo_format, %lifo_init_ok_9, %lifo_add_a_ok_9, %lifo_add_b_ok,
          %lifo_b_ok, %lifo_info_b_ok, %lifo_remove_b_ok, %lifo_a_ok,
          %lifo_info_a_ok, %lifo_remove_a_ok)
          newline = true radix = 10
          flags = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0] :
          !obelisk_sim.bytes, i1, i1, i1, i1, i1, i1, i1, i1, i1
      obelisk_sim.return
    }
  }
}
