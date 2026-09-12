// RUN: obelisk-opt %s --obelisk-sim-prepare-coverage | FileCheck %s

module attributes {
  obelisk.coverage.config = "{\22exclude\22:[{\22metrics\22:[\22toggle\22],\22hierarchy\22:\22top.excluded\22,\22reason\22:\22test exclusion\22}]}",
  obelisk.coverage.metrics = ["toggle"]
} {
  obelisk_sim.design @coverage {
    obelisk_sim.scope.decl 0
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    obelisk_sim.scope.decl 2 parent 0 hierarchy "C" vpi_kind 652

    obelisk_sim.storage.decl 0 in 1 : i4 design hierarchy "top.data" debug "data" {
      obelisk.coverage.source_authored,
      source_range = !obelisk.source_range<"toggle.sv", 1, 3, "toggle.sv", 1, 6, "">
    }
    obelisk_sim.storage.decl 1 in 1 : !obelisk_sim.logic<2> design hierarchy "top.logic" debug "logic" {
      obelisk.coverage.source_authored,
      source_range = !obelisk.source_range<"toggle.sv", 2, 3, "toggle.sv", 2, 7, "">
    }
    obelisk_sim.net.decl 0 in 1 : !obelisk_sim.logic<1> design hierarchy "top.net" debug "net" {
      obelisk.coverage.source_authored,
      source_range = !obelisk.source_range<"toggle.sv", 3, 3, "toggle.sv", 3, 5, "">
    }
    // Ports remain separate source obligations even when they alias state.
    obelisk_sim.port.decl 0 in 1 source 0 net = false at 1 : i2 input ordinal 0 hierarchy "top.alias" debug "alias" {
      obelisk.coverage.source_authored,
      source_range = !obelisk.source_range<"toggle.sv", 4, 3, "toggle.sv", 4, 7, "">
    }
    // Excluded obligations stay in the schema and flat bit count but do not
    // make their canonical state observable.
    obelisk_sim.storage.decl 2 in 1 : i1 design hierarchy "top.excluded" {
      obelisk.coverage.source_authored,
      source_range = !obelisk.source_range<"toggle.sv", 5, 3, "toggle.sv", 5, 10, "">
    }

    // Automatic, non-integral, and class-owned objects are not toggle signals.
    obelisk_sim.storage.decl 3 in 1 : i3 automatic hierarchy "top.automatic" {
      obelisk.coverage.source_authored,
      source_range = !obelisk.source_range<"toggle.sv", 6, 3, "toggle.sv", 6, 11, "">
    }
    obelisk_sim.storage.decl 4 in 1 : f64 design hierarchy "top.real" {
      obelisk.coverage.source_authored,
      source_range = !obelisk.source_range<"toggle.sv", 7, 3, "toggle.sv", 7, 6, "">
    }
    obelisk_sim.storage.decl 5 in 2 : i5 design hierarchy "C.static" {
      obelisk.coverage.source_authored,
      source_range = !obelisk.source_range<"toggle.sv", 8, 3, "toggle.sv", 8, 8, "">
    }
    // Compiler support state can have source locations and hierarchy names;
    // only the explicit source-authored marker makes it an obligation.
    obelisk_sim.storage.decl 6 in 1 : i7 design hierarchy "top.__timer" {
      source_range = !obelisk.source_range<"toggle.sv", 9, 3, "toggle.sv", 9, 10, "">
    }
  }
}

// CHECK: module attributes {
// CHECK-SAME: obelisk.coverage.toggle_bit_count = 10 : i64
// CHECK-SAME: obelisk.coverage.toggle_initial_unknown = array<i8: -128, 3>
// CHECK-SAME: obelisk.coverage.toggle_initial_value = array<i8: -128, 0>
// CHECK-SAME: obelisk.execution.coverage_schema_blob = array<i8:
// CHECK-DAG: obelisk_sim.storage.decl 0 {{.*}}hierarchy "top.data"{{.*}}obelisk.coverage.toggle_bindings = [{base = 0 : i64, low = 0 : i64, width = 4 : i64}, {base = 4 : i64, low = 1 : i64, width = 2 : i64}]{{.*}}obelisk.coverage.toggle_observable
// CHECK-DAG: obelisk_sim.storage.decl 1 {{.*}}hierarchy "top.logic"{{.*}}obelisk.coverage.toggle_bindings = [{base = 8 : i64, low = 0 : i64, width = 2 : i64}]{{.*}}obelisk.coverage.toggle_observable
// CHECK-DAG: obelisk_sim.net.decl 0 {{.*}}hierarchy "top.net"{{.*}}obelisk.coverage.toggle_bindings = [{base = 7 : i64, low = 0 : i64, width = 1 : i64}]{{.*}}obelisk.coverage.toggle_observable
// CHECK-DAG: obelisk_sim.storage.decl 2 in 1 : i1 design hierarchy "top.excluded" {{.*}}source_range
// CHECK-DAG: obelisk_sim.storage.decl 3 in 1 : i3 automatic hierarchy "top.automatic" {{.*}}source_range
// CHECK-DAG: obelisk_sim.storage.decl 4 in 1 : f64 design hierarchy "top.real" {{.*}}source_range
// CHECK-DAG: obelisk_sim.storage.decl 5 in 2 : i5 design hierarchy "C.static" {{.*}}source_range
// CHECK-DAG: obelisk_sim.storage.decl 6 in 1 : i7 design hierarchy "top.__timer" {{.*}}source_range
