// A single configuration can elaborate multiple design roots. Each instance
// rule is rooted independently, and the report remains globally lexical.
// RUN: obelisk -emit-bindings --top=multi %s \
// RUN:   | FileCheck %s --check-prefix=REPORT

module replacement_a;
endmodule

module replacement_b;
endmodule

module placeholder;
endmodule

module alpha;
  placeholder selected();
endmodule

module beta;
  placeholder selected();
endmodule

config multi;
  design beta alpha;
  instance alpha.selected use replacement_a;
  instance beta.selected use replacement_b;
endconfig

// REPORT: binding alpha -> work.alpha config=work.multi root=alpha liblist=[]
// REPORT-NEXT: binding alpha.selected -> work.replacement_a config=work.multi root=alpha liblist=[] rule=instance@config-bindings-multiple-roots.sv:
// REPORT-NEXT: binding beta -> work.beta config=work.multi root=beta liblist=[]
// REPORT-NEXT: binding beta.selected -> work.replacement_b config=work.multi root=beta liblist=[] rule=instance@config-bindings-multiple-roots.sv:
