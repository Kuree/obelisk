// RUN: obelisk-opt %s --convert-obelisk-sim-values-to-standard \
// RUN:   | FileCheck %s --implicit-check-not=!obelisk_sim.logic \
// RUN:       --implicit-check-not=obelisk_sim. \
// RUN:       --implicit-check-not=unrealized_conversion_cast
// RUN: obelisk-opt %s --convert-obelisk-sim-values-to-standard \
// RUN:   | obelisk-opt -o /dev/null

// CHECK-NOT: !obelisk_sim.logic
// CHECK-NOT: obelisk_sim.
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
  func.func @identity(%arg: !obelisk_sim.logic<5>) -> !obelisk_sim.logic<5> {
    return %arg : !obelisk_sim.logic<5>
  }

  func.func @all_values(%a: !obelisk_sim.logic<5>,
                        %b: !obelisk_sim.logic<5>,
                        %wide: !obelisk_sim.logic<65>,
                        %index: !obelisk_sim.logic<5>,
                        %bits: i37, %bit_index: i37, %condition: i1)
      -> !obelisk_sim.logic<5> {
    %c = obelisk_sim.logic.constant 21 : i5, 10 : i5 : !obelisk_sim.logic<5>
    %from = obelisk_sim.logic.from_bits %bits : i37 -> !obelisk_sim.logic<37>
    %to = obelisk_sim.logic.to_bits %a : !obelisk_sim.logic<5> -> i5
    %truth = obelisk_sim.logic.is_true %a : !obelisk_sim.logic<5>
    %resize_s = obelisk_sim.logic.resize %a signed = true : !obelisk_sim.logic<5> -> !obelisk_sim.logic<37>
    %resize_u = obelisk_sim.logic.resize %wide signed = false : !obelisk_sim.logic<65> -> !obelisk_sim.logic<5>

    %plus = obelisk_sim.logic.unary plus %a : (!obelisk_sim.logic<5>) -> !obelisk_sim.logic<5>
    %neg = obelisk_sim.logic.unary negate %a : (!obelisk_sim.logic<5>) -> !obelisk_sim.logic<5>
    %not = obelisk_sim.logic.unary bit_not %a : (!obelisk_sim.logic<5>) -> !obelisk_sim.logic<5>
    %logical_not = obelisk_sim.logic.unary logical_not %a : (!obelisk_sim.logic<5>) -> !obelisk_sim.logic<1>
    %mux = obelisk_sim.logic.mux %logical_not ? %a : %b : (!obelisk_sim.logic<1>, !obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<5>

    %red_and = obelisk_sim.logic.reduction and %a : !obelisk_sim.logic<5> -> !obelisk_sim.logic<1>
    %red_or = obelisk_sim.logic.reduction or %a : !obelisk_sim.logic<5> -> !obelisk_sim.logic<1>
    %red_xor = obelisk_sim.logic.reduction xor %a : !obelisk_sim.logic<5> -> !obelisk_sim.logic<1>
    %red_nand = obelisk_sim.logic.reduction nand %a : !obelisk_sim.logic<5> -> !obelisk_sim.logic<1>
    %red_nor = obelisk_sim.logic.reduction nor %a : !obelisk_sim.logic<5> -> !obelisk_sim.logic<1>
    %red_xnor = obelisk_sim.logic.reduction xnor %a : !obelisk_sim.logic<5> -> !obelisk_sim.logic<1>
    %count = obelisk_sim.logic.count_bits %a matching %logical_not : (!obelisk_sim.logic<5>, !obelisk_sim.logic<1>) -> i32
    %clog2 = obelisk_sim.logic.clog2 %a : !obelisk_sim.logic<5>

    %add = obelisk_sim.logic.binary add %a, %b : !obelisk_sim.logic<5>
    %sub = obelisk_sim.logic.binary sub %a, %b : !obelisk_sim.logic<5>
    %mul = obelisk_sim.logic.binary mul %a, %b : !obelisk_sim.logic<5>
    %udiv = obelisk_sim.logic.binary udiv %a, %b : !obelisk_sim.logic<5>
    %sdiv = obelisk_sim.logic.binary sdiv %a, %b : !obelisk_sim.logic<5>
    %umod = obelisk_sim.logic.binary umod %a, %b : !obelisk_sim.logic<5>
    %smod = obelisk_sim.logic.binary smod %a, %b : !obelisk_sim.logic<5>
    %and = obelisk_sim.logic.binary and %a, %b : !obelisk_sim.logic<5>
    %or = obelisk_sim.logic.binary or %a, %b : !obelisk_sim.logic<5>
    %xor = obelisk_sim.logic.binary xor %a, %b : !obelisk_sim.logic<5>
    %xnor = obelisk_sim.logic.binary xnor %a, %b : !obelisk_sim.logic<5>

    %land = obelisk_sim.logic.logical and %a, %b : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<1>
    %lor = obelisk_sim.logic.logical or %a, %b : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<1>
    %shl = obelisk_sim.logic.shift left %a by %index : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<5>
    %shr = obelisk_sim.logic.shift right %a by %bit_index : (!obelisk_sim.logic<5>, i37) -> !obelisk_sim.logic<5>
    %ashr = obelisk_sim.logic.shift right_arith %a by %index : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<5>

    %eq = obelisk_sim.logic.compare eq %a, %b : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<1>
    %ne = obelisk_sim.logic.compare ne %a, %b : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<1>
    %case_eq = obelisk_sim.logic.compare case_eq %a, %b : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1
    %case_ne = obelisk_sim.logic.compare case_ne %a, %b : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1
    %ult = obelisk_sim.logic.compare ult %a, %b : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<1>
    %ule = obelisk_sim.logic.compare ule %a, %b : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<1>
    %ugt = obelisk_sim.logic.compare ugt %a, %b : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<1>
    %uge = obelisk_sim.logic.compare uge %a, %b : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<1>
    %slt = obelisk_sim.logic.compare slt %a, %b : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<1>
    %sle = obelisk_sim.logic.compare sle %a, %b : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<1>
    %sgt = obelisk_sim.logic.compare sgt %a, %b : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<1>
    %sge = obelisk_sim.logic.compare sge %a, %b : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<1>

    %concat = obelisk_sim.logic.concat %a, %logical_not : (!obelisk_sim.logic<5>, !obelisk_sim.logic<1>) -> !obelisk_sim.logic<6>
    %replicate = obelisk_sim.logic.replicate %a times 3 : !obelisk_sim.logic<5> -> !obelisk_sim.logic<15>
    %extract = obelisk_sim.logic.extract %wide from 28 : !obelisk_sim.logic<65> -> !obelisk_sim.logic<37>
    %dyn = obelisk_sim.logic.dyn_extract %from from %index : (!obelisk_sim.logic<37>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<5>
    %bits_dyn = obelisk_sim.bits.dyn_extract %bits from %index : (i37, !obelisk_sim.logic<5>) -> i5
    %insert = obelisk_sim.logic.insert %logical_not into %a at 2 : (!obelisk_sim.logic<5>, !obelisk_sim.logic<1>) -> !obelisk_sim.logic<5>
    %called = func.call @identity(%insert) : (!obelisk_sim.logic<5>) -> !obelisk_sim.logic<5>
    cf.cond_br %condition, ^bb1(%called : !obelisk_sim.logic<5>), ^bb1(%c : !obelisk_sim.logic<5>)

  ^bb1(%result: !obelisk_sim.logic<5>):
    return %result : !obelisk_sim.logic<5>
  }
}
