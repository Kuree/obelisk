// RUN: mlir-translate %s --mlir-to-llvmir | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe | FileCheck %s

// Wire-format validation through the C ABI, expressed entirely in MLIR.
// One valid eight-bit equality plan has a singleton sample with frozen-bit
// cofactors. Each malformed variant below patches a single byte of a stack
// copy of that plan: a forward edge, a random-order flag, a capture index and
// bit, an atom, the root handle, the core length and the trailer version.
// They are rejected before sampling (status 2), preserve the RNG state and
// commit no assignment. State-only relations have one empty assignment, and a
// wide call whose capture inventory disagrees with the plan is rejected. The
// target consumes precompiled Boolean plans without Z3.
// CHECK: sampler plan validation 1

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  llvm.mlir.global @obelisk_final_time(0 : i64) : i64
  llvm.mlir.global @obelisk_time_precision_fs(1 : i64) : i64
  llvm.mlir.global internal constant @ok("sampler plan validation 1\00")
  llvm.mlir.global internal constant @bad("sampler plan validation 0\00")
  llvm.func @puts(!llvm.ptr) -> i32
  llvm.func @obelisk_rt_v1_context_create(!llvm.ptr) -> i32
  llvm.func @obelisk_rt_v1_context_destroy(!llvm.ptr)
  llvm.func @obelisk_rt_v1_random_solve_modes_state(!llvm.ptr, !llvm.ptr, i64, i64, i64, i64, i64, i64, i64, !llvm.ptr, i64, !llvm.ptr, !llvm.ptr, !llvm.ptr) -> i32
  llvm.func @obelisk_rt_v1_random_solve_wide_modes_state(!llvm.ptr, !llvm.ptr, i64, !llvm.ptr, !llvm.ptr, i64, i64, i64, i64, i64, !llvm.ptr, i64, !llvm.ptr, i64, !llvm.ptr, !llvm.ptr, !llvm.ptr) -> i32
  llvm.mlir.global internal constant @valid("\4F\44\52\31\01\00\18\00\08\00\00\00\04\00\00\00\01\00\00\00\20\02\00\00\01\08\00\00\00\00\00\00\00\00\00\00\00\00\00\00\02\08\00\00\00\00\00\00\00\00\00\00\00\00\00\00\15\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\20\01\00\00\FF\FF\FF\FF\00\00\00\00\00\00\00\00\53\41\4D\50\01\00\00\00\30\00\00\00\01\00\00\00\08\00\00\00\01\00\00\00\58\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\01\00\00\00\02\00\00\00\02\00\00\00\00\00\00\00\01\00\00\00\03\00\00\00\03\00\00\00\00\00\00\00\01\00\00\00\04\00\00\00\04\00\00\00\00\00\00\00\01\00\00\00\05\00\00\00\05\00\00\00\00\00\00\00\01\00\00\00\06\00\00\00\06\00\00\00\00\00\00\00\01\00\00\00\07\00\00\00\07\00\00\00\00\00\00\00\02\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\02\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\02\00\00\00\00\00\00\00\02\00\00\00\00\00\00\00\02\00\00\00\00\00\00\00\03\00\00\00\00\00\00\00\02\00\00\00\00\00\00\00\04\00\00\00\00\00\00\00\02\00\00\00\00\00\00\00\05\00\00\00\00\00\00\00\02\00\00\00\00\00\00\00\06\00\00\00\00\00\00\00\02\00\00\00\00\00\00\00\07\00\00\00\00\00\00\00\04\00\00\00\02\00\00\00\13\00\00\00\00\00\00\00\04\00\00\00\03\00\00\00\12\00\00\00\00\00\00\00\04\00\00\00\23\00\00\00\25\00\00\00\00\00\00\00\04\00\00\00\04\00\00\00\15\00\00\00\00\00\00\00\04\00\00\00\05\00\00\00\14\00\00\00\00\00\00\00\04\00\00\00\29\00\00\00\2B\00\00\00\00\00\00\00\04\00\00\00\26\00\00\00\2C\00\00\00\00\00\00\00\04\00\00\00\06\00\00\00\17\00\00\00\00\00\00\00\04\00\00\00\07\00\00\00\16\00\00\00\00\00\00\00\04\00\00\00\31\00\00\00\33\00\00\00\00\00\00\00\04\00\00\00\2E\00\00\00\34\00\00\00\00\00\00\00\04\00\00\00\08\00\00\00\19\00\00\00\00\00\00\00\04\00\00\00\09\00\00\00\18\00\00\00\00\00\00\00\04\00\00\00\39\00\00\00\3B\00\00\00\00\00\00\00\04\00\00\00\36\00\00\00\3C\00\00\00\00\00\00\00\04\00\00\00\0A\00\00\00\1B\00\00\00\00\00\00\00\04\00\00\00\0B\00\00\00\1A\00\00\00\00\00\00\00\04\00\00\00\41\00\00\00\43\00\00\00\00\00\00\00\04\00\00\00\3E\00\00\00\44\00\00\00\00\00\00\00\04\00\00\00\0C\00\00\00\1D\00\00\00\00\00\00\00\04\00\00\00\0D\00\00\00\1C\00\00\00\00\00\00\00\04\00\00\00\49\00\00\00\4B\00\00\00\00\00\00\00\04\00\00\00\46\00\00\00\4C\00\00\00\00\00\00\00\04\00\00\00\0E\00\00\00\1F\00\00\00\00\00\00\00\04\00\00\00\0F\00\00\00\1E\00\00\00\00\00\00\00\04\00\00\00\51\00\00\00\53\00\00\00\00\00\00\00\04\00\00\00\4E\00\00\00\54\00\00\00\00\00\00\00\04\00\00\00\10\00\00\00\21\00\00\00\00\00\00\00\04\00\00\00\11\00\00\00\20\00\00\00\00\00\00\00\04\00\00\00\59\00\00\00\5B\00\00\00\00\00\00\00\04\00\00\00\56\00\00\00\5C\00\00\00\00\00\00\00\03\00\00\00\5E\00\00\00\50\4C\41\4E\01\00\00\00\28\03\00\00\00\00\00\00")
  llvm.mlir.global internal constant @zero_width("\4F\44\52\31\01\00\18\00\00\00\00\00\02\00\00\00\01\00\00\00\20\02\00\00\02\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\20\01\00\00\FF\FF\FF\FF\00\00\00\00\00\00\00\00\53\41\4D\50\01\00\00\00\02\00\00\00\01\00\00\00\00\00\00\00\01\00\00\00\38\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\02\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\02\00\00\00\50\4C\41\4E\01\00\00\00\48\00\00\00\00\00\00\00")
  llvm.mlir.global internal constant @wide_capture_inventory("\4F\44\52\31\02\00\20\00\82\00\00\00\02\00\00\00\02\00\00\00\20\02\00\00\00\00\00\00\00\00\00\00\02\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\20\00\00\00\01\00\00\00\FF\FF\FF\FF\00\00\00\00\53\41\4D\50\01\00\00\00\84\00\00\00\01\00\00\00\82\00\00\00\02\00\00\00\40\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\01\00\00\00\02\00\00\00\02\00\00\00\00\00\00\00\01\00\00\00\03\00\00\00\03\00\00\00\00\00\00\00\01\00\00\00\04\00\00\00\04\00\00\00\00\00\00\00\01\00\00\00\05\00\00\00\05\00\00\00\00\00\00\00\01\00\00\00\06\00\00\00\06\00\00\00\00\00\00\00\01\00\00\00\07\00\00\00\07\00\00\00\00\00\00\00\01\00\00\00\08\00\00\00\08\00\00\00\00\00\00\00\01\00\00\00\09\00\00\00\09\00\00\00\00\00\00\00\01\00\00\00\0A\00\00\00\0A\00\00\00\00\00\00\00\01\00\00\00\0B\00\00\00\0B\00\00\00\00\00\00\00\01\00\00\00\0C\00\00\00\0C\00\00\00\00\00\00\00\01\00\00\00\0D\00\00\00\0D\00\00\00\00\00\00\00\01\00\00\00\0E\00\00\00\0E\00\00\00\00\00\00\00\01\00\00\00\0F\00\00\00\0F\00\00\00\00\00\00\00\01\00\00\00\10\00\00\00\10\00\00\00\00\00\00\00\01\00\00\00\11\00\00\00\11\00\00\00\00\00\00\00\01\00\00\00\12\00\00\00\12\00\00\00\00\00\00\00\01\00\00\00\13\00\00\00\13\00\00\00\00\00\00\00\01\00\00\00\14\00\00\00\14\00\00\00\00\00\00\00\01\00\00\00\15\00\00\00\15\00\00\00\00\00\00\00\01\00\00\00\16\00\00\00\16\00\00\00\00\00\00\00\01\00\00\00\17\00\00\00\17\00\00\00\00\00\00\00\01\00\00\00\18\00\00\00\18\00\00\00\00\00\00\00\01\00\00\00\19\00\00\00\19\00\00\00\00\00\00\00\01\00\00\00\1A\00\00\00\1A\00\00\00\00\00\00\00\01\00\00\00\1B\00\00\00\1B\00\00\00\00\00\00\00\01\00\00\00\1C\00\00\00\1C\00\00\00\00\00\00\00\01\00\00\00\1D\00\00\00\1D\00\00\00\00\00\00\00\01\00\00\00\1E\00\00\00\1E\00\00\00\00\00\00\00\01\00\00\00\1F\00\00\00\1F\00\00\00\00\00\00\00\01\00\00\00\20\00\00\00\20\00\00\00\00\00\00\00\01\00\00\00\21\00\00\00\21\00\00\00\00\00\00\00\01\00\00\00\22\00\00\00\22\00\00\00\00\00\00\00\01\00\00\00\23\00\00\00\23\00\00\00\00\00\00\00\01\00\00\00\24\00\00\00\24\00\00\00\00\00\00\00\01\00\00\00\25\00\00\00\25\00\00\00\00\00\00\00\01\00\00\00\26\00\00\00\26\00\00\00\00\00\00\00\01\00\00\00\27\00\00\00\27\00\00\00\00\00\00\00\01\00\00\00\28\00\00\00\28\00\00\00\00\00\00\00\01\00\00\00\29\00\00\00\29\00\00\00\00\00\00\00\01\00\00\00\2A\00\00\00\2A\00\00\00\00\00\00\00\01\00\00\00\2B\00\00\00\2B\00\00\00\00\00\00\00\01\00\00\00\2C\00\00\00\2C\00\00\00\00\00\00\00\01\00\00\00\2D\00\00\00\2D\00\00\00\00\00\00\00\01\00\00\00\2E\00\00\00\2E\00\00\00\00\00\00\00\01\00\00\00\2F\00\00\00\2F\00\00\00\00\00\00\00\01\00\00\00\30\00\00\00\30\00\00\00\00\00\00\00\01\00\00\00\31\00\00\00\31\00\00\00\00\00\00\00\01\00\00\00\32\00\00\00\32\00\00\00\00\00\00\00\01\00\00\00\33\00\00\00\33\00\00\00\00\00\00\00\01\00\00\00\34\00\00\00\34\00\00\00\00\00\00\00\01\00\00\00\35\00\00\00\35\00\00\00\00\00\00\00\01\00\00\00\36\00\00\00\36\00\00\00\00\00\00\00\01\00\00\00\37\00\00\00\37\00\00\00\00\00\00\00\01\00\00\00\38\00\00\00\38\00\00\00\00\00\00\00\01\00\00\00\39\00\00\00\39\00\00\00\00\00\00\00\01\00\00\00\3A\00\00\00\3A\00\00\00\00\00\00\00\01\00\00\00\3B\00\00\00\3B\00\00\00\00\00\00\00\01\00\00\00\3C\00\00\00\3C\00\00\00\00\00\00\00\01\00\00\00\3D\00\00\00\3D\00\00\00\00\00\00\00\01\00\00\00\3E\00\00\00\3E\00\00\00\00\00\00\00\01\00\00\00\3F\00\00\00\3F\00\00\00\00\00\00\00\01\00\00\00\40\00\00\00\40\00\00\00\00\00\00\00\01\00\00\00\41\00\00\00\41\00\00\00\00\00\00\00\01\00\00\00\42\00\00\00\42\00\00\00\00\00\00\00\01\00\00\00\43\00\00\00\43\00\00\00\00\00\00\00\01\00\00\00\44\00\00\00\44\00\00\00\00\00\00\00\01\00\00\00\45\00\00\00\45\00\00\00\00\00\00\00\01\00\00\00\46\00\00\00\46\00\00\00\00\00\00\00\01\00\00\00\47\00\00\00\47\00\00\00\00\00\00\00\01\00\00\00\48\00\00\00\48\00\00\00\00\00\00\00\01\00\00\00\49\00\00\00\49\00\00\00\00\00\00\00\01\00\00\00\4A\00\00\00\4A\00\00\00\00\00\00\00\01\00\00\00\4B\00\00\00\4B\00\00\00\00\00\00\00\01\00\00\00\4C\00\00\00\4C\00\00\00\00\00\00\00\01\00\00\00\4D\00\00\00\4D\00\00\00\00\00\00\00\01\00\00\00\4E\00\00\00\4E\00\00\00\00\00\00\00\01\00\00\00\4F\00\00\00\4F\00\00\00\00\00\00\00\01\00\00\00\50\00\00\00\50\00\00\00\00\00\00\00\01\00\00\00\51\00\00\00\51\00\00\00\00\00\00\00\01\00\00\00\52\00\00\00\52\00\00\00\00\00\00\00\01\00\00\00\53\00\00\00\53\00\00\00\00\00\00\00\01\00\00\00\54\00\00\00\54\00\00\00\00\00\00\00\01\00\00\00\55\00\00\00\55\00\00\00\00\00\00\00\01\00\00\00\56\00\00\00\56\00\00\00\00\00\00\00\01\00\00\00\57\00\00\00\57\00\00\00\00\00\00\00\01\00\00\00\58\00\00\00\58\00\00\00\00\00\00\00\01\00\00\00\59\00\00\00\59\00\00\00\00\00\00\00\01\00\00\00\5A\00\00\00\5A\00\00\00\00\00\00\00\01\00\00\00\5B\00\00\00\5B\00\00\00\00\00\00\00\01\00\00\00\5C\00\00\00\5C\00\00\00\00\00\00\00\01\00\00\00\5D\00\00\00\5D\00\00\00\00\00\00\00\01\00\00\00\5E\00\00\00\5E\00\00\00\00\00\00\00\01\00\00\00\5F\00\00\00\5F\00\00\00\00\00\00\00\01\00\00\00\60\00\00\00\60\00\00\00\00\00\00\00\01\00\00\00\61\00\00\00\61\00\00\00\00\00\00\00\01\00\00\00\62\00\00\00\62\00\00\00\00\00\00\00\01\00\00\00\63\00\00\00\63\00\00\00\00\00\00\00\01\00\00\00\64\00\00\00\64\00\00\00\00\00\00\00\01\00\00\00\65\00\00\00\65\00\00\00\00\00\00\00\01\00\00\00\66\00\00\00\66\00\00\00\00\00\00\00\01\00\00\00\67\00\00\00\67\00\00\00\00\00\00\00\01\00\00\00\68\00\00\00\68\00\00\00\00\00\00\00\01\00\00\00\69\00\00\00\69\00\00\00\00\00\00\00\01\00\00\00\6A\00\00\00\6A\00\00\00\00\00\00\00\01\00\00\00\6B\00\00\00\6B\00\00\00\00\00\00\00\01\00\00\00\6C\00\00\00\6C\00\00\00\00\00\00\00\01\00\00\00\6D\00\00\00\6D\00\00\00\00\00\00\00\01\00\00\00\6E\00\00\00\6E\00\00\00\00\00\00\00\01\00\00\00\6F\00\00\00\6F\00\00\00\00\00\00\00\01\00\00\00\70\00\00\00\70\00\00\00\00\00\00\00\01\00\00\00\71\00\00\00\71\00\00\00\00\00\00\00\01\00\00\00\72\00\00\00\72\00\00\00\00\00\00\00\01\00\00\00\73\00\00\00\73\00\00\00\00\00\00\00\01\00\00\00\74\00\00\00\74\00\00\00\00\00\00\00\01\00\00\00\75\00\00\00\75\00\00\00\00\00\00\00\01\00\00\00\76\00\00\00\76\00\00\00\00\00\00\00\01\00\00\00\77\00\00\00\77\00\00\00\00\00\00\00\01\00\00\00\78\00\00\00\78\00\00\00\00\00\00\00\01\00\00\00\79\00\00\00\79\00\00\00\00\00\00\00\01\00\00\00\7A\00\00\00\7A\00\00\00\00\00\00\00\01\00\00\00\7B\00\00\00\7B\00\00\00\00\00\00\00\01\00\00\00\7C\00\00\00\7C\00\00\00\00\00\00\00\01\00\00\00\7D\00\00\00\7D\00\00\00\00\00\00\00\01\00\00\00\7E\00\00\00\7E\00\00\00\00\00\00\00\01\00\00\00\7F\00\00\00\7F\00\00\00\00\00\00\00\01\00\00\00\80\00\00\00\80\00\00\00\00\00\00\00\01\00\00\00\81\00\00\00\81\00\00\00\00\00\00\00\02\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\06\01\00\00\50\4C\41\4E\01\00\00\00\68\08\00\00\00\00\00\00")

  // Copy the first %size bytes of %program to the stack, patch the byte at
  // %offset (when non-negative) and solve with the single capture 173.
  // Returns whether status, success flag, assignment and RNG state all match.
  llvm.func internal @check(%ctx: !llvm.ptr, %program: !llvm.ptr, %size: i64,
                            %offset: i64, %byte: i8, %start: i64, %mask: i64,
                            %expected_status: i32, %expected_success: i32,
                            %expected_assignment: i64) -> i1 {
    %one = llvm.mlir.constant(1 : i64) : i64
    %zero = llvm.mlir.constant(0 : i64) : i64
    %seed = llvm.mlir.constant(71 : i64) : i64
    %increment = llvm.mlir.constant(15 : i64) : i64
    %capture = llvm.mlir.constant(173 : i64) : i64
    %buffer_size = llvm.mlir.constant(912 : i64) : i64
    %buffer = llvm.alloca %buffer_size x i8 : (i64) -> !llvm.ptr
    %assignment = llvm.alloca %one x i64 : (i64) -> !llvm.ptr
    %success = llvm.alloca %one x i32 : (i64) -> !llvm.ptr
    %state = llvm.alloca %one x i64 : (i64) -> !llvm.ptr
    %captures = llvm.alloca %one x i64 : (i64) -> !llvm.ptr
    "llvm.intr.memcpy"(%buffer, %program, %size) <{isVolatile = false}> : (!llvm.ptr, !llvm.ptr, i64) -> ()
    %patch = llvm.icmp "sge" %offset, %zero : i64
    llvm.cond_br %patch, ^patch, ^solve
  ^patch:
    %slot = llvm.getelementptr %buffer[%offset] : (!llvm.ptr, i64) -> !llvm.ptr, i8
    llvm.store %byte, %slot : i8, !llvm.ptr
    llvm.br ^solve
  ^solve:
    llvm.store %capture, %captures : i64, !llvm.ptr
    llvm.store %zero, %assignment : i64, !llvm.ptr
    %sentinel = llvm.mlir.constant(-1 : i32) : i32
    llvm.store %sentinel, %success : i32, !llvm.ptr
    llvm.store %zero, %state : i64, !llvm.ptr
    %status = llvm.call @obelisk_rt_v1_random_solve_modes_state(%ctx, %buffer, %size, %start, %mask, %zero, %one, %seed, %increment, %captures, %one, %assignment, %success, %state) : (!llvm.ptr, !llvm.ptr, i64, i64, i64, i64, i64, i64, i64, !llvm.ptr, i64, !llvm.ptr, !llvm.ptr, !llvm.ptr) -> i32
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

  llvm.func @main() -> i32 {
    %z32 = llvm.mlir.constant(0 : i32) : i32
    %zero = llvm.mlir.constant(0 : i64) : i64
    %one = llvm.mlir.constant(1 : i64) : i64
    %three = llvm.mlir.constant(3 : i64) : i64
    %seed = llvm.mlir.constant(71 : i64) : i64
    %increment = llvm.mlir.constant(15 : i64) : i64
    %capture = llvm.mlir.constant(173 : i64) : i64
    %width = llvm.mlir.constant(130 : i32) : i32
    %ctx_ptr = llvm.alloca %one x !llvm.ptr : (i64) -> !llvm.ptr
    %created = llvm.call @obelisk_rt_v1_context_create(%ctx_ptr) : (!llvm.ptr) -> i32
    %ctx = llvm.load %ctx_ptr : !llvm.ptr -> !llvm.ptr
    %valid_0 = llvm.icmp "eq" %created, %z32 : i32
    // singleton equality samples its only assignment
    %program_1 = llvm.mlir.addressof @valid : !llvm.ptr
    %size_1 = llvm.mlir.constant(912 : i64) : i64
    %offset_1 = llvm.mlir.constant(-1 : i64) : i64
    %byte_1 = llvm.mlir.constant(0 : i8) : i8
    %start_1 = llvm.mlir.constant(0 : i64) : i64
    %mask_1 = llvm.mlir.constant(255 : i64) : i64
    %status_1 = llvm.mlir.constant(0 : i32) : i32
    %success_1 = llvm.mlir.constant(1 : i32) : i32
    %assignment_1 = llvm.mlir.constant(173 : i64) : i64
    %ok_1 = llvm.call @check(%ctx, %program_1, %size_1, %offset_1, %byte_1, %start_1, %mask_1, %status_1, %success_1, %assignment_1) : (!llvm.ptr, !llvm.ptr, i64, i64, i8, i64, i64, i32, i32, i64) -> i1
    %valid_1 = llvm.and %valid_0, %ok_1 : i1
    // frozen low bits that agree with the singleton keep it
    %program_2 = llvm.mlir.addressof @valid : !llvm.ptr
    %size_2 = llvm.mlir.constant(912 : i64) : i64
    %offset_2 = llvm.mlir.constant(-1 : i64) : i64
    %byte_2 = llvm.mlir.constant(0 : i8) : i8
    %start_2 = llvm.mlir.constant(173 : i64) : i64
    %mask_2 = llvm.mlir.constant(15 : i64) : i64
    %status_2 = llvm.mlir.constant(0 : i32) : i32
    %success_2 = llvm.mlir.constant(1 : i32) : i32
    %assignment_2 = llvm.mlir.constant(173 : i64) : i64
    %ok_2 = llvm.call @check(%ctx, %program_2, %size_2, %offset_2, %byte_2, %start_2, %mask_2, %status_2, %success_2, %assignment_2) : (!llvm.ptr, !llvm.ptr, i64, i64, i8, i64, i64, i32, i32, i64) -> i1
    %valid_2 = llvm.and %valid_1, %ok_2 : i1
    // frozen low bits that disagree fail without an assignment
    %program_3 = llvm.mlir.addressof @valid : !llvm.ptr
    %size_3 = llvm.mlir.constant(912 : i64) : i64
    %offset_3 = llvm.mlir.constant(-1 : i64) : i64
    %byte_3 = llvm.mlir.constant(0 : i8) : i8
    %start_3 = llvm.mlir.constant(0 : i64) : i64
    %mask_3 = llvm.mlir.constant(15 : i64) : i64
    %status_3 = llvm.mlir.constant(0 : i32) : i32
    %success_3 = llvm.mlir.constant(0 : i32) : i32
    %assignment_3 = llvm.mlir.constant(0 : i64) : i64
    %ok_3 = llvm.call @check(%ctx, %program_3, %size_3, %offset_3, %byte_3, %start_3, %mask_3, %status_3, %success_3, %assignment_3) : (!llvm.ptr, !llvm.ptr, i64, i64, i8, i64, i64, i32, i32, i64) -> i1
    %valid_3 = llvm.and %valid_2, %ok_3 : i1
    // forward edge
    %program_4 = llvm.mlir.addressof @valid : !llvm.ptr
    %size_4 = llvm.mlir.constant(912 : i64) : i64
    %offset_4 = llvm.mlir.constant(396 : i64) : i64
    %byte_4 = llvm.mlir.constant(34 : i8) : i8
    %start_4 = llvm.mlir.constant(0 : i64) : i64
    %mask_4 = llvm.mlir.constant(255 : i64) : i64
    %status_4 = llvm.mlir.constant(2 : i32) : i32
    %success_4 = llvm.mlir.constant(0 : i32) : i32
    %assignment_4 = llvm.mlir.constant(0 : i64) : i64
    %ok_4 = llvm.call @check(%ctx, %program_4, %size_4, %offset_4, %byte_4, %start_4, %mask_4, %status_4, %success_4, %assignment_4) : (!llvm.ptr, !llvm.ptr, i64, i64, i8, i64, i64, i32, i32, i64) -> i1
    %valid_4 = llvm.and %valid_3, %ok_4 : i1
    // random order
    %program_5 = llvm.mlir.addressof @valid : !llvm.ptr
    %size_5 = llvm.mlir.constant(912 : i64) : i64
    %offset_5 = llvm.mlir.constant(160 : i64) : i64
    %byte_5 = llvm.mlir.constant(0 : i8) : i8
    %start_5 = llvm.mlir.constant(0 : i64) : i64
    %mask_5 = llvm.mlir.constant(255 : i64) : i64
    %status_5 = llvm.mlir.constant(2 : i32) : i32
    %success_5 = llvm.mlir.constant(0 : i32) : i32
    %assignment_5 = llvm.mlir.constant(0 : i64) : i64
    %ok_5 = llvm.call @check(%ctx, %program_5, %size_5, %offset_5, %byte_5, %start_5, %mask_5, %status_5, %success_5, %assignment_5) : (!llvm.ptr, !llvm.ptr, i64, i64, i8, i64, i64, i32, i32, i64) -> i1
    %valid_5 = llvm.and %valid_4, %ok_5 : i1
    // capture index
    %program_6 = llvm.mlir.addressof @valid : !llvm.ptr
    %size_6 = llvm.mlir.constant(912 : i64) : i64
    %offset_6 = llvm.mlir.constant(268 : i64) : i64
    %byte_6 = llvm.mlir.constant(1 : i8) : i8
    %start_6 = llvm.mlir.constant(0 : i64) : i64
    %mask_6 = llvm.mlir.constant(255 : i64) : i64
    %status_6 = llvm.mlir.constant(2 : i32) : i32
    %success_6 = llvm.mlir.constant(0 : i32) : i32
    %assignment_6 = llvm.mlir.constant(0 : i64) : i64
    %ok_6 = llvm.call @check(%ctx, %program_6, %size_6, %offset_6, %byte_6, %start_6, %mask_6, %status_6, %success_6, %assignment_6) : (!llvm.ptr, !llvm.ptr, i64, i64, i8, i64, i64, i32, i32, i64) -> i1
    %valid_6 = llvm.and %valid_5, %ok_6 : i1
    // capture bit
    %program_7 = llvm.mlir.addressof @valid : !llvm.ptr
    %size_7 = llvm.mlir.constant(912 : i64) : i64
    %offset_7 = llvm.mlir.constant(272 : i64) : i64
    %byte_7 = llvm.mlir.constant(64 : i8) : i8
    %start_7 = llvm.mlir.constant(0 : i64) : i64
    %mask_7 = llvm.mlir.constant(255 : i64) : i64
    %status_7 = llvm.mlir.constant(2 : i32) : i32
    %success_7 = llvm.mlir.constant(0 : i32) : i32
    %assignment_7 = llvm.mlir.constant(0 : i64) : i64
    %ok_7 = llvm.call @check(%ctx, %program_7, %size_7, %offset_7, %byte_7, %start_7, %mask_7, %status_7, %success_7, %assignment_7) : (!llvm.ptr, !llvm.ptr, i64, i64, i8, i64, i64, i32, i32, i64) -> i1
    %valid_7 = llvm.and %valid_6, %ok_7 : i1
    // wrong atom
    %program_8 = llvm.mlir.addressof @valid : !llvm.ptr
    %size_8 = llvm.mlir.constant(912 : i64) : i64
    %offset_8 = llvm.mlir.constant(888 : i64) : i64
    %byte_8 = llvm.mlir.constant(0 : i8) : i8
    %start_8 = llvm.mlir.constant(0 : i64) : i64
    %mask_8 = llvm.mlir.constant(255 : i64) : i64
    %status_8 = llvm.mlir.constant(2 : i32) : i32
    %success_8 = llvm.mlir.constant(0 : i32) : i32
    %assignment_8 = llvm.mlir.constant(0 : i64) : i64
    %ok_8 = llvm.call @check(%ctx, %program_8, %size_8, %offset_8, %byte_8, %start_8, %mask_8, %status_8, %success_8, %assignment_8) : (!llvm.ptr, !llvm.ptr, i64, i64, i8, i64, i64, i32, i32, i64) -> i1
    %valid_8 = llvm.and %valid_7, %ok_8 : i1
    // root handle
    %program_9 = llvm.mlir.addressof @valid : !llvm.ptr
    %size_9 = llvm.mlir.constant(912 : i64) : i64
    %offset_9 = llvm.mlir.constant(892 : i64) : i64
    %byte_9 = llvm.mlir.constant(96 : i8) : i8
    %start_9 = llvm.mlir.constant(0 : i64) : i64
    %mask_9 = llvm.mlir.constant(255 : i64) : i64
    %status_9 = llvm.mlir.constant(2 : i32) : i32
    %success_9 = llvm.mlir.constant(0 : i32) : i32
    %assignment_9 = llvm.mlir.constant(0 : i64) : i64
    %ok_9 = llvm.call @check(%ctx, %program_9, %size_9, %offset_9, %byte_9, %start_9, %mask_9, %status_9, %success_9, %assignment_9) : (!llvm.ptr, !llvm.ptr, i64, i64, i8, i64, i64, i32, i32, i64) -> i1
    %valid_9 = llvm.and %valid_8, %ok_9 : i1
    // core length
    %program_10 = llvm.mlir.addressof @valid : !llvm.ptr
    %size_10 = llvm.mlir.constant(912 : i64) : i64
    %offset_10 = llvm.mlir.constant(112 : i64) : i64
    %byte_10 = llvm.mlir.constant(89 : i8) : i8
    %start_10 = llvm.mlir.constant(0 : i64) : i64
    %mask_10 = llvm.mlir.constant(255 : i64) : i64
    %status_10 = llvm.mlir.constant(2 : i32) : i32
    %success_10 = llvm.mlir.constant(0 : i32) : i32
    %assignment_10 = llvm.mlir.constant(0 : i64) : i64
    %ok_10 = llvm.call @check(%ctx, %program_10, %size_10, %offset_10, %byte_10, %start_10, %mask_10, %status_10, %success_10, %assignment_10) : (!llvm.ptr, !llvm.ptr, i64, i64, i8, i64, i64, i32, i32, i64) -> i1
    %valid_10 = llvm.and %valid_9, %ok_10 : i1
    // trailer version
    %program_11 = llvm.mlir.addressof @valid : !llvm.ptr
    %size_11 = llvm.mlir.constant(912 : i64) : i64
    %offset_11 = llvm.mlir.constant(900 : i64) : i64
    %byte_11 = llvm.mlir.constant(2 : i8) : i8
    %start_11 = llvm.mlir.constant(0 : i64) : i64
    %mask_11 = llvm.mlir.constant(255 : i64) : i64
    %status_11 = llvm.mlir.constant(2 : i32) : i32
    %success_11 = llvm.mlir.constant(0 : i32) : i32
    %assignment_11 = llvm.mlir.constant(0 : i64) : i64
    %ok_11 = llvm.call @check(%ctx, %program_11, %size_11, %offset_11, %byte_11, %start_11, %mask_11, %status_11, %success_11, %assignment_11) : (!llvm.ptr, !llvm.ptr, i64, i64, i8, i64, i64, i32, i32, i64) -> i1
    %valid_11 = llvm.and %valid_10, %ok_11 : i1
    // state-only relation has one empty assignment
    %program_12 = llvm.mlir.addressof @zero_width : !llvm.ptr
    %size_12 = llvm.mlir.constant(144 : i64) : i64
    %offset_12 = llvm.mlir.constant(-1 : i64) : i64
    %byte_12 = llvm.mlir.constant(0 : i8) : i8
    %start_12 = llvm.mlir.constant(0 : i64) : i64
    %mask_12 = llvm.mlir.constant(0 : i64) : i64
    %status_12 = llvm.mlir.constant(0 : i32) : i32
    %success_12 = llvm.mlir.constant(1 : i32) : i32
    %assignment_12 = llvm.mlir.constant(0 : i64) : i64
    %ok_12 = llvm.call @check(%ctx, %program_12, %size_12, %offset_12, %byte_12, %start_12, %mask_12, %status_12, %success_12, %assignment_12) : (!llvm.ptr, !llvm.ptr, i64, i64, i8, i64, i64, i32, i32, i64) -> i1
    %valid_12 = llvm.and %valid_11, %ok_12 : i1
    // A wide call whose capture inventory disagrees with the plan is rejected.
    %captures = llvm.alloca %three x i64 : (i64) -> !llvm.ptr
    %widths = llvm.alloca %one x i32 : (i64) -> !llvm.ptr
    %start_words = llvm.alloca %three x i64 : (i64) -> !llvm.ptr
    %masks = llvm.alloca %three x i64 : (i64) -> !llvm.ptr
    %assignment = llvm.alloca %three x i64 : (i64) -> !llvm.ptr
    %success = llvm.alloca %one x i32 : (i64) -> !llvm.ptr
    %state = llvm.alloca %one x i64 : (i64) -> !llvm.ptr
    llvm.store %capture, %captures : i64, !llvm.ptr
    llvm.store %width, %widths : i32, !llvm.ptr
    llvm.store %zero, %assignment : i64, !llvm.ptr
    %program_w = llvm.mlir.addressof @wide_capture_inventory : !llvm.ptr
    %size_w = llvm.mlir.constant(2232 : i64) : i64
    %status_w = llvm.call @obelisk_rt_v1_random_solve_wide_modes_state(%ctx, %program_w, %size_w, %start_words, %masks, %three, %zero, %one, %seed, %increment, %captures, %three, %widths, %one, %assignment, %success, %state) : (!llvm.ptr, !llvm.ptr, i64, !llvm.ptr, !llvm.ptr, i64, i64, i64, i64, i64, !llvm.ptr, i64, !llvm.ptr, i64, !llvm.ptr, !llvm.ptr, !llvm.ptr) -> i32
    %actual_success_w = llvm.load %success : !llvm.ptr -> i32
    %actual_assignment_w = llvm.load %assignment : !llvm.ptr -> i64
    %actual_state_w = llvm.load %state : !llvm.ptr -> i64
    %two = llvm.mlir.constant(2 : i32) : i32
    %status_ok_w = llvm.icmp "eq" %status_w, %two : i32
    %success_ok_w = llvm.icmp "eq" %actual_success_w, %z32 : i32
    %assignment_ok_w = llvm.icmp "eq" %actual_assignment_w, %zero : i64
    %state_ok_w = llvm.icmp "eq" %actual_state_w, %seed : i64
    %a_w = llvm.and %status_ok_w, %success_ok_w : i1
    %b_w = llvm.and %assignment_ok_w, %state_ok_w : i1
    %c_w = llvm.and %a_w, %b_w : i1
    %valid_w = llvm.and %valid_12, %c_w : i1
    llvm.call @obelisk_rt_v1_context_destroy(%ctx) : (!llvm.ptr) -> ()
    %ok_ptr = llvm.mlir.addressof @ok : !llvm.ptr
    %bad_ptr = llvm.mlir.addressof @bad : !llvm.ptr
    %message = llvm.select %valid_w, %ok_ptr, %bad_ptr : i1, !llvm.ptr
    %printed = llvm.call @puts(%message) : (!llvm.ptr) -> i32
    llvm.return %z32 : i32
  }
}
