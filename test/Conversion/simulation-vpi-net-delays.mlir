// RUN: env OBELISK_TEST_INPUT=%s OBELISK_TEST_OUTPUT=%t.dump \
// RUN:   OBELISK_TEST_VPI=read %obj_root/test/obelisk-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.Dump
// RUN: FileCheck %s < %t.dump
// RUN: env OBELISK_TEST_INPUT=%s \
// RUN:   %obj_root/test/obelisk-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.NetIdentityQueries

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @vpi_net_delays {
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<4> design
        hierarchy "top.uniform" {
      propagation_delays = array<i64: 7, 11, 13>,
      vpi_type = #obelisk_sim.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [3, 0], children = [], childNames = []>,
      vpi_properties = #obelisk_sim.vpi_properties<[
        #obelisk_sim.vpi_property<selector = 22 : i32, value = 1 : i32>
      ]>
    }
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<3> design
        hierarchy "top.per_bit" {
      propagation_delays = array<i64: 1, 2, 3, 1, 2, 3, 4, 5, 6>
    }
    obelisk_sim.net.decl 2 in 0 : !obelisk_sim.logic<2> design
        hierarchy "top.immediate"
    obelisk_sim.net.decl 4 in 0 : !obelisk_sim.logic<3> design
        hierarchy "top.mixed" {
      propagation_delays = array<i64: -1, -1, -1, 2, 3, 4, -1, -1, -1>
    }
    obelisk_sim.net.decl 3 in 0 : !obelisk_sim.logic<1> design
        hierarchy "top.retained" {
      propagation_delays = array<i64: 17, 19, -1>,
      resolution_kind = 9 : i32
    }
    obelisk_sim.vpi_net_identity.decl 0 backed_by 0 in 0
        : !obelisk_sim.packed_array<0 : 3 x !obelisk_sim.logic<1>>
        hierarchy "top.uniform_alias" debug "uniform_alias" {
      vpi_type = #obelisk_sim.vpi_type<kind = packed_array, isSigned = false,
          isFourState = true, range = [0, 3], children = [
            #obelisk_sim.vpi_type<kind = logic, isSigned = false,
                isFourState = true, range = [0, 0], children = [],
                childNames = []>
          ], childNames = []>,
      vpi_properties = #obelisk_sim.vpi_properties<[
        #obelisk_sim.vpi_property<selector = 22 : i32, value = 1 : i32>
      ]>
    }
    obelisk_sim.vpi_relation.decl <kind = net_identity, id = 0 : i64>
        selector 126 handle ordinal 0 to <kind = net, id = 0 : i64>
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.func @initial(%ctx: !obelisk_sim.context
        {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      obelisk_sim.return
    }
  }
}

// CHECK-NOT: net_delay_run {{.*}} object_name=top.immediate
// CHECK: net_delay_run object={{[0-9]+}} object_name=top.mixed bits=[1:2) delays=[2,3,4]
// CHECK-NEXT: net_delay_run object={{[0-9]+}} object_name=top.per_bit bits=[0:2) delays=[1,2,3]
// CHECK-NEXT: net_delay_run object={{[0-9]+}} object_name=top.per_bit bits=[2:3) delays=[4,5,6]
// CHECK-NEXT: net_delay_run object={{[0-9]+}} object_name=top.retained bits=[0:1) delays=[17,19,-1]
// CHECK-NEXT: net_delay_run object={{[0-9]+}} object_name=top.uniform bits=[0:4) delays=[7,11,13]
// CHECK-NOT: net_delay_run {{.*}} object_name=top.uniform_alias
