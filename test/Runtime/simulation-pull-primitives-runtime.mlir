// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
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
  obelisk_sim.design @pull_primitives {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 2 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 3 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design
        {strength0 = 5 : i32, strength1 = 5 : i32}
    obelisk_sim.driver.decl 1 in 0 drives 1 : !obelisk_sim.logic<1> design
        {strength0 = 5 : i32, strength1 = 5 : i32}
    obelisk_sim.driver.decl 2 in 0 drives 2 : !obelisk_sim.logic<1> design
        {strength0 = 5 : i32, strength1 = 5 : i32}
    obelisk_sim.driver.decl 3 in 0 drives 2 : !obelisk_sim.logic<1> design
        {strength0 = 5 : i32, strength1 = 5 : i32}
    obelisk_sim.driver.decl 4 in 0 drives 3 : !obelisk_sim.logic<1> design
        {strength0 = 5 : i32, strength1 = 6 : i32}
    obelisk_sim.driver.decl 5 in 0 drives 3 : !obelisk_sim.logic<1> design
        {strength0 = 3 : i32, strength1 = 5 : i32}
    obelisk_sim.code_unit.decl 9950000 in 0 root_initializer hierarchy "top.root"
    obelisk_sim.code_unit.decl 9950001 in 0 continuous hierarchy "top.pullup"
    obelisk_sim.code_unit.decl 9950002 in 0 continuous hierarchy "top.pulldown"
    obelisk_sim.code_unit.decl 9950003 in 0 initial hierarchy "top.check"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9950000 : i64,
                    obelisk_sim.lowered} {
      %d0 = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d1 = obelisk_sim.context.driver %ctx[1] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d2 = obelisk_sim.context.driver %ctx[2] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d3 = obelisk_sim.context.driver %ctx[3] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d4 = obelisk_sim.context.driver %ctx[4] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d5 = obelisk_sim.context.driver %ctx[5] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %n0 = obelisk_sim.context.net %ctx[0] :
          !obelisk_sim.net<!obelisk_sim.logic<1>>
      %n1 = obelisk_sim.context.net %ctx[1] :
          !obelisk_sim.net<!obelisk_sim.logic<1>>
      %n2 = obelisk_sim.context.net %ctx[2] :
          !obelisk_sim.net<!obelisk_sim.logic<1>>
      %n3 = obelisk_sim.context.net %ctx[3] :
          !obelisk_sim.net<!obelisk_sim.logic<1>>
      %p0 = obelisk_sim.spawn @pullup(%ctx, %d0) :
          !obelisk_sim.context, !obelisk_sim.driver<!obelisk_sim.logic<1>> ->
          !obelisk_sim.process
      %p1 = obelisk_sim.spawn @pulldown(%ctx, %d1) :
          !obelisk_sim.context, !obelisk_sim.driver<!obelisk_sim.logic<1>> ->
          !obelisk_sim.process
      %p2 = obelisk_sim.spawn @pullup(%ctx, %d2) :
          !obelisk_sim.context, !obelisk_sim.driver<!obelisk_sim.logic<1>> ->
          !obelisk_sim.process
      %p3 = obelisk_sim.spawn @pulldown(%ctx, %d3) :
          !obelisk_sim.context, !obelisk_sim.driver<!obelisk_sim.logic<1>> ->
          !obelisk_sim.process
      %p4 = obelisk_sim.spawn @pullup(%ctx, %d4) :
          !obelisk_sim.context, !obelisk_sim.driver<!obelisk_sim.logic<1>> ->
          !obelisk_sim.process
      %p5 = obelisk_sim.spawn @pulldown(%ctx, %d5) :
          !obelisk_sim.context, !obelisk_sim.driver<!obelisk_sim.logic<1>> ->
          !obelisk_sim.process
      %check = obelisk_sim.spawn @check(%ctx, %n0, %n1, %n2, %n3) :
          !obelisk_sim.context, !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @pullup(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %out: !obelisk_sim.driver<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 7 : i32, code_unit_id = 9950001 : i64,
                    obelisk_sim.primitive_name = "pullup",
                    obelisk_sim.bindings = [
                      #obelisk_sim.argument_binding<path = "top.out", argument = 1, kind = lvalue_only, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {
          assignment_kind = 0 : i32, node_id = 1 : i64,
          semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {
            node_id = 2 : i64, referenced_path = "top.out",
            referenced_symbol = @out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {
            node_id = 3 : i64, semantic_type = !logic1} {}
      }
      obelisk_sim.return
    }

    obelisk_sim.func private @pulldown(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %out: !obelisk_sim.driver<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 7 : i32, code_unit_id = 9950002 : i64,
                    obelisk_sim.primitive_name = "pulldown",
                    obelisk_sim.bindings = [
                      #obelisk_sim.argument_binding<path = "top.out", argument = 1, kind = lvalue_only, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {
          assignment_kind = 0 : i32, node_id = 4 : i64,
          semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {
            node_id = 5 : i64, referenced_path = "top.out",
            referenced_symbol = @out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {
            node_id = 6 : i64, semantic_type = !logic1} {}
      }
      obelisk_sim.return
    }

    obelisk_sim.func private @check(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %n0: !obelisk_sim.net<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 4 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %n1: !obelisk_sim.net<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 4 : i32,
             obelisk_sim.descriptor_id = 1 : i64},
        %n2: !obelisk_sim.net<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 4 : i32,
             obelisk_sim.descriptor_id = 2 : i64},
        %n3: !obelisk_sim.net<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 4 : i32,
             obelisk_sim.descriptor_id = 3 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9950003 : i64,
                    obelisk_sim.lowered} {
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^resume
    ^resume:
      %v0 = obelisk_sim.net.read %n0 :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %v1 = obelisk_sim.net.read %n1 :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %v2 = obelisk_sim.net.read %n2 :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %v3 = obelisk_sim.net.read %n3 :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %format = obelisk_sim.bytes.constant "%b%b%b%b"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%format, %v0, %v1, %v2, %v3)
          newline = true radix = 10 flags = [0, 0, 0, 0, 0] :
          !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>,
          !obelisk_sim.logic<1>, !obelisk_sim.logic<1>
      obelisk_sim.return
    }
  }
}
