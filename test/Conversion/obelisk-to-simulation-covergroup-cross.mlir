// RUN: %split-file %s %t
// RUN: obelisk -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | %python %S/Inputs/mutate-functional-batch.py add-layout \
// RUN:   > %t/lowered.mlir
// RUN: %python %S/Inputs/dump-coverage-schema.py < %t/lowered.mlir \
// RUN:   | FileCheck %s --check-prefix=SCHEMA
// RUN: FileCheck %s --check-prefix=LOWER < %t/lowered.mlir
// RUN: obelisk-opt %t/lowered.mlir \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' > /dev/null
// RUN: obelisk -emit-obelisk %t/explicit-bin.sv -o %t/explicit-bin.mlir
// RUN: obelisk-opt %t/explicit-bin.mlir \
// RUN:   '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | %python %S/Inputs/mutate-functional-batch.py add-layout \
// RUN:   > %t/explicit-bin-lowered.mlir
// RUN: %python %S/Inputs/dump-coverage-schema.py \
// RUN:   < %t/explicit-bin-lowered.mlir \
// RUN:   | FileCheck %s --check-prefix=EXPLICIT-BIN
// RUN: FileCheck %s --check-prefix=EXPLICIT-SIM \
// RUN:   < %t/explicit-bin-lowered.mlir
// RUN: obelisk-opt %t/explicit-bin-lowered.mlir \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' > /dev/null
// RUN: obelisk -emit-obelisk %t/and-selector.sv -o %t/and-selector.mlir
// RUN: obelisk-opt %t/and-selector.mlir \
// RUN:   '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | %python %S/Inputs/mutate-functional-batch.py add-layout \
// RUN:   > %t/and-selector-lowered.mlir
// RUN: %python %S/Inputs/dump-coverage-schema.py \
// RUN:   < %t/and-selector-lowered.mlir \
// RUN:   | FileCheck %s --check-prefix=AND-SELECTOR
// RUN: obelisk-opt %t/and-selector-lowered.mlir \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' > /dev/null
// RUN: obelisk -emit-obelisk %t/or-selector.sv -o %t/or-selector.mlir
// RUN: obelisk-opt %t/or-selector.mlir \
// RUN:   '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | %python %S/Inputs/mutate-functional-batch.py add-layout \
// RUN:   > %t/or-selector-lowered.mlir
// RUN: %python %S/Inputs/dump-coverage-schema.py \
// RUN:   < %t/or-selector-lowered.mlir \
// RUN:   | FileCheck %s --check-prefix=OR-SELECTOR
// RUN: obelisk-opt %t/or-selector-lowered.mlir \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' > /dev/null
// RUN: obelisk -emit-obelisk %t/transition-selector.sv \
// RUN:   -o %t/transition-selector.mlir
// RUN: obelisk-opt %t/transition-selector.mlir \
// RUN:   '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | %python %S/Inputs/mutate-functional-batch.py add-layout \
// RUN:   > %t/transition-selector-lowered.mlir
// RUN: %python %S/Inputs/dump-coverage-schema.py \
// RUN:   < %t/transition-selector-lowered.mlir \
// RUN:   | FileCheck %s --check-prefix=TRANSITION-SELECTOR
// RUN: obelisk-opt %t/transition-selector-lowered.mlir \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' > /dev/null

