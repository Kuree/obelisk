// RUN: obelisk-opt %s --obelisk-sim-optimize-native-regions | FileCheck %s

module {
  obelisk_sim.design @native_region {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 always hierarchy "native_region.region"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<1> design

    obelisk_sim.func private @region(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %target: !obelisk_sim.ref<!obelisk_sim.logic<8>>
          {obelisk_sim.capture_kind = 3 : i32,
           obelisk_sim.descriptor_id = 0 : i64},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 3 : i32,
           obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32,
                    code_unit_id = 1 : i64,
                    obelisk.native.region_body} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %clock to ^body :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^body:
      %first = obelisk_sim.logic.constant 1 : i8, 0 : i8 :
          !obelisk_sim.logic<8>
      obelisk_sim.nba.enqueue %first to %target {
        site = #obelisk_sim.nba_site<id = 0, commit = 7,
          storage = root_accumulator>
      } : (!obelisk_sim.logic<8>,
           !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      %overwrite = arith.constant true
      cf.cond_br %overwrite, ^overwrite, ^join
    ^overwrite:
      %last = obelisk_sim.logic.constant 2 : i8, 0 : i8 :
          !obelisk_sim.logic<8>
      obelisk_sim.nba.enqueue %last to %target {
        site = #obelisk_sim.nba_site<id = 1, commit = 7,
          storage = root_accumulator>
      } : (!obelisk_sim.logic<8>,
           !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      cf.br ^join
    ^join:
      cf.br ^wait
    }
  }
}

// CHECK-LABEL: obelisk_sim.func private @region
// CHECK-NOT: obelisk.native.region_body
// CHECK: ^{{.*}}(%{{.*}}: i1, %{{.*}}: !obelisk_sim.logic<8>):
// CHECK-COUNT-1: obelisk_sim.nba.enqueue
// CHECK-SAME: site = #obelisk_sim.nba_site<id = 1, commit = 7,
