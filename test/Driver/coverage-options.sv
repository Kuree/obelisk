// RUN: %obelisk --coverage=line,toggle \
// RUN:   --coverage-config=%S/../Inputs/coverage-config.json \
// RUN:   --coverage-prefix-map=%S=src -emit-slang %s | FileCheck %s
// RUN: %obelisk --coverage=line -emit-sim %s | FileCheck %s --check-prefix=SIM
// RUN: not %obelisk --coverage=line,line -emit-slang %s 2>&1 | FileCheck %s --check-prefix=DUPLICATE
// RUN: not %obelisk --coverage-prefix-map=missing-equals -emit-slang %s 2>&1 | FileCheck %s --check-prefix=MAP

module top;
endmodule

// CHECK: module attributes
// CHECK-SAME: coverage.config
// CHECK-SAME: coverage.metrics = ["line", "toggle"]
// CHECK-SAME: coverage.prefix_maps
// SIM: module attributes
// SIM-SAME: coverage.metrics = ["line"]
// SIM-SAME: runtime.coverage_schema_blob = array<i8:
// DUPLICATE: invalid --coverage metric list 'line,line'
// MAP: invalid --coverage-prefix-map 'missing-equals'
