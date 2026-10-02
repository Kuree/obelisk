// Scheduling plans and executable scheduler actions.
#ifndef OBELISK_DIALECT_SCHEDULE_SCHEDULEOPS_TD
#define OBELISK_DIALECT_SCHEDULE_SCHEDULEOPS_TD
include "obelisk/Dialect/Simulation/SimulationAttrs.td"
include "obelisk/Dialect/Simulation/SimulationTypes.td"
include "obelisk/Dialect/Simulation/SimulationResources.td"
include "mlir/IR/SymbolInterfaces.td"
include "mlir/Interfaces/ControlFlowInterfaces.td"
include "mlir/Interfaces/CallInterfaces.td"
include "obelisk/Dialect/Schedule/ScheduleAttrs.td"
include "mlir/Dialect/LLVMIR/LLVMTypes.td"
include "obelisk/Dialect/Runtime/RuntimeTypes.td"
class ScheduleNativeOp<string mnemonic, list<Trait> traits = []>
    : Op<ScheduleDialect, mnemonic, traits> {
  let assemblyFormat = "operands attr-dict `:` functional-type(operands, results)";
}
def TransferActivationOp : ScheduleNativeOp<"transfer.activation",
    [DeclareOpInterfaceMethods<SymbolUserOpInterface>]> {
  let summary = "one ordered continuous copy activation and its source wait";
  let description = [{
    IEEE 1800-2023 4.9.1, 4.9.6, 9.4.2 and 10.6.2.
    Binds an original process to a continuous transfer between entry storage
    captures. The process executes initially and on each source change, in
    Active, retaining its identity, canonical frame, wait and publication.
    Source and destination remain distinct handles even when implementations
    are shared. A transfer samples the source, performs the complete continuous
    assignment (including force/release contribution), and then arms its wait.
    It must not be interpreted as an alias or as a final-value-only update.

    signature, source, destination and capture_offsets specify the canonical
    capture ABI. -1 offsets identify context captures supplied by the instance.
    Without kernel, this actor has its own table-process entry. With kernel,
    it uses the shared implementation selected by Schedule optimization;
    representation conversion must not regroup activations.
  }];
  let arguments = (ins SymbolRefAttr:$actor, TypeAttr:$signature,
      I32Attr:$source, I32Attr:$destination,
      DenseI64ArrayAttr:$capture_offsets,
      OptionalAttr<FlatSymbolRefAttr>:$kernel);
  let hasVerifier = 1;
}
def TransferKernelOp : ScheduleNativeOp<"transfer.kernel", [Symbol]> {
  let summary = "shared implementation of ordered continuous transfers";
  let description = [{
    IEEE 1800-2023 4.9.1 and 10.3.2: code sharing preserves separate updates.
    Implements the transfer semantics of transfer.activation using the
    executing instance's context and canonical storage captures. No storage
    identity, source owner, continuation ID or wait record is baked into this
    kernel. Each activation supplies those through its own instance and wait
    plan. All sharing and ABI compatibility decisions precede conversion.
  }];
  let arguments = (ins SymbolNameAttr:$sym_name, TypeAttr:$signature,
      I32Attr:$source, I32Attr:$destination,
      DenseI64ArrayAttr:$capture_offsets);
  let hasVerifier = 1;
}
// Executable actions retain facts that used to be recovered from runtime-call
// names and the shape of expanded LLVM loads/stores.
def NativeKnownValueOp : ScheduleNativeOp<"known.value", [Pure]> {
  let summary = "test the complete unknown plane of a fixed native value";
  let description = [{
    Establishes that every represented bit of input is two-state. Fixed
    unpacked arrays and structs retain their native value/unknown planes;
    there is no bit-stream export, allocation, runtime call or state effect.
    This predicate proves knownness only, not equality, visibility or bounds.
  }];
  let arguments = (ins AnyType:$input);
  let results = (outs I1:$known);
  let hasVerifier = 1;
}
def NativeTransitionOp
    : ScheduleNativeOp<
          "transition",
          [MemoryEffects<
              [MemRead, MemWrite, MemRead<SimStorageResource>,
               MemWrite<SimStorageResource>, MemRead<SimSchedulerResource>,
               MemWrite<SimSchedulerResource>, MemRead<SimProcessResource>,
               MemWrite<SimProcessResource>, MemRead<SimExternalResource>,
               MemWrite<SimExternalResource>]>]> {
  let summary = "publish one exact scalar four-state transition";
  let description = [{
    Publishes the captured old/new value and unknown planes of one store in
    the executing process's current event region. The offset is relative to
    the static root, and width is between 1 and 64. Dynamic offsets retain
    their original selector semantics. source_owner identifies the original
    logical process, including inside a fused activation; its absence requires
    runtime source resolution. Publication is synchronous and may deliver
    observers/callbacks. It is never a final-value-only notification and may
    not be moved across another effect or coalesced with another occurrence.
  }];
  let arguments = (ins LLVM_AnyPointer:$context, I64:$offset, I64:$old_value,
      I64:$old_unknown, I64:$new_value, I64:$new_unknown, I64Attr:$static_state,
      I64Attr:$width, OptionalAttr<ScheduleSourceOwnerAttr>:$source_owner);
  let hasVerifier = 1;
}
def NativeReadyUpdateOp
    : ScheduleNativeOp<"ready.update", [DeclareOpInterfaceMethods<
                                           MemoryEffectsOpInterface>]> {
  let summary = "add or consume an exact word of pending fine activations";
  let description = [{
    Updates the authoritative ReadySetLayout leaf and its derived indexes.
    With consume=false it unions mask; with consume=true it removes mask.
    capacity freezes the ready universe and word identifies the leaf, without
    recovering either from addresses. These are pending activations, not
    event history. Ordered updates may be combined only while no selection,
    observer, callback, opaque alias, or handoff can observe intermediate
    storage. All leaves/indexes must be materialized at those boundaries.
  }];
  let arguments = (ins LLVM_AnyPointer:$base, I64:$mask, I64Attr:$capacity,
      I64Attr:$word, BoolAttr:$consume);
  let hasVerifier = 1;
}
def NativeReadyCommitOp
    : ScheduleNativeOp<"ready.commit", [DeclareOpInterfaceMethods<
                                           MemoryEffectsOpInterface>]> {
  let summary = "materialize one SSA pending-word update and its indexes";
  let description = [{
    Replaces a certified unobserved sequence of ordered add/consume actions.
    The resulting pending word is (previous & ~remove_mask) | add_mask.
    SSA masks include every intervening consume; event publications remain
    distinct ordered effects. Materializes the authoritative leaf and derived
    indexes together before any selection, observer or handoff boundary.
  }];
  let arguments = (ins LLVM_AnyPointer:$base, I64:$remove_mask, I64:$add_mask,
      I64Attr:$capacity, I64Attr:$word);
  let hasVerifier = 1;
}
class ScheduleNativeSuspendOp<string mnemonic, dag extraArgs = (ins),
                   list<Trait> extraTraits = []>
    : ScheduleNativeOp<mnemonic,
            !listconcat(
                [
                  Terminator, DeclareOpInterfaceMethods<BranchOpInterface>,
                  MemoryEffects<[
                    MemWrite<SimSchedulerResource>, MemWrite<SimProcessResource>
                  ]>
                ],
                extraTraits)> {
  let arguments = !con(extraArgs, (ins Variadic<AnyType>:$continuation_operands,
                                      OptionalAttr<SimContinuationSiteAttr>:$site,
                                      OptionalAttr<SimEventRegion>:$resume_region));
  let successors = (successor AnySuccessor : $continuation);
  let hasVerifier = 1;
}
def NativeSuspendDelayOp : ScheduleNativeSuspendOp<"suspend.delay", (ins SimTimeType:$delay,
    OptionalAttr<SimTimingSiteAttr>:$timing)> {
  let assemblyFormat = [{$delay `to` $continuation
    (`(` $continuation_operands^ `:` type($continuation_operands) `)`)? attr-dict}];
}

