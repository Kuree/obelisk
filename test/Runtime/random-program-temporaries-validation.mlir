// RUN: mlir-translate %s --mlir-to-llvmir | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe | FileCheck %s --check-prefix=RUNTIME
// RUN: mlir-translate %s --mlir-to-llvmir | %llc -O3 -filetype=obj -relocation-model=pic -o %t.opt.o
// RUN: %llvm_dist/bin/clang++ %t.opt.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.opt.exe
// RUN: %t.opt.exe | FileCheck %s --check-prefix=RUNTIME

// Both wire formats compute v = x + k, then require (v ^ v) == 0 and v == t
// across hard and soft boundaries. STORE preserves the stack top. Repeated
// candidate evaluation must refresh temporary 0 before the load can select
// x == 3. V2 uses 130-bit temporaries with k = 2^129 and t = 2^129 + 3.
// Malformed slot indices, redefinitions, flags, widths, auxiliary fields and
// reads before writes are rejected without advancing the caller's RNG.
// RUNTIME: program temporaries valid 1

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "aarch64-unknown-linux-gnu"} {
  llvm.mlir.global @obelisk_final_time(0 : i64) : i64
  llvm.mlir.global @obelisk_time_precision_fs(1 : i64) : i64
  llvm.mlir.global internal constant @ok("program temporaries valid 1\00")
  llvm.mlir.global internal constant @bad("program temporaries valid 0\00")
  llvm.mlir.global internal @narrow("\4F\44\52\31\01\00\18\00\04\00\00\00\11\00\00\00\00\00\00\00\01\00\00\00\01\04\00\00\00\00\00\00\00\00\00\00\00\00\00\00\22\04\00\00\00\00\00\00\01\00\00\00\00\00\00\00\0E\04\00\00\00\00\00\00\00\00\00\00\00\00\00\00\2D\04\00\00\00\00\00\00\00\00\00\00\00\00\00\00\2E\04\00\00\00\00\00\00\00\00\00\00\00\00\00\00\13\04\00\00\00\00\00\00\00\00\00\00\00\00\00\00\22\04\00\00\00\00\00\00\00\00\00\00\00\00\00\00\15\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\20\01\00\00\FF\FF\FF\FF\00\00\00\00\00\00\00\00\2E\04\00\00\00\00\00\00\00\00\00\00\00\00\00\00\22\04\00\00\00\00\00\00\04\00\00\00\00\00\00\00\15\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\20\01\00\00\FF\FF\FF\FF\00\00\00\00\00\00\00\00\2E\04\00\00\00\00\00\00\00\00\00\00\00\00\00\00\22\04\00\00\00\00\00\00\04\00\00\00\00\00\00\00\15\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\21\01\00\00\FF\FF\FF\FF\00\00\00\00\00\00\00\00")
  llvm.mlir.global internal @wide("\4F\44\52\31\02\00\20\00\82\00\00\00\11\00\00\00\00\00\00\00\01\00\00\00\0C\00\00\00\00\00\00\00\01\00\00\00\82\00\00\00\00\00\00\00\00\00\00\00\22\00\00\00\82\00\00\00\00\00\00\00\00\00\00\00\0E\00\00\00\82\00\00\00\00\00\00\00\00\00\00\00\2D\00\00\00\82\00\00\00\00\00\00\00\00\00\00\00\2E\00\00\00\82\00\00\00\00\00\00\00\00\00\00\00\13\00\00\00\82\00\00\00\00\00\00\00\00\00\00\00\22\00\00\00\82\00\00\00\00\00\00\00\03\00\00\00\15\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\20\00\00\00\01\00\00\00\FF\FF\FF\FF\00\00\00\00\2E\00\00\00\82\00\00\00\00\00\00\00\00\00\00\00\22\00\00\00\82\00\00\00\00\00\00\00\06\00\00\00\15\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\20\00\00\00\01\00\00\00\FF\FF\FF\FF\00\00\00\00\2E\00\00\00\82\00\00\00\00\00\00\00\00\00\00\00\22\00\00\00\82\00\00\00\00\00\00\00\09\00\00\00\15\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\21\00\00\00\01\00\00\00\FF\FF\FF\FF\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\02\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\03\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\02\00\00\00\00\00\00\00\03\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\02\00\00\00\00\00\00\00")
  llvm.func @puts(!llvm.ptr) -> i32
  llvm.func @obelisk_rt_v1_context_create(!llvm.ptr) -> i32
  llvm.func @obelisk_rt_v1_context_destroy(!llvm.ptr)
  llvm.func @obelisk_rt_v1_random_solve_modes_state(!llvm.ptr, !llvm.ptr, i64, i64, i64, i64, i64, i64, i64, !llvm.ptr, i64, !llvm.ptr, !llvm.ptr, !llvm.ptr) -> i32
  llvm.func @obelisk_rt_v1_random_solve_wide_modes_state(!llvm.ptr, !llvm.ptr, i64, !llvm.ptr, !llvm.ptr, i64, i64, i64, i64, i64, !llvm.ptr, i64, !llvm.ptr, i64, !llvm.ptr, !llvm.ptr, !llvm.ptr) -> i32

  llvm.func internal @check_narrow(%ctx: !llvm.ptr, %program: !llvm.ptr, %expected_status: i32, %expected_success: i32, %expected_assignment: i64) -> i1 {
    %zero = llvm.mlir.constant(0 : i64) : i64
    %one = llvm.mlir.constant(1 : i64) : i64
    %mask = llvm.mlir.constant(15 : i64) : i64
    %attempts = llvm.mlir.constant(16 : i64) : i64
    %size = llvm.mlir.constant(296 : i64) : i64
    %seed = llvm.mlir.constant(123 : i64) : i64
    %increment = llvm.mlir.constant(7 : i64) : i64
    %assignment = llvm.alloca %one x i64 : (i64) -> !llvm.ptr
    %success = llvm.alloca %one x i32 : (i64) -> !llvm.ptr
    %state = llvm.alloca %one x i64 : (i64) -> !llvm.ptr
    %null = llvm.mlir.zero : !llvm.ptr
    llvm.store %zero, %assignment : i64, !llvm.ptr
    %status = llvm.call @obelisk_rt_v1_random_solve_modes_state(%ctx, %program, %size, %zero, %mask, %zero, %attempts, %seed, %increment, %null, %zero, %assignment, %success, %state) : (!llvm.ptr, !llvm.ptr, i64, i64, i64, i64, i64, i64, i64, !llvm.ptr, i64, !llvm.ptr, !llvm.ptr, !llvm.ptr) -> i32
    %actual_success = llvm.load %success : !llvm.ptr -> i32
    %actual_assignment = llvm.load %assignment : !llvm.ptr -> i64
    %actual_state = llvm.load %state : !llvm.ptr -> i64
    %status_ok = llvm.icmp "eq" %status, %expected_status : i32
    %success_ok = llvm.icmp "eq" %actual_success, %expected_success : i32
    %assignment_ok = llvm.icmp "eq" %actual_assignment, %expected_assignment : i64
    %state_ok = llvm.icmp "eq" %actual_state, %seed : i64
    %a = llvm.and %status_ok, %success_ok : i1
    %b = llvm.and %assignment_ok, %state_ok : i1
    %result = llvm.and %a, %b : i1
    llvm.return %result : i1
  }

  llvm.func internal @check_wide(%ctx: !llvm.ptr, %program: !llvm.ptr, %expected_status: i32, %expected_success: i32, %expected_assignment: i64) -> i1 {
    %zero = llvm.mlir.constant(0 : i64) : i64
    %one = llvm.mlir.constant(1 : i64) : i64
    %three = llvm.mlir.constant(3 : i64) : i64
    %mask_low = llvm.mlir.constant(15 : i64) : i64
    %attempts = llvm.mlir.constant(16 : i64) : i64
    %size = llvm.mlir.constant(400 : i64) : i64
    %seed = llvm.mlir.constant(123 : i64) : i64
    %increment = llvm.mlir.constant(7 : i64) : i64
    %assignment = llvm.alloca %three x i64 : (i64) -> !llvm.ptr
    %start = llvm.alloca %three x i64 : (i64) -> !llvm.ptr
    %mask = llvm.alloca %three x i64 : (i64) -> !llvm.ptr
    %success = llvm.alloca %one x i32 : (i64) -> !llvm.ptr
    %state = llvm.alloca %one x i64 : (i64) -> !llvm.ptr
    %null = llvm.mlir.zero : !llvm.ptr
    %a_middle = llvm.getelementptr %assignment[1] : (!llvm.ptr) -> !llvm.ptr, i64
    %a_high = llvm.getelementptr %assignment[2] : (!llvm.ptr) -> !llvm.ptr, i64
    %s_middle = llvm.getelementptr %start[1] : (!llvm.ptr) -> !llvm.ptr, i64
    %s_high = llvm.getelementptr %start[2] : (!llvm.ptr) -> !llvm.ptr, i64
    %m_middle = llvm.getelementptr %mask[1] : (!llvm.ptr) -> !llvm.ptr, i64
    %m_high = llvm.getelementptr %mask[2] : (!llvm.ptr) -> !llvm.ptr, i64
    llvm.store %zero, %assignment : i64, !llvm.ptr
    llvm.store %zero, %a_middle : i64, !llvm.ptr
    llvm.store %zero, %a_high : i64, !llvm.ptr
    llvm.store %zero, %start : i64, !llvm.ptr
    llvm.store %zero, %s_middle : i64, !llvm.ptr
    llvm.store %zero, %s_high : i64, !llvm.ptr
    llvm.store %mask_low, %mask : i64, !llvm.ptr
    llvm.store %zero, %m_middle : i64, !llvm.ptr
    llvm.store %zero, %m_high : i64, !llvm.ptr
    %status = llvm.call @obelisk_rt_v1_random_solve_wide_modes_state(%ctx, %program, %size, %start, %mask, %three, %zero, %attempts, %seed, %increment, %null, %zero, %null, %zero, %assignment, %success, %state) : (!llvm.ptr, !llvm.ptr, i64, !llvm.ptr, !llvm.ptr, i64, i64, i64, i64, i64, !llvm.ptr, i64, !llvm.ptr, i64, !llvm.ptr, !llvm.ptr, !llvm.ptr) -> i32
    %actual_success = llvm.load %success : !llvm.ptr -> i32
    %actual_low = llvm.load %assignment : !llvm.ptr -> i64
    %actual_middle = llvm.load %a_middle : !llvm.ptr -> i64
    %actual_high = llvm.load %a_high : !llvm.ptr -> i64
    %actual_state = llvm.load %state : !llvm.ptr -> i64
    %status_ok = llvm.icmp "eq" %status, %expected_status : i32
    %success_ok = llvm.icmp "eq" %actual_success, %expected_success : i32
    %assignment_ok = llvm.icmp "eq" %actual_low, %expected_assignment : i64
    %middle_ok = llvm.icmp "eq" %actual_middle, %zero : i64
    %high_ok = llvm.icmp "eq" %actual_high, %zero : i64
    %state_ok = llvm.icmp "eq" %actual_state, %seed : i64
    %a = llvm.and %status_ok, %success_ok : i1
    %b = llvm.and %assignment_ok, %state_ok : i1
    %c = llvm.and %middle_ok, %high_ok : i1
    %d = llvm.and %a, %b : i1
    %result = llvm.and %d, %c : i1
    llvm.return %result : i1
  }

  llvm.func @main() -> i32 {
    %zero = llvm.mlir.constant(0 : i64) : i64
    %one = llvm.mlir.constant(1 : i64) : i64
    %three = llvm.mlir.constant(3 : i64) : i64
    %z32 = llvm.mlir.constant(0 : i32) : i32
    %o32 = llvm.mlir.constant(1 : i32) : i32
    %invalid = llvm.mlir.constant(2 : i32) : i32
    %ctx_slot = llvm.alloca %one x !llvm.ptr : (i64) -> !llvm.ptr
    %created = llvm.call @obelisk_rt_v1_context_create(%ctx_slot) : (!llvm.ptr) -> i32
    %ctx = llvm.load %ctx_slot : !llvm.ptr -> !llvm.ptr
    %narrow = llvm.mlir.addressof @narrow : !llvm.ptr
    %wide = llvm.mlir.addressof @wide : !llvm.ptr
    %valid_0 = llvm.icmp "eq" %created, %z32 : i32
    %narrow_ok = llvm.call @check_narrow(%ctx, %narrow, %z32, %o32, %three) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_1 = llvm.and %valid_0, %narrow_ok : i1
    %wide_ok = llvm.call @check_wide(%ctx, %wide, %z32, %o32, %three) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_2 = llvm.and %valid_1, %wide_ok : i1
    // narrow: store_before_value
    %address_3 = llvm.getelementptr %narrow[24] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_3 = llvm.mlir.constant(45 : i8) : i8
    %old_3 = llvm.mlir.constant(1 : i8) : i8
    llvm.store %bad_3, %address_3 : i8, !llvm.ptr
    %check_3 = llvm.call @check_narrow(%ctx, %narrow, %invalid, %z32, %zero) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_3 = llvm.and %valid_2, %check_3 : i1
    llvm.store %old_3, %address_3 : i8, !llvm.ptr
    // narrow: load_before_store
    %address_4 = llvm.getelementptr %narrow[24] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_4 = llvm.mlir.constant(46 : i8) : i8
    %old_4 = llvm.mlir.constant(1 : i8) : i8
    llvm.store %bad_4, %address_4 : i8, !llvm.ptr
    %check_4 = llvm.call @check_narrow(%ctx, %narrow, %invalid, %z32, %zero) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_4 = llvm.and %valid_3, %check_4 : i1
    llvm.store %old_4, %address_4 : i8, !llvm.ptr
    // narrow: noncontiguous_store
    %address_5 = llvm.getelementptr %narrow[76] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_5 = llvm.mlir.constant(1 : i32) : i32
    %old_5 = llvm.mlir.constant(0 : i32) : i32
    llvm.store %bad_5, %address_5 : i32, !llvm.ptr
    %check_5 = llvm.call @check_narrow(%ctx, %narrow, %invalid, %z32, %zero) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_5 = llvm.and %valid_4, %check_5 : i1
    llvm.store %old_5, %address_5 : i32, !llvm.ptr
    // narrow: huge_store
    %address_6 = llvm.getelementptr %narrow[76] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_6 = llvm.mlir.constant(4294967295 : i32) : i32
    %old_6 = llvm.mlir.constant(0 : i32) : i32
    llvm.store %bad_6, %address_6 : i32, !llvm.ptr
    %check_6 = llvm.call @check_narrow(%ctx, %narrow, %invalid, %z32, %zero) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_6 = llvm.and %valid_5, %check_6 : i1
    llvm.store %old_6, %address_6 : i32, !llvm.ptr
    // narrow: unknown_load
    %address_7 = llvm.getelementptr %narrow[92] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_7 = llvm.mlir.constant(1 : i32) : i32
    %old_7 = llvm.mlir.constant(0 : i32) : i32
    llvm.store %bad_7, %address_7 : i32, !llvm.ptr
    %check_7 = llvm.call @check_narrow(%ctx, %narrow, %invalid, %z32, %zero) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_7 = llvm.and %valid_6, %check_7 : i1
    llvm.store %old_7, %address_7 : i32, !llvm.ptr
    // narrow: huge_load
    %address_8 = llvm.getelementptr %narrow[92] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_8 = llvm.mlir.constant(4294967295 : i32) : i32
    %old_8 = llvm.mlir.constant(0 : i32) : i32
    llvm.store %bad_8, %address_8 : i32, !llvm.ptr
    %check_8 = llvm.call @check_narrow(%ctx, %narrow, %invalid, %z32, %zero) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_8 = llvm.and %valid_7, %check_8 : i1
    llvm.store %old_8, %address_8 : i32, !llvm.ptr
    // narrow: overwrite_store
    %address_9 = llvm.getelementptr %narrow[88] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_9 = llvm.mlir.constant(45 : i8) : i8
    %old_9 = llvm.mlir.constant(46 : i8) : i8
    llvm.store %bad_9, %address_9 : i8, !llvm.ptr
    %check_9 = llvm.call @check_narrow(%ctx, %narrow, %invalid, %z32, %zero) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_9 = llvm.and %valid_8, %check_9 : i1
    llvm.store %old_9, %address_9 : i8, !llvm.ptr
    // narrow: overwrite_across_boundary
    %address_10 = llvm.getelementptr %narrow[168] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_10 = llvm.mlir.constant(45 : i8) : i8
    %old_10 = llvm.mlir.constant(46 : i8) : i8
    llvm.store %bad_10, %address_10 : i8, !llvm.ptr
    %check_10 = llvm.call @check_narrow(%ctx, %narrow, %invalid, %z32, %zero) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_10 = llvm.and %valid_9, %check_10 : i1
    llvm.store %old_10, %address_10 : i8, !llvm.ptr
    // narrow: store_width
    %address_11 = llvm.getelementptr %narrow[73] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_11 = llvm.mlir.constant(8 : i8) : i8
    %old_11 = llvm.mlir.constant(4 : i8) : i8
    llvm.store %bad_11, %address_11 : i8, !llvm.ptr
    %check_11 = llvm.call @check_narrow(%ctx, %narrow, %invalid, %z32, %zero) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_11 = llvm.and %valid_10, %check_11 : i1
    llvm.store %old_11, %address_11 : i8, !llvm.ptr
    // narrow: load_width
    %address_12 = llvm.getelementptr %narrow[89] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_12 = llvm.mlir.constant(8 : i8) : i8
    %old_12 = llvm.mlir.constant(4 : i8) : i8
    llvm.store %bad_12, %address_12 : i8, !llvm.ptr
    %check_12 = llvm.call @check_narrow(%ctx, %narrow, %invalid, %z32, %zero) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_12 = llvm.and %valid_11, %check_12 : i1
    llvm.store %old_12, %address_12 : i8, !llvm.ptr
    // narrow: store_flags
    %address_13 = llvm.getelementptr %narrow[74] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_13 = llvm.mlir.constant(1 : i8) : i8
    %old_13 = llvm.mlir.constant(0 : i8) : i8
    llvm.store %bad_13, %address_13 : i8, !llvm.ptr
    %check_13 = llvm.call @check_narrow(%ctx, %narrow, %invalid, %z32, %zero) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_13 = llvm.and %valid_12, %check_13 : i1
    llvm.store %old_13, %address_13 : i8, !llvm.ptr
    // narrow: load_flags
    %address_14 = llvm.getelementptr %narrow[90] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_14 = llvm.mlir.constant(1 : i8) : i8
    %old_14 = llvm.mlir.constant(0 : i8) : i8
    llvm.store %bad_14, %address_14 : i8, !llvm.ptr
    %check_14 = llvm.call @check_narrow(%ctx, %narrow, %invalid, %z32, %zero) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_14 = llvm.and %valid_13, %check_14 : i1
    llvm.store %old_14, %address_14 : i8, !llvm.ptr
    // narrow: store_auxiliary
    %address_15 = llvm.getelementptr %narrow[80] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_15 = llvm.mlir.constant(1 : i64) : i64
    %old_15 = llvm.mlir.constant(0 : i64) : i64
    llvm.store %bad_15, %address_15 : i64, !llvm.ptr
    %check_15 = llvm.call @check_narrow(%ctx, %narrow, %invalid, %z32, %zero) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_15 = llvm.and %valid_14, %check_15 : i1
    llvm.store %old_15, %address_15 : i64, !llvm.ptr
    // narrow: load_auxiliary
    %address_16 = llvm.getelementptr %narrow[96] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_16 = llvm.mlir.constant(1 : i64) : i64
    %old_16 = llvm.mlir.constant(0 : i64) : i64
    llvm.store %bad_16, %address_16 : i64, !llvm.ptr
    %check_16 = llvm.call @check_narrow(%ctx, %narrow, %invalid, %z32, %zero) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_16 = llvm.and %valid_15, %check_16 : i1
    llvm.store %old_16, %address_16 : i64, !llvm.ptr
    // wide: store_before_value
    %address_17 = llvm.getelementptr %wide[32] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_17 = llvm.mlir.constant(45 : i8) : i8
    %old_17 = llvm.mlir.constant(1 : i8) : i8
    llvm.store %bad_17, %address_17 : i8, !llvm.ptr
    %check_17 = llvm.call @check_wide(%ctx, %wide, %invalid, %z32, %zero) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_17 = llvm.and %valid_16, %check_17 : i1
    llvm.store %old_17, %address_17 : i8, !llvm.ptr
    // wide: load_before_store
    %address_18 = llvm.getelementptr %wide[32] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_18 = llvm.mlir.constant(46 : i8) : i8
    %old_18 = llvm.mlir.constant(1 : i8) : i8
    llvm.store %bad_18, %address_18 : i8, !llvm.ptr
    %check_18 = llvm.call @check_wide(%ctx, %wide, %invalid, %z32, %zero) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_18 = llvm.and %valid_17, %check_18 : i1
    llvm.store %old_18, %address_18 : i8, !llvm.ptr
    // wide: noncontiguous_store
    %address_19 = llvm.getelementptr %wide[88] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_19 = llvm.mlir.constant(1 : i32) : i32
    %old_19 = llvm.mlir.constant(0 : i32) : i32
    llvm.store %bad_19, %address_19 : i32, !llvm.ptr
    %check_19 = llvm.call @check_wide(%ctx, %wide, %invalid, %z32, %zero) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_19 = llvm.and %valid_18, %check_19 : i1
    llvm.store %old_19, %address_19 : i32, !llvm.ptr
    // wide: huge_store
    %address_20 = llvm.getelementptr %wide[88] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_20 = llvm.mlir.constant(4294967295 : i32) : i32
    %old_20 = llvm.mlir.constant(0 : i32) : i32
    llvm.store %bad_20, %address_20 : i32, !llvm.ptr
    %check_20 = llvm.call @check_wide(%ctx, %wide, %invalid, %z32, %zero) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_20 = llvm.and %valid_19, %check_20 : i1
    llvm.store %old_20, %address_20 : i32, !llvm.ptr
    // wide: unknown_load
    %address_21 = llvm.getelementptr %wide[104] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_21 = llvm.mlir.constant(1 : i32) : i32
    %old_21 = llvm.mlir.constant(0 : i32) : i32
    llvm.store %bad_21, %address_21 : i32, !llvm.ptr
    %check_21 = llvm.call @check_wide(%ctx, %wide, %invalid, %z32, %zero) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_21 = llvm.and %valid_20, %check_21 : i1
    llvm.store %old_21, %address_21 : i32, !llvm.ptr
    // wide: huge_load
    %address_22 = llvm.getelementptr %wide[104] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_22 = llvm.mlir.constant(4294967295 : i32) : i32
    %old_22 = llvm.mlir.constant(0 : i32) : i32
    llvm.store %bad_22, %address_22 : i32, !llvm.ptr
    %check_22 = llvm.call @check_wide(%ctx, %wide, %invalid, %z32, %zero) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_22 = llvm.and %valid_21, %check_22 : i1
    llvm.store %old_22, %address_22 : i32, !llvm.ptr
    // wide: overwrite_store
    %address_23 = llvm.getelementptr %wide[96] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_23 = llvm.mlir.constant(45 : i8) : i8
    %old_23 = llvm.mlir.constant(46 : i8) : i8
    llvm.store %bad_23, %address_23 : i8, !llvm.ptr
    %check_23 = llvm.call @check_wide(%ctx, %wide, %invalid, %z32, %zero) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_23 = llvm.and %valid_22, %check_23 : i1
    llvm.store %old_23, %address_23 : i8, !llvm.ptr
    // wide: overwrite_across_boundary
    %address_24 = llvm.getelementptr %wide[176] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_24 = llvm.mlir.constant(45 : i8) : i8
    %old_24 = llvm.mlir.constant(46 : i8) : i8
    llvm.store %bad_24, %address_24 : i8, !llvm.ptr
    %check_24 = llvm.call @check_wide(%ctx, %wide, %invalid, %z32, %zero) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_24 = llvm.and %valid_23, %check_24 : i1
    llvm.store %old_24, %address_24 : i8, !llvm.ptr
    // wide: store_width
    %address_25 = llvm.getelementptr %wide[84] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_25 = llvm.mlir.constant(129 : i32) : i32
    %old_25 = llvm.mlir.constant(130 : i32) : i32
    llvm.store %bad_25, %address_25 : i32, !llvm.ptr
    %check_25 = llvm.call @check_wide(%ctx, %wide, %invalid, %z32, %zero) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_25 = llvm.and %valid_24, %check_25 : i1
    llvm.store %old_25, %address_25 : i32, !llvm.ptr
    // wide: load_width
    %address_26 = llvm.getelementptr %wide[100] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_26 = llvm.mlir.constant(129 : i32) : i32
    %old_26 = llvm.mlir.constant(130 : i32) : i32
    llvm.store %bad_26, %address_26 : i32, !llvm.ptr
    %check_26 = llvm.call @check_wide(%ctx, %wide, %invalid, %z32, %zero) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_26 = llvm.and %valid_25, %check_26 : i1
    llvm.store %old_26, %address_26 : i32, !llvm.ptr
    // wide: store_flags
    %address_27 = llvm.getelementptr %wide[81] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_27 = llvm.mlir.constant(1 : i8) : i8
    %old_27 = llvm.mlir.constant(0 : i8) : i8
    llvm.store %bad_27, %address_27 : i8, !llvm.ptr
    %check_27 = llvm.call @check_wide(%ctx, %wide, %invalid, %z32, %zero) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_27 = llvm.and %valid_26, %check_27 : i1
    llvm.store %old_27, %address_27 : i8, !llvm.ptr
    // wide: load_flags
    %address_28 = llvm.getelementptr %wide[97] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_28 = llvm.mlir.constant(1 : i8) : i8
    %old_28 = llvm.mlir.constant(0 : i8) : i8
    llvm.store %bad_28, %address_28 : i8, !llvm.ptr
    %check_28 = llvm.call @check_wide(%ctx, %wide, %invalid, %z32, %zero) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_28 = llvm.and %valid_27, %check_28 : i1
    llvm.store %old_28, %address_28 : i8, !llvm.ptr
    // wide: store_auxiliary
    %address_29 = llvm.getelementptr %wide[92] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_29 = llvm.mlir.constant(1 : i32) : i32
    %old_29 = llvm.mlir.constant(0 : i32) : i32
    llvm.store %bad_29, %address_29 : i32, !llvm.ptr
    %check_29 = llvm.call @check_wide(%ctx, %wide, %invalid, %z32, %zero) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_29 = llvm.and %valid_28, %check_29 : i1
    llvm.store %old_29, %address_29 : i32, !llvm.ptr
    // wide: load_auxiliary
    %address_30 = llvm.getelementptr %wide[108] : (!llvm.ptr) -> !llvm.ptr, i8
    %bad_30 = llvm.mlir.constant(1 : i32) : i32
    %old_30 = llvm.mlir.constant(0 : i32) : i32
    llvm.store %bad_30, %address_30 : i32, !llvm.ptr
    %check_30 = llvm.call @check_wide(%ctx, %wide, %invalid, %z32, %zero) : (!llvm.ptr, !llvm.ptr, i32, i32, i64) -> i1
    %valid_30 = llvm.and %valid_29, %check_30 : i1
    llvm.store %old_30, %address_30 : i32, !llvm.ptr
    %ok_text = llvm.mlir.addressof @ok : !llvm.ptr
    %bad_text = llvm.mlir.addressof @bad : !llvm.ptr
    %text = llvm.select %valid_30, %ok_text, %bad_text : i1, !llvm.ptr
    %printed = llvm.call @puts(%text) : (!llvm.ptr) -> i32
    llvm.call @obelisk_rt_v1_context_destroy(%ctx) : (!llvm.ptr) -> ()
    llvm.return %z32 : i32
  }
}
