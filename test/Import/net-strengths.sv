// RUN: obelisk -emit-slang %s 2>/dev/null | FileCheck %s --check-prefix=SLANG
// RUN: obelisk -emit-obelisk %s 2>/dev/null | FileCheck %s --check-prefix=OBELISK

module net_strengths;
  wire (pull0, weak1) driven = 1'b1;
  wire assigned;
  assign (weak0, pull1) assigned = 1'b1;
  trireg (large) charged;
endmodule

// This is an importer-boundary test. Execution semantics are covered from
// semantic and simulation MLIR without an end-to-end SystemVerilog test.
// SLANG-DAG: slang.symbol.net attributes {{.*}}drive_strength0 = 2 : i32{{.*}}drive_strength1 = 3 : i32{{.*}}name = "driven"
// SLANG-DAG: slang.symbol.continuous_assign attributes {{.*}}drive_strength0 = 3 : i32{{.*}}drive_strength1 = 2 : i32
// SLANG-DAG: slang.symbol.net attributes {{.*}}charge_strength = 2 : i32{{.*}}name = "charged"
// OBELISK-DAG: obelisk.sv.symbol.net attributes {{.*}}drive_strength0 = 2 : i32{{.*}}drive_strength1 = 3 : i32{{.*}}name = "driven"
// OBELISK-DAG: obelisk.sv.symbol.continuous_assign attributes {{.*}}drive_strength0 = 3 : i32{{.*}}drive_strength1 = 2 : i32
// OBELISK-DAG: obelisk.sv.symbol.net attributes {{.*}}charge_strength = 2 : i32{{.*}}name = "charged"
