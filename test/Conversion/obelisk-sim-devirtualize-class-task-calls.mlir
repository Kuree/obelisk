// RUN: obelisk-opt %s --obelisk-sim-devirtualize-class-calls | FileCheck %s
// RUN: obelisk-opt %s --obelisk-sim-devirtualize-class-calls \
// RUN:   --obelisk-sim-devirtualize-class-calls | FileCheck %s --check-prefix=TWICE
// RUN: obelisk-opt %s --obelisk-sim-devirtualize-class-calls \
// RUN:   --encode-obelisk-sim-to-bytecode='vpi=off' -o /dev/null
// RUN: obelisk-opt %s --obelisk-sim-devirtualize-class-calls \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @task_calls {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "exact"
    obelisk_sim.code_unit.decl 2 in 0 task hierarchy "Derived.run"
    obelisk_sim.code_unit.decl 3 in 0 task hierarchy "MonoBase.run"
    obelisk_sim.code_unit.decl 4 in 0 task hierarchy "PolyLeft.run"
    obelisk_sim.code_unit.decl 5 in 0 task hierarchy "PolyRight.run"
    obelisk_sim.code_unit.decl 6 in 0 task hierarchy "guarded_mono"
    obelisk_sim.code_unit.decl 7 in 0 task hierarchy "guarded_interface"
    obelisk_sim.code_unit.decl 8 in 0 task hierarchy "polymorphic"
    obelisk_sim.code_unit.decl 9 in 0 initial hierarchy "known_null"

    obelisk_sim.class.decl @Runner id 1 {
      is_abstract = true, is_final = false, is_interface = true
    }
    obelisk_sim.class.decl @Base id 2 {
      is_abstract = true, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @Derived id 3 extends @Base {
      is_abstract = false, is_final = true, is_interface = false
    }
    obelisk_sim.class.decl @MonoBase id 4 implements [@Runner] {
      is_abstract = true, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @MonoLeft id 5 extends @MonoBase {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @MonoRight id 6 extends @MonoBase {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @PolyBase id 7 {
      is_abstract = true, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @PolyLeft id 8 extends @PolyBase {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @PolyRight id 9 extends @PolyBase {
      is_abstract = false, is_final = false, is_interface = false
    }

    obelisk_sim.class.method @Runner_run of @Runner slot 4294967295
        signature_id 81 :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@Runner>, i32) -> () {
        is_final = false, is_pure = true, is_static = false,
        is_task = true, is_virtual = true
      }
    obelisk_sim.class.method @Base_run of @Base slot 0 signature_id 71 :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@Base>, i32) -> () {
        is_final = false, is_pure = true, is_static = false,
        is_task = true, is_virtual = true
      }
    obelisk_sim.class.method @Derived_run of @Derived slot 0 signature_id 71
        implemented_by @derived_run :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@Derived>, i32) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = true, is_virtual = true
      }
    obelisk_sim.class.method @MonoBase_run of @MonoBase slot 0 signature_id 81
        implemented_by @mono_run :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@MonoBase>, i32) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = true, is_virtual = true
      }
    obelisk_sim.class.method @PolyBase_run of @PolyBase slot 0
        signature_id 91 :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@PolyBase>, i32) -> () {
        is_final = false, is_pure = true, is_static = false,
        is_task = true, is_virtual = true
      }
    obelisk_sim.class.method @PolyLeft_run of @PolyLeft slot 0
        signature_id 91 implemented_by @poly_left_run :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@PolyLeft>, i32) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = true, is_virtual = true
      }
    obelisk_sim.class.method @PolyRight_run of @PolyRight slot 0
        signature_id 91 implemented_by @poly_right_run :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@PolyRight>, i32) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = true, is_virtual = true
      }

    obelisk_sim.func private @derived_run(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@Derived>
          {obelisk_sim.capture_kind = 1 : i32},
        %input: i32 {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 12 : i32} {
      obelisk_sim.return
    }
    obelisk_sim.func private @mono_run(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@MonoBase>
          {obelisk_sim.capture_kind = 1 : i32},
        %input: i32 {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 3 : i64, entry_kind = 12 : i32} {
      obelisk_sim.return
    }
    obelisk_sim.func private @poly_left_run(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@PolyLeft>
          {obelisk_sim.capture_kind = 1 : i32},
        %input: i32 {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 4 : i64, entry_kind = 12 : i32} {
      obelisk_sim.return
    }
    obelisk_sim.func private @poly_right_run(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@PolyRight>
          {obelisk_sim.capture_kind = 1 : i32},
        %input: i32 {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 5 : i64, entry_kind = 12 : i32} {
      obelisk_sim.return
    }

    obelisk_sim.func @exact(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %object = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@Derived>
      %base = obelisk_sim.class.cast %object :
        !obelisk_sim.class_handle<@Derived> to
        !obelisk_sim.class_handle<@Base>
      %input = arith.constant 7 : i32
      %continued = arith.constant 11 : i64
      obelisk_sim.class.virtual_task_call
        %base[@Base_run] slot 0 signature_id 71
        (%input, %continued) arguments 1 to ^done :
        (!obelisk_sim.class_handle<@Base>, i32, i64) -> ()
    ^done(%value: i64):
      obelisk_sim.return
    }

    obelisk_sim.func private @guarded_mono(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %receiver: !obelisk_sim.class_handle<@MonoBase>
          {obelisk_sim.capture_kind = 1 : i32},
        %input: i32 {obelisk_sim.capture_kind = 2 : i32},
        %continued: i64 {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 6 : i64, entry_kind = 12 : i32} {
      obelisk_sim.class.virtual_task_call
        %receiver[@MonoBase_run] slot 0 signature_id 81
        (%input, %continued) arguments 1 to ^done
        {site = #obelisk_sim.continuation<id = 7>} :
        (!obelisk_sim.class_handle<@MonoBase>, i32, i64) -> ()
    ^done(%value: i64):
      obelisk_sim.return
    }

    obelisk_sim.func private @guarded_interface(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %receiver: !obelisk_sim.class_handle<@Runner>
          {obelisk_sim.capture_kind = 1 : i32},
        %input: i32 {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 7 : i64, entry_kind = 12 : i32} {
      obelisk_sim.class.virtual_task_call
        %receiver[@Runner_run] slot 4294967295 signature_id 81
        (%input) arguments 1 to ^done :
        (!obelisk_sim.class_handle<@Runner>, i32) -> ()
    ^done:
      obelisk_sim.return
    }

    obelisk_sim.func private @polymorphic(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %receiver: !obelisk_sim.class_handle<@PolyBase>
          {obelisk_sim.capture_kind = 1 : i32},
        %input: i32 {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 8 : i64, entry_kind = 12 : i32} {
      obelisk_sim.class.virtual_task_call
        %receiver[@PolyBase_run] slot 0 signature_id 91
        (%input) arguments 1 to ^done :
        (!obelisk_sim.class_handle<@PolyBase>, i32) -> ()
    ^done:
      obelisk_sim.return
    }

    obelisk_sim.func @known_null(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 9 : i64, entry_kind = 1 : i32} {
      %receiver = obelisk_sim.class.null :
        !obelisk_sim.class_handle<@MonoBase>
      %input = arith.constant 3 : i32
      obelisk_sim.class.virtual_task_call
        %receiver[@MonoBase_run] slot 0 signature_id 81
        (%input) arguments 1 to ^done :
        (!obelisk_sim.class_handle<@MonoBase>, i32) -> ()
    ^done:
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: obelisk_sim.func @exact
// CHECK: %[[BASE:.*]] = obelisk_sim.class.cast
// CHECK: %[[THIS:.*]] = obelisk_sim.class.cast %[[BASE]]
// CHECK-NEXT: obelisk_sim.task.call @derived_run(%{{.*}}, %[[THIS]], %{{.*}}, %{{.*}}) arguments 3 to ^[[EXACT_DONE:[a-zA-Z0-9_]+]]
// CHECK: ^[[EXACT_DONE]](%{{.*}}: i64):

// CHECK-LABEL: obelisk_sim.func private @guarded_mono
// CHECK: %[[NULL:.*]] = obelisk_sim.managed.is_null %[[MONO:.*]]
// CHECK-NEXT: cf.cond_br %[[NULL]], ^[[NULL_BLOCK:[a-zA-Z0-9_]+]], ^[[DIRECT_BLOCK:[a-zA-Z0-9_]+]]
// CHECK: ^[[NULL_BLOCK]]:
// CHECK: %[[CANONICAL_NULL:.*]] = obelisk_sim.class.null
// CHECK-NEXT: obelisk_sim.class.virtual_task_call %[[CANONICAL_NULL]]
// CHECK-SAME: arguments 1 to ^[[MONO_DONE:[a-zA-Z0-9_]+]]
// CHECK-SAME: site = #obelisk_sim.continuation<id = 7>
// CHECK: ^[[DIRECT_BLOCK]]:
// CHECK: obelisk_sim.task.call @mono_run
// CHECK-SAME: arguments 3 to ^[[MONO_DONE]]
// CHECK-SAME: site = #obelisk_sim.continuation<id = 7>

// CHECK-LABEL: obelisk_sim.func private @guarded_interface
// CHECK: %[[INTERFACE_NULL:[a-zA-Z0-9_]+]] = obelisk_sim.managed.is_null %[[INTERFACE:[a-zA-Z0-9_]+]]
// CHECK: %[[NULL_INTERFACE:.*]] = obelisk_sim.class.null {{.*}}class_handle<@Runner>
// CHECK-NEXT: obelisk_sim.class.virtual_task_call %[[NULL_INTERFACE]]
// CHECK: %[[MONO_THIS:.*]] = obelisk_sim.class.cast %[[INTERFACE]]
// CHECK-NEXT: obelisk_sim.task.call @mono_run(%{{.*}}, %[[MONO_THIS]], %{{.*}}) arguments 3

// CHECK-LABEL: obelisk_sim.func private @polymorphic
// CHECK-NOT: obelisk_sim.managed.is_null
// CHECK: obelisk_sim.class.virtual_task_call

// CHECK-LABEL: obelisk_sim.func @known_null
// CHECK-NOT: obelisk_sim.managed.is_null
// CHECK: obelisk_sim.class.virtual_task_call

// TWICE-COUNT-2: obelisk_sim.managed.is_null
// TWICE-NOT: obelisk_sim.managed.is_null
