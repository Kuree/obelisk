// RUN: %split-file %s %t
// RUN: %obelisk -emit-obelisk --std=1800-2023 %t/input.sv -o %t/input.mlir
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | %python %S/Inputs/dump-coverage-schema.py \
// RUN:   | FileCheck %s

// IEEE 1800-2023 19.5.5 and 19.5.6 apply transition exclusions after
// distribution. Preserve symbolic range and wildcard step predicates in the
// v1 plan so the runtime can subtract them from concrete ordinary alternatives
// without enumerating either predicate's value space.

// CHECK-DAG: functional_bin id=[[ORDINARY:[1-9][0-9]*]] item=[[ITEM:[1-9][0-9]*]] name=ordinary kind=2 flags=0
// CHECK-DAG: functional_bin id=[[RANGED:[1-9][0-9]*]] item=[[ITEM]] name=ranged kind=2 flags=4
// CHECK-DAG: functional_bin id=[[WILDCARD:[1-9][0-9]*]] item=[[ITEM]] name=wildcarded kind=2 flags=24
// CHECK-DAG: transition_program bin=[[ORDINARY]] item=[[ITEM]] {{.*}} alternative_count=2
// CHECK-DAG: transition_program bin=[[RANGED]] item=[[ITEM]] {{.*}} alternative_count=1
// CHECK-DAG: transition_program bin=[[WILDCARD]] item=[[ITEM]] {{.*}} alternative_count=1
// CHECK-DAG: transition_step bin=[[RANGED]] value_set=[[RANGE_SET:[1-9][0-9]*]] {{.*}} ordinal=0 repetition=1
// CHECK-DAG: functional_value_set id=[[RANGE_SET]] item=[[ITEM]] atoms=1 width=4 kind=1 flags=1 signedness=1
// CHECK-DAG: functional_expression id={{[1-9][0-9]*}} owner=[[RANGE_SET]] owner_kind=5 role=15 {{.*}} width=32 signedness=2
// CHECK-DAG: functional_expression id={{[1-9][0-9]*}} owner=[[RANGE_SET]] owner_kind=5 role=16 {{.*}} width=32 signedness=2
// CHECK-DAG: transition_step bin=[[WILDCARD]] value_set=[[WILDCARD_SET:[1-9][0-9]*]] {{.*}} ordinal=0 repetition=1
// CHECK-DAG: functional_value_set id=[[WILDCARD_SET]] item=[[ITEM]] atoms=1 width=4 kind=1 flags=1 signedness=1
// CHECK-DAG: functional_expression id={{[1-9][0-9]*}} owner=[[WILDCARD_SET]] owner_kind=5 role=14 {{.*}} width=4 signedness=1

//--- input.sv
module top;
  bit [3:0] sampled;
  covergroup cg;
    cp: coverpoint sampled {
      bins ordinary = (1 => 6 => 7), (1 => 8 => 7);
      ignore_bins ranged = ([5:6] => 7);
      wildcard illegal_bins wildcarded = (4'b100? => 4'b110?);
    }
  endgroup
  cg cov;
  initial cov = new;
endmodule
