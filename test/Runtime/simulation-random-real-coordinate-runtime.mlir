// RUN: obelisk-opt %s --obelisk-sim-compile-random-plans | FileCheck %s --check-prefix=PLAN
// RUN: obelisk-opt %s --obelisk-sim-compile-random-plans --obelisk-sim-compile-random-plans --mlir-print-ir-after=obelisk-sim-compile-random-plans --mlir-print-ir-after-change -o /dev/null 2>&1 | FileCheck %s --check-prefix=ONCE --implicit-check-not='IR Dump After'
// RUN: obelisk-opt %s --obelisk-sim-compile-random-plans '--encode-obelisk-sim-to-bytecode=vpi=off' --test-obelisk-print-bytecode -o /dev/null 2>&1 | FileCheck %s --check-prefix=BYTECODE
// RUN: obelisk-opt %s --obelisk-sim-compile-random-plans '--encode-obelisk-sim-to-bytecode=vpi=off' --convert-obelisk-sim-processes-to-llvm-coroutines | obelisk-translate --mlir-to-llvmir | %llvm_dist/bin/opt -passes='coro-early,cgscc(coro-split<reuse-storage>),coro-cleanup' | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %target_cxx %t.o %runtime_link_flags -o %t.exe
// RUN: %t.exe --seed=13 | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: compiled-sampler
//
// IEEE 1800-2023 18.4.1: a live real bound is compared in numeric coordinate
// space against exact IEEE cells. Both wide solves carry a SAMP plan, the pass
// is idempotent, and the rank conversions are bytecode intrinsics (to_rank:
// 1 input, 2 outputs; from_rank: 1 input, 1 output).
// PLAN: simulation.random.solve_wide
// PLAN-SAME: SAMP
// PLAN: simulation.random.solve_wide
// PLAN-SAME: SAMP
// ONCE: IR Dump After
// BYTECODE: intrinsic {{[0-9]+}}: id=0x00010484 inputs=1 outputs=2
// BYTECODE: intrinsic {{[0-9]+}}: id=0x00010485 inputs=1 outputs=1
//
// The run sweeps all six comparisons in both operand orders, frozen and
// disabled inputs, finite endpoint clipping, subnormals, signed zero, NaN
// and infinity in both precisions, then the 18.4.1 density of (0,1) against
// (1,2) in (0,2).
// RUNTIME: real coordinates valid 1 density 1


