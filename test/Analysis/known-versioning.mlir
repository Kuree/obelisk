// RUN: obelisk-opt %s -o /dev/null --pass-pipeline='builtin.module(test-obelisk-sim-known-versioning)' 2>&1 | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(schedule-version-known-regions)' | FileCheck %s --check-prefix=COPY

// Known-state versioning admits a region only when every operation maps known
// operands to known results, every four-state read can be checked at the
// region entry, and the X/Z work it removes exceeds the check.

!logic8 = !simulation.logic<8>
!memory = !simulation.unpacked_array<0 : 7 x !simulation.logic<8>>
!record = !simulation.unpacked_struct<[
  #simulation.field<name = "logic", type = !simulation.logic<8>, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "bits", type = i8, ordinal = 1, packedOffset = 0>
]>
!records = !simulation.unpacked_array<0 : 7 x !record>

module attributes {obelisk.native.optimization_level = 3 : i32} {
  simulation.design @known_versioning {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 100 in 0 function hierarchy "top.carried_unknown"
    simulation.code_unit.decl 101 in 0 function hierarchy "top.copy"
    simulation.code_unit.decl 102 in 0 function hierarchy "top.indirect"
    simulation.code_unit.decl 103 in 0 function hierarchy "top.unknown_constant"
    simulation.code_unit.decl 104 in 0 function hierarchy "top.ranged"
    simulation.code_unit.decl 105 in 0 function hierarchy "top.straight"
    simulation.code_unit.decl 106 in 0 function hierarchy "top.guarded_divide"
    simulation.code_unit.decl 107 in 0 function hierarchy "top.ranged_record"
    simulation.code_unit.decl 108 in 0 function hierarchy "top.irreducible"
    simulation.code_unit.decl 109 in 0 function hierarchy "top.blocked_arm"
    simulation.storage.decl 0 in 0 : !memory design hierarchy "top.mem"
    simulation.storage.decl 1 in 0 : !logic8 design hierarchy "top.out"
    simulation.storage.decl 2 in 0 : i32 design hierarchy "top.index"

    // A carried value starting as X and a read checked per iteration.
    // CHECK-LABEL: known-versioning @carried_unknown
    // CHECK: admit loop bb1 operations={{[0-9]+}} benefit={{[0-9]+}} check=3 live-ins=0 checked=1 owned=0 ranged=0
    simulation.func @carried_unknown(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 100 : i64} {
      %mem = simulation.context.storage %ctx[0] : !simulation.ref<!memory>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!logic8>
      %c0 = arith.constant 0 : i32
      %c1 = arith.constant 1 : i32
      %c8 = arith.constant 8 : i32
      %x = simulation.logic.constant 0 : i8, -1 : i8 : !logic8
      cf.br ^header(%c0, %x : i32, !logic8)
    ^header(%i: i32, %acc: !logic8):
      %more = arith.cmpi slt, %i, %c8 : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %wide = arith.extsi %i : i32 to i65
      %slot = simulation.ref.array_element %mem[%wide] : (!simulation.ref<!memory>, i65) -> !simulation.ref<!logic8>
      %element = simulation.ref.load %slot : !simulation.ref<!logic8> -> !logic8
      %sum = simulation.logic.binary add %acc, %element : !logic8
      %mixed = simulation.logic.binary and %sum, %element : !logic8
      %next = arith.addi %i, %c1 : i32
      cf.br ^header(%next, %mixed : i32, !logic8)
    ^exit:
      simulation.ref.store %acc to %out : !logic8, !simulation.ref<!logic8>
      simulation.return
    }

    // A copy has no four-state work to remove.
    // CHECK-LABEL: known-versioning @copy
    // CHECK: reject loop bb1: unprofitable
    simulation.func @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 101 : i64} {
      %mem = simulation.context.storage %ctx[0] : !simulation.ref<!memory>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!logic8>
      %c0 = arith.constant 0 : i32
      %c1 = arith.constant 1 : i32
      %c8 = arith.constant 8 : i32
      cf.br ^header(%c0 : i32)
    ^header(%i: i32):
      %more = arith.cmpi slt, %i, %c8 : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %wide = arith.extsi %i : i32 to i65
      %slot = simulation.ref.array_element %mem[%wide] : (!simulation.ref<!memory>, i65) -> !simulation.ref<!logic8>
      %element = simulation.ref.load %slot : !simulation.ref<!logic8> -> !logic8
      simulation.ref.store %element to %out : !logic8, !simulation.ref<!logic8>
      %next = arith.addi %i, %c1 : i32
      cf.br ^header(%next : i32)
    ^exit:
      simulation.return
    }

