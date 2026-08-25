// Configuration and bind selection are frontend concerns, so the semantic IR
// is the primary executable specification. The two runtime invocations only
// prove that the frozen hierarchy reaches both execution tiers unchanged.
// RUN: obelisk -emit-slang --top=outer \
// RUN:   -v liba=%S/Inputs/config_bindings/liba.sv \
// RUN:   -v libb=%S/Inputs/config_bindings/libb.sv %s \
// RUN:   | FileCheck %s --check-prefix=SLANG
// RUN: obelisk -emit-bindings --top=outer \
// RUN:   -v liba=%S/Inputs/config_bindings/liba.sv \
// RUN:   -v libb=%S/Inputs/config_bindings/libb.sv %s \
// RUN:   | FileCheck %s --check-prefix=REPORT
// RUN: obelisk -emit-sim --top=outer \
// RUN:   -v liba=%S/Inputs/config_bindings/liba.sv \
// RUN:   -v libb=%S/Inputs/config_bindings/libb.sv %s \
// RUN:   | FileCheck %s --check-prefix=SIM \
// RUN:       --implicit-check-not=is_from_bind \
// RUN:       --implicit-check-not=is_below_bind \
// RUN:       --implicit-check-not=is_bind_target \
// RUN:       --implicit-check-not=selected_cell \
// RUN:       --implicit-check-not=configuration_rule_kind \
// RUN:       --implicit-check-not=configuration_rule_source_range
// RUN: obelisk -fno-lto -O0 --native-scheduler=generic --top=outer \
// RUN:   -v liba=%S/Inputs/config_bindings/liba.sv \
// RUN:   -v libb=%S/Inputs/config_bindings/libb.sv %s -o %t.native
// RUN: %t.native | FileCheck %s --check-prefix=RUNTIME
// RUN: obelisk -fno-lto -O0 --native-scheduler=generic \
// RUN:   --execution-tier=bytecode --top=outer \
// RUN:   -v liba=%S/Inputs/config_bindings/liba.sv \
// RUN:   -v libb=%S/Inputs/config_bindings/libb.sv %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s --check-prefix=RUNTIME

module mid;
  leaf exact();
  leaf by_cell();
  pick by_instance_liblist();
endmodule

module top;
  mid nested();
  leaf outer_cell();
  pick outer_cell_liblist();

  initial begin
    #1;
    if (nested.exact.VALUE != 111)
      $fatal(1, "instance use and parameter assignment did not override cell rule");
    if (nested.by_cell.VALUE != 12)
      $fatal(1, "nested cell use selected the wrong library");
    if (nested.by_instance_liblist.VALUE != 21)
      $fatal(1, "nested instance liblist selected the wrong library");
    if (nested.probe.VALUE != 31)
      $fatal(1, "bound definition did not use the nested default liblist");
    if (nested.probe.child.VALUE != 12)
      $fatal(1, "bound descendant did not inherit the nested config");
    if (outer_cell.VALUE != 12)
      $fatal(1, "outer cell use selected the wrong library");
    if (outer_cell_liblist.VALUE != 22)
      $fatal(1, "outer cell liblist selected the wrong library");
    $display("CONFIG_BIND_PASS");
    $finish;
  end
endmodule

bind mid bound_probe probe();

config inner;
  design mid;
  default liblist liba libb;
  cell leaf use libb.leaf;
  instance mid.exact use liba.leaf #(.VALUE(111));
  instance mid.by_instance_liblist liblist liba;
endconfig

config outer;
  design top;
  default liblist liba libb;
  cell leaf use libb.leaf;
  cell pick liblist libb liba;
  instance top.nested use work.inner : config;
endconfig

// Instance selection overrides the inner cell rule.
// SLANG: slang.symbol.instance attributes {{.*}}configuration = "work.inner"{{.*}}configuration_root = "top.nested"{{.*}}configuration_rule_kind = "instance"{{.*}}hierarchical_name = "top.nested.exact"{{.*}}selected_cell = "liba.leaf"
// The cell rule governs the sibling and the bound descendant.
// SLANG: slang.symbol.instance attributes {{.*}}configuration = "work.inner"{{.*}}configuration_rule_kind = "cell"{{.*}}hierarchical_name = "top.nested.by_cell"{{.*}}selected_cell = "libb.leaf"
// SLANG: slang.symbol.instance attributes {{.*}}configuration = "work.inner"{{.*}}is_from_bind = true{{.*}}selected_cell = "liba.bound_probe"
// SLANG: slang.symbol.instance attributes {{.*}}configuration = "work.inner"{{.*}}is_below_bind = true{{.*}}selected_cell = "libb.leaf"
// SLANG: configuration_liblist = ["libb", "liba"]{{.*}}hierarchical_name = "top.outer_cell_liblist"{{.*}}selected_cell = "libb.pick"

// Reports are lexical by hierarchy, independent of AST allocator order. Rule
// locations use a stable basename rather than embedding a checkout path.
// REPORT: binding top -> work.top config=work.outer root=top liblist=[liba, libb]
// REPORT-NEXT: binding top.nested -> work.mid bind-target config=work.inner root=top.nested liblist=[liba, libb]
// REPORT-NEXT: binding top.nested.by_cell -> libb.leaf config=work.inner root=top.nested liblist=[liba, libb] rule=cell@config-bindings.sv:69:3
// REPORT-NEXT: binding top.nested.by_instance_liblist -> liba.pick config=work.inner root=top.nested liblist=[liba] rule=instance@config-bindings.sv:71:3
// REPORT-NEXT: binding top.nested.exact -> liba.leaf config=work.inner root=top.nested liblist=[liba, libb] rule=instance@config-bindings.sv:70:3
// REPORT-NEXT: binding top.nested.probe -> liba.bound_probe from-bind config=work.inner root=top.nested liblist=[liba, libb]
// REPORT-NEXT: binding top.nested.probe.child -> libb.leaf below-bind config=work.inner root=top.nested liblist=[liba, libb] rule=cell@config-bindings.sv:69:3
// REPORT-NEXT: binding top.outer_cell -> libb.leaf config=work.outer root=top liblist=[liba, libb] rule=cell@config-bindings.sv:77:3
// REPORT-NEXT: binding top.outer_cell_liblist -> libb.pick config=work.outer root=top liblist=[libb, liba] rule=cell@config-bindings.sv:78:3

// SIM: obelisk_sim.design

// RUNTIME: CONFIG_BIND_PASS
