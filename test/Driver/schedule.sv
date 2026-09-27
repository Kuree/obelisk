// RUN: obelisk -emit-schedule --threads=2 --vpi=read %s | FileCheck %s
// RUN: obelisk -emit-sim --vpi=full %s | FileCheck %s --check-prefix=FULL-VPI
// RUN: obelisk -emit-schedule --compile-threads=1 %s > %t.one
// RUN: obelisk -emit-schedule --compile-threads=4 %s > %t.many
// RUN: diff %t.one %t.many
// RUN: obelisk -emit-schedule --mlir-print-debuginfo %s | FileCheck %s --check-prefix=LOC
// RUN: not obelisk -emit-schedule --threads=0 %s 2>&1 | FileCheck %s --check-prefix=BAD-THREADS
// RUN: not obelisk -emit-schedule --compile-threads=0 %s 2>&1 | FileCheck %s --check-prefix=BAD-COMPILE-THREADS
// RUN: not obelisk -emit-schedule --vpi=write %s 2>&1 | FileCheck %s --check-prefix=BAD-VPI

module schedule_smoke;
  logic source;
  logic destination;
  always_comb destination = source;
endmodule

// CHECK: schedule @design #schedule.graph<version = 1, vpi = read, workers = 2
// CHECK-SAME: nodes = [#schedule.fragment<
// CHECK-SAME: lane = 0
// CHECK-SAME: edges = [
// CHECK-SAME: kind = process_order
// CHECK-SAME: #schedule.region<kind = active
// CHECK-SAME: #schedule.region<kind = nba
// CHECK-SAME: #schedule.region<kind = observed
// CHECK-SAME: #schedule.region<kind = reactive
// CHECK-SAME: #schedule.region<kind = postponed

// LOC: schedule @design #schedule.graph<
// LOC-SAME: source_locations = [
// LOC-SAME: #1 = "{{.*schedule.sv}}":14:3

// BAD-THREADS: error: --threads must be greater than zero
// BAD-COMPILE-THREADS: error: --compile-threads must be greater than zero
// BAD-VPI: error: unsupported VPI mode 'write'; expected off, read, or full
// FULL-VPI: obelisk_sim.storage.decl
// FULL-VPI-SAME: observability = 2 : i32
