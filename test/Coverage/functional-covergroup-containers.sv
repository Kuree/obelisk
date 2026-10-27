// RUN: talos -emit-slang %s | talos-opt --convert-slang-to-talos --lower-talos-to-sim=opt-level=0 | FileCheck %s --check-prefix=LOWER
// RUN: %talos --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: talos-cov report --format=json %t.obcov | FileCheck %s --check-prefix=REPORT

// IEEE 1800-2023 7.5, 7.6, 7.8, 7.10, 19.3, and 19.8: copying a
// container copies its covergroup handles, not the coverage instances. Typed
// mailboxes (15.4) transfer the same handle without a managed-heap root.
module top;
  covergroup cg(input int key) with function sample(input int value);
    option.per_instance = 1;
    cp: coverpoint value { bins selected = {key}; }
  endgroup
  cg dynamic_groups[], dynamic_copy[];
  cg queued_groups[$], queue_copy[$];
  cg associative_groups[int], associative_copy[int];
  cg popped;
  mailbox #(cg) messages;
  int covered, total;
  real result;

  initial begin
    dynamic_groups = new[3];
    for (int i = 0; i < 3; ++i)
      dynamic_groups[i] = new(i);
    dynamic_copy = dynamic_groups;
    dynamic_groups = new[5](dynamic_groups);
    dynamic_groups[0] = dynamic_groups[1];
    dynamic_copy[0].sample(0);
    dynamic_copy[0].sample(0);
    dynamic_copy[1].sample(1);
    dynamic_groups[0].sample(1);
    dynamic_copy[2].sample(2);
    dynamic_groups[2].sample(2);
    $display("DYNAMIC %0d %0d", dynamic_groups.size(), dynamic_copy.size());
    dynamic_groups.delete();
    dynamic_copy.delete();

    popped = new(3);
    queued_groups.push_back(popped);
    popped = new(4);
    queued_groups.push_front(popped);
    queue_copy = queued_groups;
    popped = queued_groups.pop_front();
    popped.sample(4);
    popped = queued_groups.pop_back();
    popped.sample(3);
    queue_copy[0].sample(4);
    queue_copy[1].sample(3);
    $display("QUEUE %0d %0d", queued_groups.size(), queue_copy.size());
    queue_copy.delete();

    associative_groups[-19] = new(5);
    associative_copy = associative_groups;
    associative_groups.delete(-19);
    associative_copy[-19].sample(5);
    associative_copy[-19].sample(5);
    $display("ASSOCIATIVE %0d %0d %0d", associative_groups.num(),
             associative_copy.num(), associative_copy.exists(-19));
    associative_copy.delete();

    messages = new();
    popped = new(6);
    messages.put(popped);
    messages.get(popped);
    popped.sample(6);
    messages.put(popped);
    messages.get(popped);
    popped.sample(6);
    $display("MAILBOX %0d", messages.num());

    result = cg::get_coverage(covered, total);
    $display("COVERAGE %.0f %0d %0d", result, covered, total);
    $display("DELETED %0d %0d %0d %0d", dynamic_groups.size(),
             dynamic_copy.size(), queue_copy.size(), associative_copy.num());
    $finish;
  end
endmodule

// Containers and mailboxes hold a covergroup handle as its 64-bit runtime
// identity: plain bits with no four-state plane and no traced managed root.
// LOWER: simulation.container.create {{.*}}bit_width = 64 : i64, bound = 0 : i64, container_kind = #simulation.container_kind<dynamic_array>, element_flags = #simulation.element_flags<none>, element_kind = #simulation.element_kind<bits>, trace_kinds = array<i32>, trace_offsets = array<i64>, {{.*}}value_size = 8 : i64} : (i64) -> !simulation.dynamic_array<!simulation.covergroup_handle<
// LOWER: simulation.container.create {{.*}}bit_width = 64 : i64, bound = -1 : i64, container_kind = #simulation.container_kind<queue>, element_flags = #simulation.element_flags<none>, element_kind = #simulation.element_kind<bits>, trace_kinds = array<i32>, trace_offsets = array<i64>, {{.*}}value_size = 8 : i64} : (i64) -> !simulation.queue<!simulation.covergroup_handle<
// LOWER: simulation.assoc.create {{.*}}bit_width = 64 : i64, element_flags = #simulation.element_flags<none>, element_kind = #simulation.element_kind<bits>, key_kind = #simulation.assoc_key_kind<signed>, key_width = 32 : i64, trace_kinds = array<i32>, trace_offsets = array<i64>, {{.*}}value_size = 8 : i64} : () -> !simulation.assoc_array<i32, !simulation.covergroup_handle<
// LOWER: simulation.mailbox.create {{.*}}bit_width = 64 : i64, element_flags = #simulation.element_flags<none>, element_kind = #simulation.element_kind<bits>, trace_kinds = array<i32>, trace_offsets = array<i64>, {{.*}}value_size = 8 : i64} : (i64) -> !simulation.mailbox<!simulation.covergroup_handle<

// CHECK: DYNAMIC 5 3
// CHECK: QUEUE 0 2
// CHECK: ASSOCIATIVE 0 1 1
// CHECK: MAILBOX 0
// CHECK: COVERAGE 100 7 7
// CHECK: DELETED 0 0 0 0

// Every copy sampled the instance it was copied from: seven instances with two
// hits each, not fourteen instances with one.
// REPORT: "functional":{"available":true,"covered":7,"total":7,
// REPORT: "functional_counters":[{"run":"{{[^"]+}}","instance":1,"bin":{{[0-9]+}},"count":2,"overflow":false}
// REPORT-SAME: ,{"run":"{{[^"]+}}","instance":2,"bin":{{[0-9]+}},"count":2,"overflow":false}
// REPORT-SAME: ,{"run":"{{[^"]+}}","instance":3,"bin":{{[0-9]+}},"count":2,"overflow":false}
// REPORT-SAME: ,{"run":"{{[^"]+}}","instance":4,"bin":{{[0-9]+}},"count":2,"overflow":false}
// REPORT-SAME: ,{"run":"{{[^"]+}}","instance":5,"bin":{{[0-9]+}},"count":2,"overflow":false}
// REPORT-SAME: ,{"run":"{{[^"]+}}","instance":6,"bin":{{[0-9]+}},"count":2,"overflow":false}
// REPORT-SAME: ,{"run":"{{[^"]+}}","instance":7,"bin":{{[0-9]+}},"count":2,"overflow":false}]
