// RUN: talos-opt %s --test-talos-region-value-dependencies -o %t.analyzed 2>&1 | FileCheck %s
// RUN: talos-opt %s -o %t.original
// RUN: diff %t.original %t.analyzed

module {
  // Independent yielded accumulators must not acquire each other's inputs.
  // CHECK-LABEL: values @independent_for valid=1
  // CHECK-NEXT: scf.for result 0: arg0{{$}}
  // CHECK-NEXT: scf.for result 1: arg1{{$}}
  func.func @independent_for(%a: i8, %b: i8) -> (i8, i8) {
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %c4 = arith.constant 4 : index
    %zero = arith.constant 0 : i8
    %r:2 = scf.for %i = %c0 to %c4 step %c1 iter_args(%x = %zero, %y = %zero) -> (i8, i8) {
      %next_x = arith.addi %x, %a : i8
      %next_y = arith.addi %y, %b : i8
      scf.yield %next_x, %next_y : i8, i8
    } {test.values}
    return %r#0, %r#1 : i8, i8
  }

  // scf.condition forwards before-region values to both results and after
  // arguments; scf.yield forwards after values back to before arguments.
  // Their positions/types need not match the initial operand/result positions.
  // CHECK-LABEL: values @permuted_while valid=1
  // CHECK-NEXT: scf.while result 0: arg1{{$}}
  // CHECK-NEXT: scf.while result 1: arg0{{$}}
  // CHECK-NEXT: scf.while result 2:{{$}}
  func.func @permuted_while(%a: i8, %b: i8) -> (i8, i8) {
    %c0 = arith.constant 0 : i32
    %c1 = arith.constant 1 : i32
    %c4 = arith.constant 4 : i32
    %r:3 = scf.while (%i = %c0, %x = %a, %y = %b) : (i32, i8, i8) -> (i8, i8, i32) {
      %more = arith.cmpi slt, %i, %c4 : i32
      scf.condition(%more) %y, %x, %i : i8, i8, i32
    } do {
    ^bb0(%y: i8, %x: i8, %i: i32):
      %next = arith.addi %i, %c1 : i32
      scf.yield %next, %x, %y : i32, i8, i8
    } attributes {test.values}
    return %r#0, %r#1 : i8, i8
  }

  // A crossed yield really couples the recurrences; solve to a fixed point.
  // CHECK-LABEL: values @crossed_yields valid=1
  // CHECK-NEXT: scf.for result 0: arg0 arg1{{$}}
  // CHECK-NEXT: scf.for result 1: arg0 arg1{{$}}
  func.func @crossed_yields(%a: i8, %b: i8) -> (i8, i8) {
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %c4 = arith.constant 4 : index
    %r:2 = scf.for %i = %c0 to %c4 step %c1 iter_args(%x = %a, %y = %b) -> (i8, i8) {
      scf.yield %y, %x : i8, i8
    } {test.values}
    return %r#0, %r#1 : i8, i8
  }

  // A data-dependent bound controls which carried value reaches each result.
  // CHECK-LABEL: values @dynamic_bound valid=1
  // CHECK-NEXT: scf.for result 0: arg0 arg2{{$}}
  // CHECK-NEXT: scf.for result 1: arg1 arg2{{$}}
  func.func @dynamic_bound(%a: i8, %b: i8, %n: index) -> (i8, i8) {
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %zero = arith.constant 0 : i8
    %r:2 = scf.for %i = %c0 to %n step %c1 iter_args(%x = %zero, %y = %zero) -> (i8, i8) {
      %next_x = arith.addi %x, %a : i8
      %next_y = arith.addi %y, %b : i8
      scf.yield %next_x, %next_y : i8, i8
    } {test.values}
    return %r#0, %r#1 : i8, i8
  }

  // Branch control reaches the selected yield, while an unrelated carried
  // value stays independent of that branch's data inputs.
  // CHECK-LABEL: values @nested_if valid=1
  // CHECK: scf.if result 0: arg0 arg2{{$}}
  // CHECK: scf.for result 0: arg0 arg2{{$}}
  // CHECK-NEXT: scf.for result 1: arg1{{$}}
  func.func @nested_if(%a: i8, %b: i8, %select: i1) -> (i8, i8) {
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %c4 = arith.constant 4 : index
    %zero = arith.constant 0 : i8
    %r:2 = scf.for %i = %c0 to %c4 step %c1 iter_args(%x = %zero, %y = %zero) -> (i8, i8) {
      %v = scf.if %select -> (i8) {
        scf.yield %a : i8
      } else {
        scf.yield %zero : i8
      } {test.values}
      %next_x = arith.addi %x, %v : i8
      %next_y = arith.addi %y, %b : i8
      scf.yield %next_x, %next_y : i8, i8
    } {test.values}
    return %r#0, %r#1 : i8, i8
  }

  // A while condition can depend on carried data. It then affects even a
  // separately yielded result through the iteration count.
  // CHECK-LABEL: values @data_condition valid=1
  // CHECK-NEXT: scf.while result 0: arg0{{$}}
  // CHECK-NEXT: scf.while result 1: arg0 arg1{{$}}
  func.func @data_condition(%a: i8, %b: i8) -> (i8, i8) {
    %zero = arith.constant 0 : i8
    %one = arith.constant 1 : i8
    %r:2 = scf.while (%x = %a, %y = %zero) : (i8, i8) -> (i8, i8) {
      %more = arith.cmpi ne, %x, %zero : i8
      scf.condition(%more) %x, %y : i8, i8
    } do {
    ^bb0(%x: i8, %y: i8):
      %next_x = arith.subi %x, %one : i8
      %next_y = arith.addi %y, %b : i8
      scf.yield %next_x, %next_y : i8, i8
    } attributes {test.values}
    return %r#0, %r#1 : i8, i8
  }

  func.func private @observe(i8)

  // Each if result joins only its own two yielded values plus the selector.
  // CHECK-LABEL: values @if_lanes valid=1
  // CHECK-NEXT: scf.if result 0: arg0 arg1 arg3{{$}}
  // CHECK-NEXT: scf.if result 1: arg2 arg3{{$}}
  func.func @if_lanes(%a: i8, %b: i8, %c: i8, %select: i1) -> (i8, i8) {
    %r:2 = scf.if %select -> (i8, i8) {
      scf.yield %a, %c : i8, i8
    } else {
      scf.yield %b, %c : i8, i8
    } {test.values}
    return %r#0, %r#1 : i8, i8
  }

  // The interface also handles switch arms; this is not an scf.if special case.
  // CHECK-LABEL: values @switch_lanes valid=1
  // CHECK-NEXT: scf.index_switch result 0: arg0 arg1 arg2 arg3{{$}}
  // CHECK-NEXT: scf.index_switch result 1: arg0 arg3{{$}}
  func.func @switch_lanes(%a: i8, %b: i8, %c: i8, %select: index) -> (i8, i8) {
    %r:2 = scf.index_switch %select {test.values} -> i8, i8
    case 0 {
      scf.yield %a, %a : i8, i8
    }
    case 1 {
      scf.yield %b, %a : i8, i8
    }
    default {
      scf.yield %c, %a : i8, i8
    }
    return %r#0, %r#1 : i8, i8
  }

  // Region nesting retains every control dependency of an effect without
  // inventing a dependence on the value carried by an unrelated lane.
  // CHECK-LABEL: values @nested_effect valid=1
  // CHECK-NEXT: func.call effect: arg0 arg2 arg1{{$}}
  func.func @nested_effect(%v: i8, %n: index, %select: i1, %unrelated: i8) {
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %r = scf.for %i = %c0 to %n step %c1 iter_args(%x = %unrelated) -> i8 {
      scf.if %select {
        func.call @observe(%v) {test.values} : (i8) -> ()
      }
      scf.yield %x : i8
    }
    return
  }

  // Guarding a side effect matters even without any yielded SSA result.
  // CHECK-LABEL: values @guarded_effect valid=1
  // CHECK-NEXT: func.call effect: arg1 arg0{{$}}
  func.func @guarded_effect(%condition: i1, %v: i8) {
    scf.if %condition {
      func.call @observe(%v) {test.values} : (i8) -> ()
    }
    return
  }

  // The query can start at a region op, with inputs captured from its parent.
  // CHECK-LABEL: values @region_root valid=1
  // CHECK-NEXT: func.call effect: arg1 arg0{{$}}
  // CHECK-NEXT: scf.if result 0: arg0 arg1 arg2{{$}}
  func.func @region_root(%condition: i1, %a: i8, %b: i8) -> i8 {
    %r = scf.if %condition -> i8 {
      func.call @observe(%a) {test.values} : (i8) -> ()
      scf.yield %a : i8
    } else {
      scf.yield %b : i8
    } {test.values, test.root}
    return %r : i8
  }

  // Even a zero-trip loop forwards its initial iter_args. The structural
  // analysis keeps both possibilities until canonicalization removes the loop.
  // CHECK-LABEL: values @zero_trip valid=1
  // CHECK-NEXT: scf.for result 0: arg0 arg1{{$}}
  func.func @zero_trip(%initial: i8, %next: i8) -> i8 {
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %r = scf.for %i = %c0 to %c0 step %c1 iter_args(%x = %initial) -> i8 {
      scf.yield %next : i8
    } {test.values}
    return %r : i8
  }

  // Multi-block region control is outside this structured query's contract.
  // CHECK-LABEL: values @unstructured valid=0
  func.func @unstructured(%a: i8) -> i8 {
    %v = scf.execute_region -> i8 {
      cf.br ^next
    ^next:
      scf.yield %a : i8
    }
    return %v : i8
  }
}
