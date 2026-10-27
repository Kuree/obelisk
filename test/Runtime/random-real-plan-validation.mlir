// RUN: mlir-translate %s --mlir-to-llvmir | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %target_cxx %t.o %runtime_link_flags -o %t.exe
// RUN: %t.exe | FileCheck %s --check-prefix=RUNTIME
//
// IEEE 1800-2023 18.4.1: typed real constraints run only from exact compiled
// plans. Both wire versions require REAL, UNIFORM and a compiled plan for
// typed IEEE instructions, and validate cell payloads, coordinate indices and
// captures before specialization. Every row rewrites a few bytes of a valid
// base program and checks the C ABI status, the success flag, the untouched
// assignment and the untouched RNG state.
// RUNTIME: real plan validation 1

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "aarch64-unknown-linux-gnu"} {
  llvm.mlir.global @talos_final_time(0 : i64) : i64
  llvm.mlir.global @talos_time_precision_fs(1 : i64) : i64
  llvm.mlir.global internal constant @ok("real plan validation 1\00")
  llvm.mlir.global internal constant @bad("real plan validation 0\00")
  llvm.func @puts(!llvm.ptr) -> i32
  llvm.func @talos_rt_v1_context_create(!llvm.ptr) -> i32
  llvm.func @talos_rt_v1_context_destroy(!llvm.ptr)
  llvm.func @talos_rt_v1_random_solve_modes_state(!llvm.ptr, !llvm.ptr, i64, i64, i64, i64, i64, i64, i64, !llvm.ptr, i64, !llvm.ptr, !llvm.ptr, !llvm.ptr) -> i32
  llvm.func @talos_rt_v1_random_solve_wide_modes_state(!llvm.ptr, !llvm.ptr, i64, !llvm.ptr, !llvm.ptr, i64, i64, i64, i64, i64, !llvm.ptr, i64, !llvm.ptr, i64, !llvm.ptr, !llvm.ptr, !llvm.ptr) -> i32
  // Base programs concatenated; a row selects one by byte offset and size.
  llvm.mlir.global internal constant @programs("ODR1\01\00\18\00\01\00\00\00\05\00\00\00\00\00\00\00 \06\00\00\01\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\03 \00\00\00\00\00\00\00\00\00\00\00\00\00\00/ \00\00\00\00\00\00\00\00\00\00\00\00\00\00\0D\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00 \01\00\00\FF\FF\FF\FF\00\00\00\00\00\00\00\00SAMP\01\00\00\00\02\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00h\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\04\00\00\00\03\00\00\00PLAN\01\00\00\00H\00\00\00\00\00\00\00ODR1\01\00\18\00\01\00\00\00\05\00\00\00\00\00\00\00 \04\00\00\01\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\03 \00\00\00\00\00\00\00\00\00\00\00\00\00\00/ \00\00\00\00\00\00\00\00\00\00\00\00\00\00\0D\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00 \01\00\00\FF\FF\FF\FF\00\00\00\00\00\00\00\00ODR1\02\00 \00\01\00\00\00\05\00\00\00\00\00\00\00 \06\00\00\00\00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\03\00\00\00 \00\00\00\00\00\00\00\00\00\00\00/\00\00\00 \00\00\00\00\00\00\00\00\00\00\00\0D\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00 \00\00\00\01\00\00\00\FF\FF\FF\FF\00\00\00\00SAMP\01\00\00\00\02\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00p\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\04\00\00\00\03\00\00\00PLAN\01\00\00\00H\00\00\00\00\00\00\00ODR1\02\00 \00\01\00\00\00\05\00\00\00\00\00\00\00 \04\00\00\00\00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\03\00\00\00 \00\00\00\00\00\00\00\00\00\00\00/\00\00\00 \00\00\00\00\00\00\00\00\00\00\00\0D\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00 \00\00\00\01\00\00\00\FF\FF\FF\FF\00\00\00\00ODR1\01\00\18\00\01\00\00\00\09\00\00\00\01\00\00\00 \06\00\00\01\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\03 \00\00\00\00\00\00\00\00\00\00\00\00\00\00/ \00\00\00\00\00\00\00\00\00\00\00\00\00\00\0D\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\02 \00\00\00\00\00\00\00\00\00\00\00\00\00\00\02 \00\00\00\00\00\00\00\00\00\00\00\00\00\00\15\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\1B\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00 \01\00\00\FF\FF\FF\FF\00\00\00\00\00\00\00\00SAMP\01\00\00\00\03\00\00\00\01\00\00\00\01\00\00\00\01\00\00\00\A8\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\05\00\00\00\00\00\00\00\16\01\00\00\00\00\00\00\08\00\00\00\03\00\00\00PLAN\01\00\00\00X\00\00\00\00\00\00\00ODR1\02\00 \00\01\00\00\00\09\00\00\00\01\00\00\00 \06\00\00\00\00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\03\00\00\00 \00\00\00\00\00\00\00\00\00\00\00/\00\00\00 \00\00\00\00\00\00\00\00\00\00\00\0D\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\02\00\00\00 \00\00\00\00\00\00\00\00\00\00\00\02\00\00\00 \00\00\00\00\00\00\00\00\00\00\00\15\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1B\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00 \00\00\00\01\00\00\00\FF\FF\FF\FF\00\00\00\00SAMP\01\00\00\00\03\00\00\00\01\00\00\00\01\00\00\00\01\00\00\00\B0\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\05\00\00\00\00\00\00\00\16\01\00\00\00\00\00\00\08\00\00\00\03\00\00\00PLAN\01\00\00\00X\00\00\00\00\00\00\00ODR1\01\00\18\00\01\00\00\00\06\00\00\00\00\00\00\00 \06\00\00\01\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\03 \00\00\00\00\00\00\00\00\00\00\00\00\00\00/ \00\00\00\00\00\00\00\00\00\00\00\00\00\006 \01\00\00\00\00\00\00\00\00\00\00\00\00\00\0D\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00 \01\00\00\FF\FF\FF\FF\00\00\00\00\00\00\00\00SAMP\01\00\00\00\02\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00x\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\05\00\00\00\03\00\00\00PLAN\01\00\00\00H\00\00\00\00\00\00\00ODR1\02\00 \00\01\00\00\00\06\00\00\00\00\00\00\00 \06\00\00\00\00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\03\00\00\00 \00\00\00\00\00\00\00\00\00\00\00/\00\00\00 \00\00\00\00\00\00\00\00\00\00\006\01\00\00 \00\00\00\00\00\00\00\00\00\00\00\0D\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00 \00\00\00\01\00\00\00\FF\FF\FF\FF\00\00\00\00SAMP\01\00\00\00\02\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\80\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\05\00\00\00\03\00\00\00PLAN\01\00\00\00H\00\00\00\00\00\00\00")
  // Single-byte (offset, value) edits that turn a base into each variant.
  llvm.mlir.global internal constant @patches(dense<[20, 0, 21, 2, 57, 64, 60, 1, 58, 1, 64, 1, 20, 0, 21, 2, 68, 64, 72, 1, 65, 1, 76, 1, 240, 23, 236, 1, 240, 0, 241, 0, 232, 6, 232, 6, 240, 23, 232, 6, 236, 1, 240, 0, 241, 0, 89, 64, 105, 64, 232, 7, 240, 51, 241, 8, 89, 64, 105, 64, 232, 7, 240, 52, 241, 8, 89, 64, 105, 64, 232, 7, 236, 1, 240, 0, 241, 0, 89, 64, 105, 64, 232, 8, 240, 51, 241, 8, 89, 64, 105, 64, 232, 8, 240, 52, 241, 8, 89, 64, 105, 64, 232, 8, 236, 1, 240, 0, 241, 0, 248, 23, 244, 1, 248, 0, 249, 0, 248, 0, 249, 0, 240, 6, 240, 6, 248, 23, 240, 6, 244, 1, 248, 0, 249, 0, 240, 6, 248, 0, 249, 0, 100, 64, 116, 64, 240, 7, 248, 51, 249, 8, 100, 64, 116, 64, 240, 7, 248, 52, 249, 8, 100, 64, 116, 64, 240, 7, 244, 1, 248, 0, 249, 0, 100, 64, 116, 64, 240, 7, 248, 0, 249, 0, 100, 64, 116, 64, 240, 8, 248, 51, 249, 8, 100, 64, 116, 64, 240, 8, 248, 52, 249, 8, 100, 64, 116, 64, 240, 8, 244, 1, 248, 0, 249, 0, 100, 64, 116, 64, 240, 8, 248, 0, 249, 0, 240, 0, 241, 0, 244, 1, 232, 11, 240, 0, 241, 0, 248, 0, 249, 0, 252, 1, 240, 11, 248, 0, 249, 0, 89, 64, 105, 64, 232, 9, 89, 64, 105, 64, 232, 9, 240, 23, 89, 64, 105, 64, 232, 9, 236, 1, 89, 64, 105, 64, 232, 10, 89, 64, 105, 64, 232, 10, 240, 23, 89, 64, 105, 64, 232, 10, 236, 1, 100, 64, 116, 64, 240, 9, 100, 64, 116, 64, 240, 9, 248, 23, 100, 64, 116, 64, 240, 9, 244, 1, 100, 63, 116, 63, 240, 9, 100, 64, 116, 64, 240, 10, 100, 64, 116, 64, 240, 10, 248, 23, 100, 64, 116, 64, 240, 10, 244, 1, 100, 63, 116, 63, 240, 10, 74, 0, 76, 1, 74, 0, 80, 1, 73, 0, 74, 0, 56, 3, 57, 16, 74, 0, 72, 55, 72, 55, 74, 0, 76, 1, 72, 55, 74, 0, 80, 1, 72, 55, 73, 0, 74, 0, 56, 3, 57, 16, 72, 55, 74, 0, 72, 56, 73, 1, 72, 56, 73, 1, 74, 0, 76, 1, 72, 56, 73, 1, 74, 0, 80, 1, 72, 56, 74, 0, 56, 3, 57, 16, 72, 56, 73, 1, 74, 0, 81, 0, 88, 1, 81, 0, 92, 1, 81, 0, 84, 0, 64, 3, 68, 16, 81, 0, 80, 55, 80, 55, 81, 0, 88, 1, 80, 55, 81, 0, 92, 1, 80, 55, 81, 0, 84, 0, 64, 3, 68, 16, 80, 55, 81, 0, 80, 56, 84, 1, 80, 56, 81, 0, 84, 1, 88, 1, 80, 56, 81, 0, 84, 1, 92, 1, 80, 56, 81, 0, 64, 3, 68, 16, 80, 56, 81, 0, 84, 1]> : tensor<510xi64>) : !llvm.array<510 x i64>
  // Narrow wire version 1 (ODR1, 64-bit solve entry). Base program: valid uniform REAL program with a compiled plan. frozen_failure freezes the variable (start=1, mask=0) so the solve fails with success 0. missing_plan is a truncated program; missing_uniform clears the UNIFORM flag (byte 20); missing_real changes the REAL type byte (21); payload_width, operand, signed_marker and auxiliary set an illegal width, operand, signedness or auxiliary field in the typed IEEE instruction.
  // rows: narrow_valid narrow_frozen_failure narrow_missing_plan narrow_missing_uniform narrow_missing_real narrow_payload_width narrow_operand narrow_signed_marker narrow_auxiliary
  llvm.mlir.global internal constant @rows_narrow(dense<[0, 0, 192, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 192, 0, 0, 1, 0, 0, 0, 0, 0, 0, 192, 104, 0, 0, 0, 1, 0, 0, 2, 0, 0, 0, 192, 0, 1, 0, 1, 0, 0, 2, 0, 0, 0, 192, 1, 1, 0, 1, 0, 0, 2, 0, 0, 0, 192, 2, 1, 0, 1, 0, 0, 2, 0, 0, 0, 192, 3, 1, 0, 1, 0, 0, 2, 0, 0, 0, 192, 4, 1, 0, 1, 0, 0, 2, 0, 0, 0, 192, 5, 1, 0, 1, 0, 0, 2, 0]> : tensor<99xi64>) : !llvm.array<99 x i64>
  // Wide wire version 2 (ODR1, wide solve entry): the same cases as the narrow group.
  // rows: wide_valid wide_frozen_failure wide_missing_plan wide_missing_uniform wide_missing_real wide_payload_width wide_operand wide_signed_marker wide_auxiliary
  llvm.mlir.global internal constant @rows_wide(dense<[1, 296, 200, 6, 0, 0, 1, 0, 0, 0, 1, 1, 296, 200, 6, 0, 1, 0, 0, 0, 0, 0, 1, 496, 112, 6, 0, 0, 1, 0, 0, 2, 0, 1, 296, 200, 6, 1, 0, 1, 0, 0, 2, 0, 1, 296, 200, 7, 1, 0, 1, 0, 0, 2, 0, 1, 296, 200, 8, 1, 0, 1, 0, 0, 2, 0, 1, 296, 200, 9, 1, 0, 1, 0, 0, 2, 0, 1, 296, 200, 10, 1, 0, 1, 0, 0, 2, 0, 1, 296, 200, 11, 1, 0, 1, 0, 0, 2, 0]> : tensor<99xi64>) : !llvm.array<99 x i64>
  // Narrow cell instructions 5-8 (deterministic IEEE cell parameters). valid decodes the payload coordinate; coordinate_width and capture_index corrupt the coordinate width or capture index; padding sets a reserved byte; unknown_opcode uses opcode 11.
  // rows: cell_narrow_5_valid cell_narrow_5_coordinate_width cell_narrow_5_capture_index cell_narrow_6_valid cell_narrow_6_coordinate_width cell_narrow_6_capture_index cell_narrow_7_valid cell_narrow_7_coordinate_width cell_narrow_7_capture_index cell_narrow_8_valid cell_narrow_8_coordinate_width cell_narrow_8_capture_index cell_narrow_5_padding cell_narrow_11_unknown_opcode
  llvm.mlir.global internal constant @rows_cell_narrow(dense<[2, 608, 272, 12, 0, 0, 1, 1065353216, 32, 0, 1, 2, 608, 272, 12, 1, 0, 1, 1065353216, 32, 2, 0, 2, 608, 272, 13, 3, 0, 1, 1065353216, 32, 2, 0, 2, 608, 272, 16, 1, 0, 1, 1065353216, 32, 0, 1, 2, 608, 272, 17, 2, 0, 1, 1065353216, 32, 2, 0, 2, 608, 272, 19, 4, 0, 1, 1065353216, 32, 2, 0, 2, 608, 272, 23, 5, 0, 1, 4607182418800017408, 64, 0, 1, 2, 608, 272, 28, 5, 0, 1, 4607182418800017408, 64, 2, 0, 2, 608, 272, 33, 6, 0, 1, 4607182418800017408, 64, 2, 0, 2, 608, 272, 39, 5, 0, 1, 4607182418800017408, 64, 0, 1, 2, 608, 272, 44, 5, 0, 1, 4607182418800017408, 64, 2, 0, 2, 608, 272, 49, 6, 0, 1, 4607182418800017408, 64, 2, 0, 2, 608, 272, 113, 3, 0, 1, 1065353216, 32, 2, 0, 2, 608, 272, 116, 3, 0, 1, 1065353216, 32, 2, 0]> : tensor<154xi64>) : !llvm.array<154 x i64>
  // Wide cell instructions 5-8: as the narrow group plus payload_width, where a zero payload width must be rejected before specialization.
  // rows: cell_wide_5_valid cell_wide_5_coordinate_width cell_wide_5_capture_index cell_wide_5_payload_width cell_wide_6_valid cell_wide_6_coordinate_width cell_wide_6_capture_index cell_wide_6_payload_width cell_wide_7_valid cell_wide_7_coordinate_width cell_wide_7_capture_index cell_wide_7_payload_width cell_wide_8_valid cell_wide_8_coordinate_width cell_wide_8_capture_index cell_wide_8_payload_width cell_wide_5_padding cell_wide_11_unknown_opcode
  llvm.mlir.global internal constant @rows_cell_wide(dense<[3, 880, 280, 55, 0, 0, 1, 1065353216, 32, 0, 1, 3, 880, 280, 55, 1, 0, 1, 1065353216, 32, 2, 0, 3, 880, 280, 56, 3, 0, 1, 1065353216, 32, 2, 0, 3, 880, 280, 59, 2, 0, 1, 1065353216, 31, 2, 0, 3, 880, 280, 61, 1, 0, 1, 1065353216, 32, 0, 1, 3, 880, 280, 62, 2, 0, 1, 1065353216, 32, 2, 0, 3, 880, 280, 64, 4, 0, 1, 1065353216, 32, 2, 0, 3, 880, 280, 68, 3, 0, 1, 1065353216, 31, 2, 0, 3, 880, 280, 71, 5, 0, 1, 4607182418800017408, 64, 0, 1, 3, 880, 280, 76, 5, 0, 1, 4607182418800017408, 64, 2, 0, 3, 880, 280, 81, 6, 0, 1, 4607182418800017408, 64, 2, 0, 3, 880, 280, 87, 5, 0, 1, 4607182418800017408, 63, 2, 0, 3, 880, 280, 92, 5, 0, 1, 4607182418800017408, 64, 0, 1, 3, 880, 280, 97, 5, 0, 1, 4607182418800017408, 64, 2, 0, 3, 880, 280, 102, 6, 0, 1, 4607182418800017408, 64, 2, 0, 3, 880, 280, 108, 5, 0, 1, 4607182418800017408, 63, 2, 0, 3, 880, 280, 119, 3, 0, 1, 1065353216, 32, 2, 0, 3, 880, 280, 122, 3, 0, 1, 1065353216, 32, 2, 0]> : tensor<198xi64>) : !llvm.array<198 x i64>
  // Narrow numeric-to-integer conversion instructions 9 and 10: valid, bad coordinate width and bad capture index.
  // rows: converted_narrow_9_valid converted_narrow_9_coordinate_width converted_narrow_9_capture_index converted_narrow_10_valid converted_narrow_10_coordinate_width converted_narrow_10_capture_index
  llvm.mlir.global internal constant @rows_converted_narrow(dense<[2, 608, 272, 125, 3, 0, 1, 4607182418800017408, 64, 0, 1, 2, 608, 272, 128, 4, 0, 1, 4607182418800017408, 64, 2, 0, 2, 608, 272, 132, 4, 0, 1, 4607182418800017408, 64, 2, 0, 2, 608, 272, 136, 3, 0, 1, 4607182418800017408, 64, 0, 1, 2, 608, 272, 139, 4, 0, 1, 4607182418800017408, 64, 2, 0, 2, 608, 272, 143, 4, 0, 1, 4607182418800017408, 64, 2, 0]> : tensor<66xi64>) : !llvm.array<66 x i64>
  // Wide conversion instructions 9 and 10 additionally reject a capture narrower than the converted value (capture_width).
  // rows: converted_wide_9_valid converted_wide_9_coordinate_width converted_wide_9_capture_index converted_wide_9_capture_width converted_wide_10_valid converted_wide_10_coordinate_width converted_wide_10_capture_index converted_wide_10_capture_width
  llvm.mlir.global internal constant @rows_converted_wide(dense<[3, 880, 280, 147, 3, 0, 1, 4607182418800017408, 64, 0, 1, 3, 880, 280, 150, 4, 0, 1, 4607182418800017408, 64, 2, 0, 3, 880, 280, 154, 4, 0, 1, 4607182418800017408, 64, 2, 0, 3, 880, 280, 158, 3, 0, 1, 4607182418800017408, 63, 2, 0, 3, 880, 280, 161, 3, 0, 1, 4607182418800017408, 64, 0, 1, 3, 880, 280, 164, 4, 0, 1, 4607182418800017408, 64, 2, 0, 3, 880, 280, 168, 4, 0, 1, 4607182418800017408, 64, 2, 0, 3, 880, 280, 172, 3, 0, 1, 4607182418800017408, 63, 2, 0]> : tensor<88xi64>) : !llvm.array<88 x i64>
  // Narrow integer instructions 54-56 over a REAL program: bad flags, operand, auxiliary, output width and input width must all be rejected.
  // rows: integer_narrow_54_flags integer_narrow_54_operand integer_narrow_54_auxiliary integer_narrow_54_output_width integer_narrow_54_input_width integer_narrow_55_flags integer_narrow_55_operand integer_narrow_55_auxiliary integer_narrow_55_output_width integer_narrow_55_input_width integer_narrow_56_flags integer_narrow_56_operand integer_narrow_56_auxiliary integer_narrow_56_output_width integer_narrow_56_input_width
  llvm.mlir.global internal constant @rows_integer_narrow(dense<[0, 1160, 208, 175, 0, 0, 1, 0, 0, 2, 0, 0, 1160, 208, 175, 2, 0, 1, 0, 0, 2, 0, 0, 1160, 208, 177, 2, 0, 1, 0, 0, 2, 0, 0, 1160, 208, 179, 2, 0, 1, 0, 0, 2, 0, 0, 1160, 208, 181, 3, 0, 1, 0, 0, 2, 0, 0, 1160, 208, 184, 1, 0, 1, 0, 0, 2, 0, 0, 1160, 208, 185, 3, 0, 1, 0, 0, 2, 0, 0, 1160, 208, 188, 3, 0, 1, 0, 0, 2, 0, 0, 1160, 208, 191, 3, 0, 1, 0, 0, 2, 0, 0, 1160, 208, 194, 4, 0, 1, 0, 0, 2, 0, 0, 1160, 208, 198, 2, 0, 1, 0, 0, 2, 0, 0, 1160, 208, 200, 4, 0, 1, 0, 0, 2, 0, 0, 1160, 208, 204, 4, 0, 1, 0, 0, 2, 0, 0, 1160, 208, 208, 2, 0, 1, 0, 0, 2, 0, 0, 1160, 208, 210, 5, 0, 1, 0, 0, 2, 0]> : tensor<165xi64>) : !llvm.array<165 x i64>
  // Wide integer instructions 54-56: same cases as the narrow group.
  // rows: integer_wide_54_flags integer_wide_54_operand integer_wide_54_auxiliary integer_wide_54_output_width integer_wide_54_input_width integer_wide_55_flags integer_wide_55_operand integer_wide_55_auxiliary integer_wide_55_output_width integer_wide_55_input_width integer_wide_56_flags integer_wide_56_operand integer_wide_56_auxiliary integer_wide_56_output_width integer_wide_56_input_width
  llvm.mlir.global internal constant @rows_integer_wide(dense<[1, 1368, 216, 215, 0, 0, 1, 0, 0, 2, 0, 1, 1368, 216, 215, 2, 0, 1, 0, 0, 2, 0, 1, 1368, 216, 217, 2, 0, 1, 0, 0, 2, 0, 1, 1368, 216, 219, 2, 0, 1, 0, 0, 2, 0, 1, 1368, 216, 221, 3, 0, 1, 0, 0, 2, 0, 1, 1368, 216, 224, 1, 0, 1, 0, 0, 2, 0, 1, 1368, 216, 225, 3, 0, 1, 0, 0, 2, 0, 1, 1368, 216, 228, 3, 0, 1, 0, 0, 2, 0, 1, 1368, 216, 231, 3, 0, 1, 0, 0, 2, 0, 1, 1368, 216, 234, 4, 0, 1, 0, 0, 2, 0, 1, 1368, 216, 238, 2, 0, 1, 0, 0, 2, 0, 1, 1368, 216, 240, 4, 0, 1, 0, 0, 2, 0, 1, 1368, 216, 244, 4, 0, 1, 0, 0, 2, 0, 1, 1368, 216, 248, 2, 0, 1, 0, 0, 2, 0, 1, 1368, 216, 250, 5, 0, 1, 0, 0, 2, 0]> : tensor<165xi64>) : !llvm.array<165 x i64>

  // Row layout: function (0 narrow, 1 wide, 2 cell_narrow, 3 cell_wide),
  // program offset, size, patch index, patch count, start, mask, payload,
  // capture width, expected status, expected success.
  // Calls the narrow or wide solve entry and checks that a rejected or failed
  // call reports the expected status and success flag while leaving the
  // assignment and RNG state untouched.
  llvm.func internal @solve_case(%ctx: !llvm.ptr, %program: !llvm.ptr, %size: i64, %start: i64, %mask: i64, %payload: i64, %capture_width: i32, %expected_status: i32, %expected_success: i32, %wide: i1, %captured: i1) -> i1 {
    %zero = llvm.mlir.constant(0 : i64) : i64
    %one = llvm.mlir.constant(1 : i64) : i64
    %seed = llvm.mlir.constant(71 : i64) : i64
    %increment = llvm.mlir.constant(15 : i64) : i64
    %null = llvm.mlir.zero : !llvm.ptr
    %assignment = llvm.alloca %one x i64 : (i64) -> !llvm.ptr
    %success = llvm.alloca %one x i32 : (i64) -> !llvm.ptr
    %state = llvm.alloca %one x i64 : (i64) -> !llvm.ptr
    %captures = llvm.alloca %one x i64 : (i64) -> !llvm.ptr
    %widths = llvm.alloca %one x i32 : (i64) -> !llvm.ptr
    %start_words = llvm.alloca %one x i64 : (i64) -> !llvm.ptr
    %mask_words = llvm.alloca %one x i64 : (i64) -> !llvm.ptr
    llvm.store %zero, %assignment : i64, !llvm.ptr
    llvm.store %payload, %captures : i64, !llvm.ptr
    llvm.store %capture_width, %widths : i32, !llvm.ptr
    llvm.store %start, %start_words : i64, !llvm.ptr
    llvm.store %mask, %mask_words : i64, !llvm.ptr
    %capture_ptr = llvm.select %captured, %captures, %null : i1, !llvm.ptr
    %width_ptr = llvm.select %captured, %widths, %null : i1, !llvm.ptr
    %capture_count = llvm.select %captured, %one, %zero : i1, i64
    llvm.cond_br %wide, ^wide, ^narrow
  ^narrow:
    %narrow_status = llvm.call @talos_rt_v1_random_solve_modes_state(%ctx, %program, %size, %start, %mask, %zero, %one, %seed, %increment, %capture_ptr, %capture_count, %assignment, %success, %state) : (!llvm.ptr, !llvm.ptr, i64, i64, i64, i64, i64, i64, i64, !llvm.ptr, i64, !llvm.ptr, !llvm.ptr, !llvm.ptr) -> i32
    llvm.br ^check(%narrow_status : i32)
  ^wide:
    %wide_status = llvm.call @talos_rt_v1_random_solve_wide_modes_state(%ctx, %program, %size, %start_words, %mask_words, %one, %zero, %one, %seed, %increment, %capture_ptr, %capture_count, %width_ptr, %capture_count, %assignment, %success, %state) : (!llvm.ptr, !llvm.ptr, i64, !llvm.ptr, !llvm.ptr, i64, i64, i64, i64, i64, !llvm.ptr, i64, !llvm.ptr, i64, !llvm.ptr, !llvm.ptr, !llvm.ptr) -> i32
    llvm.br ^check(%wide_status : i32)
  ^check(%status: i32):
    %actual_success = llvm.load %success : !llvm.ptr -> i32
    %actual_assignment = llvm.load %assignment : !llvm.ptr -> i64
    %actual_state = llvm.load %state : !llvm.ptr -> i64
    %status_ok = llvm.icmp "eq" %status, %expected_status : i32
    %success_ok = llvm.icmp "eq" %actual_success, %expected_success : i32
    %assignment_ok = llvm.icmp "eq" %actual_assignment, %zero : i64
    %rng_ok = llvm.icmp "eq" %actual_state, %seed : i64
    %a = llvm.and %status_ok, %success_ok : i1
    %b = llvm.and %assignment_ok, %rng_ok : i1
    %good = llvm.and %a, %b : i1
    llvm.return %good : i1
  }

  llvm.func internal @field(%rows: !llvm.ptr, %base: i64, %column: i64) -> i64 {
    %index = llvm.add %base, %column : i64
    %ptr = llvm.getelementptr %rows[%index] : (!llvm.ptr, i64) -> !llvm.ptr, i64
    %value = llvm.load %ptr : !llvm.ptr -> i64
    llvm.return %value : i64
  }

  llvm.func internal @run_table(%ctx: !llvm.ptr, %rows: !llvm.ptr, %count: i64) -> i1 {
    %zero = llvm.mlir.constant(0 : i64) : i64
    %one = llvm.mlir.constant(1 : i64) : i64
    %two = llvm.mlir.constant(2 : i64) : i64
    %stride = llvm.mlir.constant(11 : i64) : i64
    %capacity = llvm.mlir.constant(512 : i64) : i64
    %true = llvm.mlir.constant(true) : i1
    %false = llvm.mlir.constant(false) : i1
    %k0 = llvm.mlir.constant(0 : i64) : i64
    %k1 = llvm.mlir.constant(1 : i64) : i64
    %k2 = llvm.mlir.constant(2 : i64) : i64
    %k3 = llvm.mlir.constant(3 : i64) : i64
    %k4 = llvm.mlir.constant(4 : i64) : i64
    %k5 = llvm.mlir.constant(5 : i64) : i64
    %k6 = llvm.mlir.constant(6 : i64) : i64
    %k7 = llvm.mlir.constant(7 : i64) : i64
    %k8 = llvm.mlir.constant(8 : i64) : i64
    %k9 = llvm.mlir.constant(9 : i64) : i64
    %k10 = llvm.mlir.constant(10 : i64) : i64
    %programs = llvm.mlir.addressof @programs : !llvm.ptr
    %patches = llvm.mlir.addressof @patches : !llvm.ptr
    %buffer = llvm.alloca %capacity x i8 : (i64) -> !llvm.ptr
    llvm.br ^row(%zero, %true : i64, i1)
  ^row(%i: i64, %ok: i1):
    %base = llvm.mul %i, %stride : i64
    %func = llvm.call @field(%rows, %base, %k0) : (!llvm.ptr, i64, i64) -> i64
    %offset = llvm.call @field(%rows, %base, %k1) : (!llvm.ptr, i64, i64) -> i64
    %size = llvm.call @field(%rows, %base, %k2) : (!llvm.ptr, i64, i64) -> i64
    %patch_first = llvm.call @field(%rows, %base, %k3) : (!llvm.ptr, i64, i64) -> i64
    %patch_count = llvm.call @field(%rows, %base, %k4) : (!llvm.ptr, i64, i64) -> i64
    %start = llvm.call @field(%rows, %base, %k5) : (!llvm.ptr, i64, i64) -> i64
    %mask = llvm.call @field(%rows, %base, %k6) : (!llvm.ptr, i64, i64) -> i64
    %payload = llvm.call @field(%rows, %base, %k7) : (!llvm.ptr, i64, i64) -> i64
    %width64 = llvm.call @field(%rows, %base, %k8) : (!llvm.ptr, i64, i64) -> i64
    %status64 = llvm.call @field(%rows, %base, %k9) : (!llvm.ptr, i64, i64) -> i64
    %success64 = llvm.call @field(%rows, %base, %k10) : (!llvm.ptr, i64, i64) -> i64
    %width = llvm.trunc %width64 : i64 to i32
    %status = llvm.trunc %status64 : i64 to i32
    %success = llvm.trunc %success64 : i64 to i32
    %source = llvm.getelementptr %programs[%offset] : (!llvm.ptr, i64) -> !llvm.ptr, i8
    "llvm.intr.memcpy"(%buffer, %source, %size) <{isVolatile = false}> : (!llvm.ptr, !llvm.ptr, i64) -> ()
    llvm.br ^patch(%zero : i64)
  ^patch(%p: i64):
    %more_patches = llvm.icmp "ult" %p, %patch_count : i64
    llvm.cond_br %more_patches, ^apply(%p : i64), ^dispatch
  ^apply(%q: i64):
    %index = llvm.add %patch_first, %q : i64
    %pair = llvm.mul %index, %two : i64
    %offset_ptr = llvm.getelementptr %patches[%pair] : (!llvm.ptr, i64) -> !llvm.ptr, i64
    %patch_offset = llvm.load %offset_ptr : !llvm.ptr -> i64
    %value_ptr = llvm.getelementptr %offset_ptr[1] : (!llvm.ptr) -> !llvm.ptr, i64
    %patch_value = llvm.load %value_ptr : !llvm.ptr -> i64
    %byte = llvm.trunc %patch_value : i64 to i8
    %target = llvm.getelementptr %buffer[%patch_offset] : (!llvm.ptr, i64) -> !llvm.ptr, i8
    llvm.store %byte, %target : i8, !llvm.ptr
    %next_patch = llvm.add %q, %one : i64
    llvm.br ^patch(%next_patch : i64)
  ^dispatch:
    %wide_bit = llvm.and %func, %one : i64
    %is_wide = llvm.icmp "ne" %wide_bit, %zero : i64
    %captured_bit = llvm.lshr %func, %one : i64
    %has_captures = llvm.icmp "ne" %captured_bit, %zero : i64
    %result = llvm.call @solve_case(%ctx, %buffer, %size, %start, %mask, %payload, %width, %status, %success, %is_wide, %has_captures) : (!llvm.ptr, !llvm.ptr, i64, i64, i64, i64, i32, i32, i32, i1, i1) -> i1
    %all = llvm.and %ok, %result : i1
    %next_row = llvm.add %i, %one : i64
    %more_rows = llvm.icmp "ult" %next_row, %count : i64
    llvm.cond_br %more_rows, ^row(%next_row, %all : i64, i1), ^done(%all : i1)
  ^done(%final: i1):
    llvm.return %final : i1
  }

  llvm.func @main() -> i32 {
    %one = llvm.mlir.constant(1 : i64) : i64
    %z32 = llvm.mlir.constant(0 : i32) : i32
    %ctx_ptr = llvm.alloca %one x !llvm.ptr : (i64) -> !llvm.ptr
    %created = llvm.call @talos_rt_v1_context_create(%ctx_ptr) : (!llvm.ptr) -> i32
    %ctx = llvm.load %ctx_ptr : !llvm.ptr -> !llvm.ptr
    %valid_0 = llvm.icmp "eq" %created, %z32 : i32
    %rows_narrow = llvm.mlir.addressof @rows_narrow : !llvm.ptr
    %count_narrow = llvm.mlir.constant(9 : i64) : i64
    %r_narrow = llvm.call @run_table(%ctx, %rows_narrow, %count_narrow) : (!llvm.ptr, !llvm.ptr, i64) -> i1
    %v_narrow = llvm.and %valid_0, %r_narrow : i1
    %rows_wide = llvm.mlir.addressof @rows_wide : !llvm.ptr
    %count_wide = llvm.mlir.constant(9 : i64) : i64
    %r_wide = llvm.call @run_table(%ctx, %rows_wide, %count_wide) : (!llvm.ptr, !llvm.ptr, i64) -> i1
    %v_wide = llvm.and %v_narrow, %r_wide : i1
    %rows_cell_narrow = llvm.mlir.addressof @rows_cell_narrow : !llvm.ptr
    %count_cell_narrow = llvm.mlir.constant(14 : i64) : i64
    %r_cell_narrow = llvm.call @run_table(%ctx, %rows_cell_narrow, %count_cell_narrow) : (!llvm.ptr, !llvm.ptr, i64) -> i1
    %v_cell_narrow = llvm.and %v_wide, %r_cell_narrow : i1
    %rows_cell_wide = llvm.mlir.addressof @rows_cell_wide : !llvm.ptr
    %count_cell_wide = llvm.mlir.constant(18 : i64) : i64
    %r_cell_wide = llvm.call @run_table(%ctx, %rows_cell_wide, %count_cell_wide) : (!llvm.ptr, !llvm.ptr, i64) -> i1
    %v_cell_wide = llvm.and %v_cell_narrow, %r_cell_wide : i1
    %rows_converted_narrow = llvm.mlir.addressof @rows_converted_narrow : !llvm.ptr
    %count_converted_narrow = llvm.mlir.constant(6 : i64) : i64
    %r_converted_narrow = llvm.call @run_table(%ctx, %rows_converted_narrow, %count_converted_narrow) : (!llvm.ptr, !llvm.ptr, i64) -> i1
    %v_converted_narrow = llvm.and %v_cell_wide, %r_converted_narrow : i1
    %rows_converted_wide = llvm.mlir.addressof @rows_converted_wide : !llvm.ptr
    %count_converted_wide = llvm.mlir.constant(8 : i64) : i64
    %r_converted_wide = llvm.call @run_table(%ctx, %rows_converted_wide, %count_converted_wide) : (!llvm.ptr, !llvm.ptr, i64) -> i1
    %v_converted_wide = llvm.and %v_converted_narrow, %r_converted_wide : i1
    %rows_integer_narrow = llvm.mlir.addressof @rows_integer_narrow : !llvm.ptr
    %count_integer_narrow = llvm.mlir.constant(15 : i64) : i64
    %r_integer_narrow = llvm.call @run_table(%ctx, %rows_integer_narrow, %count_integer_narrow) : (!llvm.ptr, !llvm.ptr, i64) -> i1
    %v_integer_narrow = llvm.and %v_converted_wide, %r_integer_narrow : i1
    %rows_integer_wide = llvm.mlir.addressof @rows_integer_wide : !llvm.ptr
    %count_integer_wide = llvm.mlir.constant(15 : i64) : i64
    %r_integer_wide = llvm.call @run_table(%ctx, %rows_integer_wide, %count_integer_wide) : (!llvm.ptr, !llvm.ptr, i64) -> i1
    %v_integer_wide = llvm.and %v_integer_narrow, %r_integer_wide : i1
    llvm.call @talos_rt_v1_context_destroy(%ctx) : (!llvm.ptr) -> ()
    %ok = llvm.mlir.addressof @ok : !llvm.ptr
    %bad = llvm.mlir.addressof @bad : !llvm.ptr
    %message = llvm.select %v_integer_wide, %ok, %bad : i1, !llvm.ptr
    %printed = llvm.call @puts(%message) : (!llvm.ptr) -> i32
    llvm.return %z32 : i32
  }
}
