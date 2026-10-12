// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s --check-prefix=READ
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=REPORT
// RUN: obelisk-cov inspect %t.obcov | FileCheck %s --check-prefix=INSPECT

// IEEE 19.7 defaults apply to lower owners except weight, goal, comment and
// per_instance. IEEE 19.10 includes construction-only fields in option structs.
// Two constructor values and one default configuration keep this probe small.
class Base;
  covergroup group(input int count, input string tag) with function sample(bit value);
    option.name = {"constructed-", tag};
    option.comment = {"group-", tag};
    option.weight = 2;
    option.goal = 80;
    option.at_least = 2;
    option.auto_bin_max = count;
    option.detect_overlap = 1;
    option.cross_num_print_missing = 4;
    option.cross_retain_auto_bins = 0;
    option.per_instance = 1;
    option.get_inst_coverage = 1;
    cp: coverpoint value { option.comment = {"base-", tag}; }
    q: coverpoint value;
    cx: cross cp, q;
  endgroup
  function new(int count, string tag);
    group = new(count, tag);
  endfunction
endclass

class Derived extends Base;
  covergroup extends group;
    option.comment = "derived-group";
    cp: coverpoint value {
      option.comment = "derived-point";
      option.weight = 4;
      option.goal = 60;
      option.at_least = 3;
      option.auto_bin_max = 2;
      option.detect_overlap = 0;
    }
    cx: cross cp, q {
      option.comment = "derived-cross";
      option.cross_num_print_missing = 2;
      option.cross_retain_auto_bins = 1;
    }
  endgroup : group
  function new(int count, string tag);
    super.new(count, tag);
  endfunction
endclass

module top;
  bit value;
  covergroup plain;
    cp: coverpoint value;
    q: coverpoint value;
    cx: cross cp, q;
  endgroup
  plain default_a, default_b;
  Derived object, second;
  Base base_view;
  int calls;
  string name_snapshot, comment_snapshot;
  function Derived receiver();
    calls++;
    return object;
  endfunction
  initial begin
    default_a = new;
    default_b = new;
    $display("defaults %0d %0d %0d %0d %0d %0d %0d %0d %0d",
             default_a.option.weight, default_a.option.goal,
             default_a.option.at_least, default_a.option.auto_bin_max,
             default_a.option.cross_num_print_missing,
             default_a.option.cross_retain_auto_bins,
             default_a.option.detect_overlap, default_a.option.per_instance,
             default_a.option.get_inst_coverage);
    $display("default-strings %0d %0d %0d %0d",
             default_a.option.name != default_b.option.name,
             default_a.option.comment.len(), default_a.cp.option.comment.len(),
             default_a.cx.option.comment.len());
    object = new(5, "first");
    second = new(6, "second");
    base_view = object;
    $display("configured %s %s %0d %0d %0d %0d %0d %0d %0d %0d %0d",
             object.group.option.name, object.group.option.comment,
             object.group.option.weight, object.group.option.goal,
             object.group.option.at_least, object.group.option.auto_bin_max,
             object.group.option.cross_num_print_missing,
             object.group.option.cross_retain_auto_bins,
             object.group.option.detect_overlap, object.group.option.per_instance,
             object.group.option.get_inst_coverage);
    $display("point %s %0d %0d %0d %0d %0d", object.group.cp.option.comment,
             object.group.cp.option.weight, object.group.cp.option.goal,
             object.group.cp.option.at_least, object.group.cp.option.auto_bin_max,
             object.group.cp.option.detect_overlap);
    $display("inherited %0d %0d %0d %0d %0d %0d", object.group.q.option.comment.len(),
             object.group.q.option.weight, object.group.q.option.goal,
             object.group.q.option.at_least, object.group.q.option.auto_bin_max,
             object.group.q.option.detect_overlap);
    $display("cross %s %0d %0d %0d %0d %0d", object.group.cx.option.comment,
             object.group.cx.option.weight, object.group.cx.option.goal,
             object.group.cx.option.at_least,
             object.group.cx.option.cross_num_print_missing,
             object.group.cx.option.cross_retain_auto_bins);
    $display("base-view %s %s", base_view.group.option.comment,
             base_view.group.cp.option.comment);
    name_snapshot = receiver().group.option.name;
    comment_snapshot = receiver().group.cp.option.comment;
    receiver().group.option.comment = "edited-group";
    receiver().group.cp.option.comment = "edited-point";
    object.group.set_inst_name("renamed");
    $display("snapshots %s %s calls=%0d", name_snapshot, comment_snapshot, calls);
    $display("edited %s %s %s", object.group.option.name,
             object.group.option.comment, object.group.cp.option.comment);
    $display("independent %s %s %s %0d", second.group.option.name,
             second.group.option.comment, second.group.cp.option.comment,
             second.group.q.option.auto_bin_max);
    object.group.sample(1);
    second.group.sample(1);
    $finish;
  end
endmodule

// READ: defaults 1 100 1 64 0 1 0 0 0
// READ-NEXT: default-strings 1 0 0 0
// READ-NEXT: configured constructed-first derived-group 2 80 2 5 4 0 1 1 1
// READ-NEXT: point derived-point 4 60 3 2 0
// READ-NEXT: inherited 0 1 100 2 5 1
// READ-NEXT: cross derived-cross 1 100 2 2 1
// READ-NEXT: base-view derived-group derived-point
// READ-NEXT: snapshots constructed-first derived-point calls=4
// READ-NEXT: edited renamed edited-group edited-point
// READ-NEXT: independent constructed-second derived-group derived-point 6

// The database keeps each instance's final names and comments (19.10), and the
// two edited derived instances resolve to distinct configurations.
// Item order within an instance follows resolved identities.
// REPORT: {"type":{{[0-9]+}},"name":"renamed","configuration":"{{[0-9a-f]+}}","comment":"edited-group",{{[^{]*}}"per_instance":true,
// REPORT-DAG: "name":"cp","comment":"edited-point",
// REPORT-DAG: "name":"q","comment":"",
// REPORT-DAG: "name":"cx","comment":"derived-cross",
// REPORT: {"type":{{[0-9]+}},"name":"constructed-second","configuration":"{{[0-9a-f]+}}","comment":"derived-group",{{[^{]*}}"per_instance":true,
// REPORT-DAG: "name":"cp","comment":"derived-point",
// REPORT-DAG: "name":"q","comment":"",
// REPORT-DAG: "name":"cx","comment":"derived-cross",
// INSPECT: functional-configurations: 3
// INSPECT: resolved-instances: 4