def NativeSuspendChangeOp : ScheduleNativeSuspendOp<"suspend.change", (ins I64:$watched)> {
  let assemblyFormat = [{$watched `to` $continuation
    (`(` $continuation_operands^ `:` type($continuation_operands) `)`)? attr-dict `:` type($watched)}];
}

def NativeSuspendEdgeOp : ScheduleNativeSuspendOp<"suspend.edge", (ins
    SimEdgeKind:$edge, I64:$watched)> {
  let assemblyFormat = [{$edge $watched `to` $continuation
    (`(` $continuation_operands^ `:` type($continuation_operands) `)`)?
    attr-dict `:` type($watched)}];
}

def NativeSuspendEdgeIffOp : ScheduleNativeSuspendOp<"suspend.edge_iff", (ins
    SimEdgeKind:$edge, I64:$watched, I64:$condition)> {
  let summary = "suspend until a watched event whose level condition is true";
  let description = [{
    The condition is a directly addressable packed object. The scheduler
    samples its complete four-state value when the primary event occurs and
    latches a successful occurrence for the waiting process. Changes to the
    condition alone never resume the process. IEEE 1800-2017 9.4.2.3 writes
    the primary event with an optional edge identifier -- its `always @(a iff
    enable == 1)` example qualifies a plain value change -- so a `change` edge
    is as ordinary here as a directed one.
  }];
  let assemblyFormat = [{$edge $watched `iff` $condition `to` $continuation
    (`(` $continuation_operands^ `:` type($continuation_operands) `)`)?
    attr-dict `:` type($watched) `,` type($condition)}];
}

