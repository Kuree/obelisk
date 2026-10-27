// RUN: talos-opt %s --test-talos-scoped-integer-ranges -o /dev/null

// Independent selectors must not accumulate in every later CFG snapshot. The
// test.scoped_max budget of 16 live facts is half the 32 selectors, so it
// catches snapshot growth independently of wall time or machine memory.

!array = !simulation.unpacked_array<0 : 15 x i8>
module {
  simulation.design @chain {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !array design
    simulation.code_unit.decl 1 in 0 function hierarchy "selectors"
    simulation.func @selectors(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %seed: i8 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64, test.scoped_max = 16 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %mask = arith.constant 15 : i8
      cf.br ^block0
    ^block0:
      %index0 = arith.andi %seed, %mask : i8
      %view0 = simulation.ref.array_element %root[%index0] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value0 = simulation.ref.load %view0 : !simulation.ref<i8> -> i8
      simulation.ref.store %value0 to %view0 : i8, !simulation.ref<i8>
      cf.br ^block1
    ^block1:
      %index1 = arith.andi %seed, %mask : i8
      %view1 = simulation.ref.array_element %root[%index1] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value1 = simulation.ref.load %view1 : !simulation.ref<i8> -> i8
      simulation.ref.store %value1 to %view1 : i8, !simulation.ref<i8>
      cf.br ^block2
    ^block2:
      %index2 = arith.andi %seed, %mask : i8
      %view2 = simulation.ref.array_element %root[%index2] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value2 = simulation.ref.load %view2 : !simulation.ref<i8> -> i8
      simulation.ref.store %value2 to %view2 : i8, !simulation.ref<i8>
      cf.br ^block3
    ^block3:
      %index3 = arith.andi %seed, %mask : i8
      %view3 = simulation.ref.array_element %root[%index3] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value3 = simulation.ref.load %view3 : !simulation.ref<i8> -> i8
      simulation.ref.store %value3 to %view3 : i8, !simulation.ref<i8>
      cf.br ^block4
    ^block4:
      %index4 = arith.andi %seed, %mask : i8
      %view4 = simulation.ref.array_element %root[%index4] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value4 = simulation.ref.load %view4 : !simulation.ref<i8> -> i8
      simulation.ref.store %value4 to %view4 : i8, !simulation.ref<i8>
      cf.br ^block5
    ^block5:
      %index5 = arith.andi %seed, %mask : i8
      %view5 = simulation.ref.array_element %root[%index5] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value5 = simulation.ref.load %view5 : !simulation.ref<i8> -> i8
      simulation.ref.store %value5 to %view5 : i8, !simulation.ref<i8>
      cf.br ^block6
    ^block6:
      %index6 = arith.andi %seed, %mask : i8
      %view6 = simulation.ref.array_element %root[%index6] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value6 = simulation.ref.load %view6 : !simulation.ref<i8> -> i8
      simulation.ref.store %value6 to %view6 : i8, !simulation.ref<i8>
      cf.br ^block7
    ^block7:
      %index7 = arith.andi %seed, %mask : i8
      %view7 = simulation.ref.array_element %root[%index7] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value7 = simulation.ref.load %view7 : !simulation.ref<i8> -> i8
      simulation.ref.store %value7 to %view7 : i8, !simulation.ref<i8>
      cf.br ^block8
    ^block8:
      %index8 = arith.andi %seed, %mask : i8
      %view8 = simulation.ref.array_element %root[%index8] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value8 = simulation.ref.load %view8 : !simulation.ref<i8> -> i8
      simulation.ref.store %value8 to %view8 : i8, !simulation.ref<i8>
      cf.br ^block9
    ^block9:
      %index9 = arith.andi %seed, %mask : i8
      %view9 = simulation.ref.array_element %root[%index9] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value9 = simulation.ref.load %view9 : !simulation.ref<i8> -> i8
      simulation.ref.store %value9 to %view9 : i8, !simulation.ref<i8>
      cf.br ^block10
    ^block10:
      %index10 = arith.andi %seed, %mask : i8
      %view10 = simulation.ref.array_element %root[%index10] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value10 = simulation.ref.load %view10 : !simulation.ref<i8> -> i8
      simulation.ref.store %value10 to %view10 : i8, !simulation.ref<i8>
      cf.br ^block11
    ^block11:
      %index11 = arith.andi %seed, %mask : i8
      %view11 = simulation.ref.array_element %root[%index11] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value11 = simulation.ref.load %view11 : !simulation.ref<i8> -> i8
      simulation.ref.store %value11 to %view11 : i8, !simulation.ref<i8>
      cf.br ^block12
    ^block12:
      %index12 = arith.andi %seed, %mask : i8
      %view12 = simulation.ref.array_element %root[%index12] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value12 = simulation.ref.load %view12 : !simulation.ref<i8> -> i8
      simulation.ref.store %value12 to %view12 : i8, !simulation.ref<i8>
      cf.br ^block13
    ^block13:
      %index13 = arith.andi %seed, %mask : i8
      %view13 = simulation.ref.array_element %root[%index13] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value13 = simulation.ref.load %view13 : !simulation.ref<i8> -> i8
      simulation.ref.store %value13 to %view13 : i8, !simulation.ref<i8>
      cf.br ^block14
    ^block14:
      %index14 = arith.andi %seed, %mask : i8
      %view14 = simulation.ref.array_element %root[%index14] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value14 = simulation.ref.load %view14 : !simulation.ref<i8> -> i8
      simulation.ref.store %value14 to %view14 : i8, !simulation.ref<i8>
      cf.br ^block15
    ^block15:
      %index15 = arith.andi %seed, %mask : i8
      %view15 = simulation.ref.array_element %root[%index15] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value15 = simulation.ref.load %view15 : !simulation.ref<i8> -> i8
      simulation.ref.store %value15 to %view15 : i8, !simulation.ref<i8>
      cf.br ^block16
    ^block16:
      %index16 = arith.andi %seed, %mask : i8
      %view16 = simulation.ref.array_element %root[%index16] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value16 = simulation.ref.load %view16 : !simulation.ref<i8> -> i8
      simulation.ref.store %value16 to %view16 : i8, !simulation.ref<i8>
      cf.br ^block17
    ^block17:
      %index17 = arith.andi %seed, %mask : i8
      %view17 = simulation.ref.array_element %root[%index17] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value17 = simulation.ref.load %view17 : !simulation.ref<i8> -> i8
      simulation.ref.store %value17 to %view17 : i8, !simulation.ref<i8>
      cf.br ^block18
    ^block18:
      %index18 = arith.andi %seed, %mask : i8
      %view18 = simulation.ref.array_element %root[%index18] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value18 = simulation.ref.load %view18 : !simulation.ref<i8> -> i8
      simulation.ref.store %value18 to %view18 : i8, !simulation.ref<i8>
      cf.br ^block19
    ^block19:
      %index19 = arith.andi %seed, %mask : i8
      %view19 = simulation.ref.array_element %root[%index19] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value19 = simulation.ref.load %view19 : !simulation.ref<i8> -> i8
      simulation.ref.store %value19 to %view19 : i8, !simulation.ref<i8>
      cf.br ^block20
    ^block20:
      %index20 = arith.andi %seed, %mask : i8
      %view20 = simulation.ref.array_element %root[%index20] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value20 = simulation.ref.load %view20 : !simulation.ref<i8> -> i8
      simulation.ref.store %value20 to %view20 : i8, !simulation.ref<i8>
      cf.br ^block21
    ^block21:
      %index21 = arith.andi %seed, %mask : i8
      %view21 = simulation.ref.array_element %root[%index21] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value21 = simulation.ref.load %view21 : !simulation.ref<i8> -> i8
      simulation.ref.store %value21 to %view21 : i8, !simulation.ref<i8>
      cf.br ^block22
    ^block22:
      %index22 = arith.andi %seed, %mask : i8
      %view22 = simulation.ref.array_element %root[%index22] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value22 = simulation.ref.load %view22 : !simulation.ref<i8> -> i8
      simulation.ref.store %value22 to %view22 : i8, !simulation.ref<i8>
      cf.br ^block23
    ^block23:
      %index23 = arith.andi %seed, %mask : i8
      %view23 = simulation.ref.array_element %root[%index23] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value23 = simulation.ref.load %view23 : !simulation.ref<i8> -> i8
      simulation.ref.store %value23 to %view23 : i8, !simulation.ref<i8>
      cf.br ^block24
    ^block24:
      %index24 = arith.andi %seed, %mask : i8
      %view24 = simulation.ref.array_element %root[%index24] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value24 = simulation.ref.load %view24 : !simulation.ref<i8> -> i8
      simulation.ref.store %value24 to %view24 : i8, !simulation.ref<i8>
      cf.br ^block25
    ^block25:
      %index25 = arith.andi %seed, %mask : i8
      %view25 = simulation.ref.array_element %root[%index25] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value25 = simulation.ref.load %view25 : !simulation.ref<i8> -> i8
      simulation.ref.store %value25 to %view25 : i8, !simulation.ref<i8>
      cf.br ^block26
    ^block26:
      %index26 = arith.andi %seed, %mask : i8
      %view26 = simulation.ref.array_element %root[%index26] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value26 = simulation.ref.load %view26 : !simulation.ref<i8> -> i8
      simulation.ref.store %value26 to %view26 : i8, !simulation.ref<i8>
      cf.br ^block27
    ^block27:
      %index27 = arith.andi %seed, %mask : i8
      %view27 = simulation.ref.array_element %root[%index27] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value27 = simulation.ref.load %view27 : !simulation.ref<i8> -> i8
      simulation.ref.store %value27 to %view27 : i8, !simulation.ref<i8>
      cf.br ^block28
    ^block28:
      %index28 = arith.andi %seed, %mask : i8
      %view28 = simulation.ref.array_element %root[%index28] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value28 = simulation.ref.load %view28 : !simulation.ref<i8> -> i8
      simulation.ref.store %value28 to %view28 : i8, !simulation.ref<i8>
      cf.br ^block29
    ^block29:
      %index29 = arith.andi %seed, %mask : i8
      %view29 = simulation.ref.array_element %root[%index29] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value29 = simulation.ref.load %view29 : !simulation.ref<i8> -> i8
      simulation.ref.store %value29 to %view29 : i8, !simulation.ref<i8>
      cf.br ^block30
    ^block30:
      %index30 = arith.andi %seed, %mask : i8
      %view30 = simulation.ref.array_element %root[%index30] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value30 = simulation.ref.load %view30 : !simulation.ref<i8> -> i8
      simulation.ref.store %value30 to %view30 : i8, !simulation.ref<i8>
      cf.br ^block31
    ^block31:
      %index31 = arith.andi %seed, %mask : i8
      %view31 = simulation.ref.array_element %root[%index31] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value31 = simulation.ref.load %view31 : !simulation.ref<i8> -> i8
      simulation.ref.store %value31 to %view31 : i8, !simulation.ref<i8>
      cf.br ^block32
    ^block32:
      simulation.return
    }
  }
}
