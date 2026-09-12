// RUN: %split-file %s %t
// RUN: cp %t/base.sv %t/stable.sv
// RUN: obelisk --coverage=functional -emit-obelisk %t/stable.sv -o %t/base.mlir
// RUN: obelisk-opt %t/base.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | %python %S/Inputs/dump-coverage-schema.py > %t/base.txt
// RUN: cp %t/inserted.sv %t/stable.sv
// RUN: obelisk --coverage=functional -emit-obelisk %t/stable.sv -o %t/inserted.mlir
// RUN: obelisk-opt %t/inserted.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | %python %S/Inputs/dump-coverage-schema.py > %t/inserted.txt
// RUN: grep '^functional_' %t/base.txt > %t/base.ids
// RUN: grep '^functional_' %t/inserted.txt > %t/inserted.ids
// RUN: diff %t/base.ids %t/inserted.ids

// An unrelated semantic tree inserted at the same preceding source line may
// renumber frontend nodes, but cannot change schema identities or generated
// covergroup symbols.

//--- base.sv
// same-width placeholder
module stable;
  bit sampled;
  covergroup cg;
    cp: coverpoint sampled { bins one = {1}; }
  endgroup
endmodule

//--- inserted.sv
module unrelated; endmodule
module stable;
  bit sampled;
  covergroup cg;
    cp: coverpoint sampled { bins one = {1}; }
  endgroup
endmodule