def NativeSuspendLevelOp : ScheduleNativeSuspendOp<"suspend.level", (ins I64:$watched)> {
  let summary = "suspend until a directly watched packed value becomes true";
  let description = [{
    The scheduler evaluates the complete four-state value after every
    overlapping store and latches the first true occurrence. This preserves a
    transient true value even if a later store changes it again before the
    waiting continuation executes.
  }];
  let assemblyFormat = [{$watched `to` $continuation
    (`(` $continuation_operands^ `:` type($continuation_operands) `)`)?
    attr-dict `:` type($watched)}];
}

def NativeSuspendAnyOp : ScheduleNativeOp<"suspend.any", [
    Terminator,
    DeclareOpInterfaceMethods<BranchOpInterface>,
    MemoryEffects<[MemWrite<SimSchedulerResource>,
                   MemWrite<SimProcessResource>]>
  ]> {
  let summary = "suspend until any watched handle observes its requested edge";
  // The edge inventory determines the watched prefix; the remaining values
  // are successor operands. Keeping one variadic group avoids a redundant
  // segment-size attribute in the persistent IR.
  let arguments = (ins Variadic<AnyType>:$values,
                       DenseI32ArrayAttr:$edges,
                       OptionalAttr<SimContinuationSiteAttr>:$site,
                       OptionalAttr<SimEventRegion>:$resume_region);
  let successors = (successor AnySuccessor:$continuation);
  let assemblyFormat = [{$values `edges` $edges `to` $continuation
   attr-dict `:` type($values)}];
  let extraClassDeclaration = [{
    ::mlir::Operation::operand_range getWatched();
    ::mlir::Operation::operand_range getContinuationOperands();
    ::mlir::MutableOperandRange getContinuationOperandsMutable();
  }];
  let hasVerifier = 1;
}

def NativeSuspendClockSetOp : ScheduleNativeOp<"suspend.clock_set", [
    Terminator, DeclareOpInterfaceMethods<BranchOpInterface>,
    MemoryEffects<[MemWrite<SimSchedulerResource>,
                   MemWrite<SimProcessResource>]>
  ]> {
  let summary = "suspend on an exact set of frozen clock occurrences";
  let description = [{
    The first `edges.size()` values are direct signal clock primaries. They are
    followed by `condition_count` optional iff handles and then successor
    operands. `condition_indices` contains -1 or the condition index for every
    primary. At most 64 primaries are accepted. The runtime groups all matched
    primaries from one exact scheduler publication wave into an occurrence
    mask without coalescing repeated occurrences in that wave.
    The optional `slot_final` marker is reserved for Clause 31 coordinators
    whose simultaneous-event decision needs every producer region at the
    current numeric simulation time. It changes scheduler admission only; the
    occurrence ABI and queue representation remain identical.
  }];
  let arguments = (ins Variadic<AnyType>:$values,
                       I32Attr:$condition_count,
                       DenseI32ArrayAttr:$edges,
                       DenseI32ArrayAttr:$condition_indices,
                       OptionalAttr<DenseI32ArrayAttr>:$condition_predicates,
                       I64Attr:$occurrence_site,
                       OptionalAttr<UnitAttr>:$slot_final,
                       OptionalAttr<SimContinuationSiteAttr>:$site,
                       OptionalAttr<SimEventRegion>:$resume_region);
  let successors = (successor AnySuccessor:$continuation);
  let assemblyFormat = [{
    $values `conditions` $condition_count `edges` $edges
    `indices` $condition_indices `site` $occurrence_site
    `to` $continuation attr-dict `:` type($values)
  }];
  let extraClassDeclaration = [{
    ::mlir::Operation::operand_range getPrimaries();
    ::mlir::Operation::operand_range getConditions();
    ::mlir::Operation::operand_range getContinuationOperands();
    ::mlir::MutableOperandRange getContinuationOperandsMutable();
  }];
  let hasVerifier = 1;
}

