// RUN: obelisk-opt %s --obelisk-sim-materialize-dpi-exports \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=NATIVE --check-prefix=COMMON
// RUN: obelisk-opt %s --obelisk-sim-materialize-dpi-exports \
// RUN:   --encode-obelisk-sim-to-bytecode='vpi=off require-bytecode=true' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=BYTECODE --check-prefix=COMMON
// RUN: obelisk-opt %s --obelisk-sim-materialize-dpi-exports \
// RUN:   --encode-obelisk-sim-to-bytecode='vpi=off require-bytecode=false' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=AUTO --check-prefix=COMMON
// RUN: obelisk-opt %s --obelisk-sim-materialize-dpi-exports \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | mlir-translate --allow-unregistered-dialect --mlir-to-llvmir \
// RUN:   | opt -passes=verify -disable-output
// RUN: %llvm_dist/bin/clang -std=c11 -c \
// RUN:   %S/Inputs/dpi-export-runtime.c -o %t.c.o
// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk-sim-materialize-dpi-exports,obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --allow-unregistered-dialect --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.native.o
// RUN: %llvm_dist/bin/clang++ %t.native.o %t.c.o \
// RUN:   %native_support/libobelisk_rt.a %native_support/libc++.a \
// RUN:   %native_support/libc++abi.a %native_support/libunwind.a \
// RUN:   -nostdlib++ -lpthread -ldl -o %t.native
// RUN: %t.native | FileCheck %s --check-prefix=RUNTIME
// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk-sim-materialize-dpi-exports,obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --allow-unregistered-dialect --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.bytecode.o
// RUN: %llvm_dist/bin/clang++ %t.bytecode.o %t.c.o \
// RUN:   %native_support/libobelisk_rt.a %native_support/libc++.a \
// RUN:   %native_support/libc++abi.a %native_support/libunwind.a \
// RUN:   -nostdlib++ -lpthread -ldl -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s --check-prefix=RUNTIME
// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk-sim-materialize-dpi-exports,obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=false},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --allow-unregistered-dialect --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.auto.o
// RUN: %llvm_dist/bin/clang++ %t.auto.o %t.c.o \
// RUN:   %native_support/libobelisk_rt.a %native_support/libc++.a \
// RUN:   %native_support/libc++abi.a %native_support/libunwind.a \
// RUN:   -nostdlib++ -lpthread -ldl -o %t.auto
// RUN: %t.auto | FileCheck %s --check-prefix=RUNTIME

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk_sim.has_dpi_exports
} {
  obelisk_sim.design @exports {
    obelisk_sim.scope.decl 0 hierarchy "exports"
    obelisk_sim.scope.decl 1 parent 0 hierarchy "exports.child"
    obelisk_sim.storage.decl 0 in 0 : i8 design
    obelisk_sim.storage.decl 1 in 0 : i8 design
    obelisk_sim.storage.decl 2 in 1 : i8 design
    obelisk_sim.code_unit.decl 1 in 0 root_initializer
        hierarchy "exports.root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "exports.initial"
    obelisk_sim.code_unit.decl 10 in 0 function hierarchy "exports.narrow"
    obelisk_sim.code_unit.decl 11 in 0 function hierarchy "exports.vector"
    obelisk_sim.code_unit.decl 12 in 0 function hierarchy "exports.output"
    obelisk_sim.code_unit.decl 13 in 0 function hierarchy "exports.ping"
    obelisk_sim.code_unit.decl 14 in 0 function hierarchy "exports.logic"
    obelisk_sim.code_unit.decl 15 in 0 function hierarchy "exports.branch"
    obelisk_sim.code_unit.decl 16 in 0 function hierarchy "exports.echo"
    obelisk_sim.code_unit.decl 17 in 0 function hierarchy "exports.reenter"
    obelisk_sim.code_unit.decl 18 in 0 function hierarchy "exports.scoped"
    obelisk_sim.code_unit.decl 19 in 1 function hierarchy "exports.child.scoped"
    obelisk_sim.code_unit.decl 20 in 0 function hierarchy "exports.host" {
      obelisk_sim.dpi_abi_signature = [],
      obelisk_sim.dpi_c_identifier = "host",
      obelisk_sim.dpi_import,
      obelisk_sim.dpi_import_id = 200 : i32,
      obelisk_sim.dpi_logical_inputs = 0 : i32
    }
    obelisk_sim.code_unit.decl 21 in 0 function hierarchy "exports.nested" {
      obelisk_sim.dpi_abi_signature = [
        #obelisk_sim.dpi_abi<kind = string, direction = result, width = 64,
                              fourState = false, isSigned = false>
      ],
      obelisk_sim.dpi_c_identifier = "nested_echo",
      obelisk_sim.dpi_import,
      obelisk_sim.dpi_import_id = 201 : i32,
      obelisk_sim.dpi_logical_inputs = 0 : i32
    }

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %initial = obelisk_sim.spawn @initial(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @initial(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %status = obelisk_sim.dpi.call "host" id 200 scope 0
          context %ctx : !obelisk_sim.context() {
            abi_signature = [], is_context = true, is_pure = false,
            is_task = false, source_column = 1 : i32,
            source_file = "dpi-export-runtime.mlir", source_line = 1 : i32
          } : () -> !obelisk_rt.status
      obelisk_sim.return
    }

    obelisk_sim.func @narrow(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %signed: i8 {obelisk_sim.capture_kind = 2 : i32},
        %bit: i1 {obelisk_sim.capture_kind = 2 : i32},
        %logic: !obelisk_sim.logic<1>
            {obelisk_sim.capture_kind = 2 : i32}) -> i8
        attributes {
          code_unit_id = 10 : i64, entry_kind = 8 : i32,
          obelisk_sim.dpi_export,
          obelisk_sim.dpi_c_identifier = "export_narrow",
          obelisk_sim.dpi_export_id = -2001470629 : i32,
          obelisk_sim.dpi_scope_id = 0 : i64,
          obelisk_sim.dpi_logical_inputs = 3 : i32,
          obelisk_sim.dpi_abi_signature = [
            #obelisk_sim.dpi_abi<kind = byte, direction = input, width = 8,
                                  fourState = false, isSigned = true>,
            #obelisk_sim.dpi_abi<kind = bit, direction = input, width = 1,
                                  fourState = false, isSigned = true>,
            #obelisk_sim.dpi_abi<kind = logic, direction = input, width = 1,
                                  fourState = true, isSigned = true>,
            #obelisk_sim.dpi_abi<kind = byte, direction = result, width = 8,
                                  fourState = false, isSigned = true>
          ]
        } {
      %extended = arith.extui %bit : i1 to i8
      %sum = arith.addi %signed, %extended : i8
      obelisk_sim.return %sum : i8
    }

    obelisk_sim.func @vector(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.logic<37>
            {obelisk_sim.capture_kind = 2 : i32}) -> !obelisk_sim.logic<37>
        attributes {
          code_unit_id = 11 : i64, entry_kind = 8 : i32,
          obelisk_sim.dpi_export,
          obelisk_sim.dpi_c_identifier = "export_vector",
          obelisk_sim.dpi_export_id = 101 : i32,
          obelisk_sim.dpi_scope_id = 0 : i64,
          obelisk_sim.dpi_logical_inputs = 1 : i32,
          obelisk_sim.dpi_abi_signature = [
            #obelisk_sim.dpi_abi<kind = logic_vector, direction = input,
                                  width = 37, fourState = true,
                                  isSigned = false>,
            #obelisk_sim.dpi_abi<kind = logic_vector, direction = result,
                                  width = 37, fourState = true,
                                  isSigned = false>
          ]
        } {
      obelisk_sim.return %value : !obelisk_sim.logic<37>
    }

    obelisk_sim.func @output(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %unused: i16 {obelisk_sim.capture_kind = 2 : i32}) -> i16
        attributes {
          code_unit_id = 12 : i64, entry_kind = 8 : i32,
          obelisk_sim.dpi_export,
          obelisk_sim.dpi_c_identifier = "export_output",
          obelisk_sim.dpi_export_id = 102 : i32,
          obelisk_sim.dpi_scope_id = 0 : i64,
          obelisk_sim.dpi_logical_inputs = 1 : i32,
          obelisk_sim.dpi_abi_signature = [
            #obelisk_sim.dpi_abi<kind = shortint, direction = output,
                                  width = 16, fourState = false,
                                  isSigned = true>,
            #obelisk_sim.dpi_abi<kind = shortint, direction = output,
                                  width = 16, fourState = false,
                                  isSigned = true>
          ]
        } {
      %answer = arith.constant 4660 : i16
      obelisk_sim.return %answer : i16
    }

    obelisk_sim.func @ping(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {
          code_unit_id = 13 : i64, entry_kind = 8 : i32,
          obelisk_sim.dpi_export,
          obelisk_sim.dpi_c_identifier = "export_ping",
          obelisk_sim.dpi_export_id = 103 : i32,
          obelisk_sim.dpi_scope_id = 0 : i64,
          obelisk_sim.dpi_logical_inputs = 0 : i32,
          obelisk_sim.dpi_abi_signature = []
        } {
      obelisk_sim.return
    }

    obelisk_sim.func @logic(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.logic<1>
            {obelisk_sim.capture_kind = 2 : i32}) -> !obelisk_sim.logic<1>
        attributes {
          code_unit_id = 14 : i64, entry_kind = 8 : i32,
          obelisk_sim.dpi_export,
          obelisk_sim.dpi_c_identifier = "export_logic",
          obelisk_sim.dpi_export_id = 104 : i32,
          obelisk_sim.dpi_scope_id = 0 : i64,
          obelisk_sim.dpi_logical_inputs = 1 : i32,
          obelisk_sim.dpi_abi_signature = [
            #obelisk_sim.dpi_abi<kind = logic, direction = input, width = 1,
                                  fourState = true, isSigned = true>,
            #obelisk_sim.dpi_abi<kind = logic, direction = result, width = 1,
                                  fourState = true, isSigned = true>
          ]
        } {
      obelisk_sim.return %value : !obelisk_sim.logic<1>
    }

    obelisk_sim.func @branch_capture(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i8 {obelisk_sim.capture_kind = 2 : i32},
        %bias: !obelisk_sim.ref<i8>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64}) -> i8
        attributes {
          code_unit_id = 15 : i64, entry_kind = 8 : i32,
          obelisk_sim.dpi_export,
          obelisk_sim.dpi_c_identifier = "export_branch_capture",
          obelisk_sim.dpi_export_id = 105 : i32,
          obelisk_sim.dpi_scope_id = 0 : i64,
          obelisk_sim.dpi_logical_inputs = 1 : i32,
          obelisk_sim.dpi_abi_signature = [
            #obelisk_sim.dpi_abi<kind = byte, direction = input, width = 8,
                                  fourState = false, isSigned = true>,
            #obelisk_sim.dpi_abi<kind = byte, direction = result, width = 8,
                                  fourState = false, isSigned = true>
          ]
        } {
      %zero = arith.constant 0 : i8
      %positive = arith.cmpi sgt, %value, %zero : i8
      cf.cond_br %positive, ^captured, ^original
    ^captured:
      %captured = obelisk_sim.ref.load %bias : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.return %captured : i8
    ^original:
      obelisk_sim.return %value : i8
    }

    obelisk_sim.func @echo_string(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.string
            {obelisk_sim.capture_kind = 2 : i32}) -> !obelisk_sim.string
        attributes {
          code_unit_id = 16 : i64, entry_kind = 8 : i32,
          obelisk_sim.dpi_export,
          obelisk_sim.dpi_c_identifier = "export_echo_string",
          obelisk_sim.dpi_export_id = 106 : i32,
          obelisk_sim.dpi_scope_id = 0 : i64,
          obelisk_sim.dpi_logical_inputs = 1 : i32,
          obelisk_sim.dpi_abi_signature = [
            #obelisk_sim.dpi_abi<kind = string, direction = input, width = 64,
                                  fourState = false, isSigned = false>,
            #obelisk_sim.dpi_abi<kind = string, direction = result, width = 64,
                                  fourState = false, isSigned = false>
          ]
        } {
      obelisk_sim.return %value : !obelisk_sim.string
    }

    obelisk_sim.func @reenter_string(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        -> !obelisk_sim.string
        attributes {
          code_unit_id = 17 : i64, entry_kind = 8 : i32,
          obelisk_sim.dpi_export,
          obelisk_sim.dpi_c_identifier = "export_reenter_string",
          obelisk_sim.dpi_export_id = 107 : i32,
          obelisk_sim.dpi_scope_id = 0 : i64,
          obelisk_sim.dpi_logical_inputs = 0 : i32,
          obelisk_sim.dpi_abi_signature = [
            #obelisk_sim.dpi_abi<kind = string, direction = result, width = 64,
                                  fourState = false, isSigned = false>
          ]
        } {
      %nested:2 = obelisk_sim.dpi.call "nested_echo" id 201 scope 0
          context %ctx : !obelisk_sim.context() {
            abi_signature = [
              #obelisk_sim.dpi_abi<kind = string, direction = result,
                                    width = 64, fourState = false,
                                    isSigned = false>
            ],
            is_context = true, is_pure = false, is_task = false,
            source_column = 1 : i32, source_file = "dpi-export-runtime.mlir",
            source_line = 2 : i32
          } : () -> (!obelisk_sim.string, !obelisk_rt.status)
      obelisk_sim.return %nested#0 : !obelisk_sim.string
    }

    obelisk_sim.func @scoped0(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %state: !obelisk_sim.ref<i8>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64}) -> i8
        attributes {
          code_unit_id = 18 : i64, entry_kind = 8 : i32,
          obelisk_sim.dpi_export,
          obelisk_sim.dpi_c_identifier = "export_scoped",
          obelisk_sim.dpi_export_id = 108 : i32,
          obelisk_sim.dpi_scope_id = 0 : i64,
          obelisk_sim.dpi_logical_inputs = 0 : i32,
          obelisk_sim.dpi_abi_signature = [
            #obelisk_sim.dpi_abi<kind = byte, direction = result, width = 8,
                                  fourState = false, isSigned = true>
          ]
        } {
      %value = obelisk_sim.ref.load %state : !obelisk_sim.ref<i8> -> i8
      %eleven = arith.constant 11 : i8
      %result = arith.addi %value, %eleven : i8
      obelisk_sim.return %result : i8
    }

    obelisk_sim.func @scoped1(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %state: !obelisk_sim.ref<i8>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 2 : i64}) -> i8
        attributes {
          code_unit_id = 19 : i64, entry_kind = 8 : i32,
          obelisk_sim.dpi_export,
          obelisk_sim.dpi_c_identifier = "export_scoped",
          obelisk_sim.dpi_export_id = 108 : i32,
          obelisk_sim.dpi_scope_id = 1 : i64,
          obelisk_sim.dpi_logical_inputs = 0 : i32,
          obelisk_sim.dpi_abi_signature = [
            #obelisk_sim.dpi_abi<kind = byte, direction = result, width = 8,
                                  fourState = false, isSigned = true>
          ]
        } {
      %value = obelisk_sim.ref.load %state : !obelisk_sim.ref<i8> -> i8
      %twentytwo = arith.constant 22 : i8
      %result = arith.addi %value, %twentytwo : i8
      obelisk_sim.return %result : i8
    }
  }
}

