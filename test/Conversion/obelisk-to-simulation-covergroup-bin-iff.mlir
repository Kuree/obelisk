// RUN: %split-file %s %t
// RUN: obelisk -emit-slang %t/input.sv -o - | FileCheck %s --check-prefix=SLANG
// RUN: obelisk -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: FileCheck %s --check-prefix=OBELISK < %t/input.mlir
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=SIM
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | %python %S/Inputs/dump-coverage-schema.py > %t/schema.txt
// RUN: FileCheck %s --check-prefix=SCHEMA < %t/schema.txt
// RUN: obelisk -emit-obelisk %t/layout.sv -o - | FileCheck %s --check-prefix=LAYOUT

// The frontend preserves the semantic role of every direct bin child. Value
// expressions cannot be confused with iff expressions, and future transition
// lowering retains its original set/range structure.
// SLANG: slang.symbol.coverage_bin
// SLANG-SAME: child_roles = array<i64: 0, 5>
// SLANG-SAME: transition_range_item_counts = array<i64>
// SLANG-SAME: transition_set_range_counts = array<i64>
// OBELISK: obelisk.sv.symbol.coverage_bin
// OBELISK-SAME: child_roles = array<i64: 0, 5>
// LAYOUT: obelisk.sv.symbol.coverage_bin
// LAYOUT-SAME: child_roles = array<i64: 0, 1, 3, 5>
// LAYOUT: obelisk.sv.symbol.coverage_bin
// LAYOUT-SAME: child_roles = array<i64: 0, 2>
// LAYOUT: obelisk.sv.symbol.coverage_bin
// LAYOUT-SAME: child_roles = array<i64: 0, 6, 6, 7, 8>
// LAYOUT-SAME: transition_range_has_repeat_from = array<i64: 0, 1>
// LAYOUT-SAME: transition_range_has_repeat_to = array<i64: 0, 1>
// LAYOUT-SAME: transition_range_item_counts = array<i64: 1, 1>
// LAYOUT-SAME: transition_range_repeat_kinds = array<i64: 0, 1>
// LAYOUT-SAME: transition_set_range_counts = array<i64: 2>
// LAYOUT: obelisk.sv.symbol.coverage_bin
// Clause 19.5 evaluates the coverpoint value and iff conditions when the
// group is sampled. The typed helper evaluates each retained schema expression
// once and sends the values in FunctionalExpression result-ordinal order;
// bin matching and exclusion precedence belong to the runtime plan.
// SIM: obelisk_sim.covergroup.decl @[[DECL:__obelisk_covergroup_.*]] schema {{[1-9][0-9]*}}
// SIM: %[[ENABLED:.*]] = obelisk_sim.covergroup.sample_enabled
// SIM-NEXT: cf.cond_br %[[ENABLED]], ^[[GROUP:.*]], ^{{.*}}
// SIM: ^[[GROUP]]:
// SIM: %[[SAMPLED:.*]] = obelisk_sim.ref.load
// SIM-NEXT: %[[PACKED:.*]] = obelisk_sim.packed.flatten %[[SAMPLED]]
// SIM-NEXT: %[[POINT:.*]] = obelisk_sim.ref.load
// SIM-NEXT: %[[POINT_I1:.*]] = obelisk_sim.logic.is_true %[[POINT]]
// SIM-NEXT: %[[ORDINARY:.*]] = obelisk_sim.ref.load
// SIM-NEXT: %[[ORDINARY_I1:.*]] = obelisk_sim.logic.is_true %[[ORDINARY]]
// SIM-NEXT: %[[IGNORE:.*]] = obelisk_sim.ref.load
// SIM-NEXT: %[[IGNORE_I1:.*]] = obelisk_sim.logic.is_true %[[IGNORE]]
// SIM-NEXT: %[[ILLEGAL:.*]] = obelisk_sim.ref.load
// SIM-NEXT: %[[ILLEGAL_I1:.*]] = obelisk_sim.logic.is_true %[[ILLEGAL]]
// SIM-NEXT: %[[DEFAULT:.*]] = obelisk_sim.ref.load
// SIM-NEXT: %[[DEFAULT_I1:.*]] = obelisk_sim.logic.is_true %[[DEFAULT]]
// SIM-NEXT: obelisk_sim.covergroup.sample {{.*}} values[%[[PACKED]], %[[POINT_I1]], %[[ORDINARY_I1]], %[[IGNORE_I1]], %[[ILLEGAL_I1]], %[[DEFAULT_I1]]] ids [{{[1-9][0-9]*}}, {{[1-9][0-9]*}}, {{[1-9][0-9]*}}, {{[1-9][0-9]*}}, {{[1-9][0-9]*}}, {{[1-9][0-9]*}}]
// SIM-NOT: obelisk_sim.logic.compare
// SIM-NOT: obelisk_sim.error
// Every bin role has one typed plan. Ordinary, ignore, and illegal value bins
// retain deferred constructor value atoms; a default bin has no value set but
// keeps its sample-phase iff identity.
// SCHEMA-DAG: functional_bin id=[[ORDINARY:[1-9][0-9]*]] {{.*}} name=ordinary kind=1 flags=0
// SCHEMA-DAG: functional_bin id=[[IGNORE:[1-9][0-9]*]] {{.*}} name=ignored kind=1 flags=4
// SCHEMA-DAG: functional_bin id=[[ILLEGAL:[1-9][0-9]*]] {{.*}} name=illegal kind=1 flags=8
// SCHEMA-DAG: functional_bin id=[[DEFAULT:[1-9][0-9]*]] {{.*}} name=bad_default kind=1 flags=9
// SCHEMA-COUNT-3: functional_value_atom set={{[1-9][0-9]*}} ordinal=0 kind=1 flags=3 lower_expression={{[1-9][0-9]*}} upper_expression=0
// SCHEMA-DAG: functional_bin_plan bin=[[ORDINARY]] value_set={{[1-9][0-9]*}} iff_expression={{[1-9][0-9]*}}
// SCHEMA-DAG: functional_bin_plan bin=[[IGNORE]] value_set={{[1-9][0-9]*}} iff_expression={{[1-9][0-9]*}}
// SCHEMA-DAG: functional_bin_plan bin=[[ILLEGAL]] value_set={{[1-9][0-9]*}} iff_expression={{[1-9][0-9]*}}
// SCHEMA-DAG: functional_bin_plan bin=[[DEFAULT]] value_set=0 iff_expression={{[1-9][0-9]*}}

//--- input.sv
module bin_iff;
  logic [1:0] sampled;
  logic point_on, ordinary_on, ignore_on, illegal_on, default_on;

  covergroup cg;
    cp: coverpoint sampled iff (point_on) {
      bins ordinary = {0} iff (ordinary_on);
      ignore_bins ignored = {0} iff (ignore_on);
      illegal_bins illegal = {0} iff (illegal_on);
      illegal_bins bad_default = default iff (default_on);
    }
  endgroup

  cg c;
  initial begin
    c = new;
    c.sample();
  end
endmodule

//--- layout.sv
module bin_child_layout;
  bit [3:0] sampled;
  bit [3:0] value_set [2] = '{1, 2};
  bit enabled;

  covergroup cg;
    cp: coverpoint sampled {
      bins array[2] = {[0:3]} with (item != 1) iff (enabled);
      bins set_values = value_set iff (enabled);
      bins transition = (1 => 2[*3:4]) iff (enabled);
    }
    cross cp, sampled {
      bins selected = binsof(cp) iff (enabled);
    }
  endgroup
endmodule