def NativeSuspendEventOp : ScheduleNativeSuspendOp<"suspend.event", (ins I64:$event)> {
  let assemblyFormat = [{$event `to` $continuation
    (`(` $continuation_operands^ `:` type($continuation_operands) `)`)? attr-dict}];
}

def NativeSuspendEventOrderOp : ScheduleNativeOp<"suspend.event_order", [
    Terminator,
    DeclareOpInterfaceMethods<BranchOpInterface>,
    MemoryEffects<[MemWrite<SimSchedulerResource>,
                   MemWrite<SimProcessResource>]>
  ]> {
  let summary = "suspend until named events occur in source order or fail";
  let description = [{
    The first `event_count` values are event handles in required occurrence
    order. Remaining values are successor operands. The scheduler observes
    every event publication synchronously, so an out-of-order occurrence is
    retained even when several events trigger before the continuation runs.
  }];
  let arguments = (ins Variadic<AnyType>:$values,
                       I32Attr:$event_count,
                       OptionalAttr<SimContinuationSiteAttr>:$site,
                       OptionalAttr<SimEventRegion>:$resume_region);
  let successors = (successor AnySuccessor:$continuation);
  let assemblyFormat = [{$values `events` $event_count `to` $continuation
    attr-dict `:` type($values)}];
  let extraClassDeclaration = [{
    ::mlir::Operation::operand_range getEvents();
    ::mlir::Operation::operand_range getContinuationOperands();
    ::mlir::MutableOperandRange getContinuationOperandsMutable();
  }];
  let hasVerifier = 1;
}

def NativeSuspendMailboxOp : ScheduleNativeSuspendOp<"suspend.mailbox", (
    ins I64:$mailbox, SimMailboxWaitKind:$kind)> {
  let summary = "suspend until a mailbox can complete a blocking operation";
  let description = [{
    Suspends on the mailbox's not-empty or not-full predicate. The mailbox
    must also be passed to the continuation, which keeps the managed handle in
    the canonical process frame and therefore rooted while the process waits.
  }];
  let assemblyFormat = [{$mailbox $kind `to` $continuation
    (`(` $continuation_operands^ `:` type($continuation_operands) `)`)?
    attr-dict `:` type($mailbox)}];
  let hasVerifier = 1;
}

def NativeSuspendSemaphoreOp : ScheduleNativeSuspendOp<"suspend.semaphore", (
    ins I64:$semaphore, I32:$keys)> {
  let summary = "suspend for a FIFO semaphore acquisition";
  let description = [{
    Suspends until this waiter is at the head of the semaphore's FIFO wait
    order and the scheduler has atomically obtained the requested keys for it.
    The handle and key count are copied into the runtime wait record, which is
    a precise GC root and remains stable across suspension.
  }];
  let assemblyFormat = [{$keys `from` $semaphore `to` $continuation
    (`(` $continuation_operands^ `:` type($continuation_operands) `)`)?
    attr-dict `:` type($semaphore)}];
  let hasVerifier = 1;
}