module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "aarch64-unknown-linux-gnu"} {
  simulation.design @real_coordinates attributes {
    compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1,
      nodes = [], edges = [], regions = [
        #schedule.region<kind = active, groups = []>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>} {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 function hierarchy "check32"
    simulation.code_unit.decl 3 in 0 function hierarchy "check64"
    simulation.code_unit.decl 4 in 0 function hierarchy "density32"
    simulation.code_unit.decl 5 in 0 function hierarchy "density64"
    simulation.code_unit.decl 6 in 0 function hierarchy "verdict"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %zero = arith.constant 0 : i64
      %one = arith.constant 1 : i64
      %six = arith.constant 6 : i64
      %twelve = arith.constant 12 : i64
      %count = arith.constant 36 : i64
      %true = arith.constant true
      %sign32 = arith.constant 2147483648 : i64
      %sign64 = arith.constant 9223372036854775808 : i64
      %snan32 = arith.constant 4286578689 : i64
      %snan64 = arith.constant 18442240474082181121 : i64
      %last = arith.constant 17 : i64
      %thirty_two = arith.constant 32 : i64
      %sixty_four = arith.constant 64 : i64
      %table32 = arith.constant 0x7FC000017F8000007F7FFFFF3F8000013F80000000800000007FFFFF0000000100000000 : i288
      %table64 = arith.constant 0x7FF80000000000017FF00000000000007FEFFFFFFFFFFFFF3FF00000000000013FF00000000000000010000000000000000FFFFFFFFFFFFF00000000000000010000000000000000 : i576
      cf.br ^mode(%zero, %true : i64, i1)
    ^mode(%m: i64, %mode_valid: i1):
      %kind = arith.remui %m, %six : i64
      %disabled = arith.divui %m, %six : i64
      cf.br ^item(%zero, %mode_valid : i64, i1)
    ^item(%i: i64, %valid: i1):
      %j = arith.shrui %i, %one : i64
      %low = arith.andi %i, %one : i64
      %negative = arith.cmpi ne, %low, %zero : i64
      %is_last = arith.cmpi eq, %i, %last : i64
      %shift32 = arith.muli %j, %thirty_two : i64
      %amount32 = arith.extui %shift32 : i64 to i288
      %shifted32 = arith.shrui %table32, %amount32 : i288
      %entry32 = arith.trunci %shifted32 : i288 to i32
      %pos32 = arith.extui %entry32 : i32 to i64
      %flip32 = arith.ori %pos32, %sign32 : i64
      %neg32 = arith.select %is_last, %snan32, %flip32 : i64
      %raw32 = arith.select %negative, %neg32, %pos32 : i64
      %shift64 = arith.muli %j, %sixty_four : i64
      %amount64 = arith.extui %shift64 : i64 to i576
      %shifted64 = arith.shrui %table64, %amount64 : i576
      %pos64 = arith.trunci %shifted64 : i576 to i64
      %flip64 = arith.ori %pos64, %sign64 : i64
      %neg64 = arith.select %is_last, %snan64, %flip64 : i64
      %raw64 = arith.select %negative, %neg64, %pos64 : i64
      %r1 = simulation.call @check32(%ctx, %raw32, %kind, %low, %disabled, %zero) : (!simulation.context, i64, i64, i64, i64, i64) -> i1
      %r2 = simulation.call @check64(%ctx, %raw64, %kind, %low, %disabled, %zero) : (!simulation.context, i64, i64, i64, i64, i64) -> i1
      %r3 = simulation.call @check32(%ctx, %raw32, %kind, %low, %disabled, %one) : (!simulation.context, i64, i64, i64, i64, i64) -> i1
      %r4 = simulation.call @check64(%ctx, %raw64, %kind, %low, %disabled, %one) : (!simulation.context, i64, i64, i64, i64, i64) -> i1
      %v1 = arith.andi %r1, %r2 : i1
      %v2 = arith.andi %r3, %r4 : i1
      %v3 = arith.andi %v1, %v2 : i1
      %ok = arith.andi %v3, %valid : i1
      %next_item = arith.addi %i, %one : i64
      %more_items = arith.cmpi ult, %next_item, %count : i64
      cf.cond_br %more_items, ^item(%next_item, %ok : i64, i1), ^mode_end(%ok : i1)
    ^mode_end(%swept: i1):
      %next = arith.addi %m, %one : i64
      %more = arith.cmpi ult, %next, %twelve : i64
      cf.cond_br %more, ^mode(%next, %swept : i64, i1), ^done(%swept : i1)
    ^done(%checked: i1):
      %d32 = simulation.call @density32(%ctx) : (!simulation.context) -> i1
      %d64 = simulation.call @density64(%ctx) : (!simulation.context) -> i1
      %density = arith.andi %d32, %d64 : i1
      %channel = arith.constant 1 : i32
      %fmt = simulation.bytes.constant "real coordinates valid %0d density %0d"
      simulation.display %ctx to %channel(%fmt, %checked, %density) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, i1, i1
      simulation.return
    }
    simulation.func @check32(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %raw: i64 {simulation.capture_kind = 1 : i32}, %kind: i64 {simulation.capture_kind = 1 : i32}, %freeze: i64 {simulation.capture_kind = 1 : i32}, %disabled: i64 {simulation.capture_kind = 1 : i32}, %reverse: i64 {simulation.capture_kind = 1 : i32}) -> i1 attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %zero = arith.constant 0 : i64
      %one = arith.constant 1 : i64
      %seed = arith.constant 13 : i64
      %increment = arith.constant 55 : i64
      %true = arith.constant true
      %base = arith.constant 1.0 : f32
      %start, %base_finite = simulation.random.real_to_rank %base : f32 -> i279
      %none = arith.constant 0 : i279
      %all = arith.constant -1 : i279
      %frozen = arith.cmpi ne, %freeze, %zero : i64
      %mask = arith.select %frozen, %none, %all : i279
      %bits = arith.trunci %raw : i64 to i32
      %rhs = arith.bitcast %bits : i32 to f32
      %rhs_rank, %rhs_finite = simulation.random.real_to_rank %rhs : f32 -> i279
      %safe_rank = arith.select %rhs_finite, %rhs_rank, %start : i279
      %roundtrip = simulation.random.real_from_rank %safe_rank : i279 -> f32
      %rt_bits = arith.bitcast %roundtrip : f32 to i32
      %base_bits = arith.bitcast %base : f32 to i32
      %exp_bits = arith.select %rhs_finite, %bits, %base_bits : i32
      %rt_ok = arith.cmpi eq, %rt_bits, %exp_bits : i32
      %rank_zero = arith.cmpi eq, %rhs_rank, %none : i279
      %ph_ok = arith.select %rhs_finite, %true, %rank_zero : i1
      %r:3 = simulation.random.solve_wide %ctx, %start, %mask, %disabled, %one, %seed, %increment, %bits, %kind, %reverse {program = "ODR1\02\00 \00\17\01\00\00L\00\00\00\03\00\00\00 \04\00\00\06\00\00\00\00\00\00\00\01\00\00\00\17\01\00\00\00\00\00\00\00\00\00\00-\00\00\00\17\01\00\00\00\00\00\00\00\00\00\004\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00 \00\00\00\01\00\00\00\FF\FF\FF\FF\00\00\00\00.\00\00\00\17\01\00\00\00\00\00\00\00\00\00\001\00\00\00 \00\00\00\00\00\00\00\00\00\00\00-\00\00\00 \00\00\00\01\00\00\00\00\00\00\00\02\00\00\00 \00\00\00\00\00\00\00\00\00\00\00/\00\00\00 \00\00\00\00\00\00\00\00\00\00\00-\00\00\00 \00\00\00\02\00\00\00\00\00\00\00\15\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\02\00\00\00@\00\00\00\01\00\00\00\00\00\00\00\22\00\00\00@\00\00\00\00\00\00\00\00\00\00\00\15\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1B\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00.\00\00\00 \00\00\00\01\00\00\00\00\00\00\00.\00\00\00 \00\00\00\02\00\00\00\00\00\00\00\16\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\02\00\00\00@\00\00\00\01\00\00\00\00\00\00\00\22\00\00\00@\00\00\00\00\00\00\00\01\00\00\00\15\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1B\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1C\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\02\00\00\00@\00\00\00\02\00\00\00\00\00\00\00.\00\00\00 \00\00\00\02\00\00\00\00\00\00\00.\00\00\00 \00\00\00\01\00\00\00\00\00\00\00\19\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00.\00\00\00 \00\00\00\01\00\00\00\00\00\00\00.\00\00\00 \00\00\00\02\00\00\00\00\00\00\00\17\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1F\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\02\00\00\00@\00\00\00\01\00\00\00\00\00\00\00\22\00\00\00@\00\00\00\00\00\00\00\02\00\00\00\15\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1B\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1C\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\02\00\00\00@\00\00\00\02\00\00\00\00\00\00\00.\00\00\00 \00\00\00\02\00\00\00\00\00\00\00.\00\00\00 \00\00\00\01\00\00\00\00\00\00\00\1A\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00.\00\00\00 \00\00\00\01\00\00\00\00\00\00\00.\00\00\00 \00\00\00\02\00\00\00\00\00\00\00\18\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1F\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\02\00\00\00@\00\00\00\01\00\00\00\00\00\00\00\22\00\00\00@\00\00\00\00\00\00\00\03\00\00\00\15\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1B\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1C\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\02\00\00\00@\00\00\00\02\00\00\00\00\00\00\00.\00\00\00 \00\00\00\02\00\00\00\00\00\00\00.\00\00\00 \00\00\00\01\00\00\00\00\00\00\00\17\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00.\00\00\00 \00\00\00\01\00\00\00\00\00\00\00.\00\00\00 \00\00\00\02\00\00\00\00\00\00\00\19\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1F\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\02\00\00\00@\00\00\00\01\00\00\00\00\00\00\00\22\00\00\00@\00\00\00\00\00\00\00\04\00\00\00\15\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1B\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1C\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\02\00\00\00@\00\00\00\02\00\00\00\00\00\00\00.\00\00\00 \00\00\00\02\00\00\00\00\00\00\00.\00\00\00 \00\00\00\01\00\00\00\00\00\00\00\18\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00.\00\00\00 \00\00\00\01\00\00\00\00\00\00\00.\00\00\00 \00\00\00\02\00\00\00\00\00\00\00\1A\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1F\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\02\00\00\00@\00\00\00\01\00\00\00\00\00\00\00\22\00\00\00@\00\00\00\00\00\00\00\05\00\00\00\15\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1B\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1C\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00 \00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\02\00\00\00\00\00\00\00\03\00\00\00\00\00\00\00\04\00\00\00\00\00\00\00\05\00\00\00\00\00\00\00"} : (!simulation.context, i279, i279, i64, i64, i64, i64, i32, i64, i64) -> (i279, i1, i64)
      %safe = arith.select %r#1, %r#0, %start : i279
      %value = simulation.random.real_from_rank %safe : i279 -> f32
      %max_bits = arith.constant 2139095039 : i32
      %min_bits = arith.constant 4286578687 : i32
      %maximum = arith.bitcast %max_bits : i32 to f32
      %minimum = arith.bitcast %min_bits : i32 to f32
      %was_fixed = arith.cmpi eq, %safe, %start : i279
      %max_equal = arith.cmpi eq, %bits, %max_bits : i32
      %min_equal = arith.cmpi eq, %bits, %min_bits : i32
      %extreme = arith.ori %max_equal, %min_equal : i1
      %unchanged = arith.cmpi eq, %r#2, %seed : i64
      %value64 = arith.extf %value : f32 to f64
      %rhs64 = arith.extf %rhs : f32 to f64
      %base64 = arith.extf %base : f32 to f64
      %maximum64 = arith.extf %maximum : f32 to f64
      %minimum64 = arith.extf %minimum : f32 to f64
      %verdict = simulation.call @verdict(%ctx, %kind, %freeze, %disabled, %value64, %rhs64, %base64, %maximum64, %minimum64, %rhs_finite, %r#1, %was_fixed, %extreme, %unchanged) : (!simulation.context, i64, i64, i64, f64, f64, f64, f64, f64, i1, i1, i1, i1, i1) -> i1
      %ok0 = arith.andi %rt_ok, %ph_ok : i1
      %ok = arith.andi %ok0, %verdict : i1
      simulation.return %ok : i1
    }
    simulation.func @check64(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %raw: i64 {simulation.capture_kind = 1 : i32}, %kind: i64 {simulation.capture_kind = 1 : i32}, %freeze: i64 {simulation.capture_kind = 1 : i32}, %disabled: i64 {simulation.capture_kind = 1 : i32}, %reverse: i64 {simulation.capture_kind = 1 : i32}) -> i1 attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %zero = arith.constant 0 : i64
      %one = arith.constant 1 : i64
      %seed = arith.constant 13 : i64
      %increment = arith.constant 55 : i64
      %true = arith.constant true
      %base = arith.constant 1.0 : f64
      %start, %base_finite = simulation.random.real_to_rank %base : f64 -> i2100
      %none = arith.constant 0 : i2100
      %all = arith.constant -1 : i2100
      %frozen = arith.cmpi ne, %freeze, %zero : i64
      %mask = arith.select %frozen, %none, %all : i2100
      %bits = arith.ori %raw, %zero : i64
      %rhs = arith.bitcast %bits : i64 to f64
      %rhs_rank, %rhs_finite = simulation.random.real_to_rank %rhs : f64 -> i2100
      %safe_rank = arith.select %rhs_finite, %rhs_rank, %start : i2100
      %roundtrip = simulation.random.real_from_rank %safe_rank : i2100 -> f64
      %rt_bits = arith.bitcast %roundtrip : f64 to i64
      %base_bits = arith.bitcast %base : f64 to i64
      %exp_bits = arith.select %rhs_finite, %bits, %base_bits : i64
      %rt_ok = arith.cmpi eq, %rt_bits, %exp_bits : i64
      %rank_zero = arith.cmpi eq, %rhs_rank, %none : i2100
      %ph_ok = arith.select %rhs_finite, %true, %rank_zero : i1
      %r:3 = simulation.random.solve_wide %ctx, %start, %mask, %disabled, %one, %seed, %increment, %bits, %kind, %reverse {program = "ODR1\02\00 \004\08\00\00L\00\00\00\03\00\00\00 \04\00\00\06\00\00\00\00\00\00\00\01\00\00\004\08\00\00\00\00\00\00\00\00\00\00-\00\00\004\08\00\00\00\00\00\00\00\00\00\004\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00 \00\00\00\01\00\00\00\FF\FF\FF\FF\00\00\00\00.\00\00\004\08\00\00\00\00\00\00\00\00\00\002\00\00\00@\00\00\00\00\00\00\00\00\00\00\00-\00\00\00@\00\00\00\01\00\00\00\00\00\00\00\02\00\00\00@\00\00\00\00\00\00\00\00\00\00\000\00\00\00@\00\00\00\00\00\00\00\00\00\00\00-\00\00\00@\00\00\00\02\00\00\00\00\00\00\00\15\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\02\00\00\00@\00\00\00\01\00\00\00\00\00\00\00\22\00\00\00@\00\00\00\00\00\00\00\00\00\00\00\15\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1B\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00.\00\00\00@\00\00\00\01\00\00\00\00\00\00\00.\00\00\00@\00\00\00\02\00\00\00\00\00\00\00\16\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\02\00\00\00@\00\00\00\01\00\00\00\00\00\00\00\22\00\00\00@\00\00\00\00\00\00\00\01\00\00\00\15\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1B\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1C\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\02\00\00\00@\00\00\00\02\00\00\00\00\00\00\00.\00\00\00@\00\00\00\02\00\00\00\00\00\00\00.\00\00\00@\00\00\00\01\00\00\00\00\00\00\00\19\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00.\00\00\00@\00\00\00\01\00\00\00\00\00\00\00.\00\00\00@\00\00\00\02\00\00\00\00\00\00\00\17\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1F\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\02\00\00\00@\00\00\00\01\00\00\00\00\00\00\00\22\00\00\00@\00\00\00\00\00\00\00\02\00\00\00\15\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1B\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1C\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\02\00\00\00@\00\00\00\02\00\00\00\00\00\00\00.\00\00\00@\00\00\00\02\00\00\00\00\00\00\00.\00\00\00@\00\00\00\01\00\00\00\00\00\00\00\1A\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00.\00\00\00@\00\00\00\01\00\00\00\00\00\00\00.\00\00\00@\00\00\00\02\00\00\00\00\00\00\00\18\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1F\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\02\00\00\00@\00\00\00\01\00\00\00\00\00\00\00\22\00\00\00@\00\00\00\00\00\00\00\03\00\00\00\15\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1B\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1C\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\02\00\00\00@\00\00\00\02\00\00\00\00\00\00\00.\00\00\00@\00\00\00\02\00\00\00\00\00\00\00.\00\00\00@\00\00\00\01\00\00\00\00\00\00\00\17\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00.\00\00\00@\00\00\00\01\00\00\00\00\00\00\00.\00\00\00@\00\00\00\02\00\00\00\00\00\00\00\19\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1F\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\02\00\00\00@\00\00\00\01\00\00\00\00\00\00\00\22\00\00\00@\00\00\00\00\00\00\00\04\00\00\00\15\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1B\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1C\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\02\00\00\00@\00\00\00\02\00\00\00\00\00\00\00.\00\00\00@\00\00\00\02\00\00\00\00\00\00\00.\00\00\00@\00\00\00\01\00\00\00\00\00\00\00\18\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00.\00\00\00@\00\00\00\01\00\00\00\00\00\00\00.\00\00\00@\00\00\00\02\00\00\00\00\00\00\00\1A\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1F\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\02\00\00\00@\00\00\00\01\00\00\00\00\00\00\00\22\00\00\00@\00\00\00\00\00\00\00\05\00\00\00\15\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1B\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1C\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00 \00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\02\00\00\00\00\00\00\00\03\00\00\00\00\00\00\00\04\00\00\00\00\00\00\00\05\00\00\00\00\00\00\00"} : (!simulation.context, i2100, i2100, i64, i64, i64, i64, i64, i64, i64) -> (i2100, i1, i64)
      %safe = arith.select %r#1, %r#0, %start : i2100
      %value = simulation.random.real_from_rank %safe : i2100 -> f64
      %max_bits = arith.constant 9218868437227405311 : i64
      %min_bits = arith.constant 18442240474082181119 : i64
      %maximum = arith.bitcast %max_bits : i64 to f64
      %minimum = arith.bitcast %min_bits : i64 to f64
      %was_fixed = arith.cmpi eq, %safe, %start : i2100
      %max_equal = arith.cmpi eq, %bits, %max_bits : i64
      %min_equal = arith.cmpi eq, %bits, %min_bits : i64
      %extreme = arith.ori %max_equal, %min_equal : i1
      %unchanged = arith.cmpi eq, %r#2, %seed : i64
      %verdict = simulation.call @verdict(%ctx, %kind, %freeze, %disabled, %value, %rhs, %base, %maximum, %minimum, %rhs_finite, %r#1, %was_fixed, %extreme, %unchanged) : (!simulation.context, i64, i64, i64, f64, f64, f64, f64, f64, i1, i1, i1, i1, i1) -> i1
      %ok0 = arith.andi %rt_ok, %ph_ok : i1
      %ok = arith.andi %ok0, %verdict : i1
      simulation.return %ok : i1
    }
    simulation.func @verdict(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %kind: i64 {simulation.capture_kind = 1 : i32}, %freeze: i64 {simulation.capture_kind = 1 : i32}, %disabled: i64 {simulation.capture_kind = 1 : i32}, %value: f64 {simulation.capture_kind = 1 : i32}, %rhs: f64 {simulation.capture_kind = 1 : i32}, %base: f64 {simulation.capture_kind = 1 : i32}, %maximum: f64 {simulation.capture_kind = 1 : i32}, %minimum: f64 {simulation.capture_kind = 1 : i32}, %rhs_finite: i1 {simulation.capture_kind = 1 : i32}, %success: i1 {simulation.capture_kind = 1 : i32}, %was_fixed: i1 {simulation.capture_kind = 1 : i32}, %extreme: i1 {simulation.capture_kind = 1 : i32}, %unchanged: i1 {simulation.capture_kind = 1 : i32}) -> i1 attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %zero = arith.constant 0 : i64
      %one = arith.constant 1 : i64
      %two = arith.constant 2 : i64
      %three = arith.constant 3 : i64
      %four = arith.constant 4 : i64
      %five = arith.constant 5 : i64
      %true = arith.constant true
      %false = arith.constant false
      %frozen = arith.cmpi ne, %freeze, %zero : i64
      %cmp0 = arith.cmpf oeq, %value, %rhs : f64
      %cmp1 = arith.cmpf une, %value, %rhs : f64
      %cmp2 = arith.cmpf oge, %value, %rhs : f64
      %cmp3 = arith.cmpf ogt, %value, %rhs : f64
      %cmp4 = arith.cmpf ole, %value, %rhs : f64
      %cmp5 = arith.cmpf olt, %value, %rhs : f64
      %fixed0 = arith.cmpf oeq, %base, %rhs : f64
      %fixed1 = arith.cmpf une, %base, %rhs : f64
      %fixed2 = arith.cmpf oge, %base, %rhs : f64
      %fixed3 = arith.cmpf ogt, %base, %rhs : f64
      %fixed4 = arith.cmpf ole, %base, %rhs : f64
      %fixed5 = arith.cmpf olt, %base, %rhs : f64
      %can2 = arith.cmpf ole, %rhs, %maximum : f64
      %can3 = arith.cmpf olt, %rhs, %maximum : f64
      %can4 = arith.cmpf oge, %rhs, %minimum : f64
      %can5 = arith.cmpf ogt, %rhs, %minimum : f64
      %kind0 = arith.cmpi eq, %kind, %zero : i64
      %kind1 = arith.cmpi eq, %kind, %one : i64
      %kind2 = arith.cmpi eq, %kind, %two : i64
      %kind3 = arith.cmpi eq, %kind, %three : i64
      %kind4 = arith.cmpi eq, %kind, %four : i64
      %kind5 = arith.cmpi eq, %kind, %five : i64
      %ps0 = arith.select %kind0, %cmp0, %false : i1
      %ps1 = arith.select %kind1, %cmp1, %ps0 : i1
      %ps2 = arith.select %kind2, %cmp2, %ps1 : i1
      %ps3 = arith.select %kind3, %cmp3, %ps2 : i1
      %ps4 = arith.select %kind4, %cmp4, %ps3 : i1
      %ps5 = arith.select %kind5, %cmp5, %ps4 : i1
      %fs0 = arith.select %kind0, %fixed0, %false : i1
      %fs1 = arith.select %kind1, %fixed1, %fs0 : i1
      %fs2 = arith.select %kind2, %fixed2, %fs1 : i1
      %fs3 = arith.select %kind3, %fixed3, %fs2 : i1
      %fs4 = arith.select %kind4, %fixed4, %fs3 : i1
      %fs5 = arith.select %kind5, %fixed5, %fs4 : i1
      %cs0 = arith.select %kind0, %rhs_finite, %false : i1
      %cs1 = arith.select %kind1, %true, %cs0 : i1
      %cs2 = arith.select %kind2, %can2, %cs1 : i1
      %cs3 = arith.select %kind3, %can3, %cs2 : i1
      %cs4 = arith.select %kind4, %can4, %cs3 : i1
      %cs5 = arith.select %kind5, %can5, %cs4 : i1
      %mode_off = arith.cmpi ne, %disabled, %zero : i64
      %expected0 = arith.select %frozen, %fs5, %cs5 : i1
      %expected = arith.select %mode_off, %true, %expected0 : i1
      %success_ok = arith.cmpi eq, %success, %expected : i1
      %exp_pred = arith.ori %mode_off, %ps5 : i1
      %predicate_ok = arith.select %success, %exp_pred, %true : i1
      %fixed_ok = arith.select %frozen, %was_fixed, %true : i1
      %single = arith.andi %extreme, %kind0 : i1
      %en_single = arith.select %mode_off, %false, %single : i1
      %failed = arith.xori %success, %true : i1
      %fx_fail = arith.ori %frozen, %failed : i1
      %no_draw = arith.ori %fx_fail, %en_single : i1
      %rng_ok = arith.select %no_draw, %unchanged, %true : i1
      %ok1 = arith.andi %success_ok, %predicate_ok : i1
      %ok2 = arith.andi %fixed_ok, %rng_ok : i1
      %ok = arith.andi %ok1, %ok2 : i1
      simulation.return %ok : i1
    }
    simulation.func @density32(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i1 attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %zero = arith.constant 0 : i64
      %one = arith.constant 1 : i64
      %count = arith.constant 512 : i64
      %low = arith.constant 200 : i64
      %high = arith.constant 312 : i64
      %seed = arith.constant 13 : i64
      %increment = arith.constant 55 : i64
      %start = arith.constant 0 : i279
      %mask = arith.constant -1 : i279
      %z = arith.constant 0.0 : f32
      %first = arith.constant 1.0 : f32
      %last = arith.constant 2.0 : f32
      %true = arith.constant true
      cf.br ^loop(%zero, %zero, %seed, %true : i64, i64, i64, i1)
    ^loop(%iteration: i64, %left: i64, %state: i64, %valid: i1):
      %r:3 = simulation.random.solve_wide %ctx, %start, %mask, %zero, %one, %state, %increment {program = "ODR1\02\00 \00\17\01\00\00\10\00\00\00\00\00\00\00 \04\00\00\02\00\00\00\00\00\00\00\01\00\00\00\17\01\00\00\00\00\00\00\00\00\00\00-\00\00\00\17\01\00\00\00\00\00\00\00\00\00\004\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00 \00\00\00\01\00\00\00\FF\FF\FF\FF\00\00\00\00.\00\00\00\17\01\00\00\00\00\00\00\00\00\00\001\00\00\00 \00\00\00\00\00\00\00\00\00\00\00-\00\00\00 \00\00\00\01\00\00\00\00\00\00\00\22\00\00\00 \00\00\00\00\00\00\00\00\00\00\00/\00\00\00 \00\00\00\00\00\00\00\00\00\00\00\18\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00.\00\00\00 \00\00\00\01\00\00\00\00\00\00\00\22\00\00\00 \00\00\00\00\00\00\00\01\00\00\00/\00\00\00 \00\00\00\00\00\00\00\00\00\00\00\1A\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1B\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00 \00\00\00\01\00\00\00\FF\FF\FF\FF\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00@\00\00\00\00"} : (!simulation.context, i279, i279, i64, i64, i64, i64) -> (i279, i1, i64)
      %value = simulation.random.real_from_rank %r#0 : i279 -> f32
      %positive = arith.cmpf ogt, %value, %z : f32
      %below = arith.cmpf olt, %value, %last : f32
      %lower_half = arith.cmpf olt, %value, %first : f32
      %which = arith.extui %lower_half : i1 to i64
      %next_left = arith.addi %left, %which : i64
      %next = arith.addi %iteration, %one : i64
      %range = arith.andi %positive, %below : i1
      %ok0 = arith.andi %range, %r#1 : i1
      %ok = arith.andi %ok0, %valid : i1
      %more = arith.cmpi ult, %next, %count : i64
      cf.cond_br %more, ^loop(%next, %next_left, %r#2, %ok : i64, i64, i64, i1), ^done(%next_left, %ok : i64, i1)
    ^done(%total_left: i64, %loop_valid: i1):
      %above_low = arith.cmpi uge, %total_left, %low : i64
      %below_high = arith.cmpi ule, %total_left, %high : i64
      %density = arith.andi %above_low, %below_high : i1
      %final_ok = arith.andi %density, %loop_valid : i1
      simulation.return %final_ok : i1
    }
    simulation.func @density64(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i1 attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %zero = arith.constant 0 : i64
      %one = arith.constant 1 : i64
      %count = arith.constant 512 : i64
      %low = arith.constant 200 : i64
      %high = arith.constant 312 : i64
      %seed = arith.constant 13 : i64
      %increment = arith.constant 55 : i64
      %start = arith.constant 0 : i2100
      %mask = arith.constant -1 : i2100
      %z = arith.constant 0.0 : f64
      %first = arith.constant 1.0 : f64
      %last = arith.constant 2.0 : f64
      %true = arith.constant true
      cf.br ^loop(%zero, %zero, %seed, %true : i64, i64, i64, i1)
    ^loop(%iteration: i64, %left: i64, %state: i64, %valid: i1):
      %r:3 = simulation.random.solve_wide %ctx, %start, %mask, %zero, %one, %state, %increment {program = "ODR1\02\00 \004\08\00\00\10\00\00\00\00\00\00\00 \04\00\00\02\00\00\00\00\00\00\00\01\00\00\004\08\00\00\00\00\00\00\00\00\00\00-\00\00\004\08\00\00\00\00\00\00\00\00\00\004\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00 \00\00\00\01\00\00\00\FF\FF\FF\FF\00\00\00\00.\00\00\004\08\00\00\00\00\00\00\00\00\00\002\00\00\00@\00\00\00\00\00\00\00\00\00\00\00-\00\00\00@\00\00\00\01\00\00\00\00\00\00\00\22\00\00\00@\00\00\00\00\00\00\00\00\00\00\000\00\00\00@\00\00\00\00\00\00\00\00\00\00\00\18\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00.\00\00\00@\00\00\00\01\00\00\00\00\00\00\00\22\00\00\00@\00\00\00\00\00\00\00\01\00\00\000\00\00\00@\00\00\00\00\00\00\00\00\00\00\00\1A\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\1B\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00 \00\00\00\01\00\00\00\FF\FF\FF\FF\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00@"} : (!simulation.context, i2100, i2100, i64, i64, i64, i64) -> (i2100, i1, i64)
      %value = simulation.random.real_from_rank %r#0 : i2100 -> f64
      %positive = arith.cmpf ogt, %value, %z : f64
      %below = arith.cmpf olt, %value, %last : f64
      %lower_half = arith.cmpf olt, %value, %first : f64
      %which = arith.extui %lower_half : i1 to i64
      %next_left = arith.addi %left, %which : i64
      %next = arith.addi %iteration, %one : i64
      %range = arith.andi %positive, %below : i1
      %ok0 = arith.andi %range, %r#1 : i1
      %ok = arith.andi %ok0, %valid : i1
      %more = arith.cmpi ult, %next, %count : i64
      cf.cond_br %more, ^loop(%next, %next_left, %r#2, %ok : i64, i64, i64, i1), ^done(%next_left, %ok : i64, i1)
    ^done(%total_left: i64, %loop_valid: i1):
      %above_low = arith.cmpi uge, %total_left, %low : i64
      %below_high = arith.cmpi ule, %total_left, %high : i64
      %density = arith.andi %above_low, %below_high : i1
      %final_ok = arith.andi %density, %loop_valid : i1
      simulation.return %final_ok : i1
    }
  }
}
