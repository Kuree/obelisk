// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-lower-unit),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// IEEE 1800-2017 28.10: default pull sources drive 1 and 0 at pull
// strength. Equal pull strengths resolve to x, while an explicit strong1
// pullup dominates an explicit weak0 pulldown. Both execution tiers must
// observe the same strength-resolved values.
// CHECK: 10x1

!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @pull_primitives {
    simulation.scope.decl 0 hierarchy "top"
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 1 in 0 : !simulation.logic<1> design
    simulation.net.decl 2 in 0 : !simulation.logic<1> design
    simulation.net.decl 3 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design
        {strength0 = 5 : i32, strength1 = 5 : i32}
    simulation.driver.decl 1 in 0 drives 1 : !simulation.logic<1> design
        {strength0 = 5 : i32, strength1 = 5 : i32}
    simulation.driver.decl 2 in 0 drives 2 : !simulation.logic<1> design
        {strength0 = 5 : i32, strength1 = 5 : i32}
    simulation.driver.decl 3 in 0 drives 2 : !simulation.logic<1> design
        {strength0 = 5 : i32, strength1 = 5 : i32}
    simulation.driver.decl 4 in 0 drives 3 : !simulation.logic<1> design
        {strength0 = 5 : i32, strength1 = 6 : i32}
    simulation.driver.decl 5 in 0 drives 3 : !simulation.logic<1> design
        {strength0 = 3 : i32, strength1 = 5 : i32}
    simulation.code_unit.decl 9950000 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 9950001 in 0 continuous hierarchy "top.pullup"
    simulation.code_unit.decl 9950002 in 0 continuous hierarchy "top.pulldown"
    simulation.code_unit.decl 9950003 in 0 initial hierarchy "top.check"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9950000 : i64,
                    simulation.lowered} {
      %d0 = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<1>>
      %d1 = simulation.context.driver %ctx[1] :
          !simulation.driver<!simulation.logic<1>>
      %d2 = simulation.context.driver %ctx[2] :
          !simulation.driver<!simulation.logic<1>>
      %d3 = simulation.context.driver %ctx[3] :
          !simulation.driver<!simulation.logic<1>>
      %d4 = simulation.context.driver %ctx[4] :
          !simulation.driver<!simulation.logic<1>>
      %d5 = simulation.context.driver %ctx[5] :
          !simulation.driver<!simulation.logic<1>>
      %n0 = simulation.context.net %ctx[0] :
          !simulation.net<!simulation.logic<1>>
      %n1 = simulation.context.net %ctx[1] :
          !simulation.net<!simulation.logic<1>>
      %n2 = simulation.context.net %ctx[2] :
          !simulation.net<!simulation.logic<1>>
      %n3 = simulation.context.net %ctx[3] :
          !simulation.net<!simulation.logic<1>>
      %p0 = simulation.spawn @pullup(%ctx, %d0) :
          !simulation.context, !simulation.driver<!simulation.logic<1>> ->
          !simulation.process
      %p1 = simulation.spawn @pulldown(%ctx, %d1) :
          !simulation.context, !simulation.driver<!simulation.logic<1>> ->
          !simulation.process
      %p2 = simulation.spawn @pullup(%ctx, %d2) :
          !simulation.context, !simulation.driver<!simulation.logic<1>> ->
          !simulation.process
      %p3 = simulation.spawn @pulldown(%ctx, %d3) :
          !simulation.context, !simulation.driver<!simulation.logic<1>> ->
          !simulation.process
      %p4 = simulation.spawn @pullup(%ctx, %d4) :
          !simulation.context, !simulation.driver<!simulation.logic<1>> ->
          !simulation.process
      %p5 = simulation.spawn @pulldown(%ctx, %d5) :
          !simulation.context, !simulation.driver<!simulation.logic<1>> ->
          !simulation.process
      %check = simulation.spawn @check(%ctx, %n0, %n1, %n2, %n3) :
          !simulation.context, !simulation.net<!simulation.logic<1>>,
          !simulation.net<!simulation.logic<1>>,
          !simulation.net<!simulation.logic<1>>,
          !simulation.net<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }

    simulation.func private @pullup(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %out: !simulation.driver<!simulation.logic<1>>
            {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 7 : i32, code_unit_id = 9950001 : i64,
                    schedule.primitive_name = "pullup",
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.out", argument = 1, kind = lvalue_only, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {
          assignment_kind = 0 : i32, node_id = 1 : i64,
          semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {
            node_id = 2 : i64, referenced_path = "top.out",
            referenced_symbol = @out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {
            node_id = 3 : i64, semantic_type = !logic1} {}
      }
      simulation.return
    }

    simulation.func private @pulldown(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %out: !simulation.driver<!simulation.logic<1>>
            {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 7 : i32, code_unit_id = 9950002 : i64,
                    schedule.primitive_name = "pulldown",
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.out", argument = 1, kind = lvalue_only, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {
          assignment_kind = 0 : i32, node_id = 4 : i64,
          semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {
            node_id = 5 : i64, referenced_path = "top.out",
            referenced_symbol = @out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {
            node_id = 6 : i64, semantic_type = !logic1} {}
      }
      simulation.return
    }

    simulation.func private @check(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %n0: !simulation.net<!simulation.logic<1>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 0 : i64},
        %n1: !simulation.net<!simulation.logic<1>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 1 : i64},
        %n2: !simulation.net<!simulation.logic<1>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 2 : i64},
        %n3: !simulation.net<!simulation.logic<1>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 3 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9950003 : i64,
                    simulation.lowered} {
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^resume
    ^resume:
      %v0 = simulation.net.read %n0 :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %v1 = simulation.net.read %n1 :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %v2 = simulation.net.read %n2 :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %v3 = simulation.net.read %n3 :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %format = simulation.bytes.constant "%b%b%b%b"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%format, %v0, %v1, %v2, %v3)
          newline = true radix = <decimal> flags = [0, 0, 0, 0, 0] :
          !simulation.bytes, !simulation.logic<1>, !simulation.logic<1>,
          !simulation.logic<1>, !simulation.logic<1>
      simulation.return
    }
  }
}
