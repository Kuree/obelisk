// Instance arrays exercise per-element provenance without expanding source.
// The same fixture is used for compile/report scaling measurements.
// RUN: obelisk -emit-bindings --top=scale -D COUNT=4 %s \
// RUN:   | FileCheck %s --check-prefix=REPORT
// RUN: obelisk -emit-bindings --top=scale -D COUNT=12 %s \
// RUN:   | FileCheck %s --check-prefix=ORDER

`ifndef COUNT
  `define COUNT 4
`endif

module scale_leaf;
endmodule

module scale_top;
  scale_leaf items[`COUNT - 1:0]();
endmodule

config scale;
  design scale_top;
  default liblist work;
endconfig

// REPORT: binding scale_top -> work.scale_top config=work.scale root=scale_top liblist=[work]
// REPORT-DAG: binding scale_top.items[0] -> work.scale_leaf config=work.scale root=scale_top liblist=[work]
// REPORT-DAG: binding scale_top.items[1] -> work.scale_leaf config=work.scale root=scale_top liblist=[work]
// REPORT-DAG: binding scale_top.items[2] -> work.scale_leaf config=work.scale root=scale_top liblist=[work]
// REPORT-DAG: binding scale_top.items[3] -> work.scale_leaf config=work.scale root=scale_top liblist=[work]

// Array indices are intentionally sorted as hierarchy strings; multi-digit
// indices make that deterministic contract observable.
// ORDER: binding scale_top -> work.scale_top config=work.scale root=scale_top liblist=[work]
// ORDER-NEXT: binding scale_top.items[0] -> work.scale_leaf config=work.scale root=scale_top liblist=[work]
// ORDER-NEXT: binding scale_top.items[10] -> work.scale_leaf config=work.scale root=scale_top liblist=[work]
// ORDER-NEXT: binding scale_top.items[11] -> work.scale_leaf config=work.scale root=scale_top liblist=[work]
// ORDER-NEXT: binding scale_top.items[1] -> work.scale_leaf config=work.scale root=scale_top liblist=[work]
// ORDER-NEXT: binding scale_top.items[2] -> work.scale_leaf config=work.scale root=scale_top liblist=[work]
// ORDER-NEXT: binding scale_top.items[3] -> work.scale_leaf config=work.scale root=scale_top liblist=[work]
// ORDER-NEXT: binding scale_top.items[4] -> work.scale_leaf config=work.scale root=scale_top liblist=[work]
// ORDER-NEXT: binding scale_top.items[5] -> work.scale_leaf config=work.scale root=scale_top liblist=[work]
// ORDER-NEXT: binding scale_top.items[6] -> work.scale_leaf config=work.scale root=scale_top liblist=[work]
// ORDER-NEXT: binding scale_top.items[7] -> work.scale_leaf config=work.scale root=scale_top liblist=[work]
// ORDER-NEXT: binding scale_top.items[8] -> work.scale_leaf config=work.scale root=scale_top liblist=[work]
// ORDER-NEXT: binding scale_top.items[9] -> work.scale_leaf config=work.scale root=scale_top liblist=[work]
