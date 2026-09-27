// RUN: obelisk -emit-sim -O0 --vpi=off %s | FileCheck %s

// A malformed literal fails before consuming an argument at runtime. The
// compile-time specialization tracker must therefore leave the following
// aggregate on its ordinary pattern-string path instead of manufacturing a
// raw-only descriptor for a conversion that does not exist.
module raw_output_aggregate_malformed_lowering;
  typedef struct {
    logic [7:0] a;
    logic [3:0] b;
  } pair_t;
  pair_t value;
  pair_t later;
  string result;
  initial begin
    result = $sformatf("%", value);
    result = $sformatf("%u%", value, later);
    result = $sformatf("%u tail %", value, later);
  end
endmodule

// CHECK-NOT: simulation.bytes.constant "%u"
// CHECK-NOT: simulation.bytes.constant "%z"
// CHECK-NOT: flags = [32, 4096]
// CHECK: simulation.string.output_format{{.*}}flags = [32, 8]
// CHECK-COUNT-2: simulation.string.output_format{{.*}}flags = [32, 4096, 8]
