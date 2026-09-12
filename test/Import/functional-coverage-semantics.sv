// RUN: obelisk --std=1800-2023 -emit-slang %s | FileCheck %s --check-prefix=SLANG
// RUN: obelisk --std=1800-2023 -emit-obelisk %s | FileCheck %s --check-prefix=OBELISK
// RUN: obelisk --std=1800-2023 -emit-slang %s | FileCheck %s --check-prefix=EXACT

module functional_coverage_semantics;
  bit clk;
  int a, b;

  covergroup empty_sample with function sample();
    ep: coverpoint a;
  endgroup

  covergroup cg(input int lower, ref int upper)
      with function sample(input int value);
    type_option.merge_instances = 1;
    type_option.real_interval = 0.25;
    option.per_instance = 1;
    cp: coverpoint value iff (value inside {[lower:upper]}) {
      option.at_least = 2;
      bins low = {[0:3]};
      bins high = {[4:7]};
    }
    bp: coverpoint b;
    x: cross cp, bp iff (value >= lower) {
      option.cross_num_print_missing = 3;
      option.cross_retain_auto_bins = 1;
      bins selected = !binsof(cp.low) || !binsof(bp) intersect {1};
      bins filtered = x with (cp + bp < upper) matches 1;
    }
  endgroup

  covergroup clocked @(posedge clk);
    coverpoint a;
  endgroup

  function automatic void entered();
  endfunction
  task automatic exited();
  endtask
  covergroup blocked @@(begin entered or end exited);
    coverpoint b;
  endgroup

  cg cov_inst;
  initial begin
    cov_inst = new(0, b);
    cov_inst.option.comment = "procedural";
    cov_inst.cp.get_coverage();
  end
endmodule

class base_group_owner;
  bit value;
  covergroup inherited (int limit) @(posedge value);
    coverpoint value;
  endgroup
endclass

class derived_group_owner extends base_group_owner;
  covergroup extends inherited;
    coverpoint value;
  endgroup : inherited
endclass

// A derived embedded group preserves its base handle and its inherited event.
// SLANG-DAG: slang.type.covergroup_type attributes {base_group = !slang.covergroup_handle<@{{[^>]+}}>{{.*}}coverage_event_kind = 4 : i32, has_coverage_event = true

// An empty custom sample signature is distinct from an omitted event.
// SLANG-DAG: slang.type.covergroup_type attributes {constructor_argument_count = 0 : i64, constructor_formals = [], coverage_event_kind = 2 : i32, has_coverage_event = false, hierarchical_name = "functional_coverage_semantics.empty_sample"{{.*}}sample_formal_count = 0 : i64, sample_formals = []

// Constructor and custom-sample formals retain exact symbol identities.
// SLANG-DAG: slang.type.covergroup_type attributes {constructor_argument_count = 2 : i64, constructor_formals = [@{{[^,]+}}, @{{[^]]+}}], coverage_event_kind = 2 : i32{{.*}}hierarchical_name = "functional_coverage_semantics.cg"{{.*}}sample_formal_count = 1 : i64, sample_formals = [@{{[^]]+}}]

// Definition-time setters are typed wrappers around one RHS expression.
// SLANG-DAG: slang.coverage.option attributes {{.*}}option_kind = 12 : i32{{.*}}owner_kind = 0 : i32, owner_symbol = @{{[^,}]+}}, scope_kind = 1 : i32
// SLANG-DAG: slang.coverage.option attributes {{.*}}option_kind = 9 : i32{{.*}}owner_kind = 0 : i32, owner_symbol = @{{[^,}]+}}, scope_kind = 0 : i32
// SLANG-DAG: slang.expression.member_access attributes {{.*}}member_name = "comment"{{.*}}referenced_path = "functional_coverage_semantics.cg.comment"
// SLANG-DAG: slang.symbol.coverpoint attributes {expression_roles = [0 : i32, 1 : i32], has_iff = true{{.*}}hierarchical_name = "functional_coverage_semantics.cg.cp"
// SLANG-DAG: slang.symbol.cover_cross attributes {expression_roles = [1 : i32], has_iff = true{{.*}}target_count = 2 : i64, target_symbols = [@{{[^,]+}}, @{{[^]]+}}]

// The selector remains a typed tree with resolved cross and bin identities.
// SLANG-DAG: slang.bins.binary attributes {enclosing_cross_symbol = @{{[^,}]+}}{{.*}}operator_kind = 1 : i32
// SLANG-DAG: slang.bins.unary attributes {enclosing_cross_symbol = @{{[^,}]+}}{{.*}}operator_kind = 0 : i32
// SLANG-DAG: slang.bins.condition attributes {enclosing_cross_symbol = @{{[^,}]+}}, intersect_count = 0 : i64{{.*}}target_symbol = @{{[^,}]+}}
// SLANG-DAG: slang.bins.with_filter attributes {enclosing_cross_symbol = @{{[^,}]+}}, has_matches = true
// SLANG-DAG: slang.bins.cross_id attributes {enclosing_cross_symbol = @{{[^,}]+}}

