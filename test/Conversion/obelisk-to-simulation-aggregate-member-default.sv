// RUN: obelisk -emit-obelisk %s | FileCheck %s --check-prefix=SEMANTIC
// RUN: obelisk -emit-sim %s | FileCheck %s --check-prefix=SIM

module aggregate_member_default;
  parameter bit [3:0] DEFAULT_LO = 4'h5;
  struct {
    bit [3:0] lo = DEFAULT_LO;
    bit [3:0] hi;
  } value;
endmodule

// SEMANTIC: obelisk.sv.symbol.variable
// SEMANTIC-SAME: obelisk.aggregate_member_initializer_ordinals
// SEMANTIC: obelisk.sv.expression.named_value

// SIM-LABEL: obelisk_sim.func @__obelisk_root
// SIM: obelisk_sim.call @unit_
// SIM: obelisk_sim.return
// SIM-LABEL: obelisk_sim.func private @unit_
// SIM: %[[FIELD:.*]] = obelisk_sim.ref.subelement %arg1{{.*}}0
// SIM: obelisk_sim.ref.store {{.*}} to %[[FIELD]]
