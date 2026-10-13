// RUN: obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt --lower-obelisk-to-sim=opt-level=0 | FileCheck %s --check-prefix=PLAN
// RUN: obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.5.3: integral expression weights retain their unsigned
// values, zero exclusions and original range normalization. Companions do
// not multiply target mass; preceding solve layers supply conditional state.
// PLAN: simulation.random.solve
// PLAN-SAME: SAMP
// RUNTIME: candidate ordered conditional weights 1
// RUNTIME: candidate frozen predecessor weight 1
// RUNTIME: candidate zero scope weight failure 1
// RUNTIME: candidate unsigned signed scope weight 1
// RUNTIME: candidate zero soft scope weight 1

module top;
  bit [1:0] tag;
  bit x;
  int n[4], a[4];
  bit ok;
  initial begin
    // tag is solved first and uniformly; x == 0 then has weight tag + 1 out of 5.
    ok = 1;
    repeat (2400) begin
      ok &= randomize(tag, x) with {
        solve tag before x;
        x dist {0 := tag + 1, 1 := 4 - tag};
      };
      n[tag]++;
      if (!x) a[tag]++;
    end
    for (int t = 0; t < 4; t++) begin
      ok &= n[t] > 450 && n[t] < 750;
      // P(x == 0 | tag == t) = (t + 1) / 5, accepted within +-30% of n * that.
      ok &= a[t] * 50 > n[t] * (7 + 10 * t) && a[t] * 50 < n[t] * (13 + 10 * t);
    end
    $display("candidate ordered conditional weights %0d", ok);

    // A frozen predecessor supplies the weight: x == 1 has weight 3 - tag = 0.
    tag = 3;
    ok = 1;
    repeat (24)
      ok &= randomize(x) with { x dist {0 := tag, 1 := 3 - tag}; } && x == 0;
    $display("candidate frozen predecessor weight %0d", ok);

    // The target's own value gives x == 0 a zero total weight: failure.
    x = 1;
    ok = !randomize(x) with { x dist {0 := x}; } && x == 1;
    $display("candidate zero scope weight failure %0d", ok);

    // x - 1 is evaluated unsigned (2^32 - 1), not treated as a negative weight.
    ok = randomize(x) with { x == 0; x dist {0 := x - 1}; } && x == 0;
    $display("candidate unsigned signed scope weight %0d", ok);

    // A soft dist whose total weight is zero is dropped, not a failure.
    ok = randomize(x) with { soft x dist {[0:1] := x ^ x}; };
    $display("candidate zero soft scope weight %0d", ok);
    $finish;
  end
endmodule
