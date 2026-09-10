// RUN: %split-file %s %t
// RUN: obelisk-opt %t/topology.mlir '--lower-obelisk-to-sim=opt-level=0 early-symbol-dce=false' | FileCheck %s
// RUN: obelisk --vpi=read -emit-sim %t/frontend.sv -o %t/frontend.mlir
// RUN: FileCheck %s --check-prefix=FRONTEND --input-file=%t/frontend.mlir
// RUN: FileCheck %s --check-prefix=PACKAGE --input-file=%t/frontend.mlir
// RUN: FileCheck %s --check-prefix=LOCATION --input-file=%t/frontend.mlir

// Nested module, interface, and program array members are not top instances.
// Their definition names remain available from legacy referenced definitions
// even when no frozen frontend identity attribute is present.

//--- topology.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32,
      hierarchical_name = "m", name = "m", node_id = 0 : i64,
      sym_name = "m_def"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 1 : i32,
      hierarchical_name = "i", name = "i", node_id = 1 : i64,
      sym_name = "i_def"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 2 : i32,
      hierarchical_name = "p", name = "p", node_id = 2 : i64,
      sym_name = "p_def"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32,
      hierarchical_name = "host", name = "host", node_id = 3 : i64,
      sym_name = "host_def"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 4 : i64, sym_name = "root"} {
    obelisk.sv.symbol.compilation_unit attributes {
        hierarchical_name = "$unit", node_id = 5 : i64, sym_name = "cu"} {}
        loc("unit.sv":1:1)
    obelisk.sv.symbol.instance attributes {hierarchical_name = "host",
        is_uninstantiated = false, name = "host", node_id = 16 : i64,
        referenced_path = "host", referenced_symbol = @host_def,
        sym_name = "host_i"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "host",
          name = "host", node_id = 17 : i64, sym_name = "host_b"} {
        obelisk.sv.symbol.instance_array attributes {
            array_range = array<i64: 0, 0>, hierarchical_name = "host.m_nested",
            name = "m_nested", node_id = 18 : i64, sym_name = "m_nested_a"} {
          obelisk.sv.symbol.instance attributes {
              hierarchical_name = "host.m_nested[0]",
              is_uninstantiated = false, node_id = 19 : i64,
              referenced_path = "m", referenced_symbol = @m_def,
              sym_name = "m_nested_i"} {
            obelisk.sv.symbol.instance_body attributes {
                hierarchical_name = "host.m_nested[0]", name = "m",
                node_id = 20 : i64, sym_name = "m_nested_b"} {}
          }
        }
        obelisk.sv.symbol.instance_array attributes {
            array_range = array<i64: 0, 0>, hierarchical_name = "host.i_nested",
            name = "i_nested", node_id = 21 : i64, sym_name = "i_nested_a"} {
          obelisk.sv.symbol.instance attributes {
              hierarchical_name = "host.i_nested[0]",
              is_uninstantiated = false, node_id = 22 : i64,
              referenced_path = "i", referenced_symbol = @i_def,
              sym_name = "i_nested_i"} {
            obelisk.sv.symbol.instance_body attributes {
                hierarchical_name = "host.i_nested[0]", name = "i",
                node_id = 23 : i64, sym_name = "i_nested_b"} {}
          }
        }
        obelisk.sv.symbol.instance_array attributes {
            array_range = array<i64: 0, 0>, hierarchical_name = "host.p_nested",
            name = "p_nested", node_id = 24 : i64, sym_name = "p_nested_a"} {
          obelisk.sv.symbol.instance attributes {
              hierarchical_name = "host.p_nested[0]",
              is_uninstantiated = false, node_id = 25 : i64,
              referenced_path = "p", referenced_symbol = @p_def,
              sym_name = "p_nested_i"} {
            obelisk.sv.symbol.instance_body attributes {
                hierarchical_name = "host.p_nested[0]", name = "p",
                node_id = 26 : i64, sym_name = "p_nested_b"} {}
          }
        }
      }
    }
  }
  obelisk.sv.symbol.package attributes {hierarchical_name = "pkg",
      name = "pkg", node_id = 27 : i64, sym_name = "pkg"} {}
      loc("pkg.sv":7:1)
}

