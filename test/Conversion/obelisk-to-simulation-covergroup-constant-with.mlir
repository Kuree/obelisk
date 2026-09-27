// RUN: %split-file %s %t
// RUN: obelisk -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: obelisk-opt %t/input.mlir --obelisk-sim-prepare \
// RUN:   | FileCheck %s --check-prefix=PREPARE
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   > %t/lowered.mlir
// RUN: FileCheck %s --check-prefix=SIM < %t/lowered.mlir
// RUN: %python %S/Inputs/dump-coverage-schema.py < %t/lowered.mlir \
// RUN:   | FileCheck %s --check-prefix=SCHEMA
// RUN: obelisk --std=1800-2017 -emit-obelisk %t/input.sv \
// RUN:   -o %t/input-2017.mlir
// RUN: obelisk-opt %t/input-2017.mlir \
// RUN:   '--lower-obelisk-to-sim=opt-level=0' > /dev/null
// RUN: obelisk -emit-obelisk %t/item.sv -o %t/item.mlir
// RUN: obelisk-opt %t/item.mlir --obelisk-sim-prepare \
// RUN:   | FileCheck %s --check-prefix=ITEM-PREPARE
// RUN: obelisk-opt %t/item.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=ITEM
// RUN: obelisk -emit-obelisk %t/signed.sv -o %t/signed.mlir
// RUN: obelisk-opt %t/signed.mlir --obelisk-sim-prepare \
// RUN:   | FileCheck %s --check-prefix=SIGNED-PREPARE
// RUN: obelisk -emit-obelisk %t/wildcard.sv -o %t/wildcard.mlir
// RUN: not obelisk-opt %t/wildcard.mlir \
// RUN:   '--lower-obelisk-to-sim=opt-level=0' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=WILDCARD
// RUN: obelisk -emit-obelisk %t/all-domain.sv -o %t/all-domain.mlir
// RUN: not obelisk-opt %t/all-domain.mlir \
// RUN:   '--lower-obelisk-to-sim=opt-level=0' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ALL-DOMAIN
// RUN: obelisk -emit-obelisk %t/dynamic-endpoint.sv \
// RUN:   -o %t/dynamic-endpoint.mlir
// RUN: not obelisk-opt %t/dynamic-endpoint.mlir \
// RUN:   '--lower-obelisk-to-sim=opt-level=0' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DYNAMIC-ENDPOINT
// RUN: obelisk -emit-obelisk %t/unknown-endpoint.sv \
// RUN:   -o %t/unknown-endpoint.mlir
// RUN: not obelisk-opt %t/unknown-endpoint.mlir \
// RUN:   '--lower-obelisk-to-sim=opt-level=0' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=UNKNOWN-ENDPOINT
// RUN: obelisk -emit-obelisk %t/clipped-endpoint.sv \
// RUN:   -o %t/clipped-endpoint.mlir
// RUN: not obelisk-opt %t/clipped-endpoint.mlir \
// RUN:   '--lower-obelisk-to-sim=opt-level=0' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=CLIPPED-ENDPOINT
// RUN: obelisk -emit-obelisk %t/unsigned-to-signed.sv \
// RUN:   -o %t/unsigned-to-signed.mlir
// RUN: not obelisk-opt %t/unsigned-to-signed.mlir \
// RUN:   '--lower-obelisk-to-sim=opt-level=0' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=UNSIGNED-TO-SIGNED
// RUN: obelisk -emit-obelisk %t/too-many.sv -o %t/too-many.mlir
// RUN: not obelisk-opt %t/too-many.mlir \
// RUN:   '--lower-obelisk-to-sim=opt-level=0' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=TOO-MANY