def NativeSuspendObserveOp : ScheduleNativeOp<"suspend.observe", [
    Terminator,
    DeclareOpInterfaceMethods<BranchOpInterface>,
    MemoryEffects<[MemWrite<SimSchedulerResource>,
                   MemWrite<SimProcessResource>]>
  ]> {
  let summary = "suspend on source-ordered computed timing clauses";
  let description = [{
    Each primary observer has one encounter-time initial value and requested
    edge. `condition_indices` has one entry per primary: -1 denotes no `iff`,
    otherwise the entry indexes the condition observer list. Conditions are
    evaluated only after their corresponding primary occurrence.
  }];
  let arguments = (ins Variadic<AnyType>:$values,
                       I32Attr:$condition_count,
                       DenseI32ArrayAttr:$edges,
                       DenseI32ArrayAttr:$condition_indices,
                       OptionalAttr<SimContinuationSiteAttr>:$site,
                       OptionalAttr<SimEventRegion>:$resume_region);
  let successors = (successor AnySuccessor:$continuation);
  let assemblyFormat = [{
    $values `conditions` $condition_count `edges` $edges
    `indices` $condition_indices `to` $continuation attr-dict
    `:` type($values)
  }];
  let extraClassDeclaration = [{
    ::mlir::Operation::operand_range getPrimaries();
    ::mlir::Operation::operand_range getInitialValues();
    ::mlir::Operation::operand_range getConditions();
    ::mlir::Operation::operand_range getContinuationOperands();
    ::mlir::MutableOperandRange getContinuationOperandsMutable();
  }];
  let hasVerifier = 1;
}

def NativeSuspendForeverOp : ScheduleNativeSuspendOp<"suspend.forever"> {
  let summary = "permanently suspend the current logical process";
  let assemblyFormat = [{`to` $continuation
    (`(` $continuation_operands^ `:` type($continuation_operands) `)`)?
    attr-dict}];
}

def NativeSuspendAwaitOp : ScheduleNativeSuspendOp<"suspend.await", (ins I64:$process)> {
  let summary = "suspend until the awaited process terminates";
  let description = [{
    Resumes once the target process has finished, whether it terminated
    normally or was killed. This is used for an explicit task/process await;
    fork/join synchronization uses `suspend.join`.
  }];
  let assemblyFormat = [{$process `to` $continuation
    (`(` $continuation_operands^ `:` type($continuation_operands) `)`)? attr-dict}];
}

def NativeSuspendJoinOp : ScheduleNativeOp<"suspend.join", [
    Terminator, DeclareOpInterfaceMethods<BranchOpInterface>,
    MemoryEffects<[MemWrite<SimSchedulerResource>,
                   MemWrite<SimProcessResource>]>
  ]> {
  let summary = "suspend until all or any child process terminates";
  // `process_count` determines the process prefix of `values`; the remainder
  // are successor operands. A single variadic group keeps segment bookkeeping
  // out of the persistent IR while still allowing the suspension-threading
  // pass to append live values.
  let arguments = (ins SimJoinKind:$kind, Variadic<AnyType>:$values,
                       I64Attr:$process_count,
                       OptionalAttr<SimContinuationSiteAttr>:$site,
                       OptionalAttr<SimEventRegion>:$resume_region);
  let successors = (successor AnySuccessor:$continuation);
  let assemblyFormat = [{$kind $values `processes` $process_count `to`
    $continuation attr-dict `:` type($values)}];
  let extraClassDeclaration = [{
    ::mlir::Operation::operand_range getProcesses();
    ::mlir::Operation::operand_range getContinuationOperands();
    ::mlir::MutableOperandRange getContinuationOperandsMutable();
  }];
  let hasVerifier = 1;
}

def NativeSuspendChildrenOp : ScheduleNativeOp<"suspend.children", [
    Terminator, DeclareOpInterfaceMethods<BranchOpInterface>,
    MemoryEffects<[MemWrite<SimSchedulerResource>,
                   MemWrite<SimProcessResource>]>
  ]> {
  let summary = "suspend until every current direct child has terminated";
  let arguments = (ins Variadic<AnyType>:$continuation_operands,
                       OptionalAttr<SimContinuationSiteAttr>:$site,
                       OptionalAttr<SimEventRegion>:$resume_region);
  let successors = (successor AnySuccessor:$continuation);
  let assemblyFormat = [{`to` $continuation
    (`(` $continuation_operands^ `:` type($continuation_operands) `)`)?
    attr-dict}];
  let hasVerifier = 1;
}

