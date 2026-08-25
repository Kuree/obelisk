// Bind provenance is useful independently of configuration selection. The
// target, inserted instance, and inserted descendants are distinguished.
// RUN: obelisk -emit-bindings %s \
// RUN:   | FileCheck %s --check-prefix=REPORT
// RUN: obelisk -emit-slang -emit-bindings %s \
// RUN:   | FileCheck %s --check-prefix=REPORT
// RUN: obelisk -emit-bindings -emit-slang %s \
// RUN:   | FileCheck %s --check-prefix=LAST-SLANG
// RUN: obelisk -emit-obelisk %s \
// RUN:   | FileCheck %s --check-prefix=OBELISK \
// RUN:       --implicit-check-not=is_from_bind \
// RUN:       --implicit-check-not=is_below_bind \
// RUN:       --implicit-check-not=selected_cell

module child;
endmodule

module probe;
  child nested();
endmodule

module target;
endmodule

checker nested_checker();
endchecker

checker bound_checker();
  nested_checker nested();
endchecker

module bind_bindings;
  target selected();
endmodule

bind target probe inserted();
bind target bound_checker inserted_checker();

// REPORT: binding bind_bindings.selected -> work.target bind-target
// REPORT-NEXT: binding bind_bindings.selected.inserted -> work.probe from-bind
// REPORT-NEXT: binding bind_bindings.selected.inserted.nested -> work.child below-bind
// REPORT-NEXT: binding bind_bindings.selected.inserted_checker -> work.bound_checker from-bind
// REPORT-NEXT: binding bind_bindings.selected.inserted_checker.nested -> work.nested_checker below-bind

// LAST-SLANG: slang.symbol.checker_instance attributes
// LAST-SLANG-SAME: is_from_bind = true
// LAST-SLANG-SAME: selected_cell = "work.bound_checker"
// LAST-SLANG: slang.symbol.checker_instance attributes
// LAST-SLANG-SAME: is_below_bind = true
// LAST-SLANG-SAME: selected_cell = "work.nested_checker"

// The checker inventory survives semantic conversion, while opt-in report
// provenance does not enter executable IR.
// OBELISK: obelisk.sv.symbol.checker_instance
// OBELISK: obelisk.sv.symbol.checker_instance
