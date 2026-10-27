// RUN: talos -emit-slang %s | talos-opt --test-talos-native-backend='scheduler=auto sim-opt-level=0 opt-level=0 closed-executable=false stop-after=lowering' --talos-sim-insert-gc-polls | FileCheck %s

// Handles live in an initial block across callees that collect at their own
// polls: an allocating loop, a recursive builder, string churn, and a virtual
// call whose overrides differ. The source is lowered as `talos -O0
// --vpi=off` lowers it (so calls stay calls), ahead of its backend passes;
// ../Runtime/simulation-gc-polled-roots.test runs it with a collection at every
// opportunity, which reclaims a handle if any of these calls skips its roots.

// node::new allocates nothing and stays poll-free at every call.
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-NOT: simulation.gc.safepoint
// heavy::weight polls once per iteration of its allocating loop.
// CHECK-LABEL: simulation.func private @unit_3(
// CHECK: simulation.gc.safepoint
// CHECK-NEXT: simulation.class.alloc
// build and churn: likewise.
// CHECK-LABEL: simulation.func private @unit_4(
// CHECK: simulation.gc.safepoint
// CHECK-NEXT: simulation.class.alloc
// CHECK-LABEL: simulation.func private @unit_5(
// CHECK: simulation.gc.safepoint
// CHECK-NEXT: simulation.class.alloc
// rec polls before recursing, not before its own allocation.
// CHECK-LABEL: simulation.func private @unit_6(
// CHECK: simulation.class.alloc
// CHECK-NOT: simulation.gc.safepoint
// CHECK: simulation.class.direct_call @unit_0 {{.*}}{simulation.gc_poll_free}
// CHECK: simulation.gc.safepoint
// CHECK-NEXT: simulation.call @unit_6(
// text polls in its concatenation loop.
// CHECK-LABEL: simulation.func private @unit_7(
// CHECK: simulation.gc.safepoint
// The initial block keeps its handles registered across every call that may
// poll, and only those.
// CHECK-LABEL: simulation.func private @unit_8(
// CHECK-NOT: simulation.gc.safepoint
// CHECK: simulation.class.direct_call @unit_0 {{.*}}{simulation.gc_poll_free}
// CHECK: simulation.call @unit_4(%{{.*}}) :
// CHECK: simulation.call @unit_5(%{{.*}}) :
// CHECK: simulation.call @unit_6(%{{.*}}) :
// CHECK: simulation.class.direct_call @unit_2 {{.*}}{simulation.gc_poll_free}
// CHECK: simulation.call @unit_7(%{{.*}}) :
// CHECK: simulation.class.virtual_call {{[^{]*}} {simulation.vpi_call_site = @{{[^}]+}}} :
// CHECK-NOT: simulation.gc.safepoint
// CHECK: simulation.return

class node;
  int v;
  node next;
  function new(int v);
    this.v = v;
  endfunction
  virtual function int weight();
    return v;
  endfunction
endclass

class heavy extends node;
  function new(int v);
    super.new(v);
  endfunction
  virtual function int weight();
    int s = 0;
    for (int i = 0; i < 8; i++) begin
      automatic node t = new(i);
      s += t.v;
    end
    return v + s;
  endfunction
endclass

function automatic node build(int n);
  node head = null;
  for (int i = 0; i < n; i++) begin
    automatic node t = new(i);
    t.next = head;
    head = t;
  end
  return head;
endfunction

function automatic int churn(int n);
  int s = 0;
  for (int i = 0; i < n; i++) begin
    automatic node t = new(i);
    s += t.v;
  end
  return s;
endfunction

function automatic int rec(int d);
  node t = new(d);
  if (d == 0)
    return t.v;
  return rec(d - 1) + t.v;
endfunction

function automatic string text(int n);
  string acc = "";
  for (int i = 0; i < n; i++)
    acc = {acc, $sformatf("%0d", i % 10)};
  return acc;
endfunction

module tb3;
  initial begin
    automatic node keep = new(42);
    automatic node list = build(100);
    automatic string label = $sformatf("label%0d", keep.v);
    automatic int s = churn(1000);
    automatic int r = rec(50);
    automatic node h = new(7);
    automatic heavy hv = new(5);
    automatic int w;
    automatic string t = text(40);
    automatic int len = 0;
    h = hv;
    w = h.weight();
    for (node walk = list; walk != null; walk = walk.next)
      len++;
    $display("keep=%0d len=%0d s=%0d r=%0d w=%0d label=%s t=%0d", keep.v, len,
             s, r, w, label, t.len());
  end
endmodule