def NativeSpawnOp : ScheduleNativeOp<"spawn", [
    CallOpInterface, DeclareOpInterfaceMethods<SymbolUserOpInterface>,
    MemoryEffects<[MemWrite<SimSchedulerResource>, MemWrite<SimProcessResource>]>
  ]> {
  let summary = "spawn a sibling simulation process with explicit captures";
  let description = [{
    A spawn is a call edge that starts an independent logical process instead
    of executing in the caller. Its operands are the callee's entry captures;
    its result is the scheduler handle of the new process, not a callee result.
  }];
  let arguments = (ins FlatSymbolRefAttr:$callee, Variadic<AnyType>:$operands,
                       OptionalAttr<DictArrayAttr>:$arg_attrs,
                       OptionalAttr<DictArrayAttr>:$res_attrs);
  let results = (outs I64:$process);
  let assemblyFormat = [{$callee `(` $operands `)` attr-dict
    `:` type($operands) `->` qualified(type($process))}];
  let extraClassDeclaration = [{
    ::mlir::CallInterfaceCallable getCallableForCallee() { return getCalleeAttr(); }
    void setCalleeFromCallable(::mlir::CallInterfaceCallable callee) {
      setCalleeAttr(::llvm::cast<::mlir::FlatSymbolRefAttr>(
          ::llvm::cast<::mlir::SymbolRefAttr>(callee)));
    }
    ::mlir::Operation::operand_range getArgOperands() { return getOperands(); }
    ::mlir::MutableOperandRange getArgOperandsMutable() {
      return getOperandsMutable();
    }
  }];
  let hasVerifier = 1;
}