// IEEE 1800-2023 19.5.1.1 evaluates `with` once per source candidate
// occurrence and preserves duplicate values and source order. Constant and
// item-dependent predicates use the same typed v1 batch contract.
// PREPARE: simulation.coverage.functional.with_candidate_values = [2 : i4, 0 : i4, 1 : i4, 2 : i4, 2 : i4]
// PREPARE-SAME: simulation.coverage.functional.with_iterator_path = "constant_with.cg.cp.item"
// PREPARE: simulation.coverage.functional.with_candidate_values = [4 : i4, 5 : i4, 6 : i4, 7 : i4]
// PREPARE-SAME: simulation.coverage.functional.with_iterator_path = ""
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
// ITEM-PREPARE: simulation.coverage.functional.with_candidate_values = [0 : i4, 1 : i4, 2 : i4, 3 : i4, 4 : i4, 5 : i4, 6 : i4, 7 : i4]
// ITEM-PREPARE-SAME: simulation.coverage.functional.with_iterator_path = "item_with.cg.cp.item"
// ITEM: %[[ITEM_FALSE:.+]] = arith.constant false
// ITEM: %[[ITEM_TRUE:.+]] = arith.constant true
// ITEM: simulation.covergroup.create
// ITEM-SAME: payloads[%[[ITEM_TRUE]], %[[ITEM_FALSE]], %[[ITEM_TRUE]], %[[ITEM_FALSE]], %[[ITEM_TRUE]], %[[ITEM_FALSE]], %[[ITEM_TRUE]], %[[ITEM_FALSE]], %[[ITEM_TRUE]],
// SIGNED-PREPARE: simulation.coverage.functional.with_candidate_values = [-3 : i4, -2 : i4, -1 : i4, 0 : i4, 1 : i4, 2 : i4]
// SIGNED-PREPARE-SAME: simulation.coverage.functional.with_iterator_path = "signed_with.cg.cp.item"
// WILDCARD: coverage bin with expressions require a finite explicit non-wildcard integral state range list
// ALL-DOMAIN: coverage bin with expressions require a finite explicit non-wildcard integral state range list
// DYNAMIC-ENDPOINT: coverage bin with range endpoints must be compiler-folded constants
// UNKNOWN-ENDPOINT: coverage bin with range endpoints must not contain X or Z bits
// CLIPPED-ENDPOINT: coverage bin with range endpoint is not representable by the effective coverpoint type
// UNSIGNED-TO-SIGNED: coverage bin with range endpoint is not representable by the effective coverpoint type
// TOO-MANY: coverage bin with candidate count exceeds the v1 limit of 4096

//--- input.sv
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

//--- item.sv
module item_with;
  bit [3:0] sampled;

  covergroup cg(bit parity);
    cp: coverpoint sampled {
      bins selected[] = {[0:7]} with (item[0] == parity);
    }
  endgroup

  cg cov = new(1'b1);
endmodule

//--- signed.sv
module signed_with;
  logic signed [3:0] sampled;
  covergroup cg;
    cp: coverpoint sampled {
      bins negative[] = {[-3:2]} with (item < 0);
    }
  endgroup
  cg cov = new;
endmodule

//--- wildcard.sv
module wildcard_with;
  logic [3:0] sampled;
  covergroup cg;
    cp: coverpoint sampled {
      wildcard bins selected[] = {[0:15]} with (item[0]);
    }
  endgroup
endmodule

//--- all-domain.sv
module all_domain_with;
  bit [3:0] sampled;
  covergroup cg;
    cp: coverpoint sampled {
      bins selected[] = cp with (item[0]);
    }
  endgroup
endmodule

//--- dynamic-endpoint.sv
module dynamic_endpoint_with;
  bit [3:0] sampled;
  covergroup cg(int high);
    cp: coverpoint sampled {
      bins selected[] = {[0:high]} with (item[0]);
    }
  endgroup
endmodule

//--- unknown-endpoint.sv
module unknown_endpoint_with;
  logic [3:0] sampled;
  covergroup cg;
    cp: coverpoint sampled {
      bins selected[] = {4'bx} with (item[0]);
    }
  endgroup
endmodule

//--- clipped-endpoint.sv
module clipped_endpoint_with;
  bit [3:0] sampled;
  covergroup cg;
    cp: coverpoint sampled {
      bins selected[] = {[0:16]} with (item[0]);
    }
  endgroup
endmodule

//--- unsigned-to-signed.sv
module unsigned_to_signed_with;
  logic signed [3:0] sampled;
  covergroup cg;
    cp: coverpoint sampled {
      bins clipped[] = {4'd15} with (item < 0);
    }
  endgroup
endmodule

//--- too-many.sv
module too_many_with;
  bit [12:0] sampled;
  covergroup cg;
    cp: coverpoint sampled {
      bins selected[] = {[0:4096]} with (item[0]);
    }
  endgroup
endmodule
