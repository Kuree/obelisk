// RUN: talos --coverage=functional -emit-talos %s \
// RUN:   | talos-opt '--lower-talos-to-sim=opt-level=0' \
// RUN:     --test-talos-print-coverage-schema -o /dev/null 2> %t.base.txt
// RUN: talos --coverage=functional -DINSERT -emit-talos %s \
// RUN:   | talos-opt '--lower-talos-to-sim=opt-level=0' \
// RUN:     --test-talos-print-coverage-schema -o /dev/null 2> %t.inserted.txt
// RUN: diff %t.base.txt %t.inserted.txt
// RUN: FileCheck %s < %t.base.txt

// An unrelated semantic tree inserted at the same preceding source line may
// renumber frontend nodes, but cannot change schema identities or generated
// covergroup symbols. The identical source path keeps path-derived IDs equal.
`ifdef INSERT
package unrelated; int x; endpackage
`else
// same-width placeholder
`endif
module stable;
  bit sampled;
  covergroup cg;
    cp: coverpoint sampled { bins one = {1}; }
  endgroup
endmodule

// CHECK: functional_type id={{[0-9]+}} name=cg language=2023 hierarchy=stable.cg
// CHECK: functional_item {{.*}} name=cp
// CHECK: functional_bin {{.*}} name=one