// IEEE 1800-2017 19.6: a cross is a typed functional item whose target order
// is the source order. Its iff is a sample-time Boolean expression. With no
// explicit bins, the physical v1 plan retains a symbolic automatic cross.
// SCHEMA-DAG: functional_item id=[[A:[0-9]+]] type=[[TYPE:[0-9]+]] name=a kind=1 ordinal=0
// SCHEMA-DAG: functional_item id=[[B:[0-9]+]] type=[[TYPE]] name=b kind=1 ordinal=1
// SCHEMA-DAG: functional_item id=[[AB:[0-9]+]] type=[[TYPE]] name=ab kind=2 ordinal=2
// SCHEMA: cross_plan item=[[AB]] first_target=0 target_count=2 first_bin=0 bin_count=0 retain=3 iff_expression=[[IFF:[0-9]+]]
// SCHEMA-NEXT: cross_target cross=[[AB]] target=[[A]] ordinal=0
// SCHEMA-NEXT: cross_target cross=[[AB]] target=[[B]] ordinal=1
// SCHEMA: functional_expression id=[[IFF]] owner=[[AB]] owner_kind=2 role=4 result_kind=1
// SCHEMA-DAG: functional_option_plan owner=[[TYPE]] expression={{[0-9]+}} owner_kind=1 scope=1 option=6
// SCHEMA-DAG: functional_option_plan owner=[[A]] expression={{[0-9]+}} owner_kind=2 scope=1 option=6
// SCHEMA-DAG: functional_option_plan owner=[[AB]] expression={{[0-9]+}} owner_kind=2 scope=1 option=2
// SCHEMA-DAG: functional_option_plan owner=[[AB]] expression={{[0-9]+}} owner_kind=2 scope=1 option=5
// SCHEMA-DAG: functional_option_plan owner=[[AB]] expression={{[0-9]+}} owner_kind=2 scope=1 option=15
// SCHEMA-DAG: functional_option_plan owner=[[AB]] expression={{[0-9]+}} owner_kind=2 scope=2 option=1
// EXPLICIT-BIN-DAG: functional_item id=[[EA:[0-9]+]] type=[[ETYPE:[0-9]+]] name=a kind=1 ordinal=0
// EXPLICIT-BIN-DAG: functional_item id=[[EB:[0-9]+]] type=[[ETYPE]] name=b kind=1 ordinal=1
// EXPLICIT-BIN-DAG: functional_item id=[[EAB:[0-9]+]] type=[[ETYPE]] name=ab kind=2 ordinal=2
// EXPLICIT-BIN-DAG: functional_bin id=[[SELECTED:[0-9]+]] item=[[EAB]] name=selected kind=3 flags=0 ordinal=0 at_least=1
// EXPLICIT-BIN-DAG: functional_bin id=[[NAMED:[0-9]+]] item=[[EAB]] name=named kind=3 flags=0 ordinal=1 at_least=1
// EXPLICIT-BIN-DAG: functional_bin id=[[NEGATED:[0-9]+]] item=[[EAB]] name=negated kind=3 flags=0 ordinal=2 at_least=1
// EXPLICIT-BIN-DAG: functional_bin id=[[IGNORED:[0-9]+]] item=[[EAB]] name=ignored kind=3 flags=4 ordinal=3 at_least=1
// EXPLICIT-BIN-DAG: functional_bin id=[[ILLEGAL:[0-9]+]] item=[[EAB]] name=illegal kind=3 flags=8 ordinal=4 at_least=1
// EXPLICIT-BIN-DAG: functional_bin id=[[ZERO:[0-9]+]] item=[[EA]] name=zero kind=1 flags=0 ordinal=0 at_least=1
// EXPLICIT-BIN-DAG: functional_bin id=[[ONE:[0-9]+]] item=[[EA]] name=one kind=1 flags=0 ordinal=1 at_least=1
// EXPLICIT-BIN-DAG: cross_plan item=[[EAB]] first_target=0 target_count=2 first_bin=0 bin_count=5 retain=3 iff_expression=0
// EXPLICIT-BIN-DAG: cross_target cross=[[EAB]] target=[[EA]] ordinal=0
// EXPLICIT-BIN-DAG: cross_target cross=[[EAB]] target=[[EB]] ordinal=1
// EXPLICIT-BIN-DAG: cross_bin bin=[[SELECTED]] cross=[[EAB]] root_selector=[[SELECTOR:[0-9]+]] flags=0
// EXPLICIT-BIN-DAG: cross_bin bin=[[NAMED]] cross=[[EAB]] root_selector=[[NAMED_SELECTOR:[0-9]+]] flags=0
// EXPLICIT-BIN-DAG: cross_bin bin=[[NEGATED]] cross=[[EAB]] root_selector=[[NEGATION:[0-9]+]] flags=0
// EXPLICIT-BIN-DAG: cross_bin bin=[[IGNORED]] cross=[[EAB]] root_selector=[[IGNORED_SELECTOR:[0-9]+]] flags=0
// EXPLICIT-BIN-DAG: cross_bin bin=[[ILLEGAL]] cross=[[EAB]] root_selector=[[ILLEGAL_SELECTOR:[0-9]+]] flags=0
// EXPLICIT-BIN-DAG: functional_expression id=[[BIN_IFF:[0-9]+]] owner=[[SELECTED]] owner_kind=3 role=5 result_kind=1
// EXPLICIT-BIN-DAG: cross_selector id=[[SELECTOR]] cross=[[EAB]] target=[[EA]] bin=0 value_set=[[SET:[0-9]+]] kind=1 ordinal=0
// EXPLICIT-BIN-DAG: cross_selector id=[[NAMED_SELECTOR]] cross=[[EAB]] target=[[EA]] bin=[[ZERO]] value_set=[[NAMED_SET:[0-9]+]] kind=1 ordinal=1
// EXPLICIT-BIN-DAG: cross_selector id=[[NEGATED_CONDITION:[0-9]+]] cross=[[EAB]] target=[[EA]] bin=[[ONE]] value_set=0 kind=1 ordinal=2 first_operand=0 operand_count=0
// EXPLICIT-BIN-DAG: cross_selector id=[[NEGATION]] cross=[[EAB]] target=0 bin=0 value_set=0 kind=2 ordinal=3 first_operand=0 operand_count=1
// EXPLICIT-BIN-DAG: cross_selector id=[[IGNORED_SELECTOR]] cross=[[EAB]] target=[[EB]] bin={{[1-9][0-9]*}} value_set=0 kind=1 ordinal=4
// EXPLICIT-BIN-DAG: cross_selector id=[[ILLEGAL_SELECTOR]] cross=[[EAB]] target=[[EB]] bin={{[1-9][0-9]*}} value_set=0 kind=1 ordinal=5
// EXPLICIT-BIN-DAG: cross_selector_operand node=[[NEGATION]] operand=[[NEGATED_CONDITION]] ordinal=0
// EXPLICIT-BIN-DAG: functional_value_set id=[[SET]] item=[[EA]] atoms=1 width=1 kind=1 flags=1 signedness=1 set_expression=0
// EXPLICIT-BIN-DAG: functional_value_set id=[[NAMED_SET]] item=[[EA]] atoms=1 width=1 kind=1 flags=1 signedness=1 set_expression=0
// EXPLICIT-BIN-DAG: functional_value_atom set=[[SET]] ordinal=0 kind=1 flags=3 lower_expression=[[VALUE:[0-9]+]] upper_expression=0
// EXPLICIT-BIN-DAG: functional_expression id=[[VALUE]] owner=[[SET]] owner_kind=5 role=14 result_kind=2
// EXPLICIT-BIN-DAG: functional_source bin=[[SELECTED]] role=expanded ordinal=0
// EXPLICIT-BIN-DAG: functional_source bin=[[SELECTED]] role=original ordinal=0
// EXPLICIT-BIN-DAG: exclusion entity=[[SELECTED]] metric=3 reason=selector excluded for reporting
// IEEE 1800-2023 19.6.1.1: && intersects the selected cross-product
// subsets. Preserve the recursive source expression as a postorder v1 selector
// tree so same-target conjunctions remain distinguishable from independent
// cross-target constraints.
// AND-SELECTOR-DAG: functional_item id=[[AA:[0-9]+]] type=[[ATYPE:[0-9]+]] name=a kind=1 ordinal=0
// AND-SELECTOR-DAG: functional_item id=[[AB:[0-9]+]] type=[[ATYPE]] name=b kind=1 ordinal=1
// AND-SELECTOR-DAG: functional_item id=[[AC:[0-9]+]] type=[[ATYPE]] name=c kind=1 ordinal=2
// AND-SELECTOR-DAG: functional_item id=[[AABC:[0-9]+]] type=[[ATYPE]] name=abc kind=2 ordinal=3
// AND-SELECTOR-DAG: functional_bin id=[[AAZERO:[0-9]+]] item=[[AA]] name=zero kind=1
// AND-SELECTOR-DAG: functional_bin id=[[AAONE:[0-9]+]] item=[[AA]] name=one kind=1
// AND-SELECTOR-DAG: functional_bin id=[[ABONE:[0-9]+]] item=[[AB]] name=one kind=1
// AND-SELECTOR-DAG: functional_bin id=[[ACZERO:[0-9]+]] item=[[AC]] name=zero kind=1
// AND-SELECTOR-DAG: functional_bin id=[[NESTED_BIN:[0-9]+]] item=[[AABC]] name=nested kind=3
// AND-SELECTOR-DAG: functional_bin id=[[EMPTY_BIN:[0-9]+]] item=[[AABC]] name=empty kind=3
// AND-SELECTOR-DAG: cross_plan item=[[AABC]] first_target=0 target_count=3 first_bin=0 bin_count=2 retain=3
// AND-SELECTOR-DAG: cross_bin bin=[[NESTED_BIN]] cross=[[AABC]] root_selector=[[NESTED_ROOT:[0-9]+]] flags=0
// AND-SELECTOR-DAG: cross_bin bin=[[EMPTY_BIN]] cross=[[AABC]] root_selector=[[EMPTY_ROOT:[0-9]+]] flags=0
// AND-SELECTOR: cross_selector id=[[LEFT_A:[0-9]+]] cross=[[AABC]] target=[[AA]] bin=[[AAZERO]] value_set=0 kind=1 ordinal=0 first_operand=0 operand_count=0
// AND-SELECTOR-NEXT: cross_selector id=[[LEFT_B:[0-9]+]] cross=[[AABC]] target=[[AB]] bin=[[ABONE]] value_set=0 kind=1 ordinal=1 first_operand=0 operand_count=0
// AND-SELECTOR-NEXT: cross_selector id=[[LEFT_AND:[0-9]+]] cross=[[AABC]] target=0 bin=0 value_set=0 kind=3 ordinal=2 first_operand=0 operand_count=2
// AND-SELECTOR-NEXT: cross_selector id=[[RIGHT_C:[0-9]+]] cross=[[AABC]] target=[[AC]] bin=[[ACZERO]] value_set=0 kind=1 ordinal=3 first_operand=2 operand_count=0
// AND-SELECTOR-NEXT: cross_selector id=[[NESTED_ROOT]] cross=[[AABC]] target=0 bin=0 value_set=0 kind=3 ordinal=4 first_operand=2 operand_count=2
// AND-SELECTOR-NEXT: cross_selector id=[[EMPTY_A_ZERO:[0-9]+]] cross=[[AABC]] target=[[AA]] bin=[[AAZERO]] value_set=0 kind=1 ordinal=5 first_operand=4 operand_count=0
// AND-SELECTOR-NEXT: cross_selector id=[[EMPTY_A_ONE:[0-9]+]] cross=[[AABC]] target=[[AA]] bin=[[AAONE]] value_set=0 kind=1 ordinal=6 first_operand=4 operand_count=0
// AND-SELECTOR-NEXT: cross_selector id=[[EMPTY_ROOT]] cross=[[AABC]] target=0 bin=0 value_set=0 kind=3 ordinal=7 first_operand=4 operand_count=2
// AND-SELECTOR: cross_selector_operand node=[[LEFT_AND]] operand=[[LEFT_A]] ordinal=0
// AND-SELECTOR-NEXT: cross_selector_operand node=[[LEFT_AND]] operand=[[LEFT_B]] ordinal=1
// AND-SELECTOR-NEXT: cross_selector_operand node=[[NESTED_ROOT]] operand=[[LEFT_AND]] ordinal=0
// AND-SELECTOR-NEXT: cross_selector_operand node=[[NESTED_ROOT]] operand=[[RIGHT_C]] ordinal=1
// AND-SELECTOR-NEXT: cross_selector_operand node=[[EMPTY_ROOT]] operand=[[EMPTY_A_ZERO]] ordinal=0
// AND-SELECTOR-NEXT: cross_selector_operand node=[[EMPTY_ROOT]] operand=[[EMPTY_A_ONE]] ordinal=1
// EXPLICIT-SIM: %[[FIRST_VALUE:.*]] = simulation.ref.load {{.*}} : !simulation.ref<i1> -> i1
// EXPLICIT-SIM-NEXT: %[[FIRST_SAMPLE:.*]] = simulation.logic.from_bits %[[FIRST_VALUE]]
// EXPLICIT-SIM-NEXT: %[[SECOND_VALUE:.*]] = simulation.ref.load {{.*}} : !simulation.ref<i1> -> i1
// EXPLICIT-SIM-NEXT: %[[SECOND_SAMPLE:.*]] = simulation.logic.from_bits %[[SECOND_VALUE]]
// EXPLICIT-SIM-NEXT: %[[BIN_IFF_BOOL:.*]] = simulation.ref.load {{.*}} : !simulation.ref<i1> -> i1
// EXPLICIT-SIM-NEXT: simulation.covergroup.sample {{.*}} values[%[[FIRST_SAMPLE]], %[[SECOND_SAMPLE]], %[[BIN_IFF_BOOL]]] ids [{{.*}}]
// IEEE 1800-2023 19.6.1: || forms the union of selected cross products. The
// exact v1 plan retains both OR itself and its nesting under AND.
// OR-SELECTOR-DAG: functional_item id=[[OA:[0-9]+]] type=[[OTYPE:[0-9]+]] name=a kind=1 ordinal=0
// OR-SELECTOR-DAG: functional_item id=[[OB:[0-9]+]] type=[[OTYPE]] name=b kind=1 ordinal=1
// OR-SELECTOR-DAG: functional_item id=[[OC:[0-9]+]] type=[[OTYPE]] name=c kind=1 ordinal=2
// OR-SELECTOR-DAG: functional_item id=[[OABC:[0-9]+]] type=[[OTYPE]] name=abc kind=2 ordinal=3
// OR-SELECTOR-DAG: functional_bin id=[[OUNION:[0-9]+]] item=[[OABC]] name=unioned kind=3
// OR-SELECTOR-DAG: functional_bin id=[[OMIXED:[0-9]+]] item=[[OABC]] name=mixed kind=3
// OR-SELECTOR-DAG: functional_bin id=[[OALL:[0-9]+]] item=[[OABC]] name=all kind=3
// OR-SELECTOR-DAG: cross_bin bin=[[OUNION]] cross=[[OABC]] root_selector=[[OR_ROOT:[0-9]+]] flags=0
// OR-SELECTOR-DAG: cross_bin bin=[[OMIXED]] cross=[[OABC]] root_selector=[[AND_ROOT:[0-9]+]] flags=0
// OR-SELECTOR-DAG: cross_bin bin=[[OALL]] cross=[[OABC]] root_selector=[[ALL_ROOT:[0-9]+]] flags=0
// OR-SELECTOR: cross_selector id=[[OR_A:[0-9]+]] cross=[[OABC]] target=[[OA]] bin={{[1-9][0-9]*}} value_set=0 kind=1 ordinal=0 first_operand=0 operand_count=0
// OR-SELECTOR-NEXT: cross_selector id=[[OR_B:[0-9]+]] cross=[[OABC]] target=[[OB]] bin={{[1-9][0-9]*}} value_set=0 kind=1 ordinal=1 first_operand=0 operand_count=0
// OR-SELECTOR-NEXT: cross_selector id=[[OR_ROOT]] cross=[[OABC]] target=0 bin=0 value_set=0 kind=4 ordinal=2 first_operand=0 operand_count=2
// OR-SELECTOR-NEXT: cross_selector id=[[MIXED_A:[0-9]+]] cross=[[OABC]] target=[[OA]] bin={{[1-9][0-9]*}} value_set=0 kind=1 ordinal=3 first_operand=2 operand_count=0
// OR-SELECTOR-NEXT: cross_selector id=[[MIXED_B:[0-9]+]] cross=[[OABC]] target=[[OB]] bin={{[1-9][0-9]*}} value_set=0 kind=1 ordinal=4 first_operand=2 operand_count=0
// OR-SELECTOR: cross_selector id=[[MIXED_OR:[0-9]+]] cross=[[OABC]] target=0 bin=0 value_set=0 kind=4 ordinal=5 first_operand=2 operand_count=2
// OR-SELECTOR-NEXT: cross_selector id=[[MIXED_C:[0-9]+]] cross=[[OABC]] target=[[OC]] bin={{[1-9][0-9]*}} value_set=0 kind=1 ordinal=6 first_operand=4 operand_count=0
// OR-SELECTOR-NEXT: cross_selector id=[[AND_ROOT]] cross=[[OABC]] target=0 bin=0 value_set=0 kind=3 ordinal=7 first_operand=4 operand_count=2
// OR-SELECTOR-NEXT: cross_selector id=[[ALL_ROOT]] cross=[[OABC]] target=0 bin=0 value_set=0 kind=6 ordinal=8 first_operand=6 operand_count=0
// OR-SELECTOR-DAG: cross_selector_operand node=[[OR_ROOT]] operand=[[OR_A]] ordinal=0
// OR-SELECTOR-DAG: cross_selector_operand node=[[OR_ROOT]] operand=[[OR_B]] ordinal=1
// OR-SELECTOR-DAG: cross_selector_operand node=[[MIXED_OR]] operand=[[MIXED_A]] ordinal=0
// OR-SELECTOR-DAG: cross_selector_operand node=[[MIXED_OR]] operand=[[MIXED_B]] ordinal=1
// OR-SELECTOR-DAG: cross_selector_operand node=[[AND_ROOT]] operand=[[MIXED_OR]] ordinal=0
// OR-SELECTOR-DAG: cross_selector_operand node=[[AND_ROOT]] operand=[[MIXED_C]] ordinal=1
// IEEE 1800-2023 19.6.1.1: the selector's value-set restriction is retained
// alongside the transition program whose last step defines intersection.
// TRANSITION-SELECTOR-DAG: functional_item id=[[TPOINT:[0-9]+]] type=[[TTYPE:[0-9]+]] name=transition_point kind=1 ordinal=0
// TRANSITION-SELECTOR-DAG: functional_item id=[[TSIDE:[0-9]+]] type=[[TTYPE]] name=side_point kind=1 ordinal=1
// TRANSITION-SELECTOR-DAG: functional_item id=[[TCROSS:[0-9]+]] type=[[TTYPE]] name=product kind=2 ordinal=2
// TRANSITION-SELECTOR-DAG: functional_bin id=[[TRISE:[0-9]+]] item=[[TPOINT]] name=rise kind=2
// TRANSITION-SELECTOR-DAG: transition_program bin=[[TRISE]] item=[[TPOINT]] first_alternative={{[0-9]+}} alternative_count=1
// TRANSITION-SELECTOR-DAG: cross_bin bin={{[1-9][0-9]*}} cross=[[TCROSS]] root_selector=[[TROOT:[0-9]+]] flags=0
// TRANSITION-SELECTOR-DAG: cross_selector id=[[TLEAF:[0-9]+]] cross=[[TCROSS]] target=[[TPOINT]] bin=0 value_set=[[TSET:[0-9]+]] kind=1 ordinal=0
// TRANSITION-SELECTOR-DAG: functional_value_set id=[[TSET]] item=[[TPOINT]] atoms=1 width=1 kind=1 flags=1 signedness=1 set_expression=0
// TRANSITION-SELECTOR-DAG: cross_selector id=[[TSIDE_LEAF:[0-9]+]] cross=[[TCROSS]] target=[[TSIDE]] bin={{[1-9][0-9]*}} value_set=0 kind=1 ordinal=1
// TRANSITION-SELECTOR-DAG: cross_selector id=[[TROOT]] cross=[[TCROSS]] target=0 bin=0 value_set=0 kind=3 ordinal=2
// LOWER: simulation.covergroup.stop {{.*}} item [[CROSS_ITEM:-?[0-9]+]]
// LOWER: simulation.covergroup.start {{.*}} item [[CROSS_ITEM]]
// LOWER: simulation.covergroup.instance_query {{.*}} item [[CROSS_ITEM]]

