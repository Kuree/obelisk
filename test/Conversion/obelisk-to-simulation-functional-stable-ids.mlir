// RUN: %split-file %s %t
// RUN: obelisk --coverage=functional -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --test-obelisk-print-coverage-schema -o /dev/null 2> %t/o0.txt
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=3' \
// RUN:   --test-obelisk-print-coverage-schema -o /dev/null 2> %t/o3.txt
// RUN: diff %t/o0.txt %t/o3.txt
// RUN: FileCheck %s --check-prefix=HIER < %t/o0.txt

// Two embedded covergroups have the same enclosing class hierarchy and the
// same coverpoint/bin names. Their canonical handle symbol references remain
// distinct stable type identities; successful lowering also proves that the
// pass's collision detector did not collapse them.
// CHECK: simulation.covergroup.decl @{{.*}} schema [[FIRST:[1-9][0-9]*]]
// CHECK: simulation.covergroup.decl @{{.*}} schema [[SECOND:[1-9][0-9]*]]
// CHECK-NOT: simulation.covergroup.decl
// CHECK-DAG: simulation.covergroup.set_type_integer_option {{.*}} type [[FIRST]] item 0 option goal
// CHECK-DAG: simulation.covergroup.set_type_integer_option {{.*}} type [[FIRST]] item [[FIRST_POINT:[1-9][0-9]*]] option weight
// CHECK-DAG: simulation.covergroup.set_type_integer_option {{.*}} type [[SECOND]] item 0 option goal
// CHECK-DAG: simulation.covergroup.set_type_integer_option {{.*}} type [[SECOND]] item [[SECOND_POINT:[1-9][0-9]*]] option weight
// HIER-DAG: functional_type id={{[1-9][0-9]*}} name={{.*}} language={{2017|2023}} hierarchy={{.*}}Owner::first
// HIER-DAG: functional_type id={{[1-9][0-9]*}} name={{.*}} language={{2017|2023}} hierarchy={{.*}}Owner::second
// Each type's only sample-phase expression is its integral coverpoint result.
// HIER-COUNT-2: functional_expression id={{[1-9][0-9]*}} owner={{[1-9][0-9]*}} owner_kind=2 role=2 result_kind=2 width=1 signedness=1 owner_ordinal=0 owner_subordinal=0 phase=2 result_ordinal=0

//--- input.sv
class Owner;
  bit sampled;

  covergroup first;
    type_option.goal = 25;
    cp: coverpoint sampled {
      type_option.weight = 2;
      bins one = {1};
    }
  endgroup

  covergroup second;
    type_option.goal = 75;
    cp: coverpoint sampled {
      type_option.weight = 3;
      bins one = {1};
    }
  endgroup

  function new;
    first = new;
    second = new;
  endfunction
endclass

module top;
  Owner owner;
  initial begin
    owner = new;
    owner.first.cp.type_option.weight = 4;
    $display("%0d %0d", owner.first.type_option.goal,
             owner.second.cp.type_option.weight);
  end
endmodule
