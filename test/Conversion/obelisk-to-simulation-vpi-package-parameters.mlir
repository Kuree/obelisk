// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0 early-symbol-dce=false' | FileCheck %s

module {
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 0 : i64, sym_name = "root"} {
    obelisk.sv.symbol.package attributes {hierarchical_name = "uvm_pkg",
        name = "uvm_pkg", node_id = 1 : i64, sym_name = "uvm_pkg"} {
      obelisk.sv.symbol.parameter attributes {constant_value = "1536",
          hierarchical_name = "uvm_pkg::UVM_HDL_MAX_WIDTH",
          name = "UVM_HDL_MAX_WIDTH", node_id = 2 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
          sym_name = "max_width"} {} loc("uvm_pkg.sv":12:17)
      obelisk.sv.symbol.parameter attributes {constant_value = "68'hdabc_x123_4567_89ab",
          hierarchical_name = "uvm_pkg::WIDE_FOUR_STATE",
          name = "WIDE_FOUR_STATE", node_id = 3 : i64,
          semantic_type = !obelisk.integral<68, false, true, 67 : 0, logic>,
          sym_name = "wide"} {} loc("uvm_pkg.sv":13:17)
      // Non-packed values stay out of this production slice; they must not
      // acquire a bogus state-backed parameter object.
      obelisk.sv.symbol.parameter attributes {constant_value = "hello",
          hierarchical_name = "uvm_pkg::TEXT", name = "TEXT",
          node_id = 4 : i64, semantic_type = !obelisk.string,
          sym_name = "text"} {}
    } loc("uvm_pkg.sv":1:1)
  }
}

// CHECK-DAG: obelisk_sim.vpi_object.anchor @[[PKG:__obelisk_vpi_anchor_[0-9]+]] id {{[0-9]+}} type 600 in 0 {{.*}}hierarchy "uvm_pkg" debug "uvm_pkg"
// CHECK-DAG: obelisk_sim.vpi_object.anchor @[[MAX:__obelisk_vpi_anchor_[0-9]+]] id {{[0-9]+}} type 41 in 0 parent @[[PKG]] {{.*}}hierarchy "uvm_pkg::UVM_HDL_MAX_WIDTH" debug "UVM_HDL_MAX_WIDTH" {{.*}}immutable_value = #obelisk_sim.frozen_constant<value = [1536 : i32, 0 : i32], isSigned = true> : i32{{.*}}vpi_type = #obelisk_sim.vpi_type<kind = int, isSigned = true, isFourState = false{{.*}}
// CHECK-DAG: obelisk_sim.vpi_object.anchor @[[WIDE:__obelisk_vpi_anchor_[0-9]+]] id {{[0-9]+}} type 41 in 0 parent @[[PKG]] {{.*}}hierarchy "uvm_pkg::WIDE_FOUR_STATE" debug "WIDE_FOUR_STATE" {{.*}}immutable_value = #obelisk_sim.frozen_constant<{{.*}}isSigned = false> : !obelisk_sim.logic<68>{{.*}}vpi_type = #obelisk_sim.vpi_type<kind = logic, isSigned = false, isFourState = true{{.*}}
// CHECK-NOT: hierarchy "uvm_pkg::TEXT"