// CHECK-DAG: obelisk_sim.vpi_object.anchor {{.*}} type 32 {{.*}} hierarchy "host.m_nested[0]" {{.*}} vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 9 : i32, value = "m">]>
// CHECK-DAG: obelisk_sim.vpi_object.anchor {{.*}} type 601 {{.*}} hierarchy "host.i_nested[0]" {{.*}} vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 9 : i32, value = "i">]>
// CHECK-DAG: obelisk_sim.vpi_object.anchor {{.*}} type 602 {{.*}} hierarchy "host.p_nested[0]" {{.*}} vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 9 : i32, value = "p">]>
// Ordinary packages are top-level instance objects. Compilation units use the
// implementation-defined `$unit` spelling for vpiDefName and additionally set
// vpiUnit.
// CHECK-DAG: #[[UNIT_LOC:loc[0-9]*]] = loc("unit.sv":1:1)
// CHECK-DAG: #[[PACKAGE_LOC:loc[0-9]*]] = loc("pkg.sv":7:1)
// CHECK-DAG: obelisk_sim.vpi_object.anchor {{.*}} type 600 {{.*}} hierarchy "pkg" {{.*}}definition_loc = #[[PACKAGE_LOC]], {{.*}}vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 9 : i32, value = "pkg">, #obelisk_sim.vpi_property<selector = 600 : i32, value = true>]>
// CHECK-DAG: obelisk_sim.vpi_object.anchor {{.*}} type 600 {{.*}} hierarchy "$unit" {{.*}}definition_loc = #[[UNIT_LOC]], {{.*}}vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 9 : i32, value = "$unit">, #obelisk_sim.vpi_property<selector = 600 : i32, value = true>, #obelisk_sim.vpi_property<selector = 602 : i32, value = true>]>

// A root-owned InstanceArray cannot be written in SystemVerilog source: Slang
// synthesizes scalar root instances from selected top definitions. This source
// regression covers frontend extraction for scalar M/P top instances and a
// nested interface; the authored topology section above covers nested M/I/P
// array members.
//--- frontend.sv
`celldefine
module cellmod(input logic [7:0] a, output logic z);
endmodule
`endcelldefine

module param_cell #(parameter int W = 8) (input logic [W-1:0] p);
endmodule

module automatic top;
  cellmod u();
  cellmod v();
  param_cell #(.W(8)) p8a();
  param_cell #(.W(8)) p8b();
  param_cell #(.W(4)) p4();
  iftop i();
endmodule

interface iftop;
endinterface

program automatic ptop;
endprogram

package automatic pkg;
endpackage

