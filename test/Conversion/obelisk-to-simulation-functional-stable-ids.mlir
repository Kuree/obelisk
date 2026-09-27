// RUN: %split-file %s %t
// RUN: obelisk --coverage=functional -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | %python %S/Inputs/dump-coverage-schema.py > %t/o0.txt
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=3' \
// RUN:   | %python %S/Inputs/dump-coverage-schema.py > %t/o3.txt
// RUN: diff %t/o0.txt %t/o3.txt
// RUN: FileCheck %s --check-prefix=HIER < %t/o0.txt

// Two embedded covergroups have the same enclosing class hierarchy and the
// same coverpoint/bin names. Their canonical handle symbol references remain
// distinct stable type identities; successful lowering also proves that the
// pass's collision detector did not collapse them.
// CHECK: simulation.covergroup.decl @{{.*}} schema [[FIRST:[1-9][0-9]*]]
// CHECK: simulation.covergroup.decl @{{.*}} schema [[SECOND:[1-9][0-9]*]]
// CHECK-NOT: simulation.covergroup.decl
// HIER-DAG: functional_type id={{[1-9][0-9]*}} name={{.*}} language={{2017|2023}} hierarchy={{.*}}Owner::first
// HIER-DAG: functional_type id={{[1-9][0-9]*}} name={{.*}} language={{2017|2023}} hierarchy={{.*}}Owner::second
// Each type's only sample-phase expression is its integral coverpoint result.
// HIER-COUNT-2: functional_expression id={{[1-9][0-9]*}} owner={{[1-9][0-9]*}} owner_kind=2 role=2 result_kind=2 width=1 signedness=1 owner_ordinal=0 owner_subordinal=0 phase=2 result_ordinal=0

//--- input.sv
class Owner;
  bit sampled;

  covergroup first;
    cp: coverpoint sampled {
      bins one = {1};
    }
  endgroup

  covergroup second;
    cp: coverpoint sampled {
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
  initial owner = new;
endmodule
