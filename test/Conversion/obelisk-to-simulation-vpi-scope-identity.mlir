// RUN: %split-file %s %t
// RUN: obelisk-opt %t/topology.mlir '--lower-obelisk-to-sim=opt-level=0 early-symbol-dce=false' | FileCheck %s
// RUN: obelisk --vpi=read -emit-sim %t/frontend.sv -o %t/frontend.mlir
// RUN: FileCheck %s --check-prefix=FRONTEND --input-file=%t/frontend.mlir

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
}

// CHECK-DAG: obelisk_sim.vpi_object.anchor {{.*}} type 32 {{.*}} hierarchy "host.m_nested[0]" {{.*}} vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 9 : i32, value = "m">]>
// CHECK-DAG: obelisk_sim.vpi_object.anchor {{.*}} type 601 {{.*}} hierarchy "host.i_nested[0]" {{.*}} vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 9 : i32, value = "i">]>
// CHECK-DAG: obelisk_sim.vpi_object.anchor {{.*}} type 602 {{.*}} hierarchy "host.p_nested[0]" {{.*}} vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 9 : i32, value = "p">]>
// Ordinary packages are top-level instance objects. Compilation units use the
// implementation-defined `$unit` spelling for vpiDefName and additionally set
// vpiUnit.
// CHECK-DAG: obelisk_sim.vpi_object.anchor {{.*}} type 600 {{.*}} hierarchy "pkg" {{.*}}vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 9 : i32, value = "pkg">, #obelisk_sim.vpi_property<selector = 600 : i32, value = true>]>
// CHECK-DAG: obelisk_sim.vpi_object.anchor {{.*}} type 600 {{.*}} hierarchy "$unit" {{.*}}vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 9 : i32, value = "$unit">, #obelisk_sim.vpi_property<selector = 600 : i32, value = true>, #obelisk_sim.vpi_property<selector = 602 : i32, value = true>]>

// A root-owned InstanceArray cannot be written in SystemVerilog source: Slang
// synthesizes scalar root instances from selected top definitions. This source
// regression covers frontend extraction for scalar M/P top instances and a
// nested interface; the authored topology section above covers nested M/I/P
// array members.
//--- frontend.sv
`celldefine
module cellmod;
endmodule
`endcelldefine

module automatic top;
  cellmod u();
  iftop i();
endmodule

interface iftop;
endinterface

program automatic ptop;
endprogram

package automatic pkg;
endpackage

// FRONTEND-DAG: obelisk_sim.vpi_object.anchor {{.*}} type 32 {{.*}} hierarchy "top" {{.*}} vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 7 : i32, value = true>, #obelisk_sim.vpi_property<selector = 9 : i32, value = "top">, #obelisk_sim.vpi_property<selector = 50 : i32, value = true>, #obelisk_sim.vpi_property<selector = 600 : i32, value = true>]>
// FRONTEND-DAG: obelisk_sim.vpi_object.anchor {{.*}} type 32 {{.*}} hierarchy "top.u" {{.*}} vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 8 : i32, value = true>, #obelisk_sim.vpi_property<selector = 9 : i32, value = "cellmod">]>
// FRONTEND-DAG: obelisk_sim.vpi_object.anchor {{.*}} type 601 {{.*}} hierarchy "top.i" {{.*}} vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 9 : i32, value = "iftop">]>
// FRONTEND-DAG: obelisk_sim.vpi_object.anchor {{.*}} type 602 {{.*}} hierarchy "ptop" {{.*}} vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 9 : i32, value = "ptop">, #obelisk_sim.vpi_property<selector = 50 : i32, value = true>, #obelisk_sim.vpi_property<selector = 600 : i32, value = true>]>
// FRONTEND-DAG: obelisk_sim.vpi_object.anchor {{.*}} type 600 {{.*}} hierarchy "pkg" {{.*}}vpi_properties = #obelisk_sim.vpi_properties<[#obelisk_sim.vpi_property<selector = 9 : i32, value = "pkg">, #obelisk_sim.vpi_property<selector = 50 : i32, value = true>, #obelisk_sim.vpi_property<selector = 600 : i32, value = true>]>
