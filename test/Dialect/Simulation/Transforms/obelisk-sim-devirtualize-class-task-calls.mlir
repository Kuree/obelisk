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
  simulation.design @task_calls {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 initial hierarchy "exact"
    simulation.code_unit.decl 2 in 0 task hierarchy "Derived.run"
    simulation.code_unit.decl 3 in 0 task hierarchy "MonoBase.run"
    simulation.code_unit.decl 4 in 0 task hierarchy "PolyLeft.run"
    simulation.code_unit.decl 5 in 0 task hierarchy "PolyRight.run"
    simulation.code_unit.decl 6 in 0 task hierarchy "guarded_mono"
    simulation.code_unit.decl 7 in 0 task hierarchy "guarded_interface"
    simulation.code_unit.decl 8 in 0 task hierarchy "polymorphic"
    simulation.code_unit.decl 9 in 0 initial hierarchy "known_null"

    simulation.class.decl @Runner id 1 {
      is_abstract = true, is_final = false, is_interface = true
    }
    simulation.class.decl @Base id 2 {
      is_abstract = true, is_final = false, is_interface = false
    }
    simulation.class.decl @Derived id 3 extends @Base {
      is_abstract = false, is_final = true, is_interface = false
    }
    simulation.class.decl @MonoBase id 4 implements [@Runner] {
      is_abstract = true, is_final = false, is_interface = false
    }
    simulation.class.decl @MonoLeft id 5 extends @MonoBase {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @MonoRight id 6 extends @MonoBase {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @PolyBase id 7 {
      is_abstract = true, is_final = false, is_interface = false
    }
    simulation.class.decl @PolyLeft id 8 extends @PolyBase {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @PolyRight id 9 extends @PolyBase {
      is_abstract = false, is_final = false, is_interface = false
    }

    simulation.class.method @Runner_run of @Runner slot 4294967295
        signature_id 81 interface_ordinal 0 :
      (!simulation.context, !simulation.class_handle<@Runner>, i32) -> () {
        is_final = false, is_pure = true, is_static = false,
        is_task = true, is_virtual = true
      }
    simulation.class.method @Base_run of @Base slot 0 signature_id 71 :
      (!simulation.context, !simulation.class_handle<@Base>, i32) -> () {
        is_final = false, is_pure = true, is_static = false,
        is_task = true, is_virtual = true
      }
    simulation.class.method @Derived_run of @Derived slot 0 signature_id 71
        implemented_by @derived_run :
      (!simulation.context, !simulation.class_handle<@Derived>, i32) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = true, is_virtual = true
      }
    simulation.class.method @MonoBase_run of @MonoBase slot 0 signature_id 81
        implemented_by @mono_run :
      (!simulation.context, !simulation.class_handle<@MonoBase>, i32) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = true, is_virtual = true
      }
    simulation.class.method @PolyBase_run of @PolyBase slot 0
        signature_id 91 :
      (!simulation.context, !simulation.class_handle<@PolyBase>, i32) -> () {
        is_final = false, is_pure = true, is_static = false,
        is_task = true, is_virtual = true
      }
    simulation.class.method @PolyLeft_run of @PolyLeft slot 0
        signature_id 91 implemented_by @poly_left_run :
      (!simulation.context, !simulation.class_handle<@PolyLeft>, i32) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = true, is_virtual = true
      }
    simulation.class.method @PolyRight_run of @PolyRight slot 0
        signature_id 91 implemented_by @poly_right_run :
      (!simulation.context, !simulation.class_handle<@PolyRight>, i32) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = true, is_virtual = true
      }

    simulation.func private @derived_run(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@Derived>
          {simulation.capture_kind = 1 : i32},
        %input: i32 {simulation.capture_kind = 2 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 12 : i32} {
      simulation.return
    }
    simulation.func private @mono_run(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@MonoBase>
          {simulation.capture_kind = 1 : i32},
        %input: i32 {simulation.capture_kind = 2 : i32})
        attributes {code_unit_id = 3 : i64, entry_kind = 12 : i32} {
      simulation.return
    }
    simulation.func private @poly_left_run(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@PolyLeft>
          {simulation.capture_kind = 1 : i32},
        %input: i32 {simulation.capture_kind = 2 : i32})
        attributes {code_unit_id = 4 : i64, entry_kind = 12 : i32} {
      simulation.return
    }
    simulation.func private @poly_right_run(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@PolyRight>
          {simulation.capture_kind = 1 : i32},
        %input: i32 {simulation.capture_kind = 2 : i32})
        attributes {code_unit_id = 5 : i64, entry_kind = 12 : i32} {
      simulation.return
    }

    simulation.func @exact(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %object = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@Derived>
      %base = simulation.class.cast %object :
        !simulation.class_handle<@Derived> to
        !simulation.class_handle<@Base>
      %input = arith.constant 7 : i32
      %continued = arith.constant 11 : i64
      simulation.class.virtual_task_call
        %base[@Base_run] slot 0 signature_id 71
        (%input, %continued) arguments 1 to ^done :
        (!simulation.class_handle<@Base>, i32, i64) -> ()
    ^done(%value: i64):
      simulation.return
    }

    simulation.func private @guarded_mono(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %receiver: !simulation.class_handle<@MonoBase>
          {simulation.capture_kind = 1 : i32},
        %input: i32 {simulation.capture_kind = 2 : i32},
        %continued: i64 {simulation.capture_kind = 2 : i32})
        attributes {code_unit_id = 6 : i64, entry_kind = 12 : i32} {
      simulation.class.virtual_task_call
        %receiver[@MonoBase_run] slot 0 signature_id 81
        (%input, %continued) arguments 1 to ^done
        {site = #schedule.continuation<id = 7>} :
        (!simulation.class_handle<@MonoBase>, i32, i64) -> ()
    ^done(%value: i64):
      simulation.return
    }

    simulation.func private @guarded_interface(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %receiver: !simulation.class_handle<@Runner>
          {simulation.capture_kind = 1 : i32},
        %input: i32 {simulation.capture_kind = 2 : i32})
        attributes {code_unit_id = 7 : i64, entry_kind = 12 : i32} {
      simulation.class.virtual_task_call
        %receiver[@Runner_run] slot 4294967295 signature_id 81
        (%input) arguments 1 to ^done :
        (!simulation.class_handle<@Runner>, i32) -> ()
    ^done:
      simulation.return
    }

    simulation.func private @polymorphic(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %receiver: !simulation.class_handle<@PolyBase>
          {simulation.capture_kind = 1 : i32},
        %input: i32 {simulation.capture_kind = 2 : i32})
        attributes {code_unit_id = 8 : i64, entry_kind = 12 : i32} {
      simulation.class.virtual_task_call
        %receiver[@PolyBase_run] slot 0 signature_id 91
        (%input) arguments 1 to ^done :
        (!simulation.class_handle<@PolyBase>, i32) -> ()
    ^done:
      simulation.return
    }

    simulation.func @known_null(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 9 : i64, entry_kind = 1 : i32} {
      %receiver = simulation.class.null :
        !simulation.class_handle<@MonoBase>
      %input = arith.constant 3 : i32
      simulation.class.virtual_task_call
        %receiver[@MonoBase_run] slot 0 signature_id 81
        (%input) arguments 1 to ^done :
        (!simulation.class_handle<@MonoBase>, i32) -> ()
    ^done:
      simulation.return
    }
  }
}

