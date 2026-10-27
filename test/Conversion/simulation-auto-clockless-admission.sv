// RUN: talos -emit-slang %s -o %t.slang.mlir
// RUN: talos-opt %t.slang.mlir --test-talos-native-backend='scheduler=auto stop-after=graph' --test-talos-native-aot-analysis -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ADMISSION
// RUN: talos-opt %t.slang.mlir --test-talos-native-backend='scheduler=auto' | FileCheck %s --check-prefix=EVAL

// A large event-driven island can use generated eval without a structural
// periodic clock (Auto scheduler): 128 combinational processes pass the
// 1024-unit native cost bar for a clockless island. The initial actor keeps
// its string across suspension (unit_129) and stays outside the admitted
// island.
// ADMISSION: native-aot eligible=true fully=false selected=true periodic=false
// ADMISSION: bytecode @unit_129 bb0
// ADMISSION-NEXT: bytecode @unit_129 bb1
// ADMISSION-NEXT: reason suspension retains managed state
// EVAL: llvm.mlir.global internal constant @__talos_aot_schedule_plan_v1()
// EVAL: llvm.func @__talos_eval_dispatch_v1(

module native_tier1_partial_clockless_admission;
  logic [7:0] stimulus = 0;
  logic [7:0] value [0:127];
  for (genvar i = 0; i < 128; i++) begin : g
    always_comb value[i] = stimulus + i;
  end
  initial begin
    automatic string message;
    message = "probe";
    #1 stimulus = 3;
    #1 $display("%s %d", message, value[127]);
    $finish;
  end
endmodule