//--- input.sv
module cross_schema;
  bit a_value;
  bit b_value;
  bit enabled;

  covergroup cg;
    option.detect_overlap = 1;
    a: coverpoint a_value {
      option.detect_overlap = 0;
    }
    b: coverpoint b_value;
    ab: cross a, b iff (enabled) {
      option.weight = 2;
      option.cross_num_print_missing = 2;
      option.cross_retain_auto_bins = 0;
      type_option.goal = 75;
    }
  endgroup

  cg cov;
  int covered;
  int total;
  real percentage;
  initial begin
    cov = new;
    cov.ab.stop();
    cov.ab.start();
    cov.sample();
    percentage = cov.ab.get_inst_coverage(covered, total);
  end
endmodule

//--- and-selector.sv
module and_cross_selector;
  bit a_value;
  bit b_value;
  bit c_value;
  covergroup cg;
    a: coverpoint a_value {
      bins zero = {0};
      bins one = {1};
    }
    b: coverpoint b_value {
      bins zero = {0};
      bins one = {1};
    }
    c: coverpoint c_value {
      bins zero = {0};
      bins one = {1};
    }
    abc: cross a, b, c {
      bins nested = (binsof(a.zero) && binsof(b.one)) && binsof(c.zero);
      bins empty = binsof(a.zero) && binsof(a.one);
    }
  endgroup
