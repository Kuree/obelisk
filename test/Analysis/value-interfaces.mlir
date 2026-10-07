// RUN: obelisk-opt %s --test-obelisk-value-interfaces -o /dev/null 2> %t.threaded
// RUN: obelisk-opt %s --test-obelisk-value-interfaces --mlir-disable-threading -o /dev/null 2> %t.serial
// RUN: diff %t.threaded %t.serial
// RUN: FileCheck %s < %t.threaded

// The test pass replaces arith.select with an independently registered op that
// implements SelectLikeOpInterface, uses false/condition/true operand order,
// and has no folder. All three analyses must use the interface contract.
module {
  simulation.design @interfaces {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "partial"
    simulation.func @partial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %x: !simulation.logic<16> {simulation.capture_kind = 2 : i32}, %y: !simulation.logic<16> {simulation.capture_kind = 2 : i32}, %choose: i1 {simulation.capture_kind = 2 : i32}) -> !simulation.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %false = arith.constant false
      %predicate = arith.xori %choose, %false {test.fact = "predicate"} : i1
      %mask = simulation.logic.constant 65280 : i16, 0 : i16 : !simulation.logic<16>
      %a = simulation.logic.binary and %x, %mask {test.fact = "left"} : !simulation.logic<16>
      %b = simulation.logic.binary and %y, %mask {test.fact = "right"} : !simulation.logic<16>
      %selected = arith.select %predicate, %a, %b {test.fact = "selected"} : !simulation.logic<16>
      %same = arith.select %predicate, %selected, %selected {test.fact = "same"} : !simulation.logic<16>
      %low = simulation.logic.extract %same from 0 {test.fact = "low"} : !simulation.logic<16> -> !simulation.logic<8>
      simulation.return %low : !simulation.logic<8>
    }
    // CHECK: predicate demand=1 domain=two-state
    // CHECK: left zero=255 one=0 known=255 demand=255 domain=may-four-state
    // CHECK: right zero=255 one=0 known=255 demand=255 domain=may-four-state
    // CHECK: selected zero=255 one=0 known=255 demand=255 domain=may-four-state
    // CHECK: same zero=255 one=0 known=255 demand=255 domain=may-four-state
    // CHECK: low zero=255 one=0 known=255 demand=255 domain=two-state

    simulation.code_unit.decl 2 in 0 function hierarchy "domains"
    simulation.func @domains(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: i8 {simulation.capture_kind = 2 : i32}, %choose: i1 {simulation.capture_kind = 2 : i32}) -> (!simulation.logic<8>, !simulation.logic<8>, !simulation.logic<8>) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %known = simulation.logic.from_bits %input : i8 -> !simulation.logic<8>
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      %z = simulation.logic.constant -1 : i8, -1 : i8 : !simulation.logic<8>
      %both = arith.select %choose, %known, %one {test.fact = "both-known"} : !simulation.logic<8>
      %maybe = arith.select %choose, %known, %z {test.fact = "maybe-z"} : !simulation.logic<8>
      %same = arith.select %choose, %z, %z {test.fact = "same-z"} : !simulation.logic<8>
      simulation.return %both, %maybe, %same : !simulation.logic<8>, !simulation.logic<8>, !simulation.logic<8>
    }
    // CHECK: both-known zero=0 one=0 known=255 demand=255 domain=two-state
    // CHECK: maybe-z zero=0 one=0 known=0 demand=255 domain=may-four-state
    // CHECK: same-z zero=0 one=0 known=0 demand=255 domain=may-four-state

    // Known bits also consume the shared constant-propagation lattice. No
    // replicate opcode transfer is needed, including for partially X/Z data.
    simulation.code_unit.decl 3 in 0 function hierarchy "folders"
    simulation.func @folders(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> (!simulation.logic<8>, !simulation.logic<8>) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %word = simulation.logic.constant 1 : i2, 0 : i2 : !simulation.logic<2>
      %unknown = simulation.logic.constant 1 : i2, 2 : i2 : !simulation.logic<2>
      %repeated = simulation.logic.replicate %word times 4 {test.fact = "folded-replicate"} : !simulation.logic<2> -> !simulation.logic<8>
      %xz = simulation.logic.replicate %unknown times 4 {test.fact = "folded-xz"} : !simulation.logic<2> -> !simulation.logic<8>
      simulation.return %repeated, %xz : !simulation.logic<8>, !simulation.logic<8>
    }
    // CHECK: folded-replicate zero=170 one=85 known=255 demand=255 domain=two-state
    // CHECK: folded-xz zero=0 one=85 known=85 demand=255 domain=may-four-state
    // A function-local query must observe returns even for private functions.
    // Demand flows through both structured yields and CFG block arguments.
    simulation.code_unit.decl 4 in 0 function hierarchy "returns"
    simulation.func private @returns(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %x: !simulation.logic<65> {simulation.capture_kind = 2 : i32}, %y: !simulation.logic<65> {simulation.capture_kind = 2 : i32}, %choose: i1 {simulation.capture_kind = 2 : i32}) -> !simulation.logic<1> attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %a = simulation.logic.unary plus %x {test.fact = "returned-left"} : (!simulation.logic<65>) -> !simulation.logic<65>
      %b = simulation.logic.unary plus %y {test.fact = "returned-right"} : (!simulation.logic<65>) -> !simulation.logic<65>
      %region = scf.if %choose -> (!simulation.logic<65>) {
        scf.yield %a : !simulation.logic<65>
      } else {
        scf.yield %b : !simulation.logic<65>
      }
      cf.cond_br %choose, ^left(%region : !simulation.logic<65>), ^right(%region : !simulation.logic<65>)
    ^left(%l: !simulation.logic<65>):
      %lo = simulation.logic.extract %l from 0 {test.fact = "returned-low"} : !simulation.logic<65> -> !simulation.logic<1>
      simulation.return %lo : !simulation.logic<1>
    ^right(%r: !simulation.logic<65>):
      %hi = simulation.logic.extract %r from 64 {test.fact = "returned-high"} : !simulation.logic<65> -> !simulation.logic<1>
      simulation.return %hi : !simulation.logic<1>
    }
    // CHECK: returned-left{{.*}} demand=18446744073709551617
    // CHECK: returned-right{{.*}} demand=18446744073709551617
    // CHECK: returned-low{{.*}} demand=1
    // CHECK: returned-high{{.*}} demand=1
  }
}
