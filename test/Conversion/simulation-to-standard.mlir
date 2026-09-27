// RUN: obelisk-opt %s --convert-obelisk-sim-values-to-standard \
// RUN:   | FileCheck %s --implicit-check-not=!simulation.logic \
// RUN:       --implicit-check-not=simulation. \
// RUN:       --implicit-check-not=unrealized_conversion_cast
// RUN: obelisk-opt %s --convert-obelisk-sim-values-to-standard \
// RUN:   | obelisk-opt -o /dev/null

// CHECK-NOT: !simulation.logic
// CHECK-NOT: simulation.
// CHECK-NOT: unrealized_conversion_cast
// CHECK: func.func @identity(%[[V:.*]]: i5, %[[U:.*]]: i5) -> (i5, i5)
// CHECK: return %[[V]], %[[U]] : i5, i5
// CHECK: func.func @all_values(
// CHECK: math.ctpop
// CHECK: math.ctlz
// CHECK: arith.divui
// CHECK: arith.divsi
// CHECK: arith.remui
// CHECK: arith.remsi
// CHECK: arith.select
// CHECK: arith.shli
// CHECK: arith.shrui
// CHECK: arith.shrsi
// CHECK: arith.cmpi
// CHECK: cf.cond_br

module {
  func.func @identity(%arg: !simulation.logic<5>) -> !simulation.logic<5> {
    return %arg : !simulation.logic<5>
  }

  func.func @all_values(%a: !simulation.logic<5>,
                        %b: !simulation.logic<5>,
                        %wide: !simulation.logic<65>,
                        %index: !simulation.logic<5>,
                        %bits: i37, %bit_index: i37, %condition: i1)
      -> !simulation.logic<5> {
    %c = simulation.logic.constant 21 : i5, 10 : i5 : !simulation.logic<5>
    %from = simulation.logic.from_bits %bits : i37 -> !simulation.logic<37>
    %to = simulation.logic.to_bits %a : !simulation.logic<5> -> i5
    %truth = simulation.logic.is_true %a : !simulation.logic<5>
    %resize_s = simulation.logic.resize %a signed = true : !simulation.logic<5> -> !simulation.logic<37>
    %resize_u = simulation.logic.resize %wide signed = false : !simulation.logic<65> -> !simulation.logic<5>

    %plus = simulation.logic.unary plus %a : (!simulation.logic<5>) -> !simulation.logic<5>
    %neg = simulation.logic.unary negate %a : (!simulation.logic<5>) -> !simulation.logic<5>
    %not = simulation.logic.unary bit_not %a : (!simulation.logic<5>) -> !simulation.logic<5>
    %logical_not = simulation.logic.unary logical_not %a : (!simulation.logic<5>) -> !simulation.logic<1>
    %mux = simulation.logic.mux %logical_not ? %a : %b : (!simulation.logic<1>, !simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<5>

    %red_and = simulation.logic.reduction and %a : !simulation.logic<5> -> !simulation.logic<1>
    %red_or = simulation.logic.reduction or %a : !simulation.logic<5> -> !simulation.logic<1>
    %red_xor = simulation.logic.reduction xor %a : !simulation.logic<5> -> !simulation.logic<1>
    %red_nand = simulation.logic.reduction nand %a : !simulation.logic<5> -> !simulation.logic<1>
    %red_nor = simulation.logic.reduction nor %a : !simulation.logic<5> -> !simulation.logic<1>
    %red_xnor = simulation.logic.reduction xnor %a : !simulation.logic<5> -> !simulation.logic<1>
    %count = simulation.logic.count_bits %a matching %logical_not : (!simulation.logic<5>, !simulation.logic<1>) -> i32
    %clog2 = simulation.logic.clog2 %a : !simulation.logic<5>

    %add = simulation.logic.binary add %a, %b : !simulation.logic<5>
    %sub = simulation.logic.binary sub %a, %b : !simulation.logic<5>
    %mul = simulation.logic.binary mul %a, %b : !simulation.logic<5>
    %udiv = simulation.logic.binary udiv %a, %b : !simulation.logic<5>
    %sdiv = simulation.logic.binary sdiv %a, %b : !simulation.logic<5>
    %umod = simulation.logic.binary umod %a, %b : !simulation.logic<5>
    %smod = simulation.logic.binary smod %a, %b : !simulation.logic<5>
    %and = simulation.logic.binary and %a, %b : !simulation.logic<5>
    %or = simulation.logic.binary or %a, %b : !simulation.logic<5>
    %xor = simulation.logic.binary xor %a, %b : !simulation.logic<5>
    %xnor = simulation.logic.binary xnor %a, %b : !simulation.logic<5>

    %land = simulation.logic.logical and %a, %b : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<1>
    %lor = simulation.logic.logical or %a, %b : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<1>
    %shl = simulation.logic.shift left %a by %index : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<5>
    %shr = simulation.logic.shift right %a by %bit_index : (!simulation.logic<5>, i37) -> !simulation.logic<5>
    %ashr = simulation.logic.shift right_arith %a by %index : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<5>

    %eq = simulation.logic.compare eq %a, %b : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<1>
    %ne = simulation.logic.compare ne %a, %b : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<1>
    %case_eq = simulation.logic.compare case_eq %a, %b : (!simulation.logic<5>, !simulation.logic<5>) -> i1
    %case_ne = simulation.logic.compare case_ne %a, %b : (!simulation.logic<5>, !simulation.logic<5>) -> i1
    %ult = simulation.logic.compare ult %a, %b : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<1>
    %ule = simulation.logic.compare ule %a, %b : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<1>
    %ugt = simulation.logic.compare ugt %a, %b : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<1>
    %uge = simulation.logic.compare uge %a, %b : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<1>
    %slt = simulation.logic.compare slt %a, %b : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<1>
    %sle = simulation.logic.compare sle %a, %b : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<1>
    %sgt = simulation.logic.compare sgt %a, %b : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<1>
    %sge = simulation.logic.compare sge %a, %b : (!simulation.logic<5>, !simulation.logic<5>) -> !simulation.logic<1>

    %concat = simulation.logic.concat %a, %logical_not : (!simulation.logic<5>, !simulation.logic<1>) -> !simulation.logic<6>
    %replicate = simulation.logic.replicate %a times 3 : !simulation.logic<5> -> !simulation.logic<15>
    %extract = simulation.logic.extract %wide from 28 : !simulation.logic<65> -> !simulation.logic<37>
    %dyn = simulation.logic.dyn_extract %from from %index : (!simulation.logic<37>, !simulation.logic<5>) -> !simulation.logic<5>
    %bits_dyn = simulation.bits.dyn_extract %bits from %index : (i37, !simulation.logic<5>) -> i5
    %insert = simulation.logic.insert %logical_not into %a at 2 : (!simulation.logic<5>, !simulation.logic<1>) -> !simulation.logic<5>
    %called = func.call @identity(%insert) : (!simulation.logic<5>) -> !simulation.logic<5>
    cf.cond_br %condition, ^bb1(%called : !simulation.logic<5>), ^bb1(%c : !simulation.logic<5>)

  ^bb1(%result: !simulation.logic<5>):
    return %result : !simulation.logic<5>
  }
}
