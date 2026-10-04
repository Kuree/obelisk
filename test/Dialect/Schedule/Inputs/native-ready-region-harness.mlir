// Separate compilation keeps the observer opaque to the code under test at
// both O0 and O3. The oracle uses ordinary LLVM memory operations and asserts;
// it does not run the schedule transformations being tested.
module {
  llvm.mlir.global external @ready() : !llvm.array<66 x i64>
  llvm.mlir.global external @ordinary(0 : i64) : i64
  llvm.mlir.global external @seen_before(0 : i64) : i64
  llvm.mlir.global external @seen_after(0 : i64) : i64
  llvm.mlir.global internal @expected(0 : i64) : i64
  llvm.func @exercise(i64, !llvm.ptr)
  llvm.func @alias_updates()

  llvm.func @observe(%n: i64) {
    %base = llvm.mlir.addressof @ready : !llvm.ptr
    %last = llvm.getelementptr %base[63] : (!llvm.ptr) -> !llvm.ptr, i64
    %cache = llvm.getelementptr %base[64] : (!llvm.ptr) -> !llvm.ptr, i64
    %summary = llvm.getelementptr %base[65] : (!llvm.ptr) -> !llvm.ptr, i64
    %expected = llvm.mlir.addressof @expected : !llvm.ptr
    %zero = arith.constant 0 : i64
    %one = arith.constant 1 : i64
    %four = arith.constant 4 : i64
    %high = arith.constant -9223372036854775808 : i64
    %without_high = arith.constant 9223372036854775807 : i64
    %bit16 = arith.constant 65536 : i64

    %wanted = llvm.load %expected : !llvm.ptr -> i64
    %actual = llvm.load %base : !llvm.ptr -> i64
    %value_ok = arith.cmpi eq, %actual, %wanted : i64
    cf.assert %value_ok, "callback must see ordered ready additions and consumes"
    %has_iterations = arith.cmpi ne, %n, %zero : i64
    %loop_high = arith.select %has_iterations, %high, %zero : i64
    %wanted_last = arith.ori %four, %loop_high : i64
    %actual_last = llvm.load %last : !llvm.ptr -> i64
    %last_ok = arith.cmpi eq, %actual_last, %wanted_last : i64
    cf.assert %last_ok, "callback must see bit 63 in the last leaf"
    %actual_cache = llvm.load %cache : !llvm.ptr -> i64
    %cache_ok = arith.cmpi eq, %actual_cache, %zero : i64
    cf.assert %cache_ok, "callback must see the minimum cache"
    %wanted_summary = arith.ori %one, %high : i64
    %actual_summary = llvm.load %summary : !llvm.ptr -> i64
    %summary_ok = arith.cmpi eq, %actual_summary, %wanted_summary : i64
    cf.assert %summary_ok, "callback must see the leaf summary"

    // Mutate authoritative state so later region commits must retain it.
    %updated = arith.ori %actual, %bit16 : i64
    llvm.store %updated, %base : i64, !llvm.ptr
    %updated_last = arith.xori %actual_last, %four : i64
    llvm.store %updated_last, %last : i64, !llvm.ptr
    %empty = arith.cmpi eq, %updated_last, %zero : i64
    %cleared_summary = arith.andi %actual_summary, %without_high : i64
    %updated_summary = arith.select %empty, %cleared_summary, %actual_summary : i64
    llvm.store %updated_summary, %summary : i64, !llvm.ptr
    llvm.return
  }

  llvm.func @main() -> i32 {
    %base = llvm.mlir.addressof @ready : !llvm.ptr
    %last = llvm.getelementptr %base[63] : (!llvm.ptr) -> !llvm.ptr, i64
    %cache = llvm.getelementptr %base[64] : (!llvm.ptr) -> !llvm.ptr, i64
    %summary = llvm.getelementptr %base[65] : (!llvm.ptr) -> !llvm.ptr, i64
    %ordinary = llvm.mlir.addressof @ordinary : !llvm.ptr
    %before = llvm.mlir.addressof @seen_before : !llvm.ptr
    %after = llvm.mlir.addressof @seen_after : !llvm.ptr
    %expected = llvm.mlir.addressof @expected : !llvm.ptr
    %empty = llvm.mlir.zero : !llvm.array<66 x i64>
    %zero = arith.constant 0 : i64
    %one = arith.constant 1 : i64
    %two = arith.constant 2 : i64
    %four = arith.constant 4 : i64
    %nine = arith.constant 9 : i64
    %sentinel = arith.constant 99 : i64
    %all = arith.constant -1 : i64
    %high = arith.constant -9223372036854775808 : i64
    %bit8 = arith.constant 256 : i64
    %bit16 = arith.constant 65536 : i64
    %bit32 = arith.constant 4294967296 : i64
    %initial = arith.constant 1099511627776 : i64
    %initial_summary = arith.ori %one, %high : i64
    llvm.call @alias_updates() : () -> ()
    %alias_value = llvm.load %base : !llvm.ptr -> i64
    %alias_ok = arith.cmpi eq, %alias_value, %two : i64
    cf.assert %alias_ok, "alias consume must prevent incorrect coalescing"
    cf.br ^test(%zero : i64)
  ^test(%n: i64):
    %more = arith.cmpi ult, %n, %nine : i64
    cf.cond_br %more, ^exercise, ^done
  ^exercise:
    llvm.store %empty, %base : !llvm.array<66 x i64>, !llvm.ptr
    llvm.store %initial, %base : i64, !llvm.ptr
    llvm.store %four, %last : i64, !llvm.ptr
    llvm.store %initial_summary, %summary : i64, !llvm.ptr
    llvm.store %sentinel, %ordinary : i64, !llvm.ptr

    // The loop sets bits [0,n); its final consume clears bit 0 iff n is odd.
    // This closed-form oracle also covers zero iterations and the old bit 40.
    %limit = arith.shli %one, %n : i64
    %low_bits = arith.subi %limit, %one : i64
    %odd_bit = arith.andi %n, %one : i64
    %keep = arith.xori %odd_bit, %all : i64
    %loop_bits = arith.andi %low_bits, %keep : i64
    %wanted = arith.ori %initial, %loop_bits : i64
    llvm.store %wanted, %expected : i64, !llvm.ptr
    llvm.call @exercise(%n, %base) : (i64, !llvm.ptr) -> ()

    %seen_before = llvm.load %before : !llvm.ptr -> i64
    %before_ok = arith.cmpi eq, %seen_before, %wanted : i64
    cf.assert %before_ok, "indirect alias read must see pending readiness"
    %with_bit8 = arith.ori %wanted, %bit8 : i64
    %wanted_after = arith.ori %with_bit8, %bit16 : i64
    %seen_after = llvm.load %after : !llvm.ptr -> i64
    %after_ok = arith.cmpi eq, %seen_after, %wanted_after : i64
    cf.assert %after_ok, "symbol alias read must retain callback writes"
    %wanted_final = arith.ori %with_bit8, %bit32 : i64
    %actual = llvm.load %base : !llvm.ptr -> i64
    %final_ok = arith.cmpi eq, %actual, %wanted_final : i64
    cf.assert %final_ok, "region exit must materialize final add/consume order"
    %actual_last = llvm.load %last : !llvm.ptr -> i64
    %last_ok = arith.cmpi eq, %actual_last, %zero : i64
    cf.assert %last_ok, "last leaf must be empty after consume"
    %actual_cache = llvm.load %cache : !llvm.ptr -> i64
    %cache_ok = arith.cmpi eq, %actual_cache, %zero : i64
    cf.assert %cache_ok, "minimum cache must survive region exit"
    %actual_summary = llvm.load %summary : !llvm.ptr -> i64
    %summary_ok = arith.cmpi eq, %actual_summary, %one : i64
    cf.assert %summary_ok, "summary must clear the emptied last leaf"
    %has_iterations = arith.cmpi ne, %n, %zero : i64
    %last_iteration = arith.subi %n, %one : i64
    %wanted_ordinary = arith.select %has_iterations, %last_iteration, %sentinel : i64
    %actual_ordinary = llvm.load %ordinary : !llvm.ptr -> i64
    %ordinary_ok = arith.cmpi eq, %actual_ordinary, %wanted_ordinary : i64
    cf.assert %ordinary_ok, "ordinary loop stores must retain their values"
    %next = arith.addi %n, %one : i64
    cf.br ^test(%next : i64)
  ^done:
    %success = arith.constant 0 : i32
    llvm.return %success : i32
  }
}
