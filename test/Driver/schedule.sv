// RUN: obelisk -emit-schedule --threads=2 --vpi=read %s | FileCheck %s
// RUN: obelisk -emit-sim --vpi=full %s | FileCheck %s --check-prefix=FULL-VPI
// RUN: obelisk -emit-schedule --compile-threads=1 %s > %t.one
// RUN: obelisk -emit-schedule --compile-threads=4 %s > %t.many
// RUN: diff %t.one %t.many
// RUN: not obelisk -emit-schedule --threads=0 %s 2>&1 | FileCheck %s --check-prefix=BAD-THREADS
// RUN: not obelisk -emit-schedule --compile-threads=0 %s 2>&1 | FileCheck %s --check-prefix=BAD-COMPILE-THREADS
// RUN: not obelisk -emit-schedule --vpi=write %s 2>&1 | FileCheck %s --check-prefix=BAD-VPI

module schedule_smoke;
  logic source;
  logic destination;
  always_comb destination = source;
endmodule

// CHECK: schedule @design #obelisk_sim.graph<version = 1, vpi = read, workers = 2
// CHECK-SAME: nodes = [#obelisk_sim.fragment<
// CHECK-SAME: lane = 0
// CHECK-SAME: edges = [
// CHECK-SAME: kind = process_order
// CHECK-SAME: #obelisk_sim.region<kind = active
// CHECK-SAME: #obelisk_sim.region<kind = nba
// CHECK-SAME: #obelisk_sim.region<kind = observed
// CHECK-SAME: #obelisk_sim.region<kind = reactive
// CHECK-SAME: #obelisk_sim.region<kind = postponed

// BAD-THREADS: error: --threads must be greater than zero
// BAD-COMPILE-THREADS: error: --compile-threads must be greater than zero
// BAD-VPI: error: unsupported VPI mode 'write'; expected off, read, or full
// FULL-VPI: obelisk_sim.storage.decl
// FULL-VPI-SAME: observability = 2 : i32
