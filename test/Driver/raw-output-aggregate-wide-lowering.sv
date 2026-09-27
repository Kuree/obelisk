// RUN: obelisk -emit-sim -O0 --vpi=off %s | FileCheck %s

// A 4097-bit scalar leaf remains one raw-format operation; the lowering is
// O(leaves), not O(bits) or O(32-bit words). The following ordinary %p must
// not eagerly materialize either raw representation, so exactly the two
// leaves of the preceding static %z appear below.
module raw_output_aggregate_wide_lowering;
  typedef struct {
    logic [4096:0] wide;
    logic [3:0] tail;
  } wide_t;

  wide_t value;
  string raw;
  string pattern;
  initial begin
    raw = $sformatf("%z", value);
    pattern = $sformatf("%p", value);
  end
endmodule

// CHECK: %[[RAW_FORMAT:[0-9]+]] = simulation.bytes.constant "%z%z"
// CHECK-NOT: simulation.bytes.constant "%u"
// CHECK: simulation.string.output_format %arg0(%[[RAW_FORMAT]], {{.*}}!simulation.logic<4097>, !simulation.logic<4>
// CHECK: simulation.string.output_format{{.*}}flags = [32, 4096]
