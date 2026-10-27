// RUN: mlir-translate %s --mlir-to-llvmir | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %target_cxx %t.o %runtime_link_flags -o %t.exe
// RUN: %t.exe | FileCheck %s --check-prefix=RUNTIME
//
// IEEE 1800-2023 18.4.1: real values are equally likely per numeric interval,
// so (0,1) and (1,2) split (0,2) evenly, which an equally weighted IEEE-encoding
// sampler would not. Numeric ranks count half-minimum-subnormal units and keep
// both zero encodings. The tables cover rank round trips and stepping, malformed
// encodings and ranks, empty, singleton and invalid intervals, and the density
// example, in both precisions.
// RUNTIME: real numeric ranks valid 1

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "aarch64-unknown-linux-gnu"} {
  llvm.mlir.global @talos_final_time(0 : i64) : i64
  llvm.mlir.global @talos_time_precision_fs(1 : i64) : i64
  llvm.mlir.global internal constant @ok("real numeric ranks valid 1\00")
  llvm.mlir.global internal constant @bad("real numeric ranks valid 0\00")
  llvm.func @puts(!llvm.ptr) -> i32
  llvm.func @talos_rt_v1_random_state_seed(!llvm.ptr, i64, i64)
  llvm.func @talos_rt_v1_random_real64_to_rank(f64, !llvm.ptr, i64) -> i32
  llvm.func @talos_rt_v1_random_real32_to_rank(f32, !llvm.ptr, i64) -> i32
  llvm.func @talos_rt_v1_random_real64_from_rank(!llvm.ptr, i64, !llvm.ptr) -> i32
  llvm.func @talos_rt_v1_random_real32_from_rank(!llvm.ptr, i64, !llvm.ptr) -> i32
  llvm.func @talos_rt_v1_random_real64_interval(!llvm.ptr, f64, f64, i32, !llvm.ptr, !llvm.ptr) -> i32
  llvm.func @talos_rt_v1_random_real32_interval(!llvm.ptr, f32, f32, i32, !llvm.ptr, !llvm.ptr) -> i32

  // Each encoding and its sign-flipped twin round-trip through the numeric rank: zeros, smallest subnormals, the subnormal/normal boundary, powers of two and the largest finite values. Columns: width, bits. The first 14 rows are double, the rest single.
  llvm.mlir.global internal constant @rows_roundtrip(dense<[28, 2,
    1, 0x0,
    1, 0x1,
    1, 0x2,
    1, 0xFFFFFFFFFFFFF,
    1, 0x10000000000000,
    1, 0x10000000000001,
    1, 0x20000000000000,
    1, 0x3FDFFFFFFFFFFFFF,
    1, 0x3FE0000000000000,
    1, 0x3FF0000000000000,
    1, 0x3FF0000000000001,
    1, 0x4000000000000000,
    1, 0x123456789ABCDEF0,
    1, 0x7FEFFFFFFFFFFFFF,
    0, 0x0,
    0, 0x1,
    0, 0x2,
    0, 0x7FFFFF,
    0, 0x800000,
    0, 0x800001,
    0, 0x1000000,
    0, 0x3EFFFFFF,
    0, 0x3F000000,
    0, 0x3F800000,
    0, 0x3F800001,
    0, 0x40000000,
    0, 0x12345678,
    0, 0x7F7FFFFF]> : tensor<58xi64>) : !llvm.array<58 x i64>
  // Adjacent ranks decode to adjacent IEEE values across zero, the sign boundary and 1.0; stepping past the largest finite value gives status 2 and keeps the sentinel. Columns: width, bits, subtract, expected status, expected bits. The first 14 rows are double, the rest single.
  llvm.mlir.global internal constant @rows_check_step(dense<[28, 5,
    1, 0x0, 0, 0, 0x0,
    1, 0x0, 1, 0, 0x8000000000000000,
    1, 0x8000000000000000, 0, 0, 0x0,
    1, 0x8000000000000000, 1, 0, 0x8000000000000001,
    1, 0x1, 0, 0, 0x1,
    1, 0x1, 1, 0, 0x0,
    1, 0x8000000000000001, 1, 0, 0x8000000000000001,
    1, 0x8000000000000001, 0, 0, 0x8000000000000000,
    1, 0x3FF0000000000000, 0, 0, 0x3FF0000000000000,
    1, 0x3FF0000000000000, 1, 0, 0x3FEFFFFFFFFFFFFF,
    1, 0xBFF0000000000000, 0, 0, 0xBFEFFFFFFFFFFFFF,
    1, 0xBFF0000000000000, 1, 0, 0xBFF0000000000000,
    1, 0x7FEFFFFFFFFFFFFF, 0, 2, 0x4045000000000000,
    1, 0xFFEFFFFFFFFFFFFF, 1, 2, 0x4045000000000000,
    0, 0x0, 0, 0, 0x0,
    0, 0x0, 1, 0, 0x80000000,
    0, 0x80000000, 0, 0, 0x0,
    0, 0x80000000, 1, 0, 0x80000001,
    0, 0x1, 0, 0, 0x1,
    0, 0x1, 1, 0, 0x0,
    0, 0x80000001, 1, 0, 0x80000001,
    0, 0x80000001, 0, 0, 0x80000000,
    0, 0x3F800000, 0, 0, 0x3F800000,
    0, 0x3F800000, 1, 0, 0x3F7FFFFF,
    0, 0xBF800000, 0, 0, 0xBF7FFFFF,
    0, 0xBF800000, 1, 0, 0xBF800000,
    0, 0x7F7FFFFF, 0, 2, 0x42280000,
    0, 0xFF7FFFFF, 1, 2, 0x42280000]> : tensor<142xi64>) : !llvm.array<142 x i64>
  // Infinities, NaNs and a rank buffer one word short are rejected with status 2 and the buffer is untouched. Columns: width, bits, rank words. The first 5 rows are double, the rest single.
  llvm.mlir.global internal constant @rows_reject_encoding(dense<[10, 3,
    1, 0x7FF0000000000000, 33,
    1, 0xFFF0000000000000, 33,
    1, 0x7FF000000000007B, 33,
    1, 0xFFF0000000001000, 33,
    1, 0x0, 32,
    0, 0x7F800000, 5,
    0, 0xFF800000, 5,
    0, 0x7F80007B, 5,
    0, 0xFF801000, 5,
    0, 0x0, 4]> : tensor<32xi64>) : !llvm.array<32 x i64>
  // Empty, singleton and invalid intervals return the exact endpoint without consuming RNG state; flag bit 0 opens the lower and bit 1 the upper bound; non-finite bounds, bad flags or increments give status 2. Columns: width, lower, upper, flags, increment, expected status, expected success, expected bits. The first 18 rows are double, the rest single.
  llvm.mlir.global internal constant @rows_check_interval(dense<[36, 8,
    1, 0x0, 0x0, 0, 5, 0, 1, 0x0,
    1, 0x8000000000000000, 0x0, 0, 5, 0, 1, 0x8000000000000000,
    1, 0x0, 0x0, 3, 5, 0, 0, 0x4045000000000000,
    1, 0x3FF0000000000000, 0x3FF0000000000000, 0, 5, 0, 1, 0x3FF0000000000000,
    1, 0x3FF0000000000000, 0x3FF0000000000001, 3, 5, 0, 0, 0x4045000000000000,
    1, 0x3FF0000000000000, 0x3FF0000000000001, 1, 5, 0, 1, 0x3FF0000000000001,
    1, 0x3FF0000000000000, 0x3FF0000000000001, 2, 5, 0, 1, 0x3FF0000000000000,
    1, 0x7FEFFFFFFFFFFFFF, 0x7FEFFFFFFFFFFFFF, 1, 5, 0, 0, 0x4045000000000000,
    1, 0xFFEFFFFFFFFFFFFF, 0xFFEFFFFFFFFFFFFF, 2, 5, 0, 0, 0x4045000000000000,
    1, 0x3FF0000000000000, 0x0, 0, 5, 0, 0, 0x4045000000000000,
    1, 0x1, 0x1, 0, 5, 0, 1, 0x1,
    1, 0x1, 0x2, 3, 5, 0, 0, 0x4045000000000000,
    1, 0x1, 0x2, 1, 5, 0, 1, 0x2,
    1, 0x8000000000000002, 0x8000000000000001, 2, 5, 0, 1, 0x8000000000000002,
    1, 0x0, 0x3FF0000000000000, 8, 5, 2, 7, 0x4045000000000000,
    1, 0x0, 0x3FF0000000000000, 0, 2, 2, 7, 0x4045000000000000,
    1, 0x0, 0x7FF0000000000000, 0, 5, 2, 7, 0x4045000000000000,
    1, 0x7FF000000000007B, 0x3FF0000000000000, 0, 5, 2, 7, 0x4045000000000000,
    0, 0x0, 0x0, 0, 5, 0, 1, 0x0,
    0, 0x80000000, 0x0, 0, 5, 0, 1, 0x80000000,
    0, 0x0, 0x0, 3, 5, 0, 0, 0x42280000,
    0, 0x3F800000, 0x3F800000, 0, 5, 0, 1, 0x3F800000,
    0, 0x3F800000, 0x3F800001, 3, 5, 0, 0, 0x42280000,
    0, 0x3F800000, 0x3F800001, 1, 5, 0, 1, 0x3F800001,
    0, 0x3F800000, 0x3F800001, 2, 5, 0, 1, 0x3F800000,
    0, 0x7F7FFFFF, 0x7F7FFFFF, 1, 5, 0, 0, 0x42280000,
    0, 0xFF7FFFFF, 0xFF7FFFFF, 2, 5, 0, 0, 0x42280000,
    0, 0x3F800000, 0x0, 0, 5, 0, 0, 0x42280000,
    0, 0x1, 0x1, 0, 5, 0, 1, 0x1,
    0, 0x1, 0x2, 3, 5, 0, 0, 0x42280000,
    0, 0x1, 0x2, 1, 5, 0, 1, 0x2,
    0, 0x80000002, 0x80000001, 2, 5, 0, 1, 0x80000002,
    0, 0x0, 0x3F800000, 8, 5, 2, 7, 0x42280000,
    0, 0x0, 0x3F800000, 0, 2, 2, 7, 0x42280000,
    0, 0x0, 0x7F800000, 0, 5, 2, 7, 0x42280000,
    0, 0x7F80007B, 0x3F800000, 0, 5, 2, 7, 0x42280000]> : tensor<290xi64>) : !llvm.array<290 x i64>
  // IEEE 1800-2023 18.4.1: about half of 4096 uniform draws fall on each side of the numeric midpoint. Columns: width, lower, upper, midpoint. The first 5 rows are double, the rest single.
  llvm.mlir.global internal constant @rows_density(dense<[10, 4,
    1, 0x0, 0x4000000000000000, 0x3FF0000000000000,
    1, 0xC000000000000000, 0x0, 0xBFF0000000000000,
    1, 0xBFF0000000000000, 0x3FF0000000000000, 0x0,
    1, 0xFFEFFFFFFFFFFFFF, 0x7FEFFFFFFFFFFFFF, 0x0,
    1, 0x7FD0000000000000, 0x7FE0000000000000, 0x7FD8000000000000,
    0, 0x0, 0x40000000, 0x3F800000,
    0, 0xC0000000, 0x0, 0xBF800000,
    0, 0xBF800000, 0x3F800000, 0x0,
    0, 0xFF7FFFFF, 0x7F7FFFFF, 0x0,
    0, 0x7E000000, 0x7E800000, 0x7E400000]> : tensor<42xi64>) : !llvm.array<42 x i64>
  // Both widths of the corrupt-rank cases.
  llvm.mlir.global internal constant @rows_reject_ranks(dense<[2, 1, 1, 0]> : tensor<4xi64>) : !llvm.array<4 x i64>

  // Advance one signed coordinate without interpreting the floating payload.
  llvm.func internal @rank_step(%rank: !llvm.ptr, %count: i64, %highmask: i64, %subtract: i1) {
    %zero = llvm.mlir.constant(0 : i64) : i64
    %one = llvm.mlir.constant(1 : i64) : i64
    %last = llvm.sub %count, %one : i64
    llvm.br ^word(%zero, %one : i64, i64)
  ^word(%index: i64, %carry: i64):
    %ptr = llvm.getelementptr %rank[%index] : (!llvm.ptr, i64) -> !llvm.ptr, i64
    %old = llvm.load %ptr : !llvm.ptr -> i64
    %added = llvm.add %old, %carry : i64
    %removed = llvm.sub %old, %carry : i64
    %value = llvm.select %subtract, %removed, %added : i1, i64
    %borrow = llvm.icmp "ult" %old, %carry : i64
    %overflow = llvm.icmp "ult" %added, %old : i64
    %next_bit = llvm.select %subtract, %borrow, %overflow : i1, i1
    %next_carry = llvm.zext %next_bit : i1 to i64
    %is_last = llvm.icmp "eq" %index, %last : i64
    %masked = llvm.and %value, %highmask : i64
    %stored = llvm.select %is_last, %masked, %value : i1, i64
    llvm.store %stored, %ptr : i64, !llvm.ptr
    %next = llvm.add %index, %one : i64
    %more = llvm.icmp "ult" %next, %count : i64
    llvm.cond_br %more, ^word(%next, %next_carry : i64, i64), ^done
  ^done:
    llvm.return
  }


  // Width adapters: a row column holds raw IEEE bits and a width flag (true
  // for double, false for single) selects the C ABI entry points.
  llvm.func internal @bits_to_f64(%bits: i64, %wide: i1) -> f64 {
    %double = llvm.bitcast %bits : i64 to f64
    %low = llvm.trunc %bits : i64 to i32
    %single = llvm.bitcast %low : i32 to f32
    %extended = llvm.fpext %single : f32 to f64
    %value = llvm.select %wide, %double, %extended : i1, f64
    llvm.return %value : f64
  }

  llvm.func internal @to_rank(%bits: i64, %rank: !llvm.ptr, %n: i64, %wide: i1) -> i32 {
    llvm.cond_br %wide, ^double, ^single
  ^double:
    %d = llvm.bitcast %bits : i64 to f64
    %ds = llvm.call @talos_rt_v1_random_real64_to_rank(%d, %rank, %n) : (f64, !llvm.ptr, i64) -> i32
    llvm.return %ds : i32
  ^single:
    %low = llvm.trunc %bits : i64 to i32
    %s = llvm.bitcast %low : i32 to f32
    %ss = llvm.call @talos_rt_v1_random_real32_to_rank(%s, %rank, %n) : (f32, !llvm.ptr, i64) -> i32
    llvm.return %ss : i32
  }

  llvm.func internal @from_rank(%rank: !llvm.ptr, %n: i64, %out: !llvm.ptr, %wide: i1) -> i32 {
    llvm.cond_br %wide, ^double, ^single
  ^double:
    %ds = llvm.call @talos_rt_v1_random_real64_from_rank(%rank, %n, %out) : (!llvm.ptr, i64, !llvm.ptr) -> i32
    llvm.return %ds : i32
  ^single:
    %ss = llvm.call @talos_rt_v1_random_real32_from_rank(%rank, %n, %out) : (!llvm.ptr, i64, !llvm.ptr) -> i32
    llvm.return %ss : i32
  }

  llvm.func internal @interval(%state: !llvm.ptr, %lower: i64, %upper: i64, %flags: i32, %out: !llvm.ptr, %success: !llvm.ptr, %wide: i1) -> i32 {
    llvm.cond_br %wide, ^double, ^single
  ^double:
    %dl = llvm.bitcast %lower : i64 to f64
    %du = llvm.bitcast %upper : i64 to f64
    %ds = llvm.call @talos_rt_v1_random_real64_interval(%state, %dl, %du, %flags, %out, %success) : (!llvm.ptr, f64, f64, i32, !llvm.ptr, !llvm.ptr) -> i32
    llvm.return %ds : i32
  ^single:
    %l32 = llvm.trunc %lower : i64 to i32
    %u32 = llvm.trunc %upper : i64 to i32
    %sl = llvm.bitcast %l32 : i32 to f32
    %su = llvm.bitcast %u32 : i32 to f32
    %ss = llvm.call @talos_rt_v1_random_real32_interval(%state, %sl, %su, %flags, %out, %success) : (!llvm.ptr, f32, f32, i32, !llvm.ptr, !llvm.ptr) -> i32
    llvm.return %ss : i32
  }

  // The decoded value, as raw bits zero-extended from the stored width.
  llvm.func internal @read_bits(%out: !llvm.ptr, %wide: i1) -> i64 {
    %double = llvm.load %out : !llvm.ptr -> i64
    %single = llvm.load %out : !llvm.ptr -> i32
    %extended = llvm.zext %single : i32 to i64
    %bits = llvm.select %wide, %double, %extended : i1, i64
    llvm.return %bits : i64
  }

  // 42.0 in the selected width.
  llvm.func internal @sentinel_bits(%wide: i1) -> i64 {
    %double = llvm.mlir.constant(0x4045000000000000 : i64) : i64
    %single = llvm.mlir.constant(0x42280000 : i64) : i64
    %bits = llvm.select %wide, %double, %single : i1, i64
    llvm.return %bits : i64
  }

  llvm.func internal @rank_words(%wide: i1) -> i64 {
    %double = llvm.mlir.constant(33 : i64) : i64
    %single = llvm.mlir.constant(5 : i64) : i64
    %n = llvm.select %wide, %double, %single : i1, i64
    llvm.return %n : i64
  }

  llvm.func internal @column(%row: !llvm.ptr, %index: i64) -> i64 {
    %ptr = llvm.getelementptr %row[%index] : (!llvm.ptr, i64) -> !llvm.ptr, i64
    %value = llvm.load %ptr : !llvm.ptr -> i64
    llvm.return %value : i64
  }

  llvm.func internal @roundtrip_bits(%bits: i64, %wide: i1) -> i1 {
    %c0 = llvm.mlir.constant(0 : i64) : i64
    %c1 = llvm.mlir.constant(1 : i64) : i64
    %zero = llvm.mlir.constant(0 : i32) : i32
    %n = llvm.call @rank_words(%wide) : (i1) -> i64
    %rank = llvm.alloca %n x i64 : (i64) -> !llvm.ptr
    %out = llvm.alloca %c1 x i64 : (i64) -> !llvm.ptr
    llvm.store %c0, %out : i64, !llvm.ptr
    %encoded = llvm.call @to_rank(%bits, %rank, %n, %wide) : (i64, !llvm.ptr, i64, i1) -> i32
    %decoded = llvm.call @from_rank(%rank, %n, %out, %wide) : (!llvm.ptr, i64, !llvm.ptr, i1) -> i32
    %result = llvm.call @read_bits(%out, %wide) : (!llvm.ptr, i1) -> i64
    %match = llvm.icmp "eq" %bits, %result : i64
    %status = llvm.or %encoded, %decoded : i32
    %valid_status = llvm.icmp "eq" %status, %zero : i32
    %valid = llvm.and %valid_status, %match : i1
    llvm.return %valid : i1
  }

  // Columns: width, bits. Each encoding and its sign-flipped twin round-trip.
  llvm.func internal @roundtrip(%row: !llvm.ptr) -> i1 {
    %c0 = llvm.mlir.constant(0 : i64) : i64
    %c1 = llvm.mlir.constant(1 : i64) : i64
    %double_sign = llvm.mlir.constant(0x8000000000000000 : i64) : i64
    %single_sign = llvm.mlir.constant(0x80000000 : i64) : i64
    %wide64 = llvm.call @column(%row, %c0) : (!llvm.ptr, i64) -> i64
    %wide = llvm.trunc %wide64 : i64 to i1
    %bits = llvm.call @column(%row, %c1) : (!llvm.ptr, i64) -> i64
    %sign = llvm.select %wide, %double_sign, %single_sign : i1, i64
    %flipped = llvm.xor %bits, %sign : i64
    %positive = llvm.call @roundtrip_bits(%bits, %wide) : (i64, i1) -> i1
    %negative = llvm.call @roundtrip_bits(%flipped, %wide) : (i64, i1) -> i1
    %valid = llvm.and %positive, %negative : i1
    llvm.return %valid : i1
  }

  // Columns: width, bits, subtract, expected decode status, expected bits.
  llvm.func internal @check_step(%row: !llvm.ptr) -> i1 {
    %c0 = llvm.mlir.constant(0 : i64) : i64
    %c1 = llvm.mlir.constant(1 : i64) : i64
    %c2 = llvm.mlir.constant(2 : i64) : i64
    %c3 = llvm.mlir.constant(3 : i64) : i64
    %c4 = llvm.mlir.constant(4 : i64) : i64
    %zero = llvm.mlir.constant(0 : i32) : i32
    %double_mask = llvm.mlir.constant(4503599627370495 : i64) : i64
    %single_mask = llvm.mlir.constant(8388607 : i64) : i64
    %wide64 = llvm.call @column(%row, %c0) : (!llvm.ptr, i64) -> i64
    %wide = llvm.trunc %wide64 : i64 to i1
    %bits = llvm.call @column(%row, %c1) : (!llvm.ptr, i64) -> i64
    %subtract64 = llvm.call @column(%row, %c2) : (!llvm.ptr, i64) -> i64
    %subtract = llvm.trunc %subtract64 : i64 to i1
    %status64 = llvm.call @column(%row, %c3) : (!llvm.ptr, i64) -> i64
    %expect_status = llvm.trunc %status64 : i64 to i32
    %expect_bits = llvm.call @column(%row, %c4) : (!llvm.ptr, i64) -> i64
    %mask = llvm.select %wide, %double_mask, %single_mask : i1, i64
    %n = llvm.call @rank_words(%wide) : (i1) -> i64
    %rank = llvm.alloca %n x i64 : (i64) -> !llvm.ptr
    %out = llvm.alloca %c1 x i64 : (i64) -> !llvm.ptr
    %sentinel = llvm.call @sentinel_bits(%wide) : (i1) -> i64
    llvm.store %sentinel, %out : i64, !llvm.ptr
    %encoded = llvm.call @to_rank(%bits, %rank, %n, %wide) : (i64, !llvm.ptr, i64, i1) -> i32
    llvm.call @rank_step(%rank, %n, %mask, %subtract) : (!llvm.ptr, i64, i64, i1) -> ()
    %decoded = llvm.call @from_rank(%rank, %n, %out, %wide) : (!llvm.ptr, i64, !llvm.ptr, i1) -> i32
    %result = llvm.call @read_bits(%out, %wide) : (!llvm.ptr, i1) -> i64
    %match = llvm.icmp "eq" %expect_bits, %result : i64
    %valid_encode = llvm.icmp "eq" %encoded, %zero : i32
    %valid_decode = llvm.icmp "eq" %decoded, %expect_status : i32
    %valid_status = llvm.and %valid_encode, %valid_decode : i1
    %valid = llvm.and %valid_status, %match : i1
    llvm.return %valid : i1
  }

  // Columns: width, bits, rank word count.
  llvm.func internal @reject_encoding(%row: !llvm.ptr) -> i1 {
    %c0 = llvm.mlir.constant(0 : i64) : i64
    %c1 = llvm.mlir.constant(1 : i64) : i64
    %c2 = llvm.mlir.constant(2 : i64) : i64
    %invalid = llvm.mlir.constant(2 : i32) : i32
    %sentinel = llvm.mlir.constant(42 : i64) : i64
    %wide64 = llvm.call @column(%row, %c0) : (!llvm.ptr, i64) -> i64
    %wide = llvm.trunc %wide64 : i64 to i1
    %bits = llvm.call @column(%row, %c1) : (!llvm.ptr, i64) -> i64
    %count = llvm.call @column(%row, %c2) : (!llvm.ptr, i64) -> i64
    %n = llvm.call @rank_words(%wide) : (i1) -> i64
    %rank = llvm.alloca %n x i64 : (i64) -> !llvm.ptr
    llvm.store %sentinel, %rank : i64, !llvm.ptr
    %encoded = llvm.call @to_rank(%bits, %rank, %count, %wide) : (i64, !llvm.ptr, i64, i1) -> i32
    %result = llvm.load %rank : !llvm.ptr -> i64
    %match = llvm.icmp "eq" %sentinel, %result : i64
    %rejected = llvm.icmp "eq" %encoded, %invalid : i32
    %valid = llvm.and %rejected, %match : i1
    llvm.return %valid : i1
  }

  // Columns: width, lower, upper, flags, increment, expected status,
  // expected success, expected bits.
  llvm.func internal @check_interval(%row: !llvm.ptr) -> i1 {
    %c0 = llvm.mlir.constant(0 : i64) : i64
    %c1 = llvm.mlir.constant(1 : i64) : i64
    %c2 = llvm.mlir.constant(2 : i64) : i64
    %c3 = llvm.mlir.constant(3 : i64) : i64
    %c4 = llvm.mlir.constant(4 : i64) : i64
    %c5 = llvm.mlir.constant(5 : i64) : i64
    %c6 = llvm.mlir.constant(6 : i64) : i64
    %c7 = llvm.mlir.constant(7 : i64) : i64
    %seed = llvm.mlir.constant(13 : i64) : i64
    %seven = llvm.mlir.constant(7 : i32) : i32
    %wide64 = llvm.call @column(%row, %c0) : (!llvm.ptr, i64) -> i64
    %wide = llvm.trunc %wide64 : i64 to i1
    %lower = llvm.call @column(%row, %c1) : (!llvm.ptr, i64) -> i64
    %upper = llvm.call @column(%row, %c2) : (!llvm.ptr, i64) -> i64
    %flags64 = llvm.call @column(%row, %c3) : (!llvm.ptr, i64) -> i64
    %flags = llvm.trunc %flags64 : i64 to i32
    %increment = llvm.call @column(%row, %c4) : (!llvm.ptr, i64) -> i64
    %status64 = llvm.call @column(%row, %c5) : (!llvm.ptr, i64) -> i64
    %expect_status = llvm.trunc %status64 : i64 to i32
    %success64 = llvm.call @column(%row, %c6) : (!llvm.ptr, i64) -> i64
    %expect_success = llvm.trunc %success64 : i64 to i32
    %expect_bits = llvm.call @column(%row, %c7) : (!llvm.ptr, i64) -> i64
    %state = llvm.alloca %c2 x i64 : (i64) -> !llvm.ptr
    %increment_ptr = llvm.getelementptr %state[1] : (!llvm.ptr) -> !llvm.ptr, i64
    llvm.store %seed, %state : i64, !llvm.ptr
    llvm.store %increment, %increment_ptr : i64, !llvm.ptr
    %out = llvm.alloca %c1 x i64 : (i64) -> !llvm.ptr
    %success = llvm.alloca %c1 x i32 : (i64) -> !llvm.ptr
    %sentinel = llvm.call @sentinel_bits(%wide) : (i1) -> i64
    llvm.store %sentinel, %out : i64, !llvm.ptr
    llvm.store %seven, %success : i32, !llvm.ptr
    %status = llvm.call @interval(%state, %lower, %upper, %flags, %out, %success, %wide) : (!llvm.ptr, i64, i64, i32, !llvm.ptr, !llvm.ptr, i1) -> i32
    %result_bits = llvm.call @read_bits(%out, %wide) : (!llvm.ptr, i1) -> i64
    %result_success = llvm.load %success : !llvm.ptr -> i32
    %next_state = llvm.load %state : !llvm.ptr -> i64
    %next_increment = llvm.load %increment_ptr : !llvm.ptr -> i64
    %v0 = llvm.icmp "eq" %status, %expect_status : i32
    %v1 = llvm.icmp "eq" %result_success, %expect_success : i32
    %v2 = llvm.icmp "eq" %result_bits, %expect_bits : i64
    %v3 = llvm.icmp "eq" %next_state, %seed : i64
    %v4 = llvm.icmp "eq" %next_increment, %increment : i64
    %a0 = llvm.and %v0, %v1 : i1
    %a1 = llvm.and %v2, %v3 : i1
    %a2 = llvm.and %a0, %a1 : i1
    %valid = llvm.and %a2, %v4 : i1
    llvm.return %valid : i1
  }

  // Columns: width, lower, upper, midpoint. Bounds are compared as exact
  // doubles, which holds for single values as well.
  llvm.func internal @density(%row: !llvm.ptr) -> i1 {
    %c0 = llvm.mlir.constant(0 : i64) : i64
    %c1 = llvm.mlir.constant(1 : i64) : i64
    %c2 = llvm.mlir.constant(2 : i64) : i64
    %c3 = llvm.mlir.constant(3 : i64) : i64
    %zero = llvm.mlir.constant(0 : i32) : i32
    %one32 = llvm.mlir.constant(1 : i32) : i32
    %flags = llvm.mlir.constant(3 : i32) : i32
    %seed = llvm.mlir.constant(13 : i64) : i64
    %sequence = llvm.mlir.constant(54 : i64) : i64
    %limit = llvm.mlir.constant(4096 : i32) : i32
    %min_count = llvm.mlir.constant(1800 : i32) : i32
    %max_count = llvm.mlir.constant(2300 : i32) : i32
    %true = llvm.mlir.constant(true) : i1
    %wide64 = llvm.call @column(%row, %c0) : (!llvm.ptr, i64) -> i64
    %wide = llvm.trunc %wide64 : i64 to i1
    %lower_bits = llvm.call @column(%row, %c1) : (!llvm.ptr, i64) -> i64
    %upper_bits = llvm.call @column(%row, %c2) : (!llvm.ptr, i64) -> i64
    %middle_bits = llvm.call @column(%row, %c3) : (!llvm.ptr, i64) -> i64
    %lower = llvm.call @bits_to_f64(%lower_bits, %wide) : (i64, i1) -> f64
    %upper = llvm.call @bits_to_f64(%upper_bits, %wide) : (i64, i1) -> f64
    %middle = llvm.call @bits_to_f64(%middle_bits, %wide) : (i64, i1) -> f64
    %state = llvm.alloca %c2 x i64 : (i64) -> !llvm.ptr
    %out = llvm.alloca %c1 x i64 : (i64) -> !llvm.ptr
    %success = llvm.alloca %c1 x i32 : (i64) -> !llvm.ptr
    llvm.call @talos_rt_v1_random_state_seed(%state, %seed, %sequence) : (!llvm.ptr, i64, i64) -> ()
    llvm.br ^sample(%zero, %zero, %true : i32, i32, i1)
  ^sample(%i: i32, %count: i32, %valid: i1):
    %status = llvm.call @interval(%state, %lower_bits, %upper_bits, %flags, %out, %success, %wide) : (!llvm.ptr, i64, i64, i32, !llvm.ptr, !llvm.ptr, i1) -> i32
    %value_bits = llvm.call @read_bits(%out, %wide) : (!llvm.ptr, i1) -> i64
    %value = llvm.call @bits_to_f64(%value_bits, %wide) : (i64, i1) -> f64
    %result_success = llvm.load %success : !llvm.ptr -> i32
    %good_status = llvm.icmp "eq" %status, %zero : i32
    %good_success = llvm.icmp "eq" %result_success, %one32 : i32
    %above = llvm.fcmp "ogt" %value, %lower : f64
    %below = llvm.fcmp "olt" %value, %upper : f64
    %good0 = llvm.and %good_status, %good_success : i1
    %good1 = llvm.and %above, %below : i1
    %good2 = llvm.and %good0, %good1 : i1
    %next_valid = llvm.and %good2, %valid : i1
    %left = llvm.fcmp "olt" %value, %middle : f64
    %left_int = llvm.zext %left : i1 to i32
    %next_count = llvm.add %count, %left_int : i32
    %next = llvm.add %i, %one32 : i32
    %more = llvm.icmp "ult" %next, %limit : i32
    llvm.cond_br %more, ^sample(%next, %next_count, %next_valid : i32, i32, i1), ^done(%next_count, %next_valid : i32, i1)
  ^done(%final_count: i32, %final_valid: i1):
    %enough = llvm.icmp "ugt" %final_count, %min_count : i32
    %balanced = llvm.icmp "ult" %final_count, %max_count : i32
    %ratio = llvm.and %enough, %balanced : i1
    %ok = llvm.and %ratio, %final_valid : i1
    llvm.return %ok : i1
  }

  // Columns: width. Corrupt rank buffers (padding bits above the stored
  // width, a value outside the rank range, a wrong word count, null
  // pointers) are rejected with status 2 and leave the output untouched.
  llvm.func internal @reject_ranks(%row: !llvm.ptr) -> i1 {
    %c0 = llvm.mlir.constant(0 : i64) : i64
    %c1 = llvm.mlir.constant(1 : i64) : i64
    %invalid = llvm.mlir.constant(2 : i32) : i32
    %double_padding = llvm.mlir.constant(4503599627370496 : i64) : i64
    %single_padding = llvm.mlir.constant(8388608 : i64) : i64
    %double_outside = llvm.mlir.constant(2251799813685248 : i64) : i64
    %single_outside = llvm.mlir.constant(4194304 : i64) : i64
    %zero_bits = llvm.mlir.constant(0 : i64) : i64
    %wide64 = llvm.call @column(%row, %c0) : (!llvm.ptr, i64) -> i64
    %wide = llvm.trunc %wide64 : i64 to i1
    %padding = llvm.select %wide, %double_padding, %single_padding : i1, i64
    %outside = llvm.select %wide, %double_outside, %single_outside : i1, i64
    %n = llvm.call @rank_words(%wide) : (i1) -> i64
    %wrong_n = llvm.sub %n, %c1 : i64
    %last = llvm.sub %n, %c1 : i64
    %rank = llvm.alloca %n x i64 : (i64) -> !llvm.ptr
    %out = llvm.alloca %c1 x i64 : (i64) -> !llvm.ptr
    %high = llvm.getelementptr %rank[%last] : (!llvm.ptr, i64) -> !llvm.ptr, i64
    %sentinel = llvm.call @sentinel_bits(%wide) : (i1) -> i64
    llvm.store %sentinel, %out : i64, !llvm.ptr
    %encoded = llvm.call @to_rank(%zero_bits, %rank, %n, %wide) : (i64, !llvm.ptr, i64, i1) -> i32
    llvm.store %padding, %high : i64, !llvm.ptr
    %s0 = llvm.call @from_rank(%rank, %n, %out, %wide) : (!llvm.ptr, i64, !llvm.ptr, i1) -> i32
    llvm.store %outside, %high : i64, !llvm.ptr
    %s1 = llvm.call @from_rank(%rank, %n, %out, %wide) : (!llvm.ptr, i64, !llvm.ptr, i1) -> i32
    %s2 = llvm.call @from_rank(%rank, %wrong_n, %out, %wide) : (!llvm.ptr, i64, !llvm.ptr, i1) -> i32
    %null = llvm.mlir.zero : !llvm.ptr
    %s3 = llvm.call @from_rank(%null, %n, %out, %wide) : (!llvm.ptr, i64, !llvm.ptr, i1) -> i32
    %s4 = llvm.call @to_rank(%zero_bits, %null, %n, %wide) : (i64, !llvm.ptr, i64, i1) -> i32
    %result = llvm.call @read_bits(%out, %wide) : (!llvm.ptr, i1) -> i64
    %preserved = llvm.icmp "eq" %result, %sentinel : i64
    %v0 = llvm.icmp "eq" %s0, %invalid : i32
    %v1 = llvm.icmp "eq" %s1, %invalid : i32
    %v2 = llvm.icmp "eq" %s2, %invalid : i32
    %v3 = llvm.icmp "eq" %s3, %invalid : i32
    %v4 = llvm.icmp "eq" %s4, %invalid : i32
    %a0 = llvm.and %v0, %v1 : i1
    %a1 = llvm.and %v2, %v3 : i1
    %a2 = llvm.and %a0, %a1 : i1
    %a3 = llvm.and %a2, %v4 : i1
    %valid = llvm.and %a3, %preserved : i1
    llvm.return %valid : i1
  }

  // A table starts with its row count and row width in words; @check is
  // applied to every row after that header.
  llvm.func internal @run_rows(%table: !llvm.ptr, %check: !llvm.ptr) -> i1 {
    %zero = llvm.mlir.constant(0 : i64) : i64
    %one = llvm.mlir.constant(1 : i64) : i64
    %true = llvm.mlir.constant(true) : i1
    %count = llvm.load %table : !llvm.ptr -> i64
    %stride_ptr = llvm.getelementptr %table[1] : (!llvm.ptr) -> !llvm.ptr, i64
    %stride = llvm.load %stride_ptr : !llvm.ptr -> i64
    %rows = llvm.getelementptr %table[2] : (!llvm.ptr) -> !llvm.ptr, i64
    llvm.br ^row(%zero, %true : i64, i1)
  ^row(%i: i64, %ok: i1):
    %offset = llvm.mul %i, %stride : i64
    %row = llvm.getelementptr %rows[%offset] : (!llvm.ptr, i64) -> !llvm.ptr, i64
    %result = llvm.call %check(%row) : !llvm.ptr, (!llvm.ptr) -> i1
    %all = llvm.and %ok, %result : i1
    %next = llvm.add %i, %one : i64
    %more = llvm.icmp "ult" %next, %count : i64
    llvm.cond_br %more, ^row(%next, %all : i64, i1), ^done(%all : i1)
  ^done(%final: i1):
    llvm.return %final : i1
  }

  llvm.func @main() -> i32 {
    %zero = llvm.mlir.constant(0 : i32) : i32
    %one = llvm.mlir.constant(1 : i32) : i32
    %valid_0 = llvm.mlir.constant(true) : i1
    %t_roundtrip = llvm.mlir.addressof @rows_roundtrip : !llvm.ptr
    %f_roundtrip = llvm.mlir.addressof @roundtrip : !llvm.ptr
    %r_roundtrip = llvm.call @run_rows(%t_roundtrip, %f_roundtrip) : (!llvm.ptr, !llvm.ptr) -> i1
    %v_roundtrip = llvm.and %valid_0, %r_roundtrip : i1
    %t_check_step = llvm.mlir.addressof @rows_check_step : !llvm.ptr
    %f_check_step = llvm.mlir.addressof @check_step : !llvm.ptr
    %r_check_step = llvm.call @run_rows(%t_check_step, %f_check_step) : (!llvm.ptr, !llvm.ptr) -> i1
    %v_check_step = llvm.and %v_roundtrip, %r_check_step : i1
    %t_reject_encoding = llvm.mlir.addressof @rows_reject_encoding : !llvm.ptr
    %f_reject_encoding = llvm.mlir.addressof @reject_encoding : !llvm.ptr
    %r_reject_encoding = llvm.call @run_rows(%t_reject_encoding, %f_reject_encoding) : (!llvm.ptr, !llvm.ptr) -> i1
    %v_reject_encoding = llvm.and %v_check_step, %r_reject_encoding : i1
    %t_check_interval = llvm.mlir.addressof @rows_check_interval : !llvm.ptr
    %f_check_interval = llvm.mlir.addressof @check_interval : !llvm.ptr
    %r_check_interval = llvm.call @run_rows(%t_check_interval, %f_check_interval) : (!llvm.ptr, !llvm.ptr) -> i1
    %v_check_interval = llvm.and %v_reject_encoding, %r_check_interval : i1
    %t_density = llvm.mlir.addressof @rows_density : !llvm.ptr
    %f_density = llvm.mlir.addressof @density : !llvm.ptr
    %r_density = llvm.call @run_rows(%t_density, %f_density) : (!llvm.ptr, !llvm.ptr) -> i1
    %v_density = llvm.and %v_check_interval, %r_density : i1
    %t_reject_ranks = llvm.mlir.addressof @rows_reject_ranks : !llvm.ptr
    %f_reject_ranks = llvm.mlir.addressof @reject_ranks : !llvm.ptr
    %r_reject_ranks = llvm.call @run_rows(%t_reject_ranks, %f_reject_ranks) : (!llvm.ptr, !llvm.ptr) -> i1
    %v_reject_ranks = llvm.and %v_density, %r_reject_ranks : i1
    llvm.cond_br %v_reject_ranks, ^pass, ^fail
  ^pass:
    %ok = llvm.mlir.addressof @ok : !llvm.ptr
    %printed = llvm.call @puts(%ok) : (!llvm.ptr) -> i32
    llvm.return %zero : i32
  ^fail:
    %bad = llvm.mlir.addressof @bad : !llvm.ptr
    %printed_bad = llvm.call @puts(%bad) : (!llvm.ptr) -> i32
    llvm.return %one : i32
  }
}