// Explicit projection at the semantic-context/native-ABI boundary.
def NativeContextOp : ScheduleNativeOp<"context", [Pure]> {
  let arguments = (ins SimContextType:$context);
  let results = (outs LLVM_AnyPointer:$result);
  let assemblyFormat = "$context attr-dict `:` type($result)";
}
def NativeTimeOp : ScheduleNativeOp<"time", [Pure]> {
  let arguments = (ins SimTimeType:$time);
  let results = (outs I64:$result);
  let assemblyFormat = "$time attr-dict";
}
def NativeProcessControlOp : ScheduleNativeOp<"process.control", [
    Terminator, DeclareOpInterfaceMethods<BranchOpInterface>,
    MemoryEffects<[MemRead<SimSchedulerResource>,
                   MemWrite<SimSchedulerResource>,
                   MemRead<SimProcessResource>, MemWrite<SimProcessResource>]>
  ]> {
  let summary = "apply an IEEE control operation to one process object";
  let description = [{
    This is a control-flow terminator because the target can dynamically name
    the executing logical process or one of its ancestors. Resuming a process,
    or controlling another process while the caller survives, transfers
    immediately to the continuation. Suspending the caller yields and later
    resumes at that continuation. Killing the caller never reaches it.

    A null handle reports a runtime invalid-handle failure. Zero-time function
    entries may contain this operation; lowering must propagate its control
    action through the function call chain before coroutine or bytecode
    materialization.
  }];
  let arguments = (ins SimProcessControlKind:$kind,
                       I64:$process,
                       Variadic<AnyType>:$continuation_operands,
                       OptionalAttr<SimContinuationSiteAttr>:$site);
  let successors = (successor AnySuccessor:$continuation);
  let assemblyFormat = [{$kind $process `to` $continuation
    (`(` $continuation_operands^ `:` type($continuation_operands) `)`)?
    attr-dict}];
  let hasVerifier = 1;
}
def NativeControlBoundaryOp : ScheduleNativeOp<"control.boundary", [
    Terminator, DeclareOpInterfaceMethods<BranchOpInterface>,
    MemoryEffects<[MemWrite<SimProcessResource>]>
  ]> {
  let summary = "register the resumable exit of a named block activation";
  let description = [{
    The resume successor is the block exit used when another logical process
    disables this activation. Its operands are checkpointed in the canonical
    process frame. Ordinary execution registers that continuation and follows
    the body successor without yielding to the scheduler.
  }];
  let arguments = (ins I64:$activation,
                       Variadic<AnyType>:$resume_operands,
                       OptionalAttr<SimContinuationSiteAttr>:$site);
  let successors = (successor AnySuccessor:$resume,
                               AnySuccessor:$body);
  let assemblyFormat = [{$activation `resume` $resume
    (`(` $resume_operands^ `:` type($resume_operands) `)`)?
    `body` $body attr-dict}];
  let hasVerifier = 1;
}
def NativeTaskCallOp : ScheduleNativeOp<"task.call", [
    Terminator, DeclareOpInterfaceMethods<BranchOpInterface>,
    DeclareOpInterfaceMethods<SymbolUserOpInterface>,
    MemoryEffects<[
      MemRead<SimStorageResource>, MemWrite<SimStorageResource>,
      MemRead<SimNetResource>, MemWrite<SimNetResource>,
      MemRead<SimSchedulerResource>, MemWrite<SimSchedulerResource>,
      MemRead<SimProcessResource>, MemWrite<SimProcessResource>,
      MemRead<SimHeapResource>, MemWrite<SimHeapResource>,
      MemRead<SimIOResource>, MemWrite<SimIOResource>,
      MemRead<SimRNGResource>, MemWrite<SimRNGResource>,
      MemRead<SimExternalResource>, MemWrite<SimExternalResource>
    ]>
  ]> {
  let summary = "transfer the current logical process into a task activation";
  let description = [{
    The argument prefix initializes the callee activation. The remaining
    operands are forwarded to the caller continuation after the task returns.
    Output destinations are explicit reference arguments in the callee
    signature, so a suspended task never retains native caller addresses.
  }];
  let arguments = (ins FlatSymbolRefAttr:$callee, Variadic<AnyType>:$values,
                       I64Attr:$argument_count,
                       OptionalAttr<SimContinuationSiteAttr>:$site);
  let successors = (successor AnySuccessor:$continuation);
  let assemblyFormat = [{$callee `(` $values `)` `arguments`
    $argument_count `to` $continuation attr-dict `:` type($values)}];
  let extraClassDeclaration = [{
    ::mlir::Operation::operand_range getArguments();
    ::mlir::Operation::operand_range getContinuationOperands();
    ::mlir::MutableOperandRange getContinuationOperandsMutable();
  }];
  let hasVerifier = 1;
}
def NativeClassVirtualTaskCallOp : ScheduleNativeOp<"class.virtual_task_call", [
    Terminator, DeclareOpInterfaceMethods<BranchOpInterface>,
    DeclareOpInterfaceMethods<SymbolUserOpInterface>,
    MemoryEffects<[
      MemRead<SimStorageResource>, MemWrite<SimStorageResource>,
      MemRead<SimNetResource>, MemWrite<SimNetResource>,
      MemRead<SimSchedulerResource>, MemWrite<SimSchedulerResource>,
      MemRead<SimProcessResource>, MemWrite<SimProcessResource>,
      MemRead<SimHeapResource>, MemWrite<SimHeapResource>,
      MemRead<SimIOResource>, MemWrite<SimIOResource>,
      MemRead<SimRNGResource>, MemWrite<SimRNGResource>,
      MemRead<SimExternalResource>, MemWrite<SimExternalResource>
    ]>
  ]> {
  let summary = "descriptor-dispatched virtual class task activation";
  let description = [{
    Resolves a virtual task through the receiver's managed class descriptor,
    then transfers the current logical process into the selected task
    activation. The receiver and explicit argument prefix initialize the
    method inputs after the implicit caller context; remaining values are
    forwarded to the continuation.
  }];
  let arguments = (ins I64:$receiver,
                       FlatSymbolRefAttr:$method,
                       I64Attr:$slot,
                       I64Attr:$signature_id,
                       Variadic<AnyType>:$values,
                       I64Attr:$argument_count,
                       OptionalAttr<SimContinuationSiteAttr>:$site);
  let successors = (successor AnySuccessor:$continuation);
  let assemblyFormat = [{
    $receiver `[` $method `]` `slot` $slot
    `signature_id` $signature_id
    `(` $values `)` `arguments` $argument_count `to` $continuation
    attr-dict `:` functional-type(operands, results)
  }];
  let extraClassDeclaration = [{
    ::mlir::Operation::operand_range getArguments();
    ::mlir::Operation::operand_range getContinuationOperands();
    ::mlir::MutableOperandRange getContinuationOperandsMutable();
  }];
  let hasVerifier = 1;
}
def NativeExecuteOp : ScheduleNativeOp<"execute", [
    DeclareOpInterfaceMethods<SymbolUserOpInterface>]> {
  let summary = "invoke a scheduled body through its prepared native ABI";
  let arguments = (ins SymbolRefAttr:$callee, Variadic<AnyType>:$operands,
                       OptionalAttr<UnitAttr>:$status_result);
  let results = (outs Variadic<AnyType>:$results);
  let assemblyFormat = "$callee `(` $operands `)` attr-dict `:` functional-type($operands, $results)";
}