// CHECK-LABEL: simulation.func @exact
// CHECK: %[[BASE:.*]] = simulation.class.cast
// CHECK: %[[THIS:.*]] = simulation.class.cast %[[BASE]]
// CHECK-NEXT: simulation.task.call @derived_run(%{{.*}}, %[[THIS]], %{{.*}}, %{{.*}}) arguments 3 to ^[[EXACT_DONE:[a-zA-Z0-9_]+]]
// CHECK: ^[[EXACT_DONE]](%{{.*}}: i64):

// CHECK-LABEL: simulation.func private @guarded_mono
// CHECK: %[[NULL:.*]] = simulation.managed.is_null %[[MONO:.*]]
// CHECK-NEXT: cf.cond_br %[[NULL]], ^[[NULL_BLOCK:[a-zA-Z0-9_]+]], ^[[DIRECT_BLOCK:[a-zA-Z0-9_]+]]
// CHECK: ^[[NULL_BLOCK]]:
// CHECK: %[[CANONICAL_NULL:.*]] = simulation.class.null
// CHECK-NEXT: simulation.class.virtual_task_call %[[CANONICAL_NULL]]
// CHECK-SAME: arguments 1 to ^[[MONO_DONE:[a-zA-Z0-9_]+]]
// CHECK-SAME: site = #schedule.continuation<id = 7>
// CHECK: ^[[DIRECT_BLOCK]]:
// CHECK: simulation.task.call @mono_run
// CHECK-SAME: arguments 3 to ^[[MONO_DONE]]
// CHECK-SAME: site = #schedule.continuation<id = 7>

// CHECK-LABEL: simulation.func private @guarded_interface
// CHECK: %[[INTERFACE_NULL:[a-zA-Z0-9_]+]] = simulation.managed.is_null %[[INTERFACE:[a-zA-Z0-9_]+]]
// CHECK: %[[NULL_INTERFACE:.*]] = simulation.class.null {{.*}}class_handle<@Runner>
// CHECK-NEXT: simulation.class.virtual_task_call %[[NULL_INTERFACE]]
// CHECK: %[[MONO_THIS:.*]] = simulation.class.cast %[[INTERFACE]]
// CHECK-NEXT: simulation.task.call @mono_run(%{{.*}}, %[[MONO_THIS]], %{{.*}}) arguments 3

// CHECK-LABEL: simulation.func private @polymorphic
// CHECK-NOT: simulation.managed.is_null
// CHECK: simulation.class.virtual_task_call

// CHECK-LABEL: simulation.func @known_null
// CHECK-NOT: simulation.managed.is_null
// CHECK: simulation.class.virtual_task_call

// TWICE-COUNT-2: simulation.managed.is_null
// TWICE-NOT: simulation.managed.is_null
