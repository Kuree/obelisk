// RUN: %split-file %s %t
// RUN: obelisk -emit-obelisk %t/pos.sv | obelisk-opt --obelisk-sim-prepare | FileCheck %s --check-prefix=PREPARE --check-prefix=ITEM-PREPARE --check-prefix=SIGNED-PREPARE
// RUN: obelisk -emit-obelisk %t/pos.sv | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' -o %t/lowered.mlir
// RUN: FileCheck %s --check-prefix=SIM --check-prefix=ITEM < %t/lowered.mlir
// RUN: obelisk-opt %t/lowered.mlir --test-obelisk-print-coverage-schema -o /dev/null 2>&1 | FileCheck %s --check-prefix=SCHEMA
// RUN: obelisk --std=1800-2017 -emit-obelisk %t/pos.sv | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' -o /dev/null
// RUN: obelisk -emit-obelisk %t/neg.sv | not obelisk-opt '--lower-obelisk-to-sim=opt-level=0' -o /dev/null 2>&1 | FileCheck %s --check-prefix=NEG

// IEEE 1800-2023 19.5.1.1 evaluates `with` once per source candidate
// occurrence and preserves duplicate values and source order. Constant and
// item-dependent predicates use the same typed v1 batch contract.
// PREPARE: coverage.with_candidate_values = [2 : i4, 0 : i4, 1 : i4, 2 : i4, 2 : i4]
// PREPARE-SAME: coverage.with_iterator_path = "constant_with.cg.cp.item"
// PREPARE: coverage.with_candidate_values = [4 : i4, 5 : i4, 6 : i4, 7 : i4]
// PREPARE-SAME: coverage.with_iterator_path = ""
// SIM: %[[FALSE:.+]] = arith.constant false
// SIM: %[[TRUE:.+]] = arith.constant true
// SIM: simulation.covergroup.create
// SIM-SAME: payloads[%[[FALSE]], %[[FALSE]], %[[TRUE]], %[[FALSE]], %[[FALSE]],
// SIM-SAME: expression_ids [{{[1-9][0-9]*}}, {{[1-9][0-9]*}}, {{[1-9][0-9]*}}, {{[1-9][0-9]*}}, {{[1-9][0-9]*}}
// SIM: simulation.covergroup.sample {{.*}} values[{{.*}}] ids [{{[1-9][0-9]*}}]
// Stable-ID order is intentionally unrelated to source order.
// SCHEMA: functional_type id=[[TYPE:[1-9][0-9]*]] name=cg
// SCHEMA-DAG: functional_bin id=[[UNSIZED:[1-9][0-9]*]] {{.*}} name=drop_unsized kind=1
// SCHEMA-DAG: functional_bin id=[[KEEP:[1-9][0-9]*]] {{.*}} name=keep kind=1
// SCHEMA-DAG: functional_bin id=[[DROP:[1-9][0-9]*]] {{.*}} name=drop kind=1
// SCHEMA-DAG: functional_bin id=[[FIXED:[1-9][0-9]*]] {{.*}} name=drop_fixed kind=1
// SCHEMA-DAG: functional_expression id={{[1-9][0-9]*}} owner=[[KEEP]] owner_kind=3 role=7 result_kind=1 {{.*}} owner_ordinal=0 owner_subordinal=0 phase=1
// SCHEMA-DAG: functional_expression id={{[1-9][0-9]*}} owner=[[KEEP]] owner_kind=3 role=7 result_kind=1 {{.*}} owner_ordinal=1 owner_subordinal=0 phase=1
// SCHEMA-DAG: functional_expression id={{[1-9][0-9]*}} owner=[[KEEP]] owner_kind=3 role=7 result_kind=1 {{.*}} owner_ordinal=2 owner_subordinal=0 phase=1
// SCHEMA-DAG: functional_expression id={{[1-9][0-9]*}} owner=[[KEEP]] owner_kind=3 role=7 result_kind=1 {{.*}} owner_ordinal=3 owner_subordinal=0 phase=1
// SCHEMA-DAG: functional_expression id={{[1-9][0-9]*}} owner=[[KEEP]] owner_kind=3 role=7 result_kind=1 {{.*}} owner_ordinal=4 owner_subordinal=0 phase=1
// SCHEMA-DAG: functional_expression id={{[1-9][0-9]*}} owner=[[DROP]] owner_kind=3 role=7 result_kind=1 {{.*}} owner_ordinal=0 owner_subordinal=0 phase=1
// SCHEMA-DAG: functional_expression id={{[1-9][0-9]*}} owner=[[DROP]] owner_kind=3 role=7 result_kind=1 {{.*}} owner_ordinal=1 owner_subordinal=0 phase=1
// SCHEMA-DAG: functional_expression id={{[1-9][0-9]*}} owner=[[DROP]] owner_kind=3 role=7 result_kind=1 {{.*}} owner_ordinal=2 owner_subordinal=0 phase=1
// SCHEMA-DAG: functional_expression id={{[1-9][0-9]*}} owner=[[DROP]] owner_kind=3 role=7 result_kind=1 {{.*}} owner_ordinal=3 owner_subordinal=0 phase=1
// SCHEMA-DAG: functional_expression id={{[1-9][0-9]*}} owner=[[UNSIZED]] owner_kind=3 role=7 result_kind=1 {{.*}} owner_ordinal=0 owner_subordinal=0 phase=1
// SCHEMA-DAG: functional_expression id={{[1-9][0-9]*}} owner=[[UNSIZED]] owner_kind=3 role=7 result_kind=1 {{.*}} owner_ordinal=1 owner_subordinal=0 phase=1
// SCHEMA-DAG: functional_expression id={{[1-9][0-9]*}} owner=[[FIXED]] owner_kind=3 role=7 result_kind=1 {{.*}} owner_ordinal=0 owner_subordinal=0 phase=1
// SCHEMA-DAG: functional_expression id={{[1-9][0-9]*}} owner=[[FIXED]] owner_kind=3 role=7 result_kind=1 {{.*}} owner_ordinal=1 owner_subordinal=0 phase=1
// SCHEMA-DAG: functional_expression id=[[DISTRIBUTE:[1-9][0-9]*]] owner=[[TYPE]] owner_kind=1 role=13 result_kind=1 width=0 signedness=3 owner_ordinal=10 owner_subordinal=2 phase=4
// SCHEMA-DAG: functional_option_plan owner=[[TYPE]] expression=[[DISTRIBUTE]] owner_kind=1 scope=2 option=10 ordinal=10 flags=0
// ITEM-PREPARE: coverage.with_candidate_values = [0 : i4, 1 : i4, 2 : i4, 3 : i4, 4 : i4, 5 : i4, 6 : i4, 7 : i4]
// ITEM-PREPARE-SAME: coverage.with_iterator_path = "item_with.item_cg.cp.item"
// A constructor-dependent predicate keeps all eight candidates and evaluates
// parity per candidate in the create payload.
// ITEM: %[[ITEM_FALSE:.+]] = arith.constant false
// ITEM: %[[ITEM_TRUE:.+]] = arith.constant true
// ITEM: simulation.covergroup.create {{.*}}payloads[%[[ITEM_TRUE]], %[[ITEM_FALSE]], %[[ITEM_TRUE]], %[[ITEM_FALSE]], %[[ITEM_TRUE]], %[[ITEM_FALSE]], %[[ITEM_TRUE]], %[[ITEM_FALSE]], %[[ITEM_TRUE]],
// SIGNED-PREPARE: coverage.with_candidate_values = [-3 : i4, -2 : i4, -1 : i4, 0 : i4, 1 : i4, 2 : i4]
// SIGNED-PREPARE-SAME: coverage.with_iterator_path = "signed_with.signed_cg.cp.item"
// Wildcard bins, the whole-domain form, non-constant, X/Z or unrepresentable
// range endpoints, and candidate counts above the v1 limit are rejected by
// lowering (IEEE 1800-2023 19.5.1.1). Diagnostics are emitted in IR walk order,
// so each is matched with CHECK-DAG; the two messages that repeat are told
// apart by the operation printed after them.
// NEG-DAG: error: coverage bin with expressions require a finite explicit non-wildcard integral state range list{{[[:space:]]+}}obelisk.sv.symbol.coverage_bin {{.*}}hierarchical_name = "wildcard_with.cg.cp.selected"
// NEG-DAG: error: coverage bin with expressions require a finite explicit non-wildcard integral state range list{{[[:space:]]+}}obelisk.sv.symbol.coverage_bin {{.*}}hierarchical_name = "all_domain_with.cg.cp.selected"
// NEG-DAG: error: coverage bin with range endpoints must be compiler-folded constants
// NEG-DAG: error: coverage bin with range endpoints must not contain X or Z bits
// NEG-DAG: error: coverage bin with range endpoint is not representable by the effective coverpoint type{{[[:space:]]+}}obelisk.sv.expression.conversion attributes {{[{]}}folded_constant = "4'b0"
// NEG-DAG: error: coverage bin with range endpoint is not representable by the effective coverpoint type{{[[:space:]]+}}obelisk.sv.expression.conversion attributes {{[{]}}folded_constant = "-4'sd1"
// NEG-DAG: error: coverage bin with candidate count exceeds the v1 limit of 4096