    // A read whose address is loaded inside the iteration cannot be checked
    // when the iteration begins.
    // CHECK-LABEL: known-versioning @indirect
    // CHECK: reject loop bb1: read reference varies inside the region (simulation.ref.load)
    simulation.func @indirect(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 102 : i64} {
      %mem = simulation.context.storage %ctx[0] : !simulation.ref<!memory>
      %index = simulation.context.storage %ctx[2] : !simulation.ref<i32>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!logic8>
      %c0 = arith.constant 0 : i32
      %c1 = arith.constant 1 : i32
      %c8 = arith.constant 8 : i32
      %zero = simulation.logic.constant 0 : i8, 0 : i8 : !logic8
      cf.br ^header(%c0, %zero : i32, !logic8)
    ^header(%i: i32, %acc: !logic8):
      %more = arith.cmpi slt, %i, %c8 : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %loaded = simulation.ref.load %index : !simulation.ref<i32> -> i32
      %wide = arith.extsi %loaded : i32 to i65
      %slot = simulation.ref.array_element %mem[%wide] : (!simulation.ref<!memory>, i65) -> !simulation.ref<!logic8>
      %element = simulation.ref.load %slot : !simulation.ref<!logic8> -> !logic8
      %sum = simulation.logic.binary add %acc, %element : !logic8
      %next = arith.addi %i, %c1 : i32
      cf.br ^header(%next, %sum : i32, !logic8)
    ^exit:
      simulation.ref.store %acc to %out : !logic8, !simulation.ref<!logic8>
      simulation.return
    }

    // An X/Z constant inside the iteration stays four-state.
    // CHECK-LABEL: known-versioning @unknown_constant
    // CHECK: reject loop bb1: uses an X/Z constant (simulation.logic.binary)
    simulation.func @unknown_constant(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 103 : i64} {
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!logic8>
      %c0 = arith.constant 0 : i32
      %c1 = arith.constant 1 : i32
      %c8 = arith.constant 8 : i32
      %zero = simulation.logic.constant 0 : i8, 0 : i8 : !logic8
      %x = simulation.logic.constant 0 : i8, 1 : i8 : !logic8
      cf.br ^header(%c0, %zero : i32, !logic8)
    ^header(%i: i32, %acc: !logic8):
      %more = arith.cmpi slt, %i, %c8 : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %sum = simulation.logic.binary add %acc, %x : !logic8
      %next = arith.addi %i, %c1 : i32
      cf.br ^header(%next, %sum : i32, !logic8)
    ^exit:
      simulation.ref.store %acc to %out : !logic8, !simulation.ref<!logic8>
      simulation.return
    }

    // A bit selected by the induction variable is in range by its bound.
    // CHECK-LABEL: known-versioning @ranged
    // CHECK: admit loop bb1 {{.*}} ranged=1
    simulation.func @ranged(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 104 : i64} {
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!logic8>
      %c0 = arith.constant 0 : i32
      %c1 = arith.constant 1 : i32
      %c8 = arith.constant 8 : i32
      %start = simulation.ref.load %out : !simulation.ref<!logic8> -> !logic8
      cf.br ^header(%c0, %start : i32, !logic8)
    ^header(%i: i32, %acc: !logic8):
      %more = arith.cmpi slt, %i, %c8 : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %bit = simulation.logic.dyn_extract %acc from %i : (!logic8, i32) -> !simulation.logic<1>
      %wide = simulation.logic.resize %bit signed = false : !simulation.logic<1> -> !logic8
      %sum = simulation.logic.binary add %acc, %wide : !logic8
      %next = arith.addi %i, %c1 : i32
      cf.br ^header(%next, %sum : i32, !logic8)
    ^exit:
      simulation.ref.store %acc to %out : !logic8, !simulation.ref<!logic8>
      simulation.return
    }

