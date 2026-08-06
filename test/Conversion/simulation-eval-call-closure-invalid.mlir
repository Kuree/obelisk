// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   --split-input-file --verify-diagnostics

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.eval.generated
} {
  llvm.func @obelisk_rt_forbidden()
  llvm.func @__obelisk_eval_fast_coordinator_bad() attributes {
      obelisk.eval.call_closure_root
  } {
    // expected-error @+1 {{generated eval hot closure calls runtime symbol obelisk_rt_forbidden}}
    llvm.call @obelisk_rt_forbidden() : () -> ()
    llvm.return
  }
}

// -----

// Checkpoint proof admits only the cold synchronization ABI, never scheduler
// mutation or re-entry.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.eval.generated
} {
  llvm.func @obelisk_rt_v1_scheduler_run(!llvm.ptr) -> i32
  llvm.func @__obelisk_eval_fast_coordinator_checkpoint_mutation(%ctx: !llvm.ptr) attributes {
      obelisk.eval.call_closure_root
  } {
    llvm.call @checkpoint_mutation(%ctx) : (!llvm.ptr) -> ()
    llvm.return
  }
  llvm.func @checkpoint_mutation(%ctx: !llvm.ptr) attributes {
      obelisk.eval.checkpoint_safe,
      obelisk.eval.may_terminate
  } {
    // expected-error @+1 {{generated eval hot closure calls runtime symbol obelisk_rt_v1_scheduler_run}}
    %status = llvm.call @obelisk_rt_v1_scheduler_run(%ctx) : (!llvm.ptr) -> i32
    llvm.return
  }
}

// -----

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.eval.generated
} {
  llvm.func @obelisk_rt_forbidden()
  llvm.func @__obelisk_eval_fast_coordinator_indirect_runtime(%fn: !llvm.ptr) attributes {
      obelisk.eval.call_closure_root
  } {
    // expected-error @+1 {{generated eval indirect route targets runtime symbol obelisk_rt_forbidden}}
    llvm.call %fn() {obelisk.eval.allowed_callees = [@obelisk_rt_forbidden]} :
        !llvm.ptr, () -> ()
    llvm.return
  }
}

// -----

// A closed indirect model route is legal and its transitive closure is still
// verified.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.eval.generated
} {
  llvm.func @__obelisk_eval_fast_coordinator_indirect_model(%fn: !llvm.ptr) attributes {
      obelisk.eval.call_closure_root
  } {
    llvm.call %fn() {obelisk.eval.allowed_callees = [@model_body]} :
        !llvm.ptr, () -> ()
    llvm.return
  }
  llvm.func @model_body() {
    llvm.call @model_leaf() : () -> ()
    llvm.return
  }
  llvm.func @model_leaf() {
    llvm.return
  }
}

// -----

// Checkpoint-safe wrappers still return through a generated branch. Runtime
// callbacks execute only after leaving the hot closure.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.eval.generated
} {
  llvm.func @obelisk_rt_v1_display()
  llvm.func @__obelisk_eval_fast_coordinator_checkpoint() attributes {
      obelisk.eval.call_closure_root
  } {
    llvm.call @checkpoint_owner() : () -> ()
    llvm.return
  }
  llvm.func @checkpoint_owner() attributes {
      obelisk.eval.checkpoint_safe,
      obelisk.eval.may_terminate
  } {
    llvm.call @checkpoint_body() : () -> ()
    llvm.return
  }
  llvm.func @checkpoint_body() {
    // expected-error @+1 {{generated eval hot closure calls runtime symbol obelisk_rt_v1_display}}
    llvm.call @obelisk_rt_v1_display() : () -> ()
    llvm.return
  }
}

// -----

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.eval.generated
} {
  llvm.func @obelisk_rt_hidden()
  llvm.func @__obelisk_eval_hybrid_coordinator_bad() attributes {
      obelisk.eval.call_closure_root
  } {
    llvm.call @checkpoint_owner() : () -> ()
    llvm.return
  }
  llvm.func @checkpoint_owner() attributes {obelisk.eval.may_terminate} {
    // expected-error @+1 {{generated eval hot closure calls runtime symbol obelisk_rt_hidden}}
    llvm.call @obelisk_rt_hidden() : () -> ()
    llvm.return
  }
}

// -----

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.eval.generated
} {
  llvm.func @__obelisk_eval_fast_coordinator_indirect(%fn: !llvm.ptr) attributes {
      obelisk.eval.call_closure_root
  } {
    // expected-error @+1 {{generated eval indirect call has no closed target set}}
    llvm.call %fn() : !llvm.ptr, () -> ()
    llvm.return
  }
}

// -----

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.eval.generated
} {
  llvm.func @__obelisk_eval_fast_coordinator_malformed(%fn: !llvm.ptr) attributes {
      obelisk.eval.call_closure_root
  } {
    // expected-error @+1 {{generated eval indirect call has malformed allowed-callee metadata}}
    llvm.call %fn() {obelisk.eval.allowed_callees = [42 : i32]} :
        !llvm.ptr, () -> ()
    llvm.return
  }
}

// -----

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.eval.generated
} {
  llvm.func @external_model_body()
  llvm.func @__obelisk_eval_periodic_two_state_coordinator_external() attributes {
      obelisk.eval.call_closure_root
  } {
    // expected-error @+1 {{generated eval hot closure calls external symbol external_model_body}}
    llvm.call @external_model_body() : () -> ()
    llvm.return
  }
}
