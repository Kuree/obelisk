// RUN: obelisk -emit-sim %s | FileCheck %s

module termination;
  function automatic int nested_finish();
    $finish;
    return 1;
  endfunction

  initial $finish(0);
  initial $stop(2);

  initial begin
    $info("info=%0d", 1);
    $warning;
    $error("continued");
  end

  initial $fatal(0, "fatal=%0d", 2);
  initial begin
    int value;
    value = nested_finish();
    $info("after=%0d", value);
  end
endmodule

// CHECK: obelisk_sim.finish
// CHECK-NEXT: obelisk_sim.return
// CHECK: obelisk_sim.stop
// CHECK-NEXT: obelisk_sim.return
// CHECK-DAG: obelisk_sim.bytes.constant "INFO: {{.*}}obelisk-to-simulation-termination.sv:
// CHECK-DAG: obelisk_sim.bytes.constant "WARNING: {{.*}}obelisk-to-simulation-termination.sv:
// CHECK-DAG: obelisk_sim.bytes.constant "ERROR: {{.*}}obelisk-to-simulation-termination.sv:
// CHECK: obelisk_sim.bytes.constant "FATAL: {{.*}}obelisk-to-simulation-termination.sv:
// CHECK: obelisk_sim.fatal
// CHECK-NEXT: obelisk_sim.display
// CHECK-NEXT: obelisk_sim.return
// CHECK: obelisk_sim.termination.requested
// CHECK: cf.cond_br