endmodule

//--- or-selector.sv
module or_cross_selector;
  bit a_value;
  bit b_value;
  bit c_value;
  covergroup cg;
    a: coverpoint a_value {
      bins zero = {0};
      bins one = {1};
    }
    b: coverpoint b_value {
      bins zero = {0};
      bins one = {1};
    }
    c: coverpoint c_value {
      bins zero = {0};
      bins one = {1};
    }
    abc: cross a, b, c {
      bins unioned = binsof(a.one) || binsof(b.one);
      bins mixed = (binsof(a.one) || binsof(b.one)) && binsof(c.zero);
      bins all = abc;
    }
  endgroup
endmodule

//--- explicit-bin.sv
module explicit_cross_bin;
  bit a_value;
  bit b_value;
  bit enabled;
  covergroup cg;
    a: coverpoint a_value {
      bins zero = {0};
      bins one = {1};
    }
    b: coverpoint b_value {
      bins zero = {0};
      bins one = {1};
    }
    ab: cross a, b {
      // obelisk coverage off functional reason="selector excluded for reporting"
      bins selected = binsof(a) intersect {0} iff (enabled);
      // obelisk coverage on functional
      bins named = binsof(a.zero) intersect {0};
      bins negated = !binsof(a.one);
      ignore_bins ignored = binsof(b.zero);
      illegal_bins illegal = binsof(b.one);
    }
  endgroup

  cg cov;
  initial begin
    cov = new;
    cov.sample();
  end
endmodule

//--- transition-selector.sv
module transition_cross_selector;
  bit transition_value;
  bit side_value;
  covergroup cg;
    transition_point: coverpoint transition_value {
      bins rise = (0 => 1);
      bins fall = (1 => 0);
    }
    side_point: coverpoint side_value {
      bins zero = {0};
      bins one = {1};
    }
    product: cross transition_point, side_point {
      bins selected = binsof(transition_point) intersect {1} &&
                      binsof(side_point.zero);
    }
  endgroup
endmodule