// COMMON-DAG: llvm.func @export_narrow(%{{.*}}: i8 {llvm.signext}, %{{.*}}: i8 {llvm.zeroext}, %{{.*}}: i8 {llvm.zeroext}) -> (i8 {llvm.signext})
// COMMON-DAG: llvm.and {{.*}}, %{{.*}} : i8
// COMMON-DAG: llvm.func @export_vector(%{{.*}}: !llvm.ptr, %{{.*}}: !llvm.ptr)
// COMMON-DAG: llvm.call @obelisk_rt_v1_dpi_export_unpack_vector
// COMMON-DAG: llvm.call @obelisk_rt_v1_dpi_export_pack_vector
// COMMON-DAG: llvm.func @export_output(%{{.*}}: !llvm.ptr)
// COMMON-DAG: llvm.store {{.*}}, {{.*}} {alignment = 1 : i64} : i16, !llvm.ptr
// COMMON-DAG: llvm.func @export_ping()
// COMMON-DAG: llvm.func @export_logic(%{{.*}}: i8 {llvm.zeroext}) -> (i8 {llvm.zeroext})
// COMMON-DAG: llvm.func @export_branch_capture(%{{.*}}: i8 {llvm.signext}) -> (i8 {llvm.signext})
// COMMON-DAG: llvm.func @export_echo_string
// COMMON-DAG: llvm.func @export_reenter_string
// COMMON-DAG: llvm.func @export_scoped
// COMMON-DAG: llvm.call @obelisk_rt_v1_export_call
// COMMON-DAG: llvm.mlir.global internal constant @__obelisk_dpi_exports_v1
// NATIVE-NOT: obelisk.feature.dpi_export_bytecode
// NATIVE-NOT: @obelisk_rt_v1_dpi_export_bytecode_link_anchor
// BYTECODE-DAG: obelisk.feature.dpi_export_bytecode
// BYTECODE-DAG: llvm.call @obelisk_rt_v1_dpi_export_bytecode_link_anchor
// AUTO-NOT: obelisk.feature.dpi_export_bytecode
// AUTO-NOT: @obelisk_rt_v1_dpi_export_bytecode_link_anchor
// RUNTIME: narrow=-1 logic=2 branch=0 scopes=11/22 direct=alpha nested=nested vector=89abcdef/12345678/15/2 output=1234