`line 700 "mapped_pkg.sv" 0
package mapped_pkg;
endpackage

// FRONTEND-DAG: obelisk_sim.vpi_definition.decl @[[PROGRAM:[^ ]+]] type 602 name "ptop"
// FRONTEND-DAG: obelisk_sim.scope.decl {{.*}} hierarchy "ptop" {{.*}} vpi_kind 602 definition @[[PROGRAM]]
// FRONTEND-DAG: obelisk_sim.vpi_definition.decl @[[CELL:[^ ]+]] type 32 name "cellmod"
// FRONTEND-DAG: obelisk_sim.vpi_definition_member.decl @[[CELL_A:[^ ]+]] of @[[CELL]] type 28 ordinal 0 name "a" direction input
// FRONTEND-DAG: obelisk_sim.vpi_definition_member.decl @[[CELL_Z:[^ ]+]] of @[[CELL]] type 28 ordinal 1 name "z" direction output
// FRONTEND-DAG: obelisk_sim.vpi_definition_specialization.decl @[[CELL_SPEC:[^ ]+]] of @[[CELL]]
// FRONTEND-DAG: obelisk_sim.vpi_definition_member.specialize @[[CELL_SPEC]] member @[[CELL_A]] type
// FRONTEND-DAG: obelisk_sim.vpi_definition_member.specialize @[[CELL_SPEC]] member @[[CELL_Z]] type
// FRONTEND-DAG: obelisk_sim.scope.decl {{.*}} hierarchy "top.u" {{.*}} vpi_kind 32 definition @[[CELL]] specialization @[[CELL_SPEC]]
// FRONTEND-DAG: obelisk_sim.scope.decl {{.*}} hierarchy "top.v" {{.*}} vpi_kind 32 definition @[[CELL]] specialization @[[CELL_SPEC]]
// FRONTEND-DAG: obelisk_sim.vpi_definition.decl @[[PARAM:[^ ]+]] type 32 name "param_cell"
// FRONTEND-DAG: obelisk_sim.vpi_definition_member.decl @[[PARAM_P:[^ ]+]] of @[[PARAM]] type 28 ordinal 0 name "p" direction input
// FRONTEND-DAG: obelisk_sim.vpi_definition_specialization.decl @[[PARAM_SPEC8:[^ ]+]] of @[[PARAM]]
// FRONTEND-DAG: obelisk_sim.vpi_definition_member.specialize @[[PARAM_SPEC8]] member @[[PARAM_P]] type <kind = packed_array, {{.*}} range = [7, 0]
// FRONTEND-DAG: obelisk_sim.vpi_definition_specialization.decl @[[PARAM_SPEC4:[^ ]+]] of @[[PARAM]]
// FRONTEND-DAG: obelisk_sim.vpi_definition_member.specialize @[[PARAM_SPEC4]] member @[[PARAM_P]] type <kind = packed_array, {{.*}} range = [3, 0]
// FRONTEND-DAG: obelisk_sim.scope.decl {{.*}} hierarchy "top.p8a" {{.*}} definition @[[PARAM]] specialization @[[PARAM_SPEC8]]
// FRONTEND-DAG: obelisk_sim.scope.decl {{.*}} hierarchy "top.p8b" {{.*}} definition @[[PARAM]] specialization @[[PARAM_SPEC8]]
// FRONTEND-DAG: obelisk_sim.scope.decl {{.*}} hierarchy "top.p4" {{.*}} definition @[[PARAM]] specialization @[[PARAM_SPEC4]]
// FRONTEND-DAG: obelisk_sim.vpi_definition.decl @[[INTERFACE:[^ ]+]] type 601 name "iftop"
// FRONTEND-DAG: obelisk_sim.scope.decl {{.*}} hierarchy "top.i" {{.*}} vpi_kind 601 definition @[[INTERFACE]]
// FRONTEND-DAG: obelisk_sim.vpi_object.anchor {{.*}} type 32 {{.*}} hierarchy "top" {{.*}} vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 7 : i32, value = true>, #obelisk_sim.vpi_property<selector = 9 : i32, value = "top">, #obelisk_sim.vpi_property<selector = 50 : i32, value = true>, #obelisk_sim.vpi_property<selector = 600 : i32, value = true>]>
// FRONTEND-DAG: obelisk_sim.vpi_object.anchor {{.*}} type 32 {{.*}} hierarchy "top.u" {{.*}} vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 8 : i32, value = true>, #obelisk_sim.vpi_property<selector = 9 : i32, value = "cellmod">]>
// FRONTEND-DAG: obelisk_sim.vpi_object.anchor {{.*}} type 32 {{.*}} hierarchy "top.v" {{.*}} vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 8 : i32, value = true>, #obelisk_sim.vpi_property<selector = 9 : i32, value = "cellmod">]>
// FRONTEND-DAG: obelisk_sim.vpi_object.anchor {{.*}} type 601 {{.*}} hierarchy "top.i" {{.*}} vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 9 : i32, value = "iftop">]>
// FRONTEND-DAG: obelisk_sim.vpi_object.anchor {{.*}} type 602 {{.*}} hierarchy "ptop" {{.*}} vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 9 : i32, value = "ptop">, #obelisk_sim.vpi_property<selector = 50 : i32, value = true>, #obelisk_sim.vpi_property<selector = 600 : i32, value = true>]>
// FRONTEND-DAG: obelisk_sim.vpi_object.anchor {{.*}} type 600 {{.*}} hierarchy "pkg" {{.*}}definition_loc = #loc{{[0-9]+}}, {{.*}}vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 9 : i32, value = "pkg">, #obelisk_sim.vpi_property<selector = 50 : i32, value = true>, #obelisk_sim.vpi_property<selector = 600 : i32, value = true>]>
// PACKAGE-NOT: obelisk_sim.vpi_definition.decl {{.*}} type 600
// LOCATION-DAG: #[[MAPPED_LOC:loc[0-9]*]] = loc("mapped_pkg.sv":700:1)
// LOCATION-DAG: obelisk_sim.vpi_object.anchor {{.*}} type 600 {{.*}} hierarchy "mapped_pkg" {{.*}}definition_loc = #[[MAPPED_LOC]]