//--- pos.sv
module constant_with;
  bit [3:0] sampled;

  covergroup cg;
    type_option.distribute_first = 1;
    cp: coverpoint sampled {
      bins keep[] = {2, [0:2], 2} with (item[0]);
      bins drop = {[4:7]} with (1'b0);
      bins drop_unsized[] = {8, 9} with (1'b0);
      bins drop_fixed[2] = {[10:11]} with (1'b0);
    }
  endgroup

  cg cov;
  initial begin
    cov = new;
    sampled = 1;
    cov.sample();
  end
endmodule


module item_with;
  bit [3:0] sampled;

  covergroup item_cg(bit parity);
    cp: coverpoint sampled {
      bins selected[] = {[0:7]} with (item[0] == parity);
    }
  endgroup

  item_cg cov = new(1'b1);
endmodule


module signed_with;
  logic signed [3:0] sampled;
  covergroup signed_cg;
    cp: coverpoint sampled {
      bins negative[] = {[-3:2]} with (item < 0);
    }
  endgroup
  signed_cg cov = new;
endmodule


//--- neg.sv
module wildcard_with;
  logic [3:0] sampled;
  covergroup cg;
    cp: coverpoint sampled {
      wildcard bins selected[] = {[0:15]} with (item[0]);
    }
  endgroup
endmodule


module all_domain_with;
  bit [3:0] sampled;
  covergroup cg;
    cp: coverpoint sampled {
      bins selected[] = cp with (item[0]);
    }
  endgroup
endmodule


module dynamic_endpoint_with;
  bit [3:0] sampled;
  covergroup cg(int high);
    cp: coverpoint sampled {
      bins selected[] = {[0:high]} with (item[0]);
    }
  endgroup
endmodule


module unknown_endpoint_with;
  logic [3:0] sampled;
  covergroup cg;
    cp: coverpoint sampled {
      bins selected[] = {4'bx} with (item[0]);
    }
  endgroup
endmodule


module clipped_endpoint_with;
  bit [3:0] sampled;
  covergroup cg;
    cp: coverpoint sampled {
      bins selected[] = {[0:16]} with (item[0]);
    }
  endgroup
endmodule


module unsigned_to_signed_with;
  logic signed [3:0] sampled;
  covergroup cg;
    cp: coverpoint sampled {
      bins clipped[] = {4'd15} with (item < 0);
    }
  endgroup
endmodule


module too_many_with;
  bit [12:0] sampled;
  covergroup cg;
    cp: coverpoint sampled {
      bins selected[] = {[0:4096]} with (item[0]);
    }
  endgroup
endmodule
