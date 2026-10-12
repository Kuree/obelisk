// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s --check-prefix=QUERY
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=REPORT

// IEEE 11.3.6, 11.4.1-2, 19.7 and 19.10: mutable coverage members are
// assignment targets. Capture receivers/indices once and publish the value
// after conversion to the member's type, including static type profiles.
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
  typedef type(a.option) options_t;
  options_t snapshot;
  int calls, indices, prior_weight;
  function automatic cg receiver();
    calls++;
    return current;
  endfunction
  function automatic int swap_receiver();
    current = b;
    return 2;
  endfunction
  function automatic int index_once();
    indices++;
    return 0;
  endfunction
  initial begin
    a = new;
    b = new;
    a.sample(0);
    cg::type_option.weight *= 0;
    $display("zero-weight %.0f", $get_coverage());
    prior_weight = cg::type_option.weight++;
    $display("type-post %0d %0d %.0f", prior_weight,
             cg::type_option.weight, $get_coverage());
    cg::cp::type_option.weight *= 0;
    $display("point-zero %.0f", cg::get_coverage());
    prior_weight = cg::cp::type_option.weight++;
    $display("point-post %0d %0d %.0f", prior_weight,
             cg::cp::type_option.weight, cg::get_coverage());
    cg::type_option.merge_instances ^= 1;
    $display("unmerged %.0f", cg::get_coverage());
    $display("merge-pre %0d", ++cg::type_option.merge_instances);
    cg::type_option.distribute_first ^= 1;
    $display("bit-wrap %0d", ++cg::type_option.distribute_first);
    fork
      begin
        cg::type_option.weight += #2 2;
        $display("type-timed %0d", cg::type_option.weight);
      end
      begin
        #1 cg::type_option.weight = 10;
      end
    join
    cg::type_option.weight = 1;

    current = a;
    prior_weight = receiver().option.weight++;
    $display("instance-post %0d %0d calls=%0d", prior_weight,
             a.option.weight, calls);
    prior_weight = --receiver().option.weight;
    $display("instance-pre %0d %0d calls=%0d", prior_weight,
             a.option.weight, calls);
    a.cp.option.weight += 3;   $display("add %0d", a.cp.option.weight);
    a.cp.option.weight -= 1;   $display("subtract %0d", a.cp.option.weight);
    a.cp.option.weight *= 2;   $display("multiply %0d", a.cp.option.weight);
    a.cp.option.weight /= 2;   $display("divide %0d", a.cp.option.weight);
    a.cp.option.weight %= 2;   $display("modulo %0d", a.cp.option.weight);
    a.cp.option.weight |= 6;   $display("or %0d", a.cp.option.weight);
    a.cp.option.weight &= 6;   $display("and %0d", a.cp.option.weight);
    a.cp.option.weight ^= 2;   $display("xor %0d", a.cp.option.weight);
    a.cp.option.weight <<= 2;  $display("left %0d", a.cp.option.weight);
    a.cp.option.weight >>= 1;  $display("right %0d", a.cp.option.weight);
    a.cp.option.weight <<<= 1; $display("arith-left %0d", a.cp.option.weight);
    a.cp.option.weight >>>= 2; $display("arith-right %0d", a.cp.option.weight);
    receiver().cp.option.weight += swap_receiver();
    $display("captured %0d %0d calls=%0d", a.cp.option.weight,
             b.cp.option.weight, calls);

    a.option.weight = 16;
    a.option.weight[3:0] = 3;
    a.option.weight[index_once()] ^= 1;
    {a.option.weight[3:0], a.option.weight[3:0]} = 8'hab;
    $display("packed %0d indices=%0d", a.option.weight, indices);
    current = a;
    fork
      begin
        receiver().option.weight += #2 2;
        $display("timed %0d %0d calls=%0d", a.option.weight,
                 b.option.weight, calls);
      end
      begin
        #1 current = b;
        a.option.weight = 50;
      end
    join
    a.option.weight = 27;
    $display("converted %0d", (a.option.goal = 64'h100000050));
    a.option.goal += 64'h100000000;
    a.cp.option.goal -= 50;
    a.cp.option.at_least++;
    --a.cp.option.at_least;
    a.option.cross_num_print_missing += 3;
    a.cx.option.cross_num_print_missing += 2;
    a.cx.option.at_least += 2;
    a.cx.option.goal -= 75;

    a.option.name = "old";
    a.option.name = {a.option.name, " name"};
    a.option.name[index_once()] = "O";
    a.option.comment = "group";
    a.option.comment = {a.option.comment, "-instance"};
    a.option.comment[0] = "G";
    a.cp.option.comment = "point";
    a.cp.option.comment = {a.cp.option.comment, "-instance"};
    a.cp.option.comment[0] ^= 8'h20;
    a.cx.option.comment = "cross";
    a.cx.option.comment = {a.cx.option.comment, "-instance"};
    snapshot = a.option;
    snapshot.weight++;
    snapshot.comment[0] = "X";
    $display("snapshot %0d %s live %0d %s indices=%0d", snapshot.weight,
             snapshot.comment, a.option.weight, a.option.comment, indices);
    current = a;
    fork
      begin
        receiver().option.comment[index_once()] ^= #2 8'h20;
        $display("string-timed %s calls=%0d indices=%0d", a.option.comment,
                 calls, indices);
      end
      begin
        #1 current = b;
        a.option.comment = "other";
      end
    join
    a.option.comment = "Group-instance";

    cg::type_option.weight[3:0] = 3;
    cg::type_option.goal -= 20;
    cg::type_option.goal += 64'h100000000;
    cg::cp::type_option.weight += 1;
    cg::cp::type_option.goal -= 50;
    cg::cx::type_option.goal -= 75;
    cg::type_option.comment = "type";
    cg::type_option.comment = {cg::type_option.comment, " group"};
    cg::type_option.comment[0] = "T";
    cg::cp::type_option.comment = "point";
    cg::cp::type_option.comment = {cg::cp::type_option.comment, "-type"};
    cg::cx::type_option.comment = "cross";
    cg::cx::type_option.comment = {cg::cx::type_option.comment, "-type"};
    $display("final %0d %0d %0d %s|%s|%s", a.option.goal,
             a.cp.option.goal, cg::type_option.weight, a.option.name,
             a.cp.option.comment, cg::type_option.comment);
    $finish;
  end
endmodule

// QUERY: zero-weight 100
// QUERY-NEXT: type-post 0 1 50
// QUERY-NEXT: point-zero 100
// QUERY-NEXT: point-post 0 1 50
// QUERY-NEXT: unmerged 25
// QUERY-NEXT: merge-pre 1
// QUERY-NEXT: bit-wrap 0
// QUERY-NEXT: type-timed 3
// QUERY-NEXT: instance-post 1 2 calls=1
// QUERY-NEXT: instance-pre 1 1 calls=2
// QUERY-NEXT: add 4
// QUERY-NEXT: subtract 3
// QUERY-NEXT: multiply 6
// QUERY-NEXT: divide 3
// QUERY-NEXT: modulo 1
// QUERY-NEXT: or 7
// QUERY-NEXT: and 6
// QUERY-NEXT: xor 4
// QUERY-NEXT: left 16
// QUERY-NEXT: right 8
// QUERY-NEXT: arith-left 16
// QUERY-NEXT: arith-right 4
// QUERY-NEXT: captured 6 1 calls=3
// QUERY-NEXT: packed 27 indices=1
// QUERY-NEXT: timed 29 1 calls=4
// QUERY-NEXT: converted 80
// QUERY-NEXT: snapshot 28 Xroup-instance live 27 Group-instance indices=2
// QUERY-NEXT: string-timed gther calls=5 indices=3
// QUERY-NEXT: final 80 50 3 Old name|Point-instance|Type group

// The database stores each owner's final values: the type's options and its
// run-local distribute_first, one shared configuration, and per-instance
// group, point and cross options over the type-level item options. Items
// within an instance follow resolved identities.
// REPORT: "functional_types":[{"id":{{[0-9]+}},"scope":{{[0-9]+}},"name":"cg",{{[^[]*}}"comment":"Type group",{{[^[]*}}"goal":80,"weight":3,"merge_instances":true,"distribute_first_overrides":[{"run":"{{[^"]+}}","value":false}]}]
// REPORT: "functional_configurations":[{"type":{{[0-9]+}},"configuration":"{{[0-9a-f]+}}","flags":0,"options":[{{[^]]*}}]}],
// REPORT: {"type":{{[0-9]+}},"name":"Old name","configuration":"{{[0-9a-f]+}}","comment":"Group-instance","goal":80,"weight":27,{{[^[]*}}"cross_num_print_missing":3,
// REPORT-DAG: "name":"cp","comment":"Point-instance",{{[^[]*}}"goal":50,"weight":6,"type_goal":50,"type_weight":2,"at_least":1,{{[^[]*}}"type_comment":"point-type",
// REPORT-DAG: "name":"cx","comment":"cross-instance",{{[^[]*}}"goal":25,"weight":0,"type_goal":25,"type_weight":0,{{[^[]*}}"type_comment":"cross-type",{{[^[]*}}"automatic_at_least":3,"cross_num_print_missing":5,
// REPORT: {"type":{{[0-9]+}},"name":"$auto$2","configuration":"{{[0-9a-f]+}}","comment":"",{{[^[]*}}"weight":1,
// REPORT-DAG: "name":"cp","comment":"",{{[^[]*}}"weight":1,"type_goal":50,"type_weight":2,{{[^[]*}}"type_comment":"point-type",
// REPORT-DAG: "name":"cx",{{[^[]*}}"type_goal":25,{{[^[]*}}"type_comment":"cross-type",
// REPORT: "functional_counters":
