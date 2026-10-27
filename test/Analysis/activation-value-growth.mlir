// RUN: talos-opt %s '--test-talos-activation-regions=input-dependencies=true inputs=true' -o /dev/null 2>&1 | FileCheck %s --check-prefix=INPUT
// RUN: talos-opt %s '--test-talos-activation-regions=reaching-values=true inputs=true' -o /dev/null 2>&1 | FileCheck %s --check-prefix=FULL

// Entry capture handles need one value node, not a proxy at every CFG block.
// The test.activation_max_value_nodes budget is linear in the 32 roots, so it
// rejects per-handle, per-block growth independently of timing or memory.
// The entry load's data must still influence all the conditional publications.
// INPUT: inputs valid=1
// INPUT-NEXT: value-input s32[0,1){{$}}
// FULL: values valid=1
// FULL-NEXT: value-input s32[0,1){{$}}
// FULL-NEXT: final-input s32[0,1){{$}}

module {
  simulation.design @chain {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i1 design
    simulation.storage.decl 2 in 0 : i1 design
    simulation.storage.decl 3 in 0 : i1 design
    simulation.storage.decl 4 in 0 : i1 design
    simulation.storage.decl 5 in 0 : i1 design
    simulation.storage.decl 6 in 0 : i1 design
    simulation.storage.decl 7 in 0 : i1 design
    simulation.storage.decl 8 in 0 : i1 design
    simulation.storage.decl 9 in 0 : i1 design
    simulation.storage.decl 10 in 0 : i1 design
    simulation.storage.decl 11 in 0 : i1 design
    simulation.storage.decl 12 in 0 : i1 design
    simulation.storage.decl 13 in 0 : i1 design
    simulation.storage.decl 14 in 0 : i1 design
    simulation.storage.decl 15 in 0 : i1 design
    simulation.storage.decl 16 in 0 : i1 design
    simulation.storage.decl 17 in 0 : i1 design
    simulation.storage.decl 18 in 0 : i1 design
    simulation.storage.decl 19 in 0 : i1 design
    simulation.storage.decl 20 in 0 : i1 design
    simulation.storage.decl 21 in 0 : i1 design
    simulation.storage.decl 22 in 0 : i1 design
    simulation.storage.decl 23 in 0 : i1 design
    simulation.storage.decl 24 in 0 : i1 design
    simulation.storage.decl 25 in 0 : i1 design
    simulation.storage.decl 26 in 0 : i1 design
    simulation.storage.decl 27 in 0 : i1 design
    simulation.storage.decl 28 in 0 : i1 design
    simulation.storage.decl 29 in 0 : i1 design
    simulation.storage.decl 30 in 0 : i1 design
    simulation.storage.decl 31 in 0 : i1 design
    simulation.storage.decl 32 in 0 : i1 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "chain"
    simulation.func @chain(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64, test.activation_max_value_nodes = 512 : i64} {
      %root0 = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %root1 = simulation.context.storage %ctx[1] : !simulation.ref<i1>
      %root2 = simulation.context.storage %ctx[2] : !simulation.ref<i1>
      %root3 = simulation.context.storage %ctx[3] : !simulation.ref<i1>
      %root4 = simulation.context.storage %ctx[4] : !simulation.ref<i1>
      %root5 = simulation.context.storage %ctx[5] : !simulation.ref<i1>
      %root6 = simulation.context.storage %ctx[6] : !simulation.ref<i1>
      %root7 = simulation.context.storage %ctx[7] : !simulation.ref<i1>
      %root8 = simulation.context.storage %ctx[8] : !simulation.ref<i1>
      %root9 = simulation.context.storage %ctx[9] : !simulation.ref<i1>
      %root10 = simulation.context.storage %ctx[10] : !simulation.ref<i1>
      %root11 = simulation.context.storage %ctx[11] : !simulation.ref<i1>
      %root12 = simulation.context.storage %ctx[12] : !simulation.ref<i1>
      %root13 = simulation.context.storage %ctx[13] : !simulation.ref<i1>
      %root14 = simulation.context.storage %ctx[14] : !simulation.ref<i1>
      %root15 = simulation.context.storage %ctx[15] : !simulation.ref<i1>
      %root16 = simulation.context.storage %ctx[16] : !simulation.ref<i1>
      %root17 = simulation.context.storage %ctx[17] : !simulation.ref<i1>
      %root18 = simulation.context.storage %ctx[18] : !simulation.ref<i1>
      %root19 = simulation.context.storage %ctx[19] : !simulation.ref<i1>
      %root20 = simulation.context.storage %ctx[20] : !simulation.ref<i1>
      %root21 = simulation.context.storage %ctx[21] : !simulation.ref<i1>
      %root22 = simulation.context.storage %ctx[22] : !simulation.ref<i1>
      %root23 = simulation.context.storage %ctx[23] : !simulation.ref<i1>
      %root24 = simulation.context.storage %ctx[24] : !simulation.ref<i1>
      %root25 = simulation.context.storage %ctx[25] : !simulation.ref<i1>
      %root26 = simulation.context.storage %ctx[26] : !simulation.ref<i1>
      %root27 = simulation.context.storage %ctx[27] : !simulation.ref<i1>
      %root28 = simulation.context.storage %ctx[28] : !simulation.ref<i1>
      %root29 = simulation.context.storage %ctx[29] : !simulation.ref<i1>
      %root30 = simulation.context.storage %ctx[30] : !simulation.ref<i1>
      %root31 = simulation.context.storage %ctx[31] : !simulation.ref<i1>
      %root32 = simulation.context.storage %ctx[32] : !simulation.ref<i1>
      %zero = arith.constant false
      %one = arith.constant true
      cf.br ^body
    ^body:
      %flag = simulation.ref.load %root32 : !simulation.ref<i1> -> i1
      cf.br ^block0
    ^block0:
      cf.cond_br %flag, ^yes0, ^no0
    ^yes0:
      simulation.ref.store %one to %root0 : i1, !simulation.ref<i1>
      cf.br ^block1
    ^no0:
      simulation.ref.store %zero to %root0 : i1, !simulation.ref<i1>
      cf.br ^block1
    ^block1:
      cf.cond_br %flag, ^yes1, ^no1
    ^yes1:
      simulation.ref.store %one to %root1 : i1, !simulation.ref<i1>
      cf.br ^block2
    ^no1:
      simulation.ref.store %zero to %root1 : i1, !simulation.ref<i1>
      cf.br ^block2
    ^block2:
      cf.cond_br %flag, ^yes2, ^no2
    ^yes2:
      simulation.ref.store %one to %root2 : i1, !simulation.ref<i1>
      cf.br ^block3
    ^no2:
      simulation.ref.store %zero to %root2 : i1, !simulation.ref<i1>
      cf.br ^block3
    ^block3:
      cf.cond_br %flag, ^yes3, ^no3
    ^yes3:
      simulation.ref.store %one to %root3 : i1, !simulation.ref<i1>
      cf.br ^block4
    ^no3:
      simulation.ref.store %zero to %root3 : i1, !simulation.ref<i1>
      cf.br ^block4
    ^block4:
      cf.cond_br %flag, ^yes4, ^no4
    ^yes4:
      simulation.ref.store %one to %root4 : i1, !simulation.ref<i1>
      cf.br ^block5
    ^no4:
      simulation.ref.store %zero to %root4 : i1, !simulation.ref<i1>
      cf.br ^block5
    ^block5:
      cf.cond_br %flag, ^yes5, ^no5
    ^yes5:
      simulation.ref.store %one to %root5 : i1, !simulation.ref<i1>
      cf.br ^block6
    ^no5:
      simulation.ref.store %zero to %root5 : i1, !simulation.ref<i1>
      cf.br ^block6
    ^block6:
      cf.cond_br %flag, ^yes6, ^no6
    ^yes6:
      simulation.ref.store %one to %root6 : i1, !simulation.ref<i1>
      cf.br ^block7
    ^no6:
      simulation.ref.store %zero to %root6 : i1, !simulation.ref<i1>
      cf.br ^block7
    ^block7:
      cf.cond_br %flag, ^yes7, ^no7
    ^yes7:
      simulation.ref.store %one to %root7 : i1, !simulation.ref<i1>
      cf.br ^block8
    ^no7:
      simulation.ref.store %zero to %root7 : i1, !simulation.ref<i1>
      cf.br ^block8
    ^block8:
      cf.cond_br %flag, ^yes8, ^no8
    ^yes8:
      simulation.ref.store %one to %root8 : i1, !simulation.ref<i1>
      cf.br ^block9
    ^no8:
      simulation.ref.store %zero to %root8 : i1, !simulation.ref<i1>
      cf.br ^block9
    ^block9:
      cf.cond_br %flag, ^yes9, ^no9
    ^yes9:
      simulation.ref.store %one to %root9 : i1, !simulation.ref<i1>
      cf.br ^block10
    ^no9:
      simulation.ref.store %zero to %root9 : i1, !simulation.ref<i1>
      cf.br ^block10
    ^block10:
      cf.cond_br %flag, ^yes10, ^no10
    ^yes10:
      simulation.ref.store %one to %root10 : i1, !simulation.ref<i1>
      cf.br ^block11
    ^no10:
      simulation.ref.store %zero to %root10 : i1, !simulation.ref<i1>
      cf.br ^block11
    ^block11:
      cf.cond_br %flag, ^yes11, ^no11
    ^yes11:
      simulation.ref.store %one to %root11 : i1, !simulation.ref<i1>
      cf.br ^block12
    ^no11:
      simulation.ref.store %zero to %root11 : i1, !simulation.ref<i1>
      cf.br ^block12
    ^block12:
      cf.cond_br %flag, ^yes12, ^no12
    ^yes12:
      simulation.ref.store %one to %root12 : i1, !simulation.ref<i1>
      cf.br ^block13
    ^no12:
      simulation.ref.store %zero to %root12 : i1, !simulation.ref<i1>
      cf.br ^block13
    ^block13:
      cf.cond_br %flag, ^yes13, ^no13
    ^yes13:
      simulation.ref.store %one to %root13 : i1, !simulation.ref<i1>
      cf.br ^block14
    ^no13:
      simulation.ref.store %zero to %root13 : i1, !simulation.ref<i1>
      cf.br ^block14
    ^block14:
      cf.cond_br %flag, ^yes14, ^no14
    ^yes14:
      simulation.ref.store %one to %root14 : i1, !simulation.ref<i1>
      cf.br ^block15
    ^no14:
      simulation.ref.store %zero to %root14 : i1, !simulation.ref<i1>
      cf.br ^block15
    ^block15:
      cf.cond_br %flag, ^yes15, ^no15
    ^yes15:
      simulation.ref.store %one to %root15 : i1, !simulation.ref<i1>
      cf.br ^block16
    ^no15:
      simulation.ref.store %zero to %root15 : i1, !simulation.ref<i1>
      cf.br ^block16
    ^block16:
      cf.cond_br %flag, ^yes16, ^no16
    ^yes16:
      simulation.ref.store %one to %root16 : i1, !simulation.ref<i1>
      cf.br ^block17
    ^no16:
      simulation.ref.store %zero to %root16 : i1, !simulation.ref<i1>
      cf.br ^block17
    ^block17:
      cf.cond_br %flag, ^yes17, ^no17
    ^yes17:
      simulation.ref.store %one to %root17 : i1, !simulation.ref<i1>
      cf.br ^block18
    ^no17:
      simulation.ref.store %zero to %root17 : i1, !simulation.ref<i1>
      cf.br ^block18
    ^block18:
      cf.cond_br %flag, ^yes18, ^no18
    ^yes18:
      simulation.ref.store %one to %root18 : i1, !simulation.ref<i1>
      cf.br ^block19
    ^no18:
      simulation.ref.store %zero to %root18 : i1, !simulation.ref<i1>
      cf.br ^block19
    ^block19:
      cf.cond_br %flag, ^yes19, ^no19
    ^yes19:
      simulation.ref.store %one to %root19 : i1, !simulation.ref<i1>
      cf.br ^block20
    ^no19:
      simulation.ref.store %zero to %root19 : i1, !simulation.ref<i1>
      cf.br ^block20
    ^block20:
      cf.cond_br %flag, ^yes20, ^no20
    ^yes20:
      simulation.ref.store %one to %root20 : i1, !simulation.ref<i1>
      cf.br ^block21
    ^no20:
      simulation.ref.store %zero to %root20 : i1, !simulation.ref<i1>
      cf.br ^block21
    ^block21:
      cf.cond_br %flag, ^yes21, ^no21
    ^yes21:
      simulation.ref.store %one to %root21 : i1, !simulation.ref<i1>
      cf.br ^block22
    ^no21:
      simulation.ref.store %zero to %root21 : i1, !simulation.ref<i1>
      cf.br ^block22
    ^block22:
      cf.cond_br %flag, ^yes22, ^no22
    ^yes22:
      simulation.ref.store %one to %root22 : i1, !simulation.ref<i1>
      cf.br ^block23
    ^no22:
      simulation.ref.store %zero to %root22 : i1, !simulation.ref<i1>
      cf.br ^block23
    ^block23:
      cf.cond_br %flag, ^yes23, ^no23
    ^yes23:
      simulation.ref.store %one to %root23 : i1, !simulation.ref<i1>
      cf.br ^block24
    ^no23:
      simulation.ref.store %zero to %root23 : i1, !simulation.ref<i1>
      cf.br ^block24
    ^block24:
      cf.cond_br %flag, ^yes24, ^no24
    ^yes24:
      simulation.ref.store %one to %root24 : i1, !simulation.ref<i1>
      cf.br ^block25
    ^no24:
      simulation.ref.store %zero to %root24 : i1, !simulation.ref<i1>
      cf.br ^block25
    ^block25:
      cf.cond_br %flag, ^yes25, ^no25
    ^yes25:
      simulation.ref.store %one to %root25 : i1, !simulation.ref<i1>
      cf.br ^block26
    ^no25:
      simulation.ref.store %zero to %root25 : i1, !simulation.ref<i1>
      cf.br ^block26
    ^block26:
      cf.cond_br %flag, ^yes26, ^no26
    ^yes26:
      simulation.ref.store %one to %root26 : i1, !simulation.ref<i1>
      cf.br ^block27
    ^no26:
      simulation.ref.store %zero to %root26 : i1, !simulation.ref<i1>
      cf.br ^block27
    ^block27:
      cf.cond_br %flag, ^yes27, ^no27
    ^yes27:
      simulation.ref.store %one to %root27 : i1, !simulation.ref<i1>
      cf.br ^block28
    ^no27:
      simulation.ref.store %zero to %root27 : i1, !simulation.ref<i1>
      cf.br ^block28
    ^block28:
      cf.cond_br %flag, ^yes28, ^no28
    ^yes28:
      simulation.ref.store %one to %root28 : i1, !simulation.ref<i1>
      cf.br ^block29
    ^no28:
      simulation.ref.store %zero to %root28 : i1, !simulation.ref<i1>
      cf.br ^block29
    ^block29:
      cf.cond_br %flag, ^yes29, ^no29
    ^yes29:
      simulation.ref.store %one to %root29 : i1, !simulation.ref<i1>
      cf.br ^block30
    ^no29:
      simulation.ref.store %zero to %root29 : i1, !simulation.ref<i1>
      cf.br ^block30
    ^block30:
      cf.cond_br %flag, ^yes30, ^no30
    ^yes30:
      simulation.ref.store %one to %root30 : i1, !simulation.ref<i1>
      cf.br ^block31
    ^no30:
      simulation.ref.store %zero to %root30 : i1, !simulation.ref<i1>
      cf.br ^block31
    ^block31:
      cf.cond_br %flag, ^yes31, ^no31
    ^yes31:
      simulation.ref.store %one to %root31 : i1, !simulation.ref<i1>
      cf.br ^block32
    ^no31:
      simulation.ref.store %zero to %root31 : i1, !simulation.ref<i1>
      cf.br ^block32
    ^block32:
      simulation.suspend.change %root32 to ^body : !simulation.ref<i1>
    }
  }
}
