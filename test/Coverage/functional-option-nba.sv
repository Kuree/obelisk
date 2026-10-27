// RUN: %talos --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: talos-cov report --format=json %t.obcov | FileCheck %s --check-prefix=REPORT

// IEEE 10.4.2, 19.7 and 19.10. One configuration, two instances and two
// event occurrences exercise whole fields, clipped masks, strings, delayed
// captures and FIFO updates. Scaling experiments belong outside this test.
module top;
  covergroup cg with function sample(bit value);
    option.per_instance = 1;
    option.get_inst_coverage = 1;
    type_option.merge_instances = 1;
    cp: coverpoint value { bins one = {1}; }
    other: coverpoint value { bins zero = {0}; }
    cx: cross cp, other {
      option.weight = 0;
      type_option.weight = 0;
    }
  endgroup
  cg a, b, current;
  string source_text = "captured rhs string";
  int calls, indices, selected_index, repetitions, ordinary;
  int covered, total;
  real point_percentage;
  logic [31:0] unknown_index = 'x;
  event pulse;
  function automatic cg receiver();
    calls++;
    return current;
  endfunction
  function automatic int index_once();
    indices++;
    return selected_index;
  endfunction
  initial begin
    // Static owner validation and publication work before any construction.
    cg::type_option.weight <= 2;
    cg::type_option.goal <= 64'h100000050;
    cg::type_option.merge_instances <= 0;
    cg::type_option.distribute_first <= 1;
    cg::type_option.comment <= "type nba";
    cg::type_option.comment[0] <= "T";
    cg::type_option.comment[unknown_index] <= "X";
    cg::cp::type_option.weight <= 2;
    cg::cp::type_option.goal <= 50;
    cg::cp::type_option.comment <= "point type";
    cg::cx::type_option.goal <= 25;
    cg::cx::type_option.comment <= "cross type";
    $display("type-pending %0d %0d %0d", cg::type_option.weight,
             cg::type_option.goal, cg::type_option.merge_instances);
    a = new;
    b = new;
    a.sample(0);
    ordinary <= 1;
    a.option.weight <= 16;
    a.option.weight[3:0] <= 3;
    a.option.weight[1] <= 0;
    {a.option.weight[3:0], a.option.weight[3:0]} <= 8'hab;
    a.option.weight[-2 +: 4] <= 4'b1000;
    a.option.weight[31 +: 4] <= 0;
    a.option.weight[100 +: 4] <= 15;
    a.option.weight[unknown_index] <= 1;
    ordinary <= 2;
    a.option.goal <= 64'h100000050;
    a.option.at_least <= 2;
    a.cp.option.weight <= 3;
    a.cp.option.goal <= 50;
    a.cp.option.at_least <= 1;
    a.cx.option.at_least <= 3;
    a.option.cross_num_print_missing <= 2;
    a.cx.option.cross_num_print_missing <= 4;
    a.option.name <= "captured name";
    a.option.name[0] <= "N";
    a.option.name[1] <= "B";
    a.option.comment <= source_text;
    a.option.comment[0] <= "C";
    a.option.comment[-1] <= "X";
    a.option.comment[100] <= "X";
    a.option.comment[unknown_index] <= "X";
    a.option.comment[0] <= 0;
    source_text = "changed rhs";
    a.cp.option.comment <= "point nba";
    a.cx.option.comment <= "cross nba";
    #0 $display("inactive %0d %0d %0d", a.option.weight,
                cg::type_option.weight, ordinary);
    #1;
    a.option.comment[unknown_index] = "X";
    cg::type_option.comment[unknown_index] = "X";
    $display("committed %0d %0d %0d %0d %0d", a.option.weight,
             a.option.goal, a.cp.option.weight, a.cp.option.at_least, ordinary);
    $display("type %0d %0d %0d %0d %s", cg::type_option.weight,
             cg::type_option.goal, cg::type_option.merge_instances,
             cg::type_option.distribute_first, cg::type_option.comment);
    $display("strings %s|%s|%s", a.option.name, a.option.comment,
             a.cp.option.comment);
    $display("owners %0d %0d %0d %0d", a.option.at_least,
             a.cx.option.at_least, a.cx.option.cross_num_print_missing,
             b.option.weight);
    a.sample(1);
    point_percentage = a.cp.get_inst_coverage(covered, total);
    $display("point %.0f %0d %0d", point_percentage,
             covered, total);

    current = a;
    a.option.weight = 6;
    receiver().option.weight[index_once()] <= #2 1;
    current = b;
    selected_index = 5;
    #1 $display("delay-pending %0d %0d", a.option.weight, b.option.weight);
    #2 $display("delay-captured %0d %0d calls=%0d indices=%0d",
                a.option.weight, b.option.weight, calls, indices);

    current = a;
    selected_index = 1;
    receiver().option.comment[index_once()] <= @(pulse) "V";
    source_text = "event point";
    a.cp.option.comment <= @(pulse) source_text;
    repetitions = 2;
    receiver().option.weight <= repeat(repetitions) @(pulse) 9;
    current = b;
    selected_index = 0;
    repetitions = 0;
    source_text = "changed again";
    a.option.comment = "changed";
    #1 -> pulse;
    #1 $display("event-captured %s|%s weight=%0d", a.option.comment,
                a.cp.option.comment, a.option.weight);
    -> pulse;
    #1 $display("repeat-captured %0d %0d calls=%0d indices=%0d",
                a.option.weight, b.option.weight, calls, indices);
    $finish;
  end
endmodule

// CHECK: type-pending 1 100 1
// CHECK-NEXT: inactive 1 1 0
// CHECK-NEXT: committed 26 80 3 1 2
// CHECK-NEXT: type 2 80 0 1 Type nba
// CHECK-NEXT: strings NBptured name|Captured rhs string|point nba
// CHECK-NEXT: owners 2 3 4 1
// CHECK-NEXT: point 100 1 1
// CHECK-NEXT: delay-pending 6 1
// CHECK-NEXT: delay-captured 7 1 calls=1 indices=1
// CHECK-NEXT: event-captured cVanged|event point weight=7
// CHECK-NEXT: repeat-captured 9 1 calls=3 indices=2

// The database stores the values the NBAs committed: the type's options and
// run-local distribute_first, one shared configuration, and the captured
// instance, point and cross options over the type-level item options. Items
// within an instance follow resolved identities.
// REPORT: "functional_types":[{"id":{{[0-9]+}},"scope":{{[0-9]+}},"name":"cg",{{[^[]*}}"comment":"Type nba",{{[^[]*}}"goal":80,"weight":2,"merge_instances":false,"distribute_first_overrides":[{"run":"{{[^"]+}}","value":true}]}]
// REPORT: "functional_configurations":[{"type":{{[0-9]+}},"configuration":"{{[0-9a-f]+}}","flags":0,"options":[{{[^]]*}}]}],
// REPORT: "functional_instance_groups":[
// REPORT-DAG: {"type":{{[0-9]+}},"name":"NBptured name","configuration":"{{[0-9a-f]+}}","comment":"cVanged","goal":80,"weight":9,{{[^[]*}}"cross_num_print_missing":2,
// REPORT-DAG: "name":"cp","comment":"event point",{{[^[]*}}"weight":3,"type_goal":50,"type_weight":2,"at_least":1,{{[^[]*}}"type_comment":"point type",
// REPORT-DAG: "name":"cx","comment":"cross nba",{{[^[]*}}"type_goal":25,{{[^[]*}}"type_comment":"cross type",{{[^[]*}}"automatic_at_least":3,"cross_num_print_missing":4,
// REPORT: "functional_counters":
