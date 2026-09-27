// RUN: obelisk-opt %s --obelisk-sim-devirtualize-class-calls | FileCheck %s
// RUN: obelisk-opt %s --obelisk-sim-devirtualize-class-calls \
// RUN:   --obelisk-sim-devirtualize-class-calls | FileCheck %s --check-prefix=TWICE

module {
  simulation.design @classes {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 function hierarchy "guarded_mono"
    simulation.code_unit.decl 3 in 0 function hierarchy "MonoBase.eval"
    simulation.code_unit.decl 4 in 0 function hierarchy "polymorphic"
    simulation.code_unit.decl 5 in 0 function hierarchy "PolyLeft.eval"
    simulation.code_unit.decl 6 in 0 function hierarchy "PolyRight.eval"
    simulation.code_unit.decl 7 in 0 function hierarchy "guarded_final"
    simulation.code_unit.decl 8 in 0 function hierarchy "FinalClass.get"
    simulation.code_unit.decl 9 in 0 function hierarchy "guarded_interface"
    simulation.code_unit.decl 10 in 0 function hierarchy "WorkerBase.run"
    simulation.code_unit.decl 11 in 0 function hierarchy "guarded_zero"
    simulation.code_unit.decl 12 in 0 function hierarchy "MonoBase.touch"

    simulation.class.decl @MonoBase id 1 {
      is_abstract = true, is_final = false, is_interface = false
    }
    simulation.class.decl @MonoLeft id 2 extends @MonoBase {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @MonoRight id 3 extends @MonoBase {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @PolyBase id 4 {
      is_abstract = true, is_final = false, is_interface = false
    }
    simulation.class.decl @PolyLeft id 5 extends @PolyBase {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @PolyRight id 6 extends @PolyBase {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @FinalClass id 7 {
      is_abstract = false, is_final = true, is_interface = false
    }
    simulation.class.decl @Worker id 8 {
      is_abstract = true, is_final = false, is_interface = true
    }
    simulation.class.decl @WorkerBase id 9 implements [@Worker] {
      is_abstract = true, is_final = false, is_interface = false
    }
    simulation.class.decl @WorkerLeft id 10 extends @WorkerBase {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @WorkerRight id 11 extends @WorkerBase {
      is_abstract = false, is_final = false, is_interface = false
    }

    simulation.class.method @MonoBase_eval of @MonoBase slot 0
        signature_id 41 implemented_by @mono_eval :
      (!simulation.context, !simulation.class_handle<@MonoBase>, i32) ->
        (i64, i32) {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    simulation.class.method @PolyBase_eval of @PolyBase slot 0
        signature_id 51 :
      (!simulation.context, !simulation.class_handle<@PolyBase>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
    simulation.class.method @PolyLeft_eval of @PolyLeft slot 0
        signature_id 51 implemented_by @poly_left :
      (!simulation.context, !simulation.class_handle<@PolyLeft>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    simulation.class.method @PolyRight_eval of @PolyRight slot 0
        signature_id 51 implemented_by @poly_right :
      (!simulation.context, !simulation.class_handle<@PolyRight>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    simulation.class.method @FinalClass_get of @FinalClass slot 0
        signature_id 61 implemented_by @final_get :
      (!simulation.context, !simulation.class_handle<@FinalClass>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    simulation.class.method @Worker_run of @Worker slot 4294967295
        signature_id 71 interface_ordinal 0 :
      (!simulation.context, !simulation.class_handle<@Worker>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
    simulation.class.method @WorkerBase_run of @WorkerBase slot 0
        signature_id 71 implemented_by @worker_run :
      (!simulation.context, !simulation.class_handle<@WorkerBase>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    simulation.class.method @MonoBase_touch of @MonoBase slot 1
        signature_id 42 implemented_by @mono_touch :
      (!simulation.context, !simulation.class_handle<@MonoBase>, i32) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      simulation.return
    }
    simulation.func private @mono_eval(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@MonoBase>
          {simulation.capture_kind = 1 : i32},
        %input: i32 {simulation.capture_kind = 1 : i32}) -> (i64, i32)
        attributes {code_unit_id = 3 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 7 : i64
      simulation.return %value, %input : i64, i32
    }
    simulation.func private @poly_left(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@PolyLeft>
          {simulation.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 5 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 1 : i64
      simulation.return %value : i64
    }
    simulation.func private @poly_right(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@PolyRight>
          {simulation.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 6 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 2 : i64
      simulation.return %value : i64
    }
    simulation.func private @final_get(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@FinalClass>
          {simulation.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 8 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 9 : i64
      simulation.return %value : i64
    }
    simulation.func private @worker_run(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@WorkerBase>
          {simulation.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 10 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 11 : i64
      simulation.return %value : i64
    }
    simulation.func private @mono_touch(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@MonoBase>
          {simulation.capture_kind = 1 : i32},
        %input: i32 {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 12 : i64, entry_kind = 8 : i32} {
      simulation.return
    }

    simulation.func private @guarded_mono(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %receiver: !simulation.class_handle<@MonoBase>
          {simulation.capture_kind = 1 : i32},
        %input: i32 {simulation.capture_kind = 1 : i32}) -> (i64, i32)
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %result:2 = simulation.class.virtual_call
        %receiver[@MonoBase_eval] slot 0 signature_id 41(%input) :
        (!simulation.class_handle<@MonoBase>, i32) -> (i64, i32)
      simulation.return %result#0, %result#1 : i64, i32
    }

    simulation.func private @polymorphic(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %receiver: !simulation.class_handle<@PolyBase>
          {simulation.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 4 : i64, entry_kind = 8 : i32} {
      %result = simulation.class.virtual_call
        %receiver[@PolyBase_eval] slot 0 signature_id 51() :
        (!simulation.class_handle<@PolyBase>) -> i64
      simulation.return %result : i64
    }

    simulation.func private @guarded_final(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %receiver: !simulation.class_handle<@FinalClass>
          {simulation.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 7 : i64, entry_kind = 8 : i32} {
      %result = simulation.class.virtual_call
        %receiver[@FinalClass_get] slot 0 signature_id 61() :
        (!simulation.class_handle<@FinalClass>) -> i64
      simulation.return %result : i64
    }

    simulation.func private @guarded_interface(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %receiver: !simulation.class_handle<@Worker>
          {simulation.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 9 : i64, entry_kind = 8 : i32} {
      %result = simulation.class.virtual_call
        %receiver[@Worker_run] slot 4294967295 signature_id 71() :
        (!simulation.class_handle<@Worker>) -> i64
      simulation.return %result : i64
    }

    simulation.func private @guarded_zero(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %receiver: !simulation.class_handle<@MonoBase>
          {simulation.capture_kind = 1 : i32},
        %input: i32 {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 11 : i64, entry_kind = 8 : i32} {
      simulation.class.virtual_call
        %receiver[@MonoBase_touch] slot 1 signature_id 42(%input) :
        (!simulation.class_handle<@MonoBase>, i32) -> ()
      simulation.return
    }
  }
}

// CHECK-LABEL: simulation.func private @guarded_mono
// CHECK: %[[NULL:[a-zA-Z0-9_]+]] = simulation.managed.is_null %[[RECEIVER:[a-zA-Z0-9_]+]]
// CHECK-NEXT: cf.cond_br %[[NULL]], ^[[NULL_BLOCK:.*]], ^[[DIRECT_BLOCK:.*]]
// CHECK: ^[[NULL_BLOCK]]:
// CHECK: %[[CANONICAL_NULL:.*]] = simulation.class.null
// CHECK-NEXT: %[[FALLBACK:.*]]:2 = simulation.class.virtual_call %[[CANONICAL_NULL]]
// CHECK-NEXT: cf.br ^[[MERGE:.*]](%[[FALLBACK]]#0, %[[FALLBACK]]#1 : i64, i32)
// CHECK: ^[[DIRECT_BLOCK]]:
// CHECK: %[[DIRECT:.*]]:2 = simulation.call @mono_eval(%{{.*}}, %[[RECEIVER]], %{{.*}})
// CHECK-NEXT: cf.br ^[[MERGE]](%[[DIRECT]]#0, %[[DIRECT]]#1 : i64, i32)
// CHECK: ^[[MERGE]](%[[RESULT0:.*]]: i64, %[[RESULT1:.*]]: i32):
// CHECK: simulation.return %[[RESULT0]], %[[RESULT1]] : i64, i32

// CHECK-LABEL: simulation.func private @polymorphic
// CHECK-NOT: simulation.managed.is_null
// CHECK: simulation.class.virtual_call

// CHECK-LABEL: simulation.func private @guarded_final
// CHECK: simulation.managed.is_null
// CHECK: simulation.class.virtual_call
// CHECK: simulation.call @final_get

// CHECK-LABEL: simulation.func private @guarded_interface
// CHECK: %[[INTERFACE_NULL:[a-zA-Z0-9_]+]] = simulation.managed.is_null %[[INTERFACE:[a-zA-Z0-9_]+]]
// CHECK: %[[NULL_INTERFACE:[a-zA-Z0-9_]+]] = simulation.class.null {{.*}}class_handle<@Worker>
// CHECK-NEXT: simulation.class.virtual_call %[[NULL_INTERFACE]]
// CHECK: %[[WORKER_BASE:.*]] = simulation.class.cast %[[INTERFACE]]
// CHECK-NEXT: simulation.call @worker_run(%{{.*}}, %[[WORKER_BASE]])

// CHECK-LABEL: simulation.func private @guarded_zero
// CHECK: simulation.managed.is_null
// CHECK: ^[[ZERO_NULL:.*]]:
// CHECK: simulation.class.virtual_call
// CHECK-NEXT: cf.br ^[[ZERO_MERGE:.*]]
// CHECK: ^[[ZERO_DIRECT:.*]]:
// CHECK: simulation.call @mono_touch
// CHECK-NEXT: cf.br ^[[ZERO_MERGE]]
// CHECK: ^[[ZERO_MERGE]]:
// CHECK: simulation.return

// TWICE-COUNT-4: simulation.managed.is_null
// TWICE-NOT: simulation.managed.is_null
