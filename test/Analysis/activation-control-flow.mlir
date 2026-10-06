// RUN: obelisk-opt %s --test-obelisk-activation-control-flow -o %t.analyzed 2>&1 | FileCheck %s
// RUN: obelisk-opt %s -o %t.original
// RUN: diff %t.original %t.analyzed

// An early break and the induction backedge share a dispatch block after
// region lowering. Translate each incoming SSA lane, including the comparison
// fact on that edge. The induction loop remains; no iteration is enumerated.
// CHECK-LABEL: control @shared_latch changed=1
// CHECK: arith.cmpi slt
// CHECK: cf.cond_br
// CHECK: arith.addi
// CHECK-NOT: arith.trunci
// CHECK: return
func.func @shared_latch(%stop: i32) {
  %zero = arith.constant 0 : i32
  %one = arith.constant 1 : i32
  %limit = arith.constant 8 : i32
  %true = arith.constant true
  cf.br ^loop(%zero : i32)
^loop(%i: i32):
  %more = arith.cmpi slt, %i, %limit : i32
  cf.cond_br %more, ^body, ^dispatch(%zero, %zero : i32, i32)
^body:
  %stop_now = arith.cmpi eq, %i, %stop : i32
  %continue = arith.xori %stop_now, %true : i1
  %tag = arith.extui %continue : i1 to i32
  cf.cond_br %stop_now, ^dispatch(%zero, %tag : i32, i32), ^increment
^increment:
  %next = arith.addi %i, %one : i32
  cf.br ^dispatch(%next, %tag : i32, i32)
^dispatch(%value: i32, %flag: i32):
  %again = arith.trunci %flag : i32 to i1
  cf.cond_br %again, ^loop(%value : i32), ^exit
^exit:
  return
}

// Distinct incoming edges from the same predecessor retain their own values.
// CHECK-LABEL: control @parallel_edges changed=1
// CHECK-NOT: arith.trunci
// CHECK: arith.extui %arg0
// CHECK: return
func.func @parallel_edges(%test: i1) -> i32 {
  %zero = arith.constant 0 : i32
  %one = arith.constant 1 : i32
  cf.cond_br %test, ^dispatch(%one : i32), ^dispatch(%zero : i32)
^dispatch(%tag: i32):
  %condition = arith.trunci %tag : i32 to i1
  cf.cond_br %condition, ^yes, ^no
^yes:
  return %one : i32
^no:
  return %zero : i32
}

// Dense meet must forget conflicting predicates at a join. The selected flag
// is unrelated to the earlier branch, so the dispatch remains unknown.
// CHECK-LABEL: control @ambiguous_join changed=0
func.func @ambiguous_join(%test: i1, %flag: i32) {
  cf.cond_br %test, ^left, ^right
^left:
  cf.br ^dispatch(%flag : i32)
^right:
  cf.br ^dispatch(%flag : i32)
^dispatch(%tag: i32):
  %condition = arith.trunci %tag : i32 to i1
  cf.cond_br %condition, ^yes, ^no
^yes:
  return
^no:
  return
}

// A predicate fact from one iteration cannot describe a newly executed load.
// The earlier truth remains known, but the new condition has a distinct SSA
// definition whose value is not constrained by that earlier edge.
// CHECK-LABEL: control @new_predicate changed=0
func.func @new_predicate(%ptr: memref<i1>) {
  cf.br ^test
^test:
  %condition = memref.load %ptr[] : memref<i1>
  cf.cond_br %condition, ^left, ^right
^left:
  %a = memref.load %ptr[] : memref<i1>
  %x = arith.extui %a : i1 to i32
  cf.br ^dispatch(%x : i32)
^right:
  %b = memref.load %ptr[] : memref<i1>
  %y = arith.extui %b : i1 to i32
  cf.br ^dispatch(%y : i32)
^dispatch(%tag: i32):
  %again = arith.trunci %tag : i32 to i1
  cf.cond_br %again, ^test, ^exit
^exit:
  return
}

// No bypass of memory effects, even with a constant decision on every edge.
// CHECK-LABEL: control @observable_dispatch changed=0
func.func @observable_dispatch(%test: i1, %ptr: memref<i32>) {
  %zero = arith.constant 0 : i32
  %one = arith.constant 1 : i32
  cf.cond_br %test, ^dispatch(%one : i32), ^dispatch(%zero : i32)
^dispatch(%tag: i32):
  memref.store %tag, %ptr[] : memref<i32>
  %condition = arith.trunci %tag : i32 to i1
  cf.cond_br %condition, ^yes, ^no
^yes:
  return
^no:
  return
}

// Direct uses of a dispatcher argument outside its block cannot be bypassed
// without SSA repair. Successor forwarding is supported, direct uses are not.
// CHECK-LABEL: control @escaping_argument changed=0
func.func @escaping_argument(%test: i1) -> i32 {
  %zero = arith.constant 0 : i32
  %one = arith.constant 1 : i32
  cf.cond_br %test, ^dispatch(%one : i32), ^dispatch(%zero : i32)
^dispatch(%tag: i32):
  %condition = arith.trunci %tag : i32 to i1
  cf.cond_br %condition, ^yes, ^no
^yes:
  return %tag : i32
^no:
  return %tag : i32
}

// CHECK-LABEL: control @escaping_result changed=0
func.func @escaping_result(%test: i1) -> i1 {
  %zero = arith.constant 0 : i32
  %one = arith.constant 1 : i32
  cf.cond_br %test, ^dispatch(%one : i32), ^dispatch(%zero : i32)
^dispatch(%tag: i32):
  %condition = arith.trunci %tag : i32 to i1
  cf.cond_br %condition, ^yes, ^no
^yes:
  return %condition : i1
^no:
  return %condition : i1
}

// Division is not speculatable for an arbitrary divisor; bypassing it could
// suppress undefined behavior. This normalization does not change that path.
// CHECK-LABEL: control @nonspeculatable changed=0
func.func @nonspeculatable(%test: i1, %divisor: i32) {
  %zero = arith.constant 0 : i32
  %one = arith.constant 1 : i32
  cf.cond_br %test, ^dispatch(%one : i32), ^dispatch(%zero : i32)
^dispatch(%tag: i32):
  %quotient = arith.divsi %tag, %divisor : i32
  %condition = arith.trunci %quotient : i32 to i1
  cf.cond_br %condition, ^yes, ^no
^yes:
  return
^no:
  return
}

// A header dominates its latch. Even when its first iteration is constant,
// the normalization must not unfold that iteration or clone a loop header.
// CHECK-LABEL: control @loop_header changed=0
func.func @loop_header(%test: i1, %next: i32) {
  %zero = arith.constant 0 : i32
  cf.br ^loop(%zero : i32)
^loop(%i: i32):
  %condition = arith.trunci %i : i32 to i1
  cf.cond_br %condition, ^body, ^exit
^body:
  cf.cond_br %test, ^loop(%next : i32), ^exit
^exit:
  return
}