    // Straight-line code is split at an effect it cannot copy; each side is a
    // run checked once at its entry.
    // CHECK-LABEL: known-versioning @straight
    // CHECK-DAG: admit run bb0 from simulation.context.storage to simulation.display
    // CHECK-DAG: admit run bb0 from simulation.ref.load to simulation.return
    simulation.func @straight(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 105 : i64} {
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!logic8>
      %a = simulation.ref.load %out : !simulation.ref<!logic8> -> !logic8
      %b = simulation.logic.binary add %a, %a : !logic8
      %c = simulation.logic.binary and %b, %a : !logic8
      simulation.ref.store %c to %out : !logic8, !simulation.ref<!logic8>
      %format = simulation.bytes.constant "x"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%format) newline = true radix = <decimal> flags = [0] : !simulation.bytes
      %d = simulation.ref.load %out : !simulation.ref<!logic8> -> !logic8
      %e = simulation.logic.binary or %d, %d : !logic8
      %f = simulation.logic.binary add %e, %d : !logic8
      simulation.ref.store %f to %out : !logic8, !simulation.ref<!logic8>
      simulation.return
    }

    // A checked read's reference is recomputed at the run entry, so a divide
    // that only the taken arm performs cannot be part of it. The read is
    // checked where the arm has computed its address instead.
    // CHECK-LABEL: known-versioning @guarded_divide
    // CHECK-DAG: admit run bb1 from simulation.ref.load to simulation.logic.binary
    // CHECK-DAG: admit run bb0 from simulation.context.storage to cf.cond_br
    simulation.func @guarded_divide(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %divisor: i32 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 106 : i64} {
      %mem = simulation.context.storage %ctx[0] : !simulation.ref<!memory>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!logic8>
      %start = simulation.ref.load %out : !simulation.ref<!logic8> -> !logic8
      %bit = simulation.logic.extract %start from 0 : !logic8 -> !simulation.logic<1>
      %taken = simulation.logic.is_true %bit : !simulation.logic<1>
      %c64 = arith.constant 64 : i32
      cf.cond_br %taken, ^read, ^join(%start : !logic8)
    ^read:
      %quotient = arith.divui %c64, %divisor : i32
      %wide = arith.extui %quotient : i32 to i65
      %slot = simulation.ref.array_element %mem[%wide] : (!simulation.ref<!memory>, i65) -> !simulation.ref<!logic8>
      %element = simulation.ref.load %slot : !simulation.ref<!logic8> -> !logic8
      %sum = simulation.logic.binary add %element, %start : !logic8
      %mixed = simulation.logic.binary and %sum, %element : !logic8
      cf.br ^join(%mixed : !logic8)
    ^join(%value: !logic8):
      %again = simulation.logic.binary or %value, %start : !logic8
      simulation.ref.store %again to %out : !logic8, !simulation.ref<!logic8>
      simulation.return
    }

    // A selection proved in range is cast in the copy, which needs a type
    // with a two-state conversion; an unpacked record has none.
    // CHECK-LABEL: known-versioning @ranged_record
    // CHECK-NOT: ranged=1
    // CHECK: admit run bb0 from simulation.aggregate.extract to simulation.return {{.*}} ranged=0
    simulation.func @ranged_record(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %records: !records {simulation.capture_kind = 2 : i32}, %runtime: i8 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 107 : i64} {
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!logic8>
      %seven = arith.constant 7 : i8
      %index = arith.andi %runtime, %seven : i8
      %record = simulation.array.extract_dynamic %records[%index] : (!records, i8) -> !record
      %field = simulation.aggregate.extract %record[0] : (!record) -> !logic8
      %sum = simulation.logic.binary add %field, %field : !logic8
      %mixed = simulation.logic.binary and %sum, %field : !logic8
      simulation.ref.store %mixed to %out : !logic8, !simulation.ref<!logic8>
      simulation.return
    }

    // A cycle with two entries is no loop; discovery skips it and ends.
    // CHECK-LABEL: known-versioning @irreducible
    // CHECK: admit run bb3 from simulation.logic.binary to simulation.return
    simulation.func @irreducible(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 108 : i64} {
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!logic8>
      %v = simulation.ref.load %out : !simulation.ref<!logic8> -> !logic8
      %bit0 = simulation.logic.extract %v from 0 : !logic8 -> !simulation.logic<1>
      %first = simulation.logic.is_true %bit0 : !simulation.logic<1>
      %bit1 = simulation.logic.extract %v from 1 : !logic8 -> !simulation.logic<1>
      %second = simulation.logic.is_true %bit1 : !simulation.logic<1>
      cf.cond_br %first, ^a, ^b
    ^a:
      cf.cond_br %second, ^b, ^exit
    ^b:
      cf.cond_br %second, ^a, ^exit
    ^exit:
      %w = simulation.logic.binary or %v, %v : !logic8
      %z = simulation.logic.binary add %w, %v : !logic8
      simulation.ref.store %z to %out : !logic8, !simulation.ref<!logic8>
      simulation.return
    }

    // An arm no run can hold blocks only the segments containing it: the
    // next condition of the chain still forms one run with its arms.
    // CHECK-LABEL: known-versioning @blocked_arm
    // CHECK: admit run bb2 from simulation.logic.extract to simulation.logic.binary
    simulation.func @blocked_arm(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 109 : i64} {
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!logic8>
      %v = simulation.ref.load %out : !simulation.ref<!logic8> -> !logic8
      %bit0 = simulation.logic.extract %v from 0 : !logic8 -> !simulation.logic<1>
      %first = simulation.logic.is_true %bit0 : !simulation.logic<1>
      cf.cond_br %first, ^report, ^second
    ^report:
      %now = simulation.time.now %ctx
      cf.br ^join(%v : !logic8)
    ^second:
      %bit1 = simulation.logic.extract %v from 1 : !logic8 -> !simulation.logic<1>
      %taken = simulation.logic.is_true %bit1 : !simulation.logic<1>
      cf.cond_br %taken, ^plus, ^third
    ^third:
      %bit2 = simulation.logic.extract %v from 2 : !logic8 -> !simulation.logic<1>
      %other = simulation.logic.is_true %bit2 : !simulation.logic<1>
      cf.cond_br %other, ^minus, ^join(%v : !logic8)
    ^plus:
      %p = simulation.logic.binary add %v, %v : !logic8
      cf.br ^join(%p : !logic8)
    ^minus:
      %m = simulation.logic.binary or %v, %v : !logic8
      cf.br ^join(%m : !logic8)
    ^join(%r: !logic8):
      %s = simulation.logic.binary and %r, %v : !logic8
      simulation.ref.store %s to %out : !logic8, !simulation.ref<!logic8>
      simulation.return
    }
  }

  // A store through a merged reference has no static root, which makes every
  // root of its design possibly unknown, so it gets a design of its own.
  simulation.design @argument_end {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 110 in 0 function hierarchy "top.argument_end"
    simulation.storage.decl 0 in 0 : !logic8 design hierarchy "top.out"
    simulation.storage.decl 1 in 0 : !logic8 design hierarchy "top.spare"

    // A run ending where a block starts passes it its arguments, so a
    // reference argument used later is no live-out of the run.
    // CHECK-LABEL: known-versioning @argument_end
    // CHECK: admit run bb0 from simulation.context.storage to simulation.time.now
    simulation.func @argument_end(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 110 : i64} {
      %out = simulation.context.storage %ctx[0] : !simulation.ref<!logic8>
      %spare = simulation.context.storage %ctx[1] : !simulation.ref<!logic8>
      %v = simulation.ref.load %out : !simulation.ref<!logic8> -> !logic8
      %s = simulation.logic.binary add %v, %v : !logic8
      %m = simulation.logic.binary and %s, %v : !logic8
      %bit = simulation.logic.extract %m from 0 : !logic8 -> !simulation.logic<1>
      %taken = simulation.logic.is_true %bit : !simulation.logic<1>
      cf.cond_br %taken, ^use(%out : !simulation.ref<!logic8>), ^use(%spare : !simulation.ref<!logic8>)
    ^use(%target: !simulation.ref<!logic8>):
      %now = simulation.time.now %ctx
      simulation.ref.store %m to %target : !logic8, !simulation.ref<!logic8>
      simulation.return
    }
  }
}

