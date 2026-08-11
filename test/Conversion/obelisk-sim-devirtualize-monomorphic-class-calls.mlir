// RUN: obelisk-opt %s --obelisk-sim-devirtualize-class-calls | FileCheck %s
// RUN: obelisk-opt %s --obelisk-sim-devirtualize-class-calls \
// RUN:   --obelisk-sim-devirtualize-class-calls | FileCheck %s --check-prefix=TWICE

module {
  obelisk_sim.design @classes {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "guarded_mono"
    obelisk_sim.code_unit.decl 3 in 0 function hierarchy "MonoBase.eval"
    obelisk_sim.code_unit.decl 4 in 0 function hierarchy "polymorphic"
    obelisk_sim.code_unit.decl 5 in 0 function hierarchy "PolyLeft.eval"
    obelisk_sim.code_unit.decl 6 in 0 function hierarchy "PolyRight.eval"
    obelisk_sim.code_unit.decl 7 in 0 function hierarchy "guarded_final"
    obelisk_sim.code_unit.decl 8 in 0 function hierarchy "FinalClass.get"
    obelisk_sim.code_unit.decl 9 in 0 function hierarchy "guarded_interface"
    obelisk_sim.code_unit.decl 10 in 0 function hierarchy "WorkerBase.run"
    obelisk_sim.code_unit.decl 11 in 0 function hierarchy "guarded_zero"
    obelisk_sim.code_unit.decl 12 in 0 function hierarchy "MonoBase.touch"

    obelisk_sim.class.decl @MonoBase id 1 {
      is_abstract = true, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @MonoLeft id 2 extends @MonoBase {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @MonoRight id 3 extends @MonoBase {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @PolyBase id 4 {
      is_abstract = true, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @PolyLeft id 5 extends @PolyBase {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @PolyRight id 6 extends @PolyBase {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @FinalClass id 7 {
      is_abstract = false, is_final = true, is_interface = false
    }
    obelisk_sim.class.decl @Worker id 8 {
      is_abstract = true, is_final = false, is_interface = true
    }
    obelisk_sim.class.decl @WorkerBase id 9 implements [@Worker] {
      is_abstract = true, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @WorkerLeft id 10 extends @WorkerBase {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @WorkerRight id 11 extends @WorkerBase {
      is_abstract = false, is_final = false, is_interface = false
    }

    obelisk_sim.class.method @MonoBase_eval of @MonoBase slot 0
        signature_id 41 implemented_by @mono_eval :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@MonoBase>, i32) ->
        (i64, i32) {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    obelisk_sim.class.method @PolyBase_eval of @PolyBase slot 0
        signature_id 51 :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@PolyBase>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
    obelisk_sim.class.method @PolyLeft_eval of @PolyLeft slot 0
        signature_id 51 implemented_by @poly_left :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@PolyLeft>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    obelisk_sim.class.method @PolyRight_eval of @PolyRight slot 0
        signature_id 51 implemented_by @poly_right :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@PolyRight>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    obelisk_sim.class.method @FinalClass_get of @FinalClass slot 0
        signature_id 61 implemented_by @final_get :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@FinalClass>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    obelisk_sim.class.method @Worker_run of @Worker slot 4294967295
        signature_id 71 :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@Worker>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
    obelisk_sim.class.method @WorkerBase_run of @WorkerBase slot 0
        signature_id 71 implemented_by @worker_run :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@WorkerBase>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    obelisk_sim.class.method @MonoBase_touch of @MonoBase slot 1
        signature_id 42 implemented_by @mono_touch :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@MonoBase>, i32) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      obelisk_sim.return
    }
    obelisk_sim.func private @mono_eval(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@MonoBase>
          {obelisk_sim.capture_kind = 1 : i32},
        %input: i32 {obelisk_sim.capture_kind = 1 : i32}) -> (i64, i32)
        attributes {code_unit_id = 3 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 7 : i64
      obelisk_sim.return %value, %input : i64, i32
    }
    obelisk_sim.func private @poly_left(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@PolyLeft>
          {obelisk_sim.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 5 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 1 : i64
      obelisk_sim.return %value : i64
    }
    obelisk_sim.func private @poly_right(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@PolyRight>
          {obelisk_sim.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 6 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 2 : i64
      obelisk_sim.return %value : i64
    }
    obelisk_sim.func private @final_get(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@FinalClass>
          {obelisk_sim.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 8 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 9 : i64
      obelisk_sim.return %value : i64
    }
    obelisk_sim.func private @worker_run(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@WorkerBase>
          {obelisk_sim.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 10 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 11 : i64
      obelisk_sim.return %value : i64
    }
    obelisk_sim.func private @mono_touch(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@MonoBase>
          {obelisk_sim.capture_kind = 1 : i32},
        %input: i32 {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 12 : i64, entry_kind = 8 : i32} {
      obelisk_sim.return
    }

    obelisk_sim.func private @guarded_mono(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %receiver: !obelisk_sim.class_handle<@MonoBase>
          {obelisk_sim.capture_kind = 1 : i32},
        %input: i32 {obelisk_sim.capture_kind = 1 : i32}) -> (i64, i32)
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %result:2 = obelisk_sim.class.virtual_call
        %receiver[@MonoBase_eval] slot 0 signature_id 41(%input) :
        (!obelisk_sim.class_handle<@MonoBase>, i32) -> (i64, i32)
      obelisk_sim.return %result#0, %result#1 : i64, i32
    }

    obelisk_sim.func private @polymorphic(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %receiver: !obelisk_sim.class_handle<@PolyBase>
          {obelisk_sim.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 4 : i64, entry_kind = 8 : i32} {
      %result = obelisk_sim.class.virtual_call
        %receiver[@PolyBase_eval] slot 0 signature_id 51() :
        (!obelisk_sim.class_handle<@PolyBase>) -> i64
      obelisk_sim.return %result : i64
    }

    obelisk_sim.func private @guarded_final(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %receiver: !obelisk_sim.class_handle<@FinalClass>
          {obelisk_sim.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 7 : i64, entry_kind = 8 : i32} {
      %result = obelisk_sim.class.virtual_call
        %receiver[@FinalClass_get] slot 0 signature_id 61() :
        (!obelisk_sim.class_handle<@FinalClass>) -> i64
      obelisk_sim.return %result : i64
    }

    obelisk_sim.func private @guarded_interface(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %receiver: !obelisk_sim.class_handle<@Worker>
          {obelisk_sim.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 9 : i64, entry_kind = 8 : i32} {
      %result = obelisk_sim.class.virtual_call
        %receiver[@Worker_run] slot 4294967295 signature_id 71() :
        (!obelisk_sim.class_handle<@Worker>) -> i64
      obelisk_sim.return %result : i64
    }

    obelisk_sim.func private @guarded_zero(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %receiver: !obelisk_sim.class_handle<@MonoBase>
          {obelisk_sim.capture_kind = 1 : i32},
        %input: i32 {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 11 : i64, entry_kind = 8 : i32} {
      obelisk_sim.class.virtual_call
        %receiver[@MonoBase_touch] slot 1 signature_id 42(%input) :
        (!obelisk_sim.class_handle<@MonoBase>, i32) -> ()
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: obelisk_sim.func private @guarded_mono
// CHECK: %[[NULL:[a-zA-Z0-9_]+]] = obelisk_sim.managed.is_null %[[RECEIVER:[a-zA-Z0-9_]+]]
// CHECK-NEXT: cf.cond_br %[[NULL]], ^[[NULL_BLOCK:.*]], ^[[DIRECT_BLOCK:.*]]
// CHECK: ^[[NULL_BLOCK]]:
// CHECK: %[[CANONICAL_NULL:.*]] = obelisk_sim.class.null
// CHECK-NEXT: %[[FALLBACK:.*]]:2 = obelisk_sim.class.virtual_call %[[CANONICAL_NULL]]
// CHECK-NEXT: cf.br ^[[MERGE:.*]](%[[FALLBACK]]#0, %[[FALLBACK]]#1 : i64, i32)
// CHECK: ^[[DIRECT_BLOCK]]:
// CHECK: %[[DIRECT:.*]]:2 = obelisk_sim.call @mono_eval(%{{.*}}, %[[RECEIVER]], %{{.*}})
// CHECK-NEXT: cf.br ^[[MERGE]](%[[DIRECT]]#0, %[[DIRECT]]#1 : i64, i32)
// CHECK: ^[[MERGE]](%[[RESULT0:.*]]: i64, %[[RESULT1:.*]]: i32):
// CHECK: obelisk_sim.return %[[RESULT0]], %[[RESULT1]] : i64, i32

// CHECK-LABEL: obelisk_sim.func private @polymorphic
// CHECK-NOT: obelisk_sim.managed.is_null
// CHECK: obelisk_sim.class.virtual_call

// CHECK-LABEL: obelisk_sim.func private @guarded_final
// CHECK: obelisk_sim.managed.is_null
// CHECK: obelisk_sim.class.virtual_call
// CHECK: obelisk_sim.call @final_get

// CHECK-LABEL: obelisk_sim.func private @guarded_interface
// CHECK: %[[INTERFACE_NULL:[a-zA-Z0-9_]+]] = obelisk_sim.managed.is_null %[[INTERFACE:[a-zA-Z0-9_]+]]
// CHECK: %[[NULL_INTERFACE:[a-zA-Z0-9_]+]] = obelisk_sim.class.null {{.*}}class_handle<@Worker>
// CHECK-NEXT: obelisk_sim.class.virtual_call %[[NULL_INTERFACE]]
// CHECK: %[[WORKER_BASE:.*]] = obelisk_sim.class.cast %[[INTERFACE]]
// CHECK-NEXT: obelisk_sim.call @worker_run(%{{.*}}, %[[WORKER_BASE]])

// CHECK-LABEL: obelisk_sim.func private @guarded_zero
// CHECK: obelisk_sim.managed.is_null
// CHECK: ^[[ZERO_NULL:.*]]:
// CHECK: obelisk_sim.class.virtual_call
// CHECK-NEXT: cf.br ^[[ZERO_MERGE:.*]]
// CHECK: ^[[ZERO_DIRECT:.*]]:
// CHECK: obelisk_sim.call @mono_touch
// CHECK-NEXT: cf.br ^[[ZERO_MERGE]]
// CHECK: ^[[ZERO_MERGE]]:
// CHECK: obelisk_sim.return

// TWICE-COUNT-4: obelisk_sim.managed.is_null
// TWICE-NOT: obelisk_sim.managed.is_null
