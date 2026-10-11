// RUN: mlir-translate %s --mlir-to-llvmir | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe | FileCheck %s --check-prefix=RUNTIME
// RUN: mlir-translate %s --mlir-to-llvmir | %llc -O3 -filetype=obj -relocation-model=pic -o %t.opt.o
// RUN: %llvm_dist/bin/clang++ %t.opt.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.opt.exe
// RUN: %t.opt.exe | FileCheck %s --check-prefix=RUNTIME

// Validate wide domain literal-pool records through the C ABI. A singleton
// 130-bit domain samples exactly, frozen illegal state is retained, and bad
// indices, aliases, padding, masks, flags and extents leave RNG unchanged.
// RUNTIME: wide domain plan validation PASS

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  llvm.mlir.global @obelisk_final_time(0 : i64) : i64
  llvm.mlir.global @obelisk_time_precision_fs(1 : i64) : i64
  llvm.mlir.global internal constant @ok("wide domain plan validation PASS\00")
  llvm.mlir.global internal constant @bad("wide domain plan validation FAIL\00")
  llvm.func @puts(!llvm.ptr) -> i32
  llvm.func @obelisk_rt_v1_context_create(!llvm.ptr) -> i32
  llvm.func @obelisk_rt_v1_context_destroy(!llvm.ptr)
  llvm.func @obelisk_rt_v1_random_solve_wide_modes_state(!llvm.ptr, !llvm.ptr, i64, !llvm.ptr, !llvm.ptr, i64, i64, i64, i64, i64, !llvm.ptr, i64, !llvm.ptr, i64, !llvm.ptr, !llvm.ptr, !llvm.ptr) -> i32
  llvm.mlir.global internal @program("\4F\44\52\31\02\00\20\00\82\00\00\00\02\00\00\00\00\00\00\00\28\02\00\00\07\00\00\00\00\00\00\00\22\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\20\00\00\00\01\00\00\00\FF\FF\FF\FF\00\00\00\00\01\00\00\00\00\00\00\00\FF\FF\FF\FF\FF\FF\FF\FF\FF\FF\FF\FF\FF\FF\FF\FF\03\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\02\00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\82\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\04\00\00\00\00\00\00\00\53\41\4D\50\01\00\00\00\83\00\00\00\01\00\00\00\82\00\00\00\00\00\00\00\A0\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\01\00\00\00\02\00\00\00\02\00\00\00\00\00\00\00\01\00\00\00\03\00\00\00\03\00\00\00\00\00\00\00\01\00\00\00\04\00\00\00\04\00\00\00\00\00\00\00\01\00\00\00\05\00\00\00\05\00\00\00\00\00\00\00\01\00\00\00\06\00\00\00\06\00\00\00\00\00\00\00\01\00\00\00\07\00\00\00\07\00\00\00\00\00\00\00\01\00\00\00\08\00\00\00\08\00\00\00\00\00\00\00\01\00\00\00\09\00\00\00\09\00\00\00\00\00\00\00\01\00\00\00\0A\00\00\00\0A\00\00\00\00\00\00\00\01\00\00\00\0B\00\00\00\0B\00\00\00\00\00\00\00\01\00\00\00\0C\00\00\00\0C\00\00\00\00\00\00\00\01\00\00\00\0D\00\00\00\0D\00\00\00\00\00\00\00\01\00\00\00\0E\00\00\00\0E\00\00\00\00\00\00\00\01\00\00\00\0F\00\00\00\0F\00\00\00\00\00\00\00\01\00\00\00\10\00\00\00\10\00\00\00\00\00\00\00\01\00\00\00\11\00\00\00\11\00\00\00\00\00\00\00\01\00\00\00\12\00\00\00\12\00\00\00\00\00\00\00\01\00\00\00\13\00\00\00\13\00\00\00\00\00\00\00\01\00\00\00\14\00\00\00\14\00\00\00\00\00\00\00\01\00\00\00\15\00\00\00\15\00\00\00\00\00\00\00\01\00\00\00\16\00\00\00\16\00\00\00\00\00\00\00\01\00\00\00\17\00\00\00\17\00\00\00\00\00\00\00\01\00\00\00\18\00\00\00\18\00\00\00\00\00\00\00\01\00\00\00\19\00\00\00\19\00\00\00\00\00\00\00\01\00\00\00\1A\00\00\00\1A\00\00\00\00\00\00\00\01\00\00\00\1B\00\00\00\1B\00\00\00\00\00\00\00\01\00\00\00\1C\00\00\00\1C\00\00\00\00\00\00\00\01\00\00\00\1D\00\00\00\1D\00\00\00\00\00\00\00\01\00\00\00\1E\00\00\00\1E\00\00\00\00\00\00\00\01\00\00\00\1F\00\00\00\1F\00\00\00\00\00\00\00\01\00\00\00\20\00\00\00\20\00\00\00\00\00\00\00\01\00\00\00\21\00\00\00\21\00\00\00\00\00\00\00\01\00\00\00\22\00\00\00\22\00\00\00\00\00\00\00\01\00\00\00\23\00\00\00\23\00\00\00\00\00\00\00\01\00\00\00\24\00\00\00\24\00\00\00\00\00\00\00\01\00\00\00\25\00\00\00\25\00\00\00\00\00\00\00\01\00\00\00\26\00\00\00\26\00\00\00\00\00\00\00\01\00\00\00\27\00\00\00\27\00\00\00\00\00\00\00\01\00\00\00\28\00\00\00\28\00\00\00\00\00\00\00\01\00\00\00\29\00\00\00\29\00\00\00\00\00\00\00\01\00\00\00\2A\00\00\00\2A\00\00\00\00\00\00\00\01\00\00\00\2B\00\00\00\2B\00\00\00\00\00\00\00\01\00\00\00\2C\00\00\00\2C\00\00\00\00\00\00\00\01\00\00\00\2D\00\00\00\2D\00\00\00\00\00\00\00\01\00\00\00\2E\00\00\00\2E\00\00\00\00\00\00\00\01\00\00\00\2F\00\00\00\2F\00\00\00\00\00\00\00\01\00\00\00\30\00\00\00\30\00\00\00\00\00\00\00\01\00\00\00\31\00\00\00\31\00\00\00\00\00\00\00\01\00\00\00\32\00\00\00\32\00\00\00\00\00\00\00\01\00\00\00\33\00\00\00\33\00\00\00\00\00\00\00\01\00\00\00\34\00\00\00\34\00\00\00\00\00\00\00\01\00\00\00\35\00\00\00\35\00\00\00\00\00\00\00\01\00\00\00\36\00\00\00\36\00\00\00\00\00\00\00\01\00\00\00\37\00\00\00\37\00\00\00\00\00\00\00\01\00\00\00\38\00\00\00\38\00\00\00\00\00\00\00\01\00\00\00\39\00\00\00\39\00\00\00\00\00\00\00\01\00\00\00\3A\00\00\00\3A\00\00\00\00\00\00\00\01\00\00\00\3B\00\00\00\3B\00\00\00\00\00\00\00\01\00\00\00\3C\00\00\00\3C\00\00\00\00\00\00\00\01\00\00\00\3D\00\00\00\3D\00\00\00\00\00\00\00\01\00\00\00\3E\00\00\00\3E\00\00\00\00\00\00\00\01\00\00\00\3F\00\00\00\3F\00\00\00\00\00\00\00\01\00\00\00\40\00\00\00\40\00\00\00\00\00\00\00\01\00\00\00\41\00\00\00\41\00\00\00\00\00\00\00\01\00\00\00\42\00\00\00\42\00\00\00\00\00\00\00\01\00\00\00\43\00\00\00\43\00\00\00\00\00\00\00\01\00\00\00\44\00\00\00\44\00\00\00\00\00\00\00\01\00\00\00\45\00\00\00\45\00\00\00\00\00\00\00\01\00\00\00\46\00\00\00\46\00\00\00\00\00\00\00\01\00\00\00\47\00\00\00\47\00\00\00\00\00\00\00\01\00\00\00\48\00\00\00\48\00\00\00\00\00\00\00\01\00\00\00\49\00\00\00\49\00\00\00\00\00\00\00\01\00\00\00\4A\00\00\00\4A\00\00\00\00\00\00\00\01\00\00\00\4B\00\00\00\4B\00\00\00\00\00\00\00\01\00\00\00\4C\00\00\00\4C\00\00\00\00\00\00\00\01\00\00\00\4D\00\00\00\4D\00\00\00\00\00\00\00\01\00\00\00\4E\00\00\00\4E\00\00\00\00\00\00\00\01\00\00\00\4F\00\00\00\4F\00\00\00\00\00\00\00\01\00\00\00\50\00\00\00\50\00\00\00\00\00\00\00\01\00\00\00\51\00\00\00\51\00\00\00\00\00\00\00\01\00\00\00\52\00\00\00\52\00\00\00\00\00\00\00\01\00\00\00\53\00\00\00\53\00\00\00\00\00\00\00\01\00\00\00\54\00\00\00\54\00\00\00\00\00\00\00\01\00\00\00\55\00\00\00\55\00\00\00\00\00\00\00\01\00\00\00\56\00\00\00\56\00\00\00\00\00\00\00\01\00\00\00\57\00\00\00\57\00\00\00\00\00\00\00\01\00\00\00\58\00\00\00\58\00\00\00\00\00\00\00\01\00\00\00\59\00\00\00\59\00\00\00\00\00\00\00\01\00\00\00\5A\00\00\00\5A\00\00\00\00\00\00\00\01\00\00\00\5B\00\00\00\5B\00\00\00\00\00\00\00\01\00\00\00\5C\00\00\00\5C\00\00\00\00\00\00\00\01\00\00\00\5D\00\00\00\5D\00\00\00\00\00\00\00\01\00\00\00\5E\00\00\00\5E\00\00\00\00\00\00\00\01\00\00\00\5F\00\00\00\5F\00\00\00\00\00\00\00\01\00\00\00\60\00\00\00\60\00\00\00\00\00\00\00\01\00\00\00\61\00\00\00\61\00\00\00\00\00\00\00\01\00\00\00\62\00\00\00\62\00\00\00\00\00\00\00\01\00\00\00\63\00\00\00\63\00\00\00\00\00\00\00\01\00\00\00\64\00\00\00\64\00\00\00\00\00\00\00\01\00\00\00\65\00\00\00\65\00\00\00\00\00\00\00\01\00\00\00\66\00\00\00\66\00\00\00\00\00\00\00\01\00\00\00\67\00\00\00\67\00\00\00\00\00\00\00\01\00\00\00\68\00\00\00\68\00\00\00\00\00\00\00\01\00\00\00\69\00\00\00\69\00\00\00\00\00\00\00\01\00\00\00\6A\00\00\00\6A\00\00\00\00\00\00\00\01\00\00\00\6B\00\00\00\6B\00\00\00\00\00\00\00\01\00\00\00\6C\00\00\00\6C\00\00\00\00\00\00\00\01\00\00\00\6D\00\00\00\6D\00\00\00\00\00\00\00\01\00\00\00\6E\00\00\00\6E\00\00\00\00\00\00\00\01\00\00\00\6F\00\00\00\6F\00\00\00\00\00\00\00\01\00\00\00\70\00\00\00\70\00\00\00\00\00\00\00\01\00\00\00\71\00\00\00\71\00\00\00\00\00\00\00\01\00\00\00\72\00\00\00\72\00\00\00\00\00\00\00\01\00\00\00\73\00\00\00\73\00\00\00\00\00\00\00\01\00\00\00\74\00\00\00\74\00\00\00\00\00\00\00\01\00\00\00\75\00\00\00\75\00\00\00\00\00\00\00\01\00\00\00\76\00\00\00\76\00\00\00\00\00\00\00\01\00\00\00\77\00\00\00\77\00\00\00\00\00\00\00\01\00\00\00\78\00\00\00\78\00\00\00\00\00\00\00\01\00\00\00\79\00\00\00\79\00\00\00\00\00\00\00\01\00\00\00\7A\00\00\00\7A\00\00\00\00\00\00\00\01\00\00\00\7B\00\00\00\7B\00\00\00\00\00\00\00\01\00\00\00\7C\00\00\00\7C\00\00\00\00\00\00\00\01\00\00\00\7D\00\00\00\7D\00\00\00\00\00\00\00\01\00\00\00\7E\00\00\00\7E\00\00\00\00\00\00\00\01\00\00\00\7F\00\00\00\7F\00\00\00\00\00\00\00\01\00\00\00\80\00\00\00\80\00\00\00\00\00\00\00\01\00\00\00\81\00\00\00\81\00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\50\4C\41\4E\01\00\00\00\58\08\00\00\00\00\00\00")

  llvm.func internal @check(%ctx: !llvm.ptr, %program: !llvm.ptr, %start: !llvm.ptr, %mask: !llvm.ptr, %expected_status: i32, %expected_success: i32, %expected_low: i64, %expected_high: i64) -> i1 {
    %zero = llvm.mlir.constant(0 : i64) : i64
    %one = llvm.mlir.constant(1 : i64) : i64
    %three = llvm.mlir.constant(3 : i64) : i64
    %seed = llvm.mlir.constant(123 : i64) : i64
    %increment = llvm.mlir.constant(7 : i64) : i64
    %assignment = llvm.alloca %three x i64 : (i64) -> !llvm.ptr
    %success = llvm.alloca %one x i32 : (i64) -> !llvm.ptr
    %state = llvm.alloca %one x i64 : (i64) -> !llvm.ptr
    %middle = llvm.getelementptr %assignment[1] : (!llvm.ptr) -> !llvm.ptr, i64
    %high = llvm.getelementptr %assignment[2] : (!llvm.ptr) -> !llvm.ptr, i64
    llvm.store %zero, %assignment : i64, !llvm.ptr
    llvm.store %zero, %middle : i64, !llvm.ptr
    llvm.store %zero, %high : i64, !llvm.ptr
    %size = llvm.mlir.constant(2312 : i64) : i64

    %status = llvm.call @obelisk_rt_v1_random_solve_wide_modes_state(%ctx, %program, %size, %start, %mask, %three, %zero, %one, %seed, %increment, %start, %zero, %start, %zero, %assignment, %success, %state) : (!llvm.ptr, !llvm.ptr, i64, !llvm.ptr, !llvm.ptr, i64, i64, i64, i64, i64, !llvm.ptr, i64, !llvm.ptr, i64, !llvm.ptr, !llvm.ptr, !llvm.ptr) -> i32
    %actual_success = llvm.load %success : !llvm.ptr -> i32
    %actual_low = llvm.load %assignment : !llvm.ptr -> i64
    %actual_middle = llvm.load %middle : !llvm.ptr -> i64
    %actual_high = llvm.load %high : !llvm.ptr -> i64
    %actual_state = llvm.load %state : !llvm.ptr -> i64
    %status_ok = llvm.icmp "eq" %status, %expected_status : i32
    %success_ok = llvm.icmp "eq" %actual_success, %expected_success : i32
    %low_ok = llvm.icmp "eq" %actual_low, %expected_low : i64
    %middle_ok = llvm.icmp "eq" %actual_middle, %zero : i64
    %high_ok = llvm.icmp "eq" %actual_high, %expected_high : i64
    %state_ok = llvm.icmp "eq" %actual_state, %seed : i64
    %a = llvm.and %status_ok, %success_ok : i1
    %b = llvm.and %low_ok, %middle_ok : i1
    %c = llvm.and %high_ok, %state_ok : i1
    %d = llvm.and %a, %b : i1
    %result = llvm.and %c, %d : i1
    llvm.return %result : i1
  }

  llvm.func @main() -> i32 {
    %zero = llvm.mlir.constant(0 : i64) : i64
    %one = llvm.mlir.constant(1 : i64) : i64
    %two = llvm.mlir.constant(2 : i64) : i64
    %three = llvm.mlir.constant(3 : i64) : i64
    %seven = llvm.mlir.constant(7 : i64) : i64
    %all = llvm.mlir.constant(-1 : i64) : i64
    %z32 = llvm.mlir.constant(0 : i32) : i32
    %o32 = llvm.mlir.constant(1 : i32) : i32
    %invalid = llvm.mlir.constant(2 : i32) : i32
    %ctx_slot = llvm.alloca %one x !llvm.ptr : (i64) -> !llvm.ptr
    %created = llvm.call @obelisk_rt_v1_context_create(%ctx_slot) : (!llvm.ptr) -> i32
    %ctx = llvm.load %ctx_slot : !llvm.ptr -> !llvm.ptr
    %program = llvm.mlir.addressof @program : !llvm.ptr
    %start = llvm.alloca %three x i64 : (i64) -> !llvm.ptr
    %mask = llvm.alloca %three x i64 : (i64) -> !llvm.ptr
    %start_middle = llvm.getelementptr %start[1] : (!llvm.ptr) -> !llvm.ptr, i64
    %start_high = llvm.getelementptr %start[2] : (!llvm.ptr) -> !llvm.ptr, i64
    %mask_middle = llvm.getelementptr %mask[1] : (!llvm.ptr) -> !llvm.ptr, i64
    %mask_high = llvm.getelementptr %mask[2] : (!llvm.ptr) -> !llvm.ptr, i64
    llvm.store %zero, %start : i64, !llvm.ptr
    llvm.store %zero, %start_middle : i64, !llvm.ptr
    llvm.store %zero, %start_high : i64, !llvm.ptr
    llvm.store %all, %mask : i64, !llvm.ptr
    llvm.store %all, %mask_middle : i64, !llvm.ptr
    llvm.store %three, %mask_high : i64, !llvm.ptr
    %created_ok = llvm.icmp "eq" %created, %z32 : i32
    %valid = llvm.call @check(%ctx, %program, %start, %mask, %z32, %o32, %one, %two) : (!llvm.ptr, !llvm.ptr, !llvm.ptr, !llvm.ptr, i32, i32, i64, i64) -> i1
    %result_0 = llvm.and %created_ok, %valid : i1
    llvm.store %zero, %mask : i64, !llvm.ptr
    llvm.store %zero, %mask_middle : i64, !llvm.ptr
    llvm.store %zero, %mask_high : i64, !llvm.ptr
    llvm.store %seven, %start : i64, !llvm.ptr
    %frozen = llvm.call @check(%ctx, %program, %start, %mask, %z32, %o32, %seven, %zero) : (!llvm.ptr, !llvm.ptr, !llvm.ptr, !llvm.ptr, i32, i32, i64, i64) -> i1
    %result_1 = llvm.and %result_0, %frozen : i1
    llvm.store %zero, %start : i64, !llvm.ptr
    llvm.store %all, %mask : i64, !llvm.ptr
    llvm.store %all, %mask_middle : i64, !llvm.ptr
    llvm.store %three, %mask_high : i64, !llvm.ptr
    // unknown_flags
    %address_2 = llvm.getelementptr %program[140] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_2 = llvm.mlir.constant(2 : i32) : i32
    %original_2 = llvm.mlir.constant(1 : i32) : i32
    llvm.store %bad_2, %address_2 : i32, !llvm.ptr
    %check_2 = llvm.call @check(%ctx, %program, %start, %mask, %invalid, %z32, %zero, %zero) : (!llvm.ptr, !llvm.ptr, !llvm.ptr, !llvm.ptr, i32, i32, i64, i64) -> i1
    %result_2 = llvm.and %result_1, %check_2 : i1
    llvm.store %original_2, %address_2 : i32, !llvm.ptr
    // huge_mask_index
    %address_3 = llvm.getelementptr %program[144] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_3 = llvm.mlir.constant(-1 : i64) : i64
    %original_3 = llvm.mlir.constant(1 : i64) : i64
    llvm.store %bad_3, %address_3 : i64, !llvm.ptr
    %check_3 = llvm.call @check(%ctx, %program, %start, %mask, %invalid, %z32, %zero, %zero) : (!llvm.ptr, !llvm.ptr, !llvm.ptr, !llvm.ptr, i32, i32, i64, i64) -> i1
    %result_3 = llvm.and %result_2, %check_3 : i1
    llvm.store %original_3, %address_3 : i64, !llvm.ptr
    // aliased_mask
    %address_4 = llvm.getelementptr %program[144] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_4 = llvm.mlir.constant(0 : i64) : i64
    %original_4 = llvm.mlir.constant(1 : i64) : i64
    llvm.store %bad_4, %address_4 : i64, !llvm.ptr
    %check_4 = llvm.call @check(%ctx, %program, %start, %mask, %invalid, %z32, %zero, %zero) : (!llvm.ptr, !llvm.ptr, !llvm.ptr, !llvm.ptr, i32, i32, i64, i64) -> i1
    %result_4 = llvm.and %result_3, %check_4 : i1
    llvm.store %original_4, %address_4 : i64, !llvm.ptr
    // aliased_value
    %address_5 = llvm.getelementptr %program[152] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_5 = llvm.mlir.constant(1 : i64) : i64
    %original_5 = llvm.mlir.constant(4 : i64) : i64
    llvm.store %bad_5, %address_5 : i64, !llvm.ptr
    %check_5 = llvm.call @check(%ctx, %program, %start, %mask, %invalid, %z32, %zero, %zero) : (!llvm.ptr, !llvm.ptr, !llvm.ptr, !llvm.ptr, i32, i32, i64, i64) -> i1
    %result_5 = llvm.and %result_4, %check_5 : i1
    llvm.store %original_5, %address_5 : i64, !llvm.ptr
    // out_of_pool
    %address_6 = llvm.getelementptr %program[152] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_6 = llvm.mlir.constant(7 : i64) : i64
    %original_6 = llvm.mlir.constant(4 : i64) : i64
    llvm.store %bad_6, %address_6 : i64, !llvm.ptr
    %check_6 = llvm.call @check(%ctx, %program, %start, %mask, %invalid, %z32, %zero, %zero) : (!llvm.ptr, !llvm.ptr, !llvm.ptr, !llvm.ptr, i32, i32, i64, i64) -> i1
    %result_6 = llvm.and %result_5, %check_6 : i1
    llvm.store %original_6, %address_6 : i64, !llvm.ptr
    // wide_extent
    %address_7 = llvm.getelementptr %program[136] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_7 = llvm.mlir.constant(131 : i32) : i32
    %original_7 = llvm.mlir.constant(130 : i32) : i32
    llvm.store %bad_7, %address_7 : i32, !llvm.ptr
    %check_7 = llvm.call @check(%ctx, %program, %start, %mask, %invalid, %z32, %zero, %zero) : (!llvm.ptr, !llvm.ptr, !llvm.ptr, !llvm.ptr, i32, i32, i64, i64) -> i1
    %result_7 = llvm.and %result_6, %check_7 : i1
    llvm.store %original_7, %address_7 : i32, !llvm.ptr
    // shifted_extent
    %address_8 = llvm.getelementptr %program[132] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_8 = llvm.mlir.constant(1 : i32) : i32
    %original_8 = llvm.mlir.constant(0 : i32) : i32
    llvm.store %bad_8, %address_8 : i32, !llvm.ptr
    %check_8 = llvm.call @check(%ctx, %program, %start, %mask, %invalid, %z32, %zero, %zero) : (!llvm.ptr, !llvm.ptr, !llvm.ptr, !llvm.ptr, i32, i32, i64, i64) -> i1
    %result_8 = llvm.and %result_7, %check_8 : i1
    llvm.store %original_8, %address_8 : i32, !llvm.ptr
    // value_padding
    %address_9 = llvm.getelementptr %program[112] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_9 = llvm.mlir.constant(4 : i64) : i64
    %original_9 = llvm.mlir.constant(2 : i64) : i64
    llvm.store %bad_9, %address_9 : i64, !llvm.ptr
    %check_9 = llvm.call @check(%ctx, %program, %start, %mask, %invalid, %z32, %zero, %zero) : (!llvm.ptr, !llvm.ptr, !llvm.ptr, !llvm.ptr, i32, i32, i64, i64) -> i1
    %result_9 = llvm.and %result_8, %check_9 : i1
    llvm.store %original_9, %address_9 : i64, !llvm.ptr
    // mask_padding
    %address_10 = llvm.getelementptr %program[88] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_10 = llvm.mlir.constant(4 : i64) : i64
    %original_10 = llvm.mlir.constant(3 : i64) : i64
    llvm.store %bad_10, %address_10 : i64, !llvm.ptr
    %check_10 = llvm.call @check(%ctx, %program, %start, %mask, %invalid, %z32, %zero, %zero) : (!llvm.ptr, !llvm.ptr, !llvm.ptr, !llvm.ptr, i32, i32, i64, i64) -> i1
    %result_10 = llvm.and %result_9, %check_10 : i1
    llvm.store %original_10, %address_10 : i64, !llvm.ptr
    // value_outside_mask
    %address_11 = llvm.getelementptr %program[72] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_11 = llvm.mlir.constant(-2 : i64) : i64
    %original_11 = llvm.mlir.constant(-1 : i64) : i64
    llvm.store %bad_11, %address_11 : i64, !llvm.ptr
    %check_11 = llvm.call @check(%ctx, %program, %start, %mask, %invalid, %z32, %zero, %zero) : (!llvm.ptr, !llvm.ptr, !llvm.ptr, !llvm.ptr, i32, i32, i64, i64) -> i1
    %result_11 = llvm.and %result_10, %check_11 : i1
    llvm.store %original_11, %address_11 : i64, !llvm.ptr
    // group_out_of_range
    %address_12 = llvm.getelementptr %program[128] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_12 = llvm.mlir.constant(1 : i32) : i32
    %original_12 = llvm.mlir.constant(0 : i32) : i32
    llvm.store %bad_12, %address_12 : i32, !llvm.ptr
    %check_12 = llvm.call @check(%ctx, %program, %start, %mask, %invalid, %z32, %zero, %zero) : (!llvm.ptr, !llvm.ptr, !llvm.ptr, !llvm.ptr, i32, i32, i64, i64) -> i1
    %result_12 = llvm.and %result_11, %check_12 : i1
    llvm.store %original_12, %address_12 : i32, !llvm.ptr
    // unused_words
    %address_13 = llvm.getelementptr %program[136] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_13 = llvm.mlir.constant(128 : i32) : i32
    %original_13 = llvm.mlir.constant(130 : i32) : i32
    llvm.store %bad_13, %address_13 : i32, !llvm.ptr
    %check_13 = llvm.call @check(%ctx, %program, %start, %mask, %invalid, %z32, %zero, %zero) : (!llvm.ptr, !llvm.ptr, !llvm.ptr, !llvm.ptr, i32, i32, i64, i64) -> i1
    %result_13 = llvm.and %result_12, %check_13 : i1
    llvm.store %original_13, %address_13 : i32, !llvm.ptr
    llvm.call @obelisk_rt_v1_context_destroy(%ctx) : (!llvm.ptr) -> ()
    %ok = llvm.mlir.addressof @ok : !llvm.ptr
    %bad = llvm.mlir.addressof @bad : !llvm.ptr
    %message = llvm.select %result_13, %ok, %bad : i1, !llvm.ptr
    %printed = llvm.call @puts(%message) : (!llvm.ptr) -> i32
    llvm.return %z32 : i32
  }
}