def NativeMethodOp : ScheduleNativeOp<"class.method", [
    Symbol, DeclareOpInterfaceMethods<SymbolUserOpInterface>,
    MemoryEffects<[MemWrite<SimInventoryResource>]>
  ]> {
  let summary = "class method signature and deterministic dispatch slot";
  let arguments = (ins
    SymbolNameAttr:$sym_name,
    FlatSymbolRefAttr:$owner,
    TypeAttr:$function_type,
    OptionalAttr<TypeAttr>:$native_type,
    OptionalAttr<I64Attr>:$slot,
    OptionalAttr<I64Attr>:$signature_id,
    OptionalAttr<I64Attr>:$interface_ordinal,
    OptionalAttr<FlatSymbolRefAttr>:$implementation,
    BoolAttr:$is_virtual,
    BoolAttr:$is_pure,
    BoolAttr:$is_static,
    BoolAttr:$is_task,
    BoolAttr:$is_final,
    OptionalAttr<StrAttr>:$debug_name
  );
  let assemblyFormat = [{
    $sym_name `of` $owner (`slot` $slot^)?
    (`signature_id` $signature_id^)?
    (`interface_ordinal` $interface_ordinal^)?
    (`implemented_by` $implementation^)? `:` $function_type attr-dict
  }];
  let hasVerifier = 1;
}

def NativeObserverOp : ScheduleNativeOp<"observer", [Pure]> {
  let summary = "computed observer descriptor with physical captures";
  let arguments = (ins Variadic<AnyType>:$values, I64Attr:$capture_count);
  let results = (outs I64:$token);
  let assemblyFormat = [{
    `(` ($values^ `:` type($values))? `)` `captures` $capture_count attr-dict
  }];
  let hasVerifier = 1;
}
def NativeScratchOp : ScheduleNativeOp<"scratch", [MemoryEffects<[MemAlloc]>]> {
  let summary = "native activation scratch storage that survives status branches";
  let arguments = (ins I64Attr:$size);
  let results = (outs RTMutableByteSpanType:$result);
  let assemblyFormat = "$size attr-dict";
  let hasVerifier = 1;
}
def NativeByteAddressOp : ScheduleNativeOp<"bytes.address", [Pure]> {
  let summary = "immutable bytes awaiting executable reachability";
  let arguments = (ins StrAttr:$name, StrAttr:$value, I64Attr:$alignment);
  let results = (outs LLVM_AnyPointer:$result);
  let assemblyFormat = [{
    $name `=` $value `alignment` $alignment attr-dict `:` qualified(type($result))
  }];
}
// Activation-local root records are lowered only after simulation design
// symbol tables have been flattened into the native module.
def NativeManagedRootPushOp : ScheduleNativeOp<"gc.root_range.push", [
    MemoryEffects<[MemRead, MemWrite, MemRead<SimProcessResource>,
                   MemWrite<SimHeapResource>]>
  ]> {
  let arguments = (ins LLVM_AnyPointer:$record, LLVM_AnyPointer:$slots, I64:$count);
  let results = (outs I32:$status);
  let assemblyFormat = [{
    $record `slots` $slots `count` $count attr-dict `:`
    qualified(type($record)) `,` qualified(type($slots))
  }];
}
def NativeManagedRootPopOp : ScheduleNativeOp<"gc.root_range.pop", [
    MemoryEffects<[MemRead, MemWrite, MemRead<SimProcessResource>,
                   MemWrite<SimHeapResource>]>
  ]> {
  let arguments = (ins LLVM_AnyPointer:$record);
  let assemblyFormat = "$record attr-dict `:` qualified(type($record))";
}
#endif