// The four-state header is entered only through its check. A known iteration
// takes the cast to the two-state copy, whose read loads only its value plane
// and whose back edge rechecks only the next read.
// COPY-LABEL: simulation.func @carried_unknown
// COPY-SAME: schedule.native.known_versioned
// COPY: cf.br ^[[GUARD:bb[0-9]+]](
// COPY: {{^ *}}^[[HEADER:bb[0-9]+]](
// COPY: {{^ *}}^[[GUARD]](%[[I:.*]]: i32, %[[ACC:.*]]: !simulation.logic<8>):
// COPY: schedule.known.value %[[ACC]]
// COPY: simulation.ref.load
// COPY: schedule.known.value
// COPY: cf.cond_br %{{.*}} weights([1048575, 1]), ^[[CAST:bb[0-9]+]](%[[I]], %[[ACC]] : {{.*}}), ^[[HEADER]](%[[I]], %[[ACC]]
// COPY: {{^ *}}^[[CAST]](%{{.*}}: i32, %[[CACC:.*]]: !simulation.logic<8>):
// COPY: simulation.logic.to_bits %[[CACC]]
// COPY: simulation.logic.from_bits
// COPY: {{^ *}}^[[RECHECK:bb[0-9]+]](
// COPY: cf.cond_br %{{.*}} weights([1048575, 1]), ^[[COPYHEADER:bb[0-9]+]](
// COPY: simulation.ref.load {{.*}}schedule.native.known_read
// COPY: simulation.logic.to_bits
// COPY: cf.br ^[[RECHECK]](
