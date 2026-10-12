// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s --check-prefix=SNAPSHOT
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=REPORT
// RUN: obelisk-cov inspect %t.obcov | FileCheck %s --check-prefix=INSPECT

// IEEE 19.10 implicitly declares ordinary unpacked structures. Their copies
// retain all fields and strings, independently of later coverage updates.
class Owner;
  covergroup cg(input int count, input string tag) with function sample(bit value);
    option.name = tag;
    option.comment = {"group-", tag};
    option.weight = 2;
    option.goal = 80;
    option.at_least = 3;
    option.auto_bin_max = count;
    option.cross_num_print_missing = 4;
    option.cross_retain_auto_bins = 0;
    option.detect_overlap = 1;
    option.per_instance = 1;
    option.get_inst_coverage = 1;
    cp: coverpoint value {
      option.comment = {"point-", tag};
      option.weight = 5;
      option.goal = 60;
      option.at_least = 2;
      option.auto_bin_max = 2;
      option.detect_overlap = 0;
    }
    q: coverpoint value;
    cx: cross cp, q {
      option.comment = {"cross-", tag};
      option.weight = 6;
      option.goal = 50;
      option.at_least = 4;
      option.cross_num_print_missing = 2;
      option.cross_retain_auto_bins = 1;
    }
  endgroup
  function new(int count, string tag);
    cg = new(count, tag);
  endfunction
endclass

module top;
  Owner object, other;
  typedef type(object.cg.option) group_options_t;
  typedef type(object.cg.cp.option) point_options_t;
  typedef type(object.cg.cx.option) cross_options_t;
  typedef struct { group_options_t option; } wrapper_t;
  group_options_t group_snapshot, copies[];
  point_options_t point_snapshot;
  cross_options_t cross_snapshot;
  wrapper_t wrapped;
  int calls;
  function Owner receiver();
    calls++;
    return object;
  endfunction
  function automatic group_options_t copy_group(group_options_t input_options);
    input_options.name = "local";
    input_options.weight = 9;
    return input_options;
  endfunction
  initial begin
    object = new(5, "first");
    other = new(6, "second");
    group_snapshot = receiver().cg.option;
    point_snapshot = receiver().cg.cp.option;
    cross_snapshot = receiver().cg.cx.option;
    copies = new[2];
    copies[0] = group_snapshot;
    copies[1] = other.cg.option;
    wrapped.option = copy_group(object.cg.option);
    object.cg.option.name = "renamed";
    object.cg.option.comment = "edited-group";
    object.cg.option.weight = 7;
    object.cg.cp.option.comment = "edited-point";
    object.cg.cx.option.comment = "edited-cross";
    $display("group %s %s %0d %0d %0d %0d %0d %0d %0d %0d %0d calls=%0d",
             group_snapshot.name, group_snapshot.comment, group_snapshot.weight,
             group_snapshot.goal, group_snapshot.at_least, group_snapshot.auto_bin_max,
             group_snapshot.cross_num_print_missing, group_snapshot.cross_retain_auto_bins,
             group_snapshot.detect_overlap, group_snapshot.per_instance,
             group_snapshot.get_inst_coverage, calls);
    $display("point %s %0d %0d %0d %0d %0d", point_snapshot.comment,
             point_snapshot.weight, point_snapshot.goal, point_snapshot.at_least,
             point_snapshot.auto_bin_max, point_snapshot.detect_overlap);
    $display("cross %s %0d %0d %0d %0d %0d", cross_snapshot.comment,
             cross_snapshot.weight, cross_snapshot.goal, cross_snapshot.at_least,
             cross_snapshot.cross_num_print_missing, cross_snapshot.cross_retain_auto_bins);
    $display("container %s %s %0d %0d", copies[0].name, copies[1].name,
             copies[0].auto_bin_max, copies[1].auto_bin_max);
    $display("local %s %0d live %s %0d", wrapped.option.name, wrapped.option.weight,
             object.cg.option.name, object.cg.option.weight);
    $display("strings %s %s %s", object.cg.option.comment,
             object.cg.cp.option.comment, object.cg.cx.option.comment);
    object.cg.sample(1);
    other.cg.sample(0);
    $finish;
  end
endmodule

// SNAPSHOT: group first group-first 2 80 3 5 4 0 1 1 1 calls=3
// SNAPSHOT-NEXT: point point-first 5 60 2 2 0
// SNAPSHOT-NEXT: cross cross-first 6 50 4 2 1
// SNAPSHOT-NEXT: container first second 5 6
// SNAPSHOT-NEXT: local local 9 live renamed 7
// SNAPSHOT-NEXT: strings edited-group edited-point edited-cross

// Snapshots never change the live options the database persists. Item order
// within an instance follows resolved identities.
// REPORT: {"type":{{[0-9]+}},"name":"renamed","configuration":"{{[0-9a-f]+}}","comment":"edited-group","goal":{{[0-9]+}},"weight":7,
// REPORT-DAG: "name":"cp","comment":"edited-point",
// REPORT-DAG: "name":"cx","comment":"edited-cross",
// REPORT: {"type":{{[0-9]+}},"name":"second","configuration":"{{[0-9a-f]+}}","comment":"group-second","goal":{{[0-9]+}},"weight":2,
// REPORT-DAG: "name":"cp","comment":"point-second",
// REPORT-DAG: "name":"cx","comment":"cross-second",
// INSPECT: functional-configurations: 2