// SLANG-DAG: slang.timing.signal_event attributes {
// SLANG-DAG: slang.timing.block_event_list attributes {event_kinds = [0 : i32, 1 : i32]

// The built-in call retains the exact selected method symbol.
// SLANG-DAG: slang.expression.call attributes {{.*}}callee_name = "get_coverage"{{.*}}referenced_symbol = @{{.*get_coverage[^,}]*}}

// OBELISK-DAG: obelisk.sv.type.covergroup_type attributes {base_group = !obelisk.covergroup_handle<@{{[^>]+}}>{{.*}}coverage_event_kind = 4 : i32, has_coverage_event = true
// OBELISK-DAG: obelisk.sv.coverage.option attributes {{.*}}option_kind = 12 : i32{{.*}}owner_kind = 0 : i32
// OBELISK-DAG: obelisk.sv.symbol.cover_cross attributes {expression_roles = [1 : i32]{{.*}}target_count = 2
// OBELISK-DAG: obelisk.sv.bins.with_filter attributes {enclosing_cross_symbol = @{{[^,}]+}}, has_matches = true
// OBELISK-DAG: obelisk.sv.expression.call attributes {{.*}}callee_name = "get_coverage"{{.*}}referenced_symbol = @{{.*get_coverage[^,}]*}}

// Exact SymbolRefs preserve formal and cross ordering, selector target kind,
// option ownership, inherited base identity, and built-in method identity.
// EXACT: slang.type.covergroup_type attributes {constructor_argument_count = 2 : i64, constructor_formals = [@{{[^,]*}}::@[[LOWER:s[0-9]+\.lower]], @{{[^]]*}}::@[[UPPER:s[0-9]+\.upper]]], coverage_event_kind = 2 : i32{{.*}}sample_formals = [@{{[^]]*}}::@[[VALUE:s[0-9]+\.value]]]{{.*}}sym_name = "[[CG:s[0-9]+\.cg]]"
// EXACT: slang.symbol.formal_argument attributes {{.*}}sym_name = "[[LOWER]]"
// EXACT: slang.symbol.formal_argument attributes {{.*}}sym_name = "[[UPPER]]"
// EXACT: slang.symbol.formal_argument attributes {{.*}}is_coverage_sample_formal{{.*}}sym_name = "[[VALUE]]"
// EXACT: slang.coverage.option attributes {{.*}}option_kind = 12 : i32{{.*}}owner_symbol = @{{[^,]*}}::@[[CG]], scope_kind = 1 : i32
// EXACT-NEXT: {{ *}}slang.expression.conversion
// EXACT: slang.coverage.option attributes {{.*}}option_kind = 14 : i32{{.*}}owner_symbol = @{{[^,]*}}::@[[CG]], scope_kind = 1 : i32
// EXACT: slang.symbol.coverpoint attributes {{.*}}hierarchical_name = "functional_coverage_semantics.cg.cp"{{.*}}sym_name = "[[CP:s[0-9]+\.cp]]"
// EXACT: slang.symbol.subroutine attributes {hierarchical_name = "functional_coverage_semantics.cg.cp.get_coverage"{{.*}}sym_name = "[[CP_GET:s[0-9]+\.get_coverage]]"
// EXACT: slang.symbol.coverage_bin attributes {{.*}}hierarchical_name = "functional_coverage_semantics.cg.cp.low"{{.*}}sym_name = "[[LOW_BIN:s[0-9]+\.low]]"
// EXACT: slang.symbol.coverpoint attributes {{.*}}hierarchical_name = "functional_coverage_semantics.cg.bp"{{.*}}sym_name = "[[BP:s[0-9]+\.bp]]"
// EXACT: slang.symbol.cover_cross attributes {{.*}}sym_name = "[[CROSS:s[0-9]+\.x]]"{{.*}}target_symbols = [@{{[^,]*}}::@[[CP]], @{{[^]]*}}::@[[BP]]]
// EXACT: slang.bins.condition attributes {{.*}}target_symbol = @{{[^,]*}}::@[[CP]]::@[[LOW_BIN]]
// EXACT: slang.bins.condition attributes {{.*}}target_symbol = @{{[^,}]*}}::@[[BP]]
// EXACT: slang.bins.cross_id attributes {enclosing_cross_symbol = @{{[^,}]*}}::@[[CROSS]]
// EXACT: slang.expression.call attributes {{.*}}callee_name = "get_coverage"{{.*}}referenced_symbol = @{{[^,]*}}::@[[CP]]::@[[CP_GET]]
