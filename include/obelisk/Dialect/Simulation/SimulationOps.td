//===- SimulationOps.td - Executable simulation operations ----*- td -*-===//

#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONOPS_TD
#define OBELISK_DIALECT_SIMULATION_SIMULATIONOPS_TD

include "obelisk/Dialect/Simulation/SimulationDialect.td"
include "obelisk/Dialect/Simulation/SimulationEnums.td"
include "obelisk/Dialect/Simulation/SimulationAttrs.td"
include "obelisk/Dialect/Simulation/SimulationTypes.td"
include "obelisk/Dialect/Runtime/RuntimeTypes.td"
include "mlir/IR/SymbolInterfaces.td"
include "mlir/Interfaces/CallInterfaces.td"
include "mlir/Interfaces/ControlFlowInterfaces.td"
include "mlir/Interfaces/FunctionInterfaces.td"
include "mlir/Interfaces/InferTypeOpInterface.td"
include "mlir/Interfaces/MemorySlotInterfaces.td"
include "mlir/Interfaces/SideEffectInterfaces.td"
include "mlir/Interfaces/ViewLikeInterface.td"

def SimStorageResource : Resource<"::obelisk::sim::StorageResource">;
def SimNetResource : Resource<"::obelisk::sim::NetResource">;
def SimSchedulerResource : Resource<"::obelisk::sim::SchedulerResource">;
def SimProcessResource : Resource<"::obelisk::sim::ProcessResource">;
def SimHeapResource : Resource<"::obelisk::sim::HeapResource">;
def SimIOResource : Resource<"::obelisk::sim::IOResource">;
def SimRNGResource : Resource<"::obelisk::sim::RNGResource">;
def SimCoverageResource : Resource<"::obelisk::sim::CoverageResource">;
def SimExternalResource : Resource<"::obelisk::sim::ExternalResource">;
// Descriptor declarations populate the design inventory. Modelling that as a
// write keeps them inside the effect system and, because they have no results,
// keeps dead-code elimination from deleting the design out from under the
// code units that reference its descriptors by ID.
def SimInventoryResource : Resource<"::obelisk::sim::InventoryResource">;

class SimOp<string mnemonic, list<Trait> traits = []>
    : Op<ObeliskSimulationDialect, mnemonic, traits> {
  let assemblyFormat =
      "operands attr-dict `:` functional-type(operands, results)";
}

class SimRefElementMatches<string ref, string value>
    : TypesMatchWith<
          "reference element type must match value type", ref, value,
          "::llvm::cast<::obelisk::sim::RefType>($_self).getElementType()">;
class SimNetElementMatches<string net, string value>
    : TypesMatchWith<
          "net element type must match value type", net, value,
          "::llvm::cast<::obelisk::sim::NetType>($_self).getElementType()">;
class SimDriverElementMatches<string driver, string value>
    : TypesMatchWith<
          "driver element type must match value type", driver, value,
          "::llvm::cast<::obelisk::sim::DriverType>($_self).getElementType()">;

//===----------------------------------------------------------------------===//
// Design inventory
//===----------------------------------------------------------------------===//

def SimDesignOp : SimOp<"design", [
  Symbol, SymbolTable, IsolatedFromAbove, NoTerminator, SingleBlock,
  RecursiveMemoryEffects
]> {
  let summary = "isolated flattened simulation design";
  let description = [{
    Owns one flattened descriptor and code-unit inventory. When present,
    `time_precision_fs` defines the femtosecond duration of one
    `obelisk_sim.time` tick for every nested code unit.
  }];
  let arguments = (ins SymbolNameAttr:$sym_name,
                       OptionalAttr<I64Attr>:$time_precision_fs,
                       OptionalAttr<SimComputeGraphAttr>:$compute_graph);
  let regions = (region AnyRegion : $body);
  let assemblyFormat = "$sym_name attr-dict-with-keyword $body";
  // A region verifier runs after the nested descriptor and code-unit
  // verifiers, so cross-referencing them here can rely on their invariants
  // and reports the most specific diagnostic first.
  let hasRegionVerifier = 1;
}

def SimScopeDeclOp : SimOp<"scope.decl", [MemoryEffects<[MemWrite<SimInventoryResource>]>]> {
  let summary = "immutable numeric hierarchy descriptor";
  let arguments = (ins I64Attr:$id, OptionalAttr<I64Attr>:$parent,
                       OptionalAttr<StrAttr>:$hierarchical_name,
                       OptionalAttr<StrAttr>:$debug_name);
  let assemblyFormat = [{$id (`parent` $parent^)?
    (`hierarchy` $hierarchical_name^)? (`debug` $debug_name^)? attr-dict}];
  let hasVerifier = 1;
}

def SimCodeUnitDeclOp : SimOp<"code_unit.decl", [
    MemoryEffects<[MemWrite<SimInventoryResource>]>
  ]> {
  let summary = "immutable process or zero-time function inventory record";
  let description = [{
    Retains the elaborated identity and lexical hierarchy of a code unit even
    when IPO erases its executable `obelisk_sim.func`. The ID is stable across
    compilation modes and is also the runtime process handle ID.
  }];
  let arguments = (ins I64Attr:$id, I64Attr:$scope_id,
                       SimEntryKind:$code_unit_kind,
                       StrAttr:$hierarchical_name,
                       OptionalAttr<StrAttr>:$debug_name,
                       OptionalAttr<UnitAttr>:$internal);
  let assemblyFormat = [{$id `in` $scope_id $code_unit_kind
    `hierarchy` $hierarchical_name (`debug` $debug_name^)? attr-dict}];
  let hasVerifier = 1;
}

def SimStorageDeclOp : SimOp<"storage.decl", [MemoryEffects<[MemWrite<SimInventoryResource>]>]> {
  let summary = "immutable mutable-storage descriptor";
  let arguments = (ins I64Attr:$id, I64Attr:$scope_id, TypeAttr:$type,
                       SimLifetime:$lifetime,
                       OptionalAttr<StrAttr>:$hierarchical_name,
                       OptionalAttr<StrAttr>:$debug_name,
                       OptionalAttr<SimComputeObservabilityKind>:$observability);
  let assemblyFormat = [{$id `in` $scope_id `:` $type $lifetime
    (`hierarchy` $hierarchical_name^)? (`debug` $debug_name^)? attr-dict}];
  let hasVerifier = 1;
}

def SimNetDeclOp : SimOp<"net.decl", [MemoryEffects<[MemWrite<SimInventoryResource>]>]> {
  let summary = "immutable resolved-net descriptor";
  let arguments = (ins I64Attr:$id, I64Attr:$scope_id, TypeAttr:$type,
                       SimLifetime:$lifetime,
                       OptionalAttr<StrAttr>:$hierarchical_name,
                       OptionalAttr<StrAttr>:$debug_name,
                       OptionalAttr<SimComputeObservabilityKind>:$observability,
                       DefaultValuedAttr<SimNetResolutionKind,
                         "::obelisk::sim::NetResolutionKind::Wire">:$resolution_kind,
                       OptionalAttr<UnitAttr>:$internal);
  let assemblyFormat = [{$id `in` $scope_id `:` $type $lifetime
    (`hierarchy` $hierarchical_name^)? (`debug` $debug_name^)? attr-dict}];
  let hasVerifier = 1;
}

def SimNetConnectDeclOp : SimOp<"net.connect.decl", [
    MemoryEffects<[MemWrite<SimInventoryResource>]>
  ]> {
  let summary = "canonical static bitwise net equivalence run";
  let arguments = (ins I64Attr:$id, I64Attr:$scope_id,
                       I64Attr:$lhs_net_id, I64Attr:$lhs_offset,
                       I64Attr:$rhs_net_id, I64Attr:$rhs_offset,
                       I64Attr:$width, BoolAttr:$rhs_reversed,
                       OptionalAttr<StrAttr>:$provenance);
  let assemblyFormat = [{$id `in` $scope_id $lhs_net_id `[` $lhs_offset `]`
    `to` $rhs_net_id `[` $rhs_offset `]` `width` $width
    `reversed` `=` $rhs_reversed
    (`provenance` $provenance^)? attr-dict}];
  let hasVerifier = 1;
}

def SimDriverDeclOp : SimOp<"driver.decl", [MemoryEffects<[MemWrite<SimInventoryResource>]>]> {
  let summary = "immutable static net-driver descriptor";
  let arguments = (ins I64Attr:$id, I64Attr:$scope_id, I64Attr:$net_id,
                       TypeAttr:$type, SimLifetime:$lifetime,
                       OptionalAttr<StrAttr>:$hierarchical_name,
                       OptionalAttr<StrAttr>:$debug_name,
                       OptionalAttr<I64Attr>:$driven_low,
                       OptionalAttr<I64Attr>:$driven_width);
  let assemblyFormat = [{$id `in` $scope_id `drives` $net_id `:` $type
    $lifetime (`hierarchy` $hierarchical_name^)?
    (`debug` $debug_name^)? attr-dict}];
  let hasVerifier = 1;
}

def SimCovergroupDeclOp : SimOp<"covergroup.decl", [
    Symbol, MemoryEffects<[MemWrite<SimInventoryResource>]>
  ]> {
  let summary = "immutable executable covergroup schema";
  let arguments = (ins SymbolNameAttr:$sym_name, I64Attr:$id,
                       DenseI64ArrayAttr:$coverpoint_bins,
                       OptionalAttr<StrAttr>:$debug_name);
  let assemblyFormat = [{
    $sym_name `id` $id `bins` $coverpoint_bins
    (`debug` $debug_name^)? attr-dict
  }];
  let hasVerifier = 1;
}

def SimCovergroupNullOp : SimOp<"covergroup.null", [Pure, ConstantLike]> {
  let summary = "typed null functional coverage handle";
  let results = (outs SimCovergroupHandleType:$result);
  let assemblyFormat = "attr-dict `:` qualified(type($result))";
  let hasVerifier = 1;
}

def SimCovergroupCreateOp : SimOp<"covergroup.create", [
    MemoryEffects<[MemRead<SimCoverageResource>,
                   MemWrite<SimCoverageResource>]>
  ]> {
  let summary = "create an enabled per-instance coverage accumulator";
  let arguments = (ins SimContextType:$context,
                       FlatSymbolRefAttr:$declaration);
  let results = (outs SimCovergroupHandleType:$result);
  let assemblyFormat = [{
    $context `from` $declaration attr-dict `:`
    type($context) `->` qualified(type($result))
  }];
  let hasVerifier = 1;
}

def SimCovergroupSampleEnabledOp : SimOp<"covergroup.sample_enabled", [
    MemoryEffects<[MemRead<SimCoverageResource>]>
  ]> {
  let summary = "test whether a covergroup instance currently samples";
  let arguments = (ins SimContextType:$context,
                       SimCovergroupHandleType:$handle);
  let results = (outs I1:$result);
  let hasVerifier = 1;
}

def SimCovergroupBinHitOp : SimOp<"covergroup.bin_hit", [
    MemoryEffects<[MemRead<SimCoverageResource>,
                   MemWrite<SimCoverageResource>]>
  ]> {
  let summary = "saturating hit of one named bin";
  let arguments = (ins SimContextType:$context,
                       SimCovergroupHandleType:$handle,
                       I32Attr:$coverpoint, I32Attr:$bin);
  let assemblyFormat = [{
    $context `,` $handle `[` $coverpoint `,` $bin `]` attr-dict `:`
    type($context) `,` qualified(type($handle))
  }];
  let hasVerifier = 1;
}

def SimCovergroupSampleOp : SimOp<"covergroup.sample", [
    MemoryEffects<[MemRead<SimCoverageResource>,
                   MemWrite<SimCoverageResource>]>
  ]> {
  let summary = "atomically commit one complete manual sample";
  let arguments = (ins SimContextType:$context,
                       SimCovergroupHandleType:$handle,
                       Variadic<I1>:$hits);
  let assemblyFormat = [{
    $context `,` $handle `[` $hits `]` attr-dict `:`
    type($context) `,` qualified(type($handle))
  }];
  let hasVerifier = 1;
}

class SimCovergroupControlOp<string mnemonic>
    : SimOp<mnemonic, [
        MemoryEffects<[MemRead<SimCoverageResource>,
                       MemWrite<SimCoverageResource>]>
      ]> {
  let arguments = (ins SimContextType:$context,
                       SimCovergroupHandleType:$handle);
  let assemblyFormat = [{
    $context `,` $handle attr-dict `:`
    type($context) `,` qualified(type($handle))
  }];
  let hasVerifier = 1;
}

def SimCovergroupStartOp
    : SimCovergroupControlOp<"covergroup.start"> {
  let summary = "enable sampling without clearing coverage";
}

def SimCovergroupStopOp
    : SimCovergroupControlOp<"covergroup.stop"> {
  let summary = "disable sampling without clearing coverage";
}

def SimCovergroupInstanceQueryOp : SimOp<"covergroup.instance_query", [
    MemoryEffects<[MemRead<SimCoverageResource>]>
  ]> {
  let summary = "query equal-weight per-instance coverage";
  let arguments = (ins SimContextType:$context,
                       SimCovergroupHandleType:$handle);
  let results = (outs F64:$percentage, I32:$covered, I32:$total);
  let hasVerifier = 1;
}

def SimCovergroupTypeQueryOp : SimOp<"covergroup.type_query", [
    MemoryEffects<[MemRead<SimCoverageResource>]>
  ]> {
  let summary = "query average coverage of all constructed instances";
  let arguments = (ins SimContextType:$context,
                       FlatSymbolRefAttr:$declaration);
  let results = (outs F64:$percentage, I32:$covered, I32:$total);
  let assemblyFormat = [{
    $context `from` $declaration attr-dict `:`
    type($context) `->` `(` type($percentage) `,`
    type($covered) `,` type($total) `)`
  }];
  let hasVerifier = 1;
}

//===----------------------------------------------------------------------===//
// Managed classes and dispatch inventory
//===----------------------------------------------------------------------===//

def SimClassDeclOp : SimOp<"class.decl", [
    Symbol, MemoryEffects<[MemWrite<SimInventoryResource>]>
  ]> {
  let summary = "immutable managed class and hierarchy descriptor";
  let arguments = (ins
    SymbolNameAttr:$sym_name,
    I64Attr:$id,
    OptionalAttr<FlatSymbolRefAttr>:$base,
    OptionalAttr<ArrayAttr>:$interfaces,
    OptionalAttr<FlatSymbolRefAttr>:$weak_referent,
    BoolAttr:$is_abstract,
    BoolAttr:$is_interface,
    BoolAttr:$is_final,
    OptionalAttr<StrAttr>:$debug_name
  );
  let assemblyFormat = [{
    $sym_name `id` $id (`extends` $base^)?
    (`implements` $interfaces^)? attr-dict
  }];
  let hasVerifier = 1;
}

def SimClassFieldDeclOp : SimOp<"class.field", [
    Symbol, MemoryEffects<[MemWrite<SimInventoryResource>]>
  ]> {
  let summary = "declaration-order class property and trace descriptor";
  let arguments = (ins
    SymbolNameAttr:$sym_name,
    FlatSymbolRefAttr:$owner,
    TypeAttr:$type,
    I64Attr:$ordinal,
    OptionalAttr<I64Attr>:$offset,
    BoolAttr:$is_static,
    BoolAttr:$is_weak,
    OptionalAttr<StrAttr>:$debug_name
  );
  let assemblyFormat = [{
    $sym_name `of` $owner `at` $ordinal (`offset` $offset^)?
    `:` $type attr-dict
  }];
  let hasVerifier = 1;
}

def SimClassMethodDeclOp : SimOp<"class.method", [
    Symbol, MemoryEffects<[MemWrite<SimInventoryResource>]>
  ]> {
  let summary = "class method signature and deterministic dispatch slot";
  let arguments = (ins
    SymbolNameAttr:$sym_name,
    FlatSymbolRefAttr:$owner,
    TypeAttr:$function_type,
    OptionalAttr<I64Attr>:$slot,
    OptionalAttr<I64Attr>:$signature_id,
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
    (`implemented_by` $implementation^)? `:` $function_type attr-dict
  }];
  let hasVerifier = 1;
}

def SimClassNullOp : SimOp<"class.null", [Pure, ConstantLike]> {
  let summary = "typed null managed class handle";
  let results = (outs SimClassHandleType:$result);
  let assemblyFormat = "attr-dict `:` qualified(type($result))";
}

def SimManagedNullOp : SimOp<"managed.null", [Pure, ConstantLike]> {
  let summary = "typed null non-class managed handle";
  let results = (outs AnyType:$result);
  let assemblyFormat = "attr-dict `:` qualified(type($result))";
  let hasVerifier = 1;
}

def SimManagedIsNullOp : SimOp<"managed.is_null", [Pure]> {
  let summary = "test a managed handle for the canonical null value";
  let arguments = (ins AnyType:$input);
  let results = (outs I1:$result);
  let hasVerifier = 1;
}

def SimEventNullOp : SimOp<"event.null", [Pure, ConstantLike]> {
  let summary = "null/default event handle";
  let results = (outs SimEventType:$result);
  let assemblyFormat = "attr-dict";
}

//===----------------------------------------------------------------------===//
// Managed sequential containers
//===----------------------------------------------------------------------===//

def SimContainerSizeOp : SimOp<"container.size", [
    MemoryEffects<[MemRead<SimHeapResource>]>
  ]> {
  let summary = "return the live size of a dynamic array or queue";
  let arguments = (ins AnyType:$container);
  let results = (outs I64:$result);
  let hasVerifier = 1;
}

def SimContainerCreateLikeOp : SimOp<"container.create_like", [
    MemoryEffects<[MemRead<SimHeapResource>, MemWrite<SimHeapResource>]>
  ]> {
  let summary = "create a sequential container with matching typed metadata";
  let arguments = (ins AnyType:$preferred, AnyType:$fallback, I64:$size);
  let results = (outs AnyType:$result);
  let hasVerifier = 1;
}

def SimContainerCreateOp : SimOp<"container.create", [
    MemoryEffects<[MemRead<SimHeapResource>, MemWrite<SimHeapResource>]>
  ]> {
  let summary = "create a sequential container from stable type metadata";
  let arguments = (ins
    I64:$size,
    I64Attr:$type_id,
    I32Attr:$element_kind,
    I32Attr:$element_flags,
    I64Attr:$value_size,
    I64Attr:$alignment,
    I64Attr:$bit_width,
    DenseI64ArrayAttr:$trace_offsets,
    DenseI32ArrayAttr:$trace_kinds,
    I32Attr:$container_kind,
    I64Attr:$bound
  );
  let results = (outs AnyType:$result);
  let hasVerifier = 1;
}

def SimContainerCloneOp : SimOp<"container.clone", [
    MemoryEffects<[MemRead<SimHeapResource>, MemWrite<SimHeapResource>]>
  ]> {
  let summary = "deep-clone a dynamic array or queue value";
  let arguments = (ins AnyType:$input);
  let results = (outs AnyType:$result);
  let hasVerifier = 1;
}

def SimContainerDeleteOp : SimOp<"container.delete", [
    MemoryEffects<[MemRead<SimHeapResource>, MemWrite<SimHeapResource>]>
  ]> {
  let summary = "clear a dynamic array or queue";
  let arguments = (ins AnyType:$container);
  let hasVerifier = 1;
}

def SimQueueDeleteOp : SimOp<"queue.delete", [
    MemoryEffects<[MemRead<SimHeapResource>, MemWrite<SimHeapResource>]>
  ]> {
  let summary = "delete one queue element by logical index";
  let arguments = (ins SimQueueType:$queue, I64:$index);
  let assemblyFormat = "$queue `[` $index `]` attr-dict `:` type($queue)";
  let hasVerifier = 1;
}

def SimQueueInsertOp : SimOp<"queue.insert", [
    MemoryEffects<[MemRead<SimHeapResource>, MemWrite<SimHeapResource>]>
  ]> {
  let summary = "insert one queue element at a logical index";
  let arguments = (ins SimQueueType:$queue, I64:$index, AnyType:$value);
  let assemblyFormat = "$value `into` $queue `[` $index `]` attr-dict `:` "
                       "type($queue) `,` type($value)";
  let hasVerifier = 1;
}

def SimRandomNextOp : SimOp<"random.next", [
    MemoryEffects<[MemRead<SimRNGResource>, MemWrite<SimRNGResource>]>
  ]> {
  let summary = "draw the next 64 bits from the active process stream";
  let arguments = (ins SimContextType:$context);
  let results = (outs I64:$result);
}

def SimRandomBoundedOp : SimOp<"random.bounded", [
    MemoryEffects<[MemRead<SimRNGResource>, MemWrite<SimRNGResource>]>
  ]> {
  let summary = "draw an unbiased active-process integer below a bound";
  let arguments = (ins SimContextType:$context, I64:$bound);
  let results = (outs I64:$result);
}

def SimRandomDistributionOp : SimOp<"random.distribution", [
    MemoryEffects<[MemRead<SimRNGResource>, MemWrite<SimRNGResource>]>
  ]> {
  let summary = "draw a $dist_* variate from the active process stream";
  let description = [{
    `distribution` selects the shape as an `obelisk_rt_distribution` value.
    `first` and `second` carry the distribution's parameters in source order;
    distributions taking a single parameter ignore `second`.
  }];
  let arguments = (ins SimContextType:$context, I32Attr:$distribution,
                       I32:$first, I32:$second);
  let results = (outs I32:$result);
}

def SimRandomSolveOp : SimOp<"random.solve", [
    MemoryEffects<[MemRead<SimRNGResource>]>
  ]> {
  let summary = "execute a compiler-produced residual random constraint plan";
  let arguments = (ins SimContextType:$context, I64:$start,
                       I64:$mutable_mask, I64:$constraint_mask,
                       I64:$max_attempts, I64:$rng_state,
                       I64:$rng_increment,
                       Variadic<I64>:$captures,
                       StrAttr:$program);
  let results = (outs I64:$assignment, I1:$success, I64:$next_rng_state);
  let assemblyFormat = [{
    $context `from` $start `mutable` $mutable_mask `constraints`
    $constraint_mask `limit` $max_attempts `state` $rng_state `increment`
    $rng_increment `captures` `(` $captures `)`
    $program attr-dict `:` type($context)
  }];
}

def SimRandomSeedOp : SimOp<"random.seed", [
    MemoryEffects<[MemWrite<SimRNGResource>]>
  ]> {
  let summary = "seed the active process stream";
  let arguments = (ins SimContextType:$context, I64:$seed);
}

def SimContainerReadOp : SimOp<"container.read", [
    MemoryEffects<[MemRead<SimHeapResource>]>
  ]> {
  let summary = "typed read from a dynamic array or queue";
  let arguments = (ins AnyType:$container, I64:$index);
  let results = (outs AnyType:$result);
  let hasVerifier = 1;
}

def SimContainerWriteOp : SimOp<"container.write", [
    MemoryEffects<[MemRead<SimHeapResource>, MemWrite<SimHeapResource>]>
  ]> {
  let summary = "typed write to a dynamic array or queue";
  let arguments = (ins AnyType:$container, I64:$index, AnyType:$value);
  let hasVerifier = 1;
}

//===----------------------------------------------------------------------===//
// Managed associative arrays
//===----------------------------------------------------------------------===//

def SimAssocCreateOp : SimOp<"assoc.create", [
    MemoryEffects<[MemRead<SimHeapResource>, MemWrite<SimHeapResource>]>
  ]> {
  let summary = "create an empty typed associative array";
  let arguments = (ins
    I64Attr:$type_id,
    I32Attr:$element_kind,
    I32Attr:$element_flags,
    I64Attr:$value_size,
    I64Attr:$alignment,
    I64Attr:$bit_width,
    DenseI64ArrayAttr:$trace_offsets,
    DenseI32ArrayAttr:$trace_kinds,
    I32Attr:$key_kind,
    I64Attr:$key_width);
  let results = (outs SimAssocArrayType:$result);
  let hasVerifier = 1;
}

def SimAssocReadOp : SimOp<"assoc.read", [
    MemoryEffects<[MemRead<SimHeapResource>]>
  ]> {
  let summary = "read an associative entry or its installed default";
  let arguments = (ins SimAssocArrayType:$array, AnyType:$key);
  let results = (outs AnyType:$result);
  let hasVerifier = 1;
}

def SimAssocWriteOp : SimOp<"assoc.write", [
    MemoryEffects<[MemRead<SimHeapResource>, MemWrite<SimHeapResource>]>
  ]> {
  let summary = "insert or replace an explicit associative entry";
  let arguments = (ins SimAssocArrayType:$array, AnyType:$key, AnyType:$value);
  let hasVerifier = 1;
}

def SimAssocExistsOp : SimOp<"assoc.exists", [
    MemoryEffects<[MemRead<SimHeapResource>]>
  ]> {
  let summary = "test whether an explicit associative entry exists";
  let arguments = (ins SimAssocArrayType:$array, AnyType:$key);
  let results = (outs I1:$result);
  let hasVerifier = 1;
}

def SimAssocDeleteOp : SimOp<"assoc.delete", [
    MemoryEffects<[MemRead<SimHeapResource>, MemWrite<SimHeapResource>]>
  ]> {
  let summary = "delete one associative key";
  let arguments = (ins SimAssocArrayType:$array, AnyType:$key);
  let hasVerifier = 1;
}

def SimAssocSetDefaultOp : SimOp<"assoc.set_default", [
    MemoryEffects<[MemRead<SimHeapResource>, MemWrite<SimHeapResource>]>
  ]> {
  let summary = "install a hidden assignment-pattern default";
  let arguments = (ins SimAssocArrayType:$array, AnyType:$value);
  let hasVerifier = 1;
}

def SimAssocTraverseOp : SimOp<"assoc.traverse", [
    MemoryEffects<[MemRead<SimHeapResource>, MemWrite<SimHeapResource>]>
  ]> {
  let summary = "perform ordered first, last, next, or previous traversal";
  let arguments = (ins SimAssocArrayType:$array, AnyType:$key,
                       I32Attr:$direction, BoolAttr:$endpoint);
  let results = (outs AnyType:$result_key, I1:$success);
  let hasVerifier = 1;
}

//===----------------------------------------------------------------------===//
// Managed strings
//===----------------------------------------------------------------------===//

class SimStringAllocatingOp<string mnemonic, dag stringArgs>
    : SimOp<mnemonic, [
        MemoryEffects<[MemRead<SimHeapResource>, MemWrite<SimHeapResource>]>
      ]> {
  let arguments = stringArgs;
  let results = (outs SimStringType:$result);
}

def SimStringLiteralOp
    : SimStringAllocatingOp<"string.literal", (ins StrAttr:$value)> {
  let summary = "materialize an immutable managed byte string";
  let assemblyFormat = "$value attr-dict";
}

def SimStringFromPackedOp
    : SimStringAllocatingOp<"string.from_packed", (ins AnyType:$input)> {
  let summary = "convert a packed value to a managed byte string";
  let hasVerifier = 1;
}

def SimStringToPackedOp : SimOp<"string.to_packed", [Pure]> {
  let summary = "convert a managed byte string to a two-state packed value";
  let arguments = (ins SimStringType:$input);
  let results = (outs AnyInteger:$result);
}

def SimStringConcatOp
    : SimStringAllocatingOp<"string.concat",
                            (ins Variadic<SimStringType>:$inputs)> {
  let summary = "concatenate managed byte strings";
  let hasVerifier = 1;
}

def SimStringRepeatOp
    : SimStringAllocatingOp<"string.repeat",
                            (ins SimStringType:$input, I64:$count)> {
  let summary = "repeat a managed byte string a runtime number of times";
}

def SimStringLengthOp : SimOp<"string.length", [Pure]> {
  let summary = "return the byte length of a managed string";
  let arguments = (ins SimStringType:$input);
  let results = (outs I64:$result);
}

def SimStringGetcOp : SimOp<"string.getc", [Pure]> {
  let summary = "read one byte, returning zero for an invalid index";
  let arguments = (ins SimStringType:$input, I64:$index);
  let results = (outs I8:$result);
}

def SimStringPutcOp
    : SimStringAllocatingOp<"string.putc",
                            (ins SimStringType:$input, I64:$index,
                                 I8:$character)> {
  let summary = "return a string with one byte conditionally updated";
}

def SimStringSubstrOp
    : SimStringAllocatingOp<"string.substr",
                            (ins SimStringType:$input, I64:$left, I64:$right)> {
  let summary = "return an inclusive byte substring";
}

def SimStringCompareOp : SimOp<"string.compare", [Pure]> {
  let summary = "compare two managed strings lexicographically";
  let arguments = (ins SimStringType:$lhs, SimStringType:$rhs,
                       BoolAttr:$case_insensitive);
  let results = (outs I32:$result);
  let assemblyFormat = [{
    $lhs `,` $rhs `case_insensitive` `=` $case_insensitive attr-dict
  }];
}

def SimStringCaseConvertOp
    : SimStringAllocatingOp<"string.case_convert",
                            (ins SimStringType:$input, BoolAttr:$to_upper)> {
  let summary = "apply ASCII upper- or lower-case conversion";
  let assemblyFormat = [{
    $input `to_upper` `=` $to_upper attr-dict
  }];
}

def SimStringScanFieldOp : SimOp<"string.scan_field", [
    MemoryEffects<[MemRead<SimHeapResource>, MemWrite<SimHeapResource>]>
  ]> {
  let summary = "take one $sscanf conversion's field out of a string";
  let description = [{
    `prefix` is the format text before the conversion: whitespace in it
    matches any run of input whitespace including none, and every other
    character must match exactly. `ok` is zero when the prefix did not match
    or the field was empty, and `next_cursor` then repeats `cursor`.
  }];
  let arguments = (ins SimStringType:$input, I32:$cursor, StrAttr:$prefix,
                       I32Attr:$specifier);
  let results = (outs SimStringType:$field, I32:$next_cursor, I32:$ok);
}

def SimStringParseIntegerOp : SimOp<"string.parse_integer", [Pure]> {
  let summary = "parse a managed string using a fixed integer radix";
  let arguments = (ins SimStringType:$input, I32Attr:$radix);
  let results = (outs I64:$result);
  let assemblyFormat = "$input `radix` `=` $radix attr-dict";
  let hasVerifier = 1;
}

def SimStringParseRealOp : SimOp<"string.parse_real", [Pure]> {
  let summary = "parse a locale-independent real from a managed string";
  let arguments = (ins SimStringType:$input);
  let results = (outs F64:$result);
}

def SimStringFormatIntegerOp
    : SimStringAllocatingOp<"string.format_integer",
                            (ins I64:$input, I32Attr:$radix,
                                 BoolAttr:$is_signed)> {
  let summary = "format an integer into a managed string";
  let assemblyFormat = [{
    $input `radix` `=` $radix `signed` `=` $is_signed attr-dict
  }];
  let hasVerifier = 1;
}

def SimStringFormatRealOp
    : SimStringAllocatingOp<"string.format_real", (ins F64:$input)> {
  let summary = "format a real into a managed string";
}

def SimClassAllocOp : SimOp<"class.alloc", [
    MemoryEffects<[MemRead<SimHeapResource>, MemWrite<SimHeapResource>]>
  ]> {
  let summary = "allocate zero-initialized storage for a concrete class";
  let arguments = (ins SimContextType:$context);
  let results = (outs SimClassHandleType:$result);
  let assemblyFormat =
      "$context attr-dict `:` type($context) `->` qualified(type($result))";
  let hasVerifier = 1;
}

def SimClassCopyOp : SimOp<"class.copy", [
    MemoryEffects<[MemRead<SimHeapResource>, MemWrite<SimHeapResource>]>
  ]> {
  let summary = "allocate a static-type SystemVerilog shallow class copy";
  let arguments = (ins SimContextType:$context, SimClassHandleType:$source);
  let results = (outs SimClassHandleType:$result);
  let assemblyFormat = [{
    $context `,` $source attr-dict `:`
    type($context) `,` qualified(type($source)) `->` qualified(type($result))
  }];
  let hasVerifier = 1;
}

def SimClassIsInstanceOp : SimOp<"class.is_instance", [Pure]> {
  let summary = "test a dynamic class against a class or interface";
  let arguments = (ins SimClassHandleType:$object,
                       FlatSymbolRefAttr:$target);
  let results = (outs I1:$result);
  let assemblyFormat =
      "$object `is` $target attr-dict `:` qualified(type($object))";
  let hasVerifier = 1;
}

def SimClassIdOp : SimOp<"class.id", [Pure]> {
  let summary = "return the stable identity of a managed object";
  let description = [{
    Returns zero for a null handle and a nonzero identity that is stable for
    the lifetime of every allocated object. This implements
    `weak_reference::get_id` without exposing a native address.
  }];
  let arguments = (ins SimClassHandleType:$object);
  let results = (outs I64:$result);
  let assemblyFormat =
      "$object attr-dict `:` qualified(type($object))";
}

def SimClassCastOp : SimOp<"class.cast", [Pure]> {
  let summary = "checked class or interface cast";
  let arguments = (ins SimClassHandleType:$object);
  let results = (outs SimClassHandleType:$result);
  let assemblyFormat = [{
    $object attr-dict `:` qualified(type($object))
    `to` qualified(type($result))
  }];
  let hasVerifier = 1;
}

def SimClassFieldRefOp : SimOp<"class.field_ref", [Pure]> {
  let summary = "managed reference to one instance property";
  let arguments = (ins SimClassHandleType:$object,
                       FlatSymbolRefAttr:$field);
  let results = (outs SimManagedRefType:$result);
  let assemblyFormat = [{
    $object `[` $field `]` attr-dict `:` qualified(type($object))
    `->` qualified(type($result))
  }];
  let hasVerifier = 1;
}

def SimManagedLoadOp : SimOp<"managed.load", [
    MemoryEffects<[MemRead<SimHeapResource>]>
  ]> {
  let summary = "race-free load through a managed field reference";
  let arguments = (ins SimManagedRefType:$reference);
  let results = (outs AnyType:$result);
  let assemblyFormat = [{
    $reference attr-dict `:` qualified(type($reference))
    `->` type($result)
  }];
  let hasVerifier = 1;
}

def SimManagedStoreOp : SimOp<"managed.store", [
    MemoryEffects<[MemWrite<SimHeapResource>]>
  ]> {
  let summary = "race-free store through a managed field reference";
  let arguments = (ins AnyType:$value, SimManagedRefType:$reference);
  let assemblyFormat = [{
    $value `to` $reference attr-dict `:`
    type($value) `,` qualified(type($reference))
  }];
  let hasVerifier = 1;
}

def SimManagedNBAEnqueueOp : SimOp<"managed.nba.enqueue", [
    MemoryEffects<[
      MemWrite<SimHeapResource>, MemWrite<SimSchedulerResource>
    ]>
  ]> {
  let summary = "schedule a nonblocking managed-field update";
  let description = [{
    Captures both the owning object and any managed value as precise heap roots
    until the scheduler commits the update. The destination is revalidated and
    written atomically with respect to other managed-field accesses.
  }];
  let arguments = (ins AnyType:$value, SimManagedRefType:$destination,
                       Optional<SimTimeType>:$delay);
  let assemblyFormat = [{$value `to` $destination (`after` $delay^)? attr-dict
    `:` functional-type(operands, results)}];
  let hasVerifier = 1;
}

def SimReferencePathNBAEnqueueOp : SimOp<"reference_path.nba.enqueue", [
    MemoryEffects<[
      MemRead<SimHeapResource>, MemWrite<SimHeapResource>,
      MemWrite<SimSchedulerResource>
    ]>
  ]> {
  let summary = "schedule a nonblocking dynamic-container element update";
  let description = [{
    Captures a managed index path and the right-hand-side value. The scheduler
    roots the path until commit, then resolves the captured container and index;
    invalid indices remain no-ops.
  }];
  let arguments = (ins AnyType:$value, SimReferencePathType:$destination,
                       Optional<SimTimeType>:$delay);
  let assemblyFormat = [{$value `to` $destination (`after` $delay^)? attr-dict
    `:` functional-type(operands, results)}];
  let hasVerifier = 1;
}

def SimArgumentRefFromRefOp : SimOp<"argument_ref.from_ref", [Pure]> {
  let summary = "erase an ordinary reference into an exact-alias formal";
  let arguments = (ins SimRefType:$input);
  let results = (outs SimArgumentRefType:$result);
  let assemblyFormat =
      "$input attr-dict `:` qualified(type($input)) `->` qualified(type($result))";
  let hasVerifier = 1;
}

def SimArgumentRefFromManagedOp
    : SimOp<"argument_ref.from_managed", [Pure]> {
  let summary = "erase a managed property reference into an exact-alias formal";
  let arguments = (ins SimManagedRefType:$input);
  let results = (outs SimArgumentRefType:$result);
  let assemblyFormat =
      "$input attr-dict `:` qualified(type($input)) `->` qualified(type($result))";
  let hasVerifier = 1;
}

def SimReferencePathIndexOp : SimOp<"reference_path.index", [
    MemoryEffects<[MemRead<SimHeapResource>, MemWrite<SimHeapResource>]>
  ]> {
  let summary = "capture an escaping dynamic-array or queue element lvalue";
  let arguments = (ins SimContextType:$context, AnyType:$container, I64:$index,
                       SimArgumentRefType:$owner_reference);
  let results = (outs SimReferencePathType:$result);
  let assemblyFormat =
      "$context `,` $container `[` $index `]` `watching` $owner_reference attr-dict `:` functional-type(operands, results)";
  let hasVerifier = 1;
}

def SimReferencePathAssocOp : SimOp<"reference_path.assoc", [
    MemoryEffects<[MemRead<SimHeapResource>, MemWrite<SimHeapResource>]>
  ]> {
  let summary = "capture an escaping associative-array element lvalue";
  let arguments = (ins SimContextType:$context, SimAssocArrayType:$array,
                       AnyType:$key, SimArgumentRefType:$owner_reference);
  let results = (outs SimReferencePathType:$result);
  let assemblyFormat =
      "$context `,` $array `[` $key `]` `watching` $owner_reference attr-dict `:` functional-type(operands, results)";
  let hasVerifier = 1;
}

def SimArgumentRefFromPathOp
    : SimOp<"argument_ref.from_path", [Pure]> {
  let summary = "erase a managed reference path into an exact-alias formal";
  let arguments = (ins SimReferencePathType:$input);
  let results = (outs SimArgumentRefType:$result);
  let assemblyFormat =
      "$input attr-dict `:` qualified(type($input)) `->` qualified(type($result))";
  let hasVerifier = 1;
}

def SimArgumentRefLoadOp : SimOp<"argument_ref.load", [
    MemoryEffects<[
      MemRead<SimStorageResource>, MemRead<SimHeapResource>
    ]>
  ]> {
  let summary = "load through an exact-alias subroutine reference";
  let arguments = (ins SimArgumentRefType:$reference);
  let results = (outs AnyType:$result);
  let assemblyFormat =
      "$reference attr-dict `:` qualified(type($reference)) `->` type($result)";
  let hasVerifier = 1;
}

def SimArgumentRefStoreOp : SimOp<"argument_ref.store", [
    MemoryEffects<[
      MemRead<SimStorageResource>, MemWrite<SimStorageResource>,
      MemRead<SimHeapResource>, MemWrite<SimHeapResource>
    ]>
  ]> {
  let summary = "store through an exact-alias subroutine reference";
  let arguments = (ins AnyType:$value, SimArgumentRefType:$reference);
  let assemblyFormat =
      "$value `to` $reference attr-dict `:` type($value) `,` qualified(type($reference))";
  let hasVerifier = 1;
}

def SimClassDirectCallOp : SimOp<"class.direct_call", [
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
  let summary = "direct instance method call with explicit receiver";
  let arguments = (ins FlatSymbolRefAttr:$callee,
                       SimClassHandleType:$receiver,
                       Variadic<AnyType>:$arguments);
  let results = (outs Variadic<AnyType>:$results);
  let assemblyFormat = [{
    $callee $receiver `(` $arguments `)` attr-dict `:`
    functional-type(operands, results)
  }];
  let hasVerifier = 1;
}

def SimClassVirtualCallOp : SimOp<"class.virtual_call", [
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
  let summary = "descriptor-table class or interface dispatch";
  let arguments = (ins SimClassHandleType:$receiver,
                       FlatSymbolRefAttr:$method,
                       I64Attr:$slot,
                       I64Attr:$signature_id,
                       Variadic<AnyType>:$arguments);
  let results = (outs Variadic<AnyType>:$results);
  let assemblyFormat = [{
    $receiver `[` $method `]` `slot` $slot `signature_id` $signature_id
    `(` $arguments `)` attr-dict `:` functional-type(operands, results)
  }];
  let hasVerifier = 1;
}

def SimWeakCreateOp : SimOp<"weak.create", [
    MemoryEffects<[MemRead<SimHeapResource>, MemWrite<SimHeapResource>]>
  ]> {
  let summary = "create a managed weak_reference wrapper";
  let arguments = (ins SimContextType:$context,
                       SimClassHandleType:$referent);
  let results = (outs SimClassHandleType:$result);
  let assemblyFormat = [{
    $context `,` $referent attr-dict `:`
    type($context) `,` qualified(type($referent))
    `->` qualified(type($result))
  }];
  let hasVerifier = 1;
}

def SimWeakGetOp : SimOp<"weak.get", [
    MemoryEffects<[MemRead<SimHeapResource>]>
  ]> {
  let summary = "promote a reachable weak referent to a strong handle";
  let arguments = (ins SimClassHandleType:$weak);
  let results = (outs SimClassHandleType:$result);
  let assemblyFormat = [{
    $weak attr-dict `:` qualified(type($weak))
    `->` qualified(type($result))
  }];
  let hasVerifier = 1;
}

def SimWeakClearOp : SimOp<"weak.clear", [
    MemoryEffects<[MemWrite<SimHeapResource>]>
  ]> {
  let summary = "clear a weak_reference referent";
  let arguments = (ins SimClassHandleType:$weak);
  let assemblyFormat = "$weak attr-dict `:` qualified(type($weak))";
  let hasVerifier = 1;
}

def SimGCSafepointOp : SimOp<"gc.safepoint", [
    MemoryEffects<[
      MemRead<SimHeapResource>, MemWrite<SimHeapResource>,
      MemRead<SimSchedulerResource>, MemWrite<SimSchedulerResource>
    ]>
  ]> {
  let summary = "precise generated collector safepoint";
  let arguments = (ins SimContextType:$context);
  let assemblyFormat = "$context attr-dict `:` type($context)";
}

// Internal native-lowering marker. The slot is an LLVM stack address whose
// root record is pushed once at function entry; binding updates the precise
// root immediately after a managed SSA value becomes available.
def SimClassRootBindOp : SimOp<"class.root_bind", [
    MemoryEffects<[MemWrite<SimHeapResource>]>
  ]> {
  let summary = "bind a managed SSA handle to a pre-registered root slot";
  let arguments = (ins AnyType:$object, AnyType:$slot, I64Attr:$bit_offset);
  let assemblyFormat =
      "$object `to` $slot `at` $bit_offset attr-dict `:` "
      "qualified(type($object)) `,` type($slot)";
  let hasVerifier = 1;
}

//===----------------------------------------------------------------------===//
// Isolated code units and direct invocation
//===----------------------------------------------------------------------===//

def SimFuncOp : SimOp<"func", [
    AutomaticAllocationScope, FunctionOpInterface, IsolatedFromAbove,
    RecursiveMemoryEffects
  ]> {
  let summary = "isolated multi-block simulation code unit";
  let arguments = (ins SymbolNameAttr:$sym_name,
                       TypeAttrOf<FunctionType>:$function_type,
                       SimEntryKind:$entry_kind,
                       DefaultValuedAttr<SimEventRegion,
                         "::obelisk::sim::EventRegion::Active">:$home_region,
                       DefaultValuedAttr<SimExecutionDomain,
                         "::obelisk::sim::ExecutionDomain::Design">:$domain,
                       OptionalAttr<StrAttr>:$sym_visibility,
                       OptionalAttr<DictArrayAttr>:$arg_attrs,
                       OptionalAttr<DictArrayAttr>:$res_attrs,
                       OptionalAttr<I64Attr>:$code_unit_id,
                       OptionalAttr<SimComputeEffectArrayAttr>:$effect_summary,
                       OptionalAttr<SimFragmentABIAttr>:$fragment_abi,
                       OptionalAttr<UnitAttr>:$internal);
  let regions = (region AnyRegion:$body);
  let builders = [OpBuilder<(ins "::llvm::StringRef":$name,
      "::mlir::FunctionType":$type,
      "::obelisk::sim::EntryKind":$entryKind,
      CArg<"::llvm::ArrayRef<::mlir::NamedAttribute>", "{}">:$attrs,
      CArg<"::llvm::ArrayRef<::mlir::DictionaryAttr>", "{}">:$argAttrs)>];
  let extraClassDeclaration = [{
    ::mlir::Region *getCallableRegion() {
      return isExternal() ? nullptr : &getBody();
    }
    ::llvm::ArrayRef<::mlir::Type> getArgumentTypes() {
      return getFunctionType().getInputs();
    }
    ::llvm::ArrayRef<::mlir::Type> getResultTypes() {
      return getFunctionType().getResults();
    }
    bool isDeclaration() { return isExternal(); }
  }];
  let assemblyFormat = ?;
  let hasCustomAssemblyFormat = 1;
  let hasVerifier = 1;
}

def SimReturnOp
    : SimOp<"return", [Pure, ReturnLike, Terminator, HasParent<"SimFuncOp">]> {
  let summary = "return from a simulation code unit";
  let arguments = (ins Variadic<AnyType> : $operands);
  let assemblyFormat = "($operands^ `:` type($operands))? attr-dict";
  let hasVerifier = 1;
}

def SimCallOp : SimOp<"call", [
  CallOpInterface, DeclareOpInterfaceMethods<SymbolUserOpInterface>,
  MemoryEffects<[
    MemRead<SimStorageResource>, MemWrite<SimStorageResource>,
    MemRead<SimNetResource>, MemWrite<SimNetResource>,
    MemRead<SimSchedulerResource>, MemWrite<SimSchedulerResource>,
    MemRead<SimProcessResource>, MemWrite<SimProcessResource>,
    MemRead<SimHeapResource>, MemWrite<SimHeapResource>, MemRead<SimIOResource>,
    MemWrite<SimIOResource>, MemRead<SimRNGResource>, MemWrite<SimRNGResource>,
    MemRead<SimExternalResource>, MemWrite<SimExternalResource>
  ]>
]> {
  let summary = "direct call to a sibling zero-time simulation function";
  let arguments = (ins FlatSymbolRefAttr:$callee, Variadic<AnyType>:$operands,
                       OptionalAttr<DictArrayAttr>:$arg_attrs,
                       OptionalAttr<DictArrayAttr>:$res_attrs);
  let results = (outs Variadic<AnyType> : $results);
  let assemblyFormat = [{$callee `(` $operands `)` attr-dict
    `:` functional-type($operands, $results)}];
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

def SimTaskCallOp : SimOp<"task.call", [
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

def SimDPICallOp : SimOp<"dpi.call", [
  DeclareOpInterfaceMethods<MemoryEffectsOpInterface>
]> {
  let summary = "shared zero-time DPI-C import boundary";
  let description = [{
    Carries stable call-site metadata and normalized value operands. Results
    contain a function return first (when present), followed by output and
    inout copy-outs. Imported task C status is not an SV result.
  }];
  let arguments = (ins
    I32Attr:$import_id,
    StrAttr:$c_identifier,
    I64Attr:$scope_id,
    SimDPIABIArrayAttr:$abi_signature,
    BoolAttr:$is_pure,
    BoolAttr:$is_context,
    BoolAttr:$is_task,
    StrAttr:$source_file,
    I32Attr:$source_line,
    I32Attr:$source_column,
    AnyType:$runtime_context,
    Variadic<AnyType>:$arguments
  );
  // Data results are followed by one internal runtime status. Keeping the
  // status in this operation makes failure propagation explicit without
  // exposing a task's C int status as a SystemVerilog value.
  let results = (outs Variadic<AnyType>:$results);
  let assemblyFormat = [{
    $c_identifier `id` $import_id `scope` $scope_id
    `context` $runtime_context `:` type($runtime_context)
    `(` $arguments `)` attr-dict `:`
    functional-type($arguments, $results)
  }];
  let hasVerifier = 1;
}

//===----------------------------------------------------------------------===//
// Formatting and file I/O
//===----------------------------------------------------------------------===//

def SimBytesConstantOp : SimOp<"bytes.constant", [Pure, ConstantLike]> {
  let summary = "immutable literal bytes preserved from source";
  let arguments = (ins StrAttr:$value);
  let results = (outs SimBytesType:$result);
  let assemblyFormat = "$value attr-dict";
  let hasFolder = 1;
}

def SimContextRuntimeOp : SimOp<"context.runtime", [Pure]> {
  let summary = "project a simulation context to its runtime context";
  // The native converter may already have changed the physical context type
  // when this projection is introduced in the same conversion transaction.
  let arguments = (ins AnyType:$context);
  let results = (outs RTContextType:$result);
  let assemblyFormat = "$context attr-dict `:` type($context)";
}

def SimStatusCheckOp : SimOp<"status.check", [
  MemoryEffects<[MemWrite<SimProcessResource>]>
]> {
  let summary = "propagate an unexpected runtime failure from a fragment";
  let description = [{
    Native fragment lowering consumes this operation and routes a non-OK
    status through the fragment ABI. Expected SystemVerilog sentinel results
    are selected before this hook and do not use it.
  }];
  let arguments = (ins RTStatusType:$status);
  let assemblyFormat = "$status attr-dict";
}

def SimFinishOp : SimOp<"finish", [
  MemoryEffects<[MemWrite<SimSchedulerResource>]>
]> {
  let summary = "request orderly design-wide simulation termination";
  let description = [{
    Records a design-wide finish request with the runtime scheduler. The
    current activation must return after this operation; the scheduler skips
    remaining ordinary work and enters the final-process phase.
  }];
  let arguments = (ins SimContextType:$context, I32:$verbosity);
  let assemblyFormat = "$context `,` $verbosity attr-dict";
}

def SimStopOp : SimOp<"stop", [
  MemoryEffects<[MemWrite<SimSchedulerResource>]>
]> {
  let summary = "request batch-mode simulation suspension";
  let description = [{
    Obelisk has no interactive debugger session, so executable batch targets
    treat this request as orderly successful termination. The distinct
    operation is preserved through the runtime ABI and bytecode intrinsic so
    interactive hosts can give it different policy in the future.
  }];
  let arguments = (ins SimContextType:$context, I32:$verbosity);
  let assemblyFormat = "$context `,` $verbosity attr-dict";
}

def SimFatalOp : SimOp<"fatal", [
  MemoryEffects<[MemWrite<SimSchedulerResource>]>
]> {
  let summary = "request orderly unsuccessful simulation termination";
  let arguments = (ins SimContextType:$context, I32:$verbosity);
  let assemblyFormat = "$context `,` $verbosity attr-dict";
}

def SimTerminationRequestedOp : SimOp<"termination.requested", [
  MemoryEffects<[MemRead<SimSchedulerResource>]>
]> {
  let summary = "test whether design-wide termination has been requested";
  let arguments = (ins SimContextType:$context);
  let results = (outs I1:$result);
  let assemblyFormat = "$context attr-dict";
}

def SimDisplayOp : SimOp<"display", [
  MemoryEffects<[MemRead<SimHeapResource>, MemWrite<SimIOResource>]>
]> {
  let summary = "formatted display or write to a SystemVerilog descriptor";
  let description = [{
    `item_flags` describes the logical argument sequence. Bit 0 marks a
    nonempty packed operand signed; bit 1 represents an omitted argument and
    consumes no entry from `items`; bit 2 marks an f64 real operand. `scope`
    and `library_cell` carry the elaborated call-site information used by `%m`
    and `%l`. `time_multiplier` converts a numeric `%t` argument from the
    invoking scope's time unit to the design precision used by the runtime
    clock.
  }];
  let arguments = (ins SimContextType:$context, I32:$descriptor,
                       Variadic<AnyType>:$items, BoolAttr:$append_newline,
                       I32Attr:$default_radix,
                       DenseI32ArrayAttr:$item_flags,
                       OptionalAttr<StrAttr>:$scope,
                       OptionalAttr<StrAttr>:$library_cell,
                       OptionalAttr<I64Attr>:$time_multiplier,
                       OptionalAttr<I32Attr>:$time_precision);
  let assemblyFormat = [{$context `to` $descriptor `(` $items `)`
    `newline` `=` $append_newline `radix` `=` $default_radix
    `flags` `=` $item_flags attr-dict
    `:` type($items)}];
  let hasVerifier = 1;
}

def SimTimeFormatOp : SimOp<"time.format", [
  MemoryEffects<[MemWrite<SimIOResource>]>
]> {
  let summary = "install the design-wide %t display format";
  let description = [{
    `units` is the decimal exponent in seconds of the unit `%t` reports in.
    `width` is a minimum field width covering the rendered suffix.
  }];
  let arguments = (ins SimContextType:$context, I32:$units,
                       I32:$fraction_digits, SimBytesType:$suffix,
                       I32:$width);
}

class SimFileOp<string mnemonic, dag fileArgs, dag fileResults,
                list<MemoryEffect> effects = [
                  MemRead<SimIOResource>, MemWrite<SimIOResource>
                ]>
    : SimOp<mnemonic, [MemoryEffects<effects>]> {
  let arguments = fileArgs;
  let results = fileResults;
}

def SimFileOpenMCDOp : SimFileOp<"file.open_mcd",
    (ins SimContextType:$context, SimBytesType:$path),
    (outs I32:$descriptor)>;

def SimFileOpenOp : SimFileOp<"file.open",
    (ins SimContextType:$context, SimBytesType:$path, SimBytesType:$mode),
    (outs I32:$descriptor)>;

def SimFileOpenStringMCDOp : SimFileOp<"file.open_string_mcd",
    (ins SimContextType:$context, SimStringType:$path),
    (outs I32:$descriptor),
    [MemRead<SimIOResource>, MemWrite<SimIOResource>,
     MemRead<SimHeapResource>]>;

def SimFileOpenStringOp : SimFileOp<"file.open_string",
    (ins SimContextType:$context, SimStringType:$path, SimStringType:$mode),
    (outs I32:$descriptor),
    [MemRead<SimIOResource>, MemWrite<SimIOResource>,
     MemRead<SimHeapResource>]>;

def SimFileCloseOp : SimFileOp<"file.close",
    (ins SimContextType:$context, I32:$descriptor), (outs)>;

def SimFileFlushOp : SimFileOp<"file.flush",
    (ins SimContextType:$context, I32:$descriptor), (outs)>;

def SimFileGetcOp : SimFileOp<"file.getc",
    (ins SimContextType:$context, I32:$descriptor), (outs I32:$result)>;

def SimFileUngetcOp : SimFileOp<"file.ungetc",
    (ins SimContextType:$context, I32:$byte, I32:$descriptor),
    (outs I32:$result)>;

def SimFileGetlineOp : SimFileOp<"file.getline",
    (ins SimContextType:$context, I32:$descriptor),
    (outs AnyInteger:$data, I32:$count)> {
  let summary = "read one bounded line for assignment to a packed value";
  let hasVerifier = 1;
}

def SimFileGetlineStringOp : SimFileOp<"file.getline_string",
    (ins SimContextType:$context, I32:$descriptor),
    (outs SimStringType:$data, I32:$count),
    [MemRead<SimIOResource>, MemWrite<SimIOResource>,
     MemRead<SimHeapResource>, MemWrite<SimHeapResource>]> {
  let summary = "read one complete line into a managed string";
}

def SimPlusargTestOp : SimFileOp<"plusarg.test",
    (ins SimContextType:$context, SimStringType:$name),
    (outs I32:$found),
    [MemRead<SimIOResource>, MemRead<SimHeapResource>]> {
  let summary = "report whether a command-line plusarg starts with a prefix";
}

def SimPlusargValueOp : SimFileOp<"plusarg.value",
    (ins SimContextType:$context, SimStringType:$prefix),
    (outs SimStringType:$tail, I32:$found),
    [MemRead<SimIOResource>, MemRead<SimHeapResource>,
     MemWrite<SimHeapResource>]> {
  let summary = "return the text following a matched command-line plusarg";
}

def SimFileErrorStringOp : SimFileOp<"file.error_string",
    (ins SimContextType:$context, I32:$descriptor),
    (outs SimStringType:$message, I32:$code),
    [MemRead<SimIOResource>, MemRead<SimHeapResource>,
     MemWrite<SimHeapResource>]> {
  let summary = "read a descriptor's pending error code and message";
}

def SimFileReadPackedOp : SimFileOp<"file.read_packed",
    (ins SimContextType:$context, I32:$descriptor),
    (outs AnyInteger:$data, I32:$count)> {
  let summary = "read bytes into an integral variable in big-endian order";
  let hasVerifier = 1;
}

def SimFileEofOp : SimFileOp<"file.eof",
    (ins SimContextType:$context, I32:$descriptor), (outs I32:$result),
    [MemRead<SimIOResource>]>;

def SimFileSeekOp : SimFileOp<"file.seek",
    (ins SimContextType:$context, I32:$descriptor, I64:$offset, I32:$origin),
    (outs I32:$result)>;

def SimFileTellOp : SimFileOp<"file.tell",
    (ins SimContextType:$context, I32:$descriptor), (outs I64:$result),
    [MemRead<SimIOResource>]>;

def SimFileRewindOp : SimFileOp<"file.rewind",
    (ins SimContextType:$context, I32:$descriptor), (outs I32:$result)>;

def SimSpawnOp : SimOp<"spawn", [
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
  let results = (outs SimProcessType:$process);
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

def SimControlEnterOp : SimOp<"control.enter", [
    MemoryEffects<[MemWrite<SimProcessResource>]>
  ]> {
  let summary = "enter one dynamic activation of a resolved named block";
  let arguments = (ins I64Attr:$target_id);
  let results = (outs SimControlType:$control);
  let assemblyFormat = "$target_id attr-dict";
  let hasVerifier = 1;
}

def SimControlLeaveOp : SimOp<"control.leave", [
    MemoryEffects<[MemWrite<SimProcessResource>]>
  ]> {
  let summary = "leave a named block activation normally";
  let arguments = (ins SimControlType:$control);
  let assemblyFormat = "$control attr-dict";
}

def SimControlDisableOp : SimOp<"control.disable", [
    MemoryEffects<[MemWrite<SimSchedulerResource>,
                   MemWrite<SimProcessResource>]>
  ]> {
  let summary = "disable resolved named block activations";
  let description = [{
    An activation operand targets one dynamically enclosing activation.
    Without it, lexical mode resolves the innermost inherited activation while
    hierarchical mode disables all live activations of the exact elaborated
    target ID.
  }];
  let arguments = (ins I64Attr:$target_id,
                       Optional<SimControlType>:$activation,
                       DefaultValuedAttr<BoolAttr, "false">:$hierarchical);
  let assemblyFormat = [{$target_id (`activation` $activation^)?
    attr-dict}];
  let hasVerifier = 1;
}

def SimStaticOnceOp : SimOp<"static.once", [
    MemoryEffects<[MemRead<SimProcessResource>,
                   MemWrite<SimProcessResource>]>
  ]> {
  let summary = "claim one context-wide static initialization site";
  let arguments = (ins I64Attr:$id);
  let results = (outs I1:$first);
  let assemblyFormat = "$id attr-dict";
  let hasVerifier = 1;
}

def SimDeferredOnceOp : SimOp<"assert.deferred_once", [
    MemoryEffects<[MemRead<SimSchedulerResource>,
                   MemWrite<SimSchedulerResource>,
                   MemRead<SimProcessResource>]>
  ]> {
  let summary = "claim one deferred-immediate assertion site in this slot";
  let arguments = (ins I64Attr:$id);
  let results = (outs I1:$first);
  let assemblyFormat = "$id attr-dict";
  let hasVerifier = 1;
}

def SimMonitorRegisterOp : SimOp<"monitor.register", [
    MemoryEffects<[MemWrite<SimSchedulerResource>,
                   MemWrite<SimProcessResource>]>
  ]> {
  let summary = "replace the context-wide SystemVerilog monitor process";
  let arguments = (ins SimProcessType:$process);
  let assemblyFormat = "$process attr-dict";
}

def SimMonitorControlOp : SimOp<"monitor.control", [
    MemoryEffects<[MemWrite<SimSchedulerResource>]>
  ]> {
  let summary = "enable or disable context-wide SystemVerilog monitor output";
  let arguments = (ins BoolAttr:$enabled);
  let assemblyFormat = "$enabled attr-dict";
}

def SimMonitorCurrentOp : SimOp<"monitor.current", [
    MemoryEffects<[MemRead<SimSchedulerResource>]>
  ]> {
  let summary = "test whether the executing process is the current monitor";
  let results = (outs I1:$current);
  let assemblyFormat = "attr-dict";
}

def SimDisableChildrenOp : SimOp<"children.disable", [
    MemoryEffects<[MemWrite<SimSchedulerResource>,
                   MemWrite<SimProcessResource>]>
  ]> {
  let summary = "recursively cancel all current direct-child subtrees";
  let assemblyFormat = "attr-dict";
}

//===----------------------------------------------------------------------===//
// Context descriptors and mutable state
//===----------------------------------------------------------------------===//

def SimContextStorageOp : SimOp<"context.storage", [Pure]> {
  let arguments = (ins SimContextType:$context, I64Attr:$id);
  let results = (outs SimRefType:$result);
  let assemblyFormat = "$context `[` $id `]` attr-dict `:` qualified(type($result))";
  let hasVerifier = 1;
}

def SimContextNetOp : SimOp<"context.net", [Pure]> {
  let arguments = (ins SimContextType:$context, I64Attr:$id);
  let results = (outs SimNetType:$result);
  let assemblyFormat = "$context `[` $id `]` attr-dict `:` qualified(type($result))";
  let hasVerifier = 1;
}

def SimContextDriverOp : SimOp<"context.driver", [Pure]> {
  let arguments = (ins SimContextType:$context, I64Attr:$id);
  let results = (outs SimDriverType:$result);
  let assemblyFormat = "$context `[` $id `]` attr-dict `:` qualified(type($result))";
  let hasVerifier = 1;
}

def SimContextEventOp : SimOp<"context.event", [Pure]> {
  let arguments = (ins SimContextType:$context, I64Attr:$id);
  let results = (outs SimEventType:$result);
  let assemblyFormat = "$context `[` $id `]` attr-dict `:` qualified(type($result))";
  let hasVerifier = 1;
}

//===----------------------------------------------------------------------===//
// First-class fixed aggregate values
//===----------------------------------------------------------------------===//

def SimPackedFlattenOp : SimOp<"packed.flatten", [Pure]> {
  let summary = "view a packed aggregate as its scalar bit representation";
  let arguments = (ins AnyType:$input);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$input attr-dict `:` functional-type(operands, results)";
  let hasVerifier = 1;
  let hasCanonicalizer = 1;
}

def SimPackedUnflattenOp : SimOp<"packed.unflatten", [Pure]> {
  let summary = "form a packed aggregate from its scalar bit representation";
  let arguments = (ins AnyType:$input);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$input attr-dict `:` functional-type(operands, results)";
  let hasVerifier = 1;
  let hasCanonicalizer = 1;
}

def SimAggregateDefaultOp : SimOp<"aggregate.default", [Pure]> {
  let summary = "SystemVerilog default value of a fixed aggregate";
  let description = [{
    Struct and array elements default recursively. Union defaults retain their
    language-level active-member or tag state; in particular, an unpacked
    tagged union has no active member and a four-state packed tag is unknown.
  }];
  let results = (outs AnyType:$result);
  let assemblyFormat = "attr-dict `:` type($result)";
  let hasVerifier = 1;
}

def SimAggregateConstructOp : SimOp<"aggregate.construct", [Pure]> {
  let summary = "construct a fixed struct or array in declaration order";
  let arguments = (ins Variadic<AnyType>:$elements);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$elements attr-dict `:` functional-type($elements, $result)";
  let hasVerifier = 1;
  let hasCanonicalizer = 1;
}

def SimAggregateExtractOp : SimOp<"aggregate.extract", [Pure]> {
  let summary = "extract one static struct or array subelement";
  let arguments = (ins AnyType:$input, I64Attr:$index);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$input `[` $index `]` attr-dict `:` functional-type(operands, results)";
  let hasVerifier = 1;
  let hasCanonicalizer = 1;
}

def SimAggregateInsertOp : SimOp<"aggregate.insert", [Pure]> {
  let summary = "replace one static struct or array subelement";
  let arguments = (ins AnyType:$input, AnyType:$replacement, I64Attr:$index);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$replacement `into` $input `[` $index `]` attr-dict `:` functional-type(operands, results)";
  let hasVerifier = 1;
  let hasCanonicalizer = 1;
}

def SimArrayDynExtractOp : SimOp<"array.extract_dynamic", [Pure]> {
  let summary = "extract from a fixed array using a declared-range index";
  let description = [{
    The index is interpreted in the array's preserved source range. Unknown or
    out-of-range indices produce the element type's recursive default.
  }];
  let arguments = (ins AnyType:$input, AnyType:$index);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$input `[` $index `]` attr-dict `:` functional-type(operands, results)";
  let hasVerifier = 1;
  let hasCanonicalizer = 1;
}

def SimUnionConstructOp : SimOp<"union.construct", [Pure]> {
  let summary = "construct a union with one selected field";
  let arguments = (ins AnyType:$value, I64Attr:$index);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$value `as` $index attr-dict `:` functional-type(operands, results)";
  let hasVerifier = 1;
  let hasCanonicalizer = 1;
}

def SimUnionExtractOp : SimOp<"union.extract", [Pure]> {
  let summary = "extract one statically selected union field";
  let arguments = (ins AnyType:$input, I64Attr:$index);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$input `[` $index `]` attr-dict `:` functional-type(operands, results)";
  let hasVerifier = 1;
  let hasCanonicalizer = 1;
}

def SimUnionIsActiveOp : SimOp<"union.is_active", [Pure]> {
  let summary = "test the active member of a tagged union";
  let description = [{
    Returns false when the requested member is inactive or when the encoded
    tag is unknown or invalid.
  }];
  let arguments = (ins AnyType:$input, I64Attr:$index);
  let results = (outs I1:$result);
  let assemblyFormat = "$input `[` $index `]` attr-dict `:` type($input)";
  let hasVerifier = 1;
  let hasFolder = 1;
}

def SimRefAllocOp : SimOp<"ref.alloc", [
    DeclareOpInterfaceMethods<PromotableAllocationOpInterface>,
    DeclareOpInterfaceMethods<DestructurableAllocationOpInterface>
  ]> {
  let summary = "promotable automatic local allocation";
  let arguments = (ins AnyType:$initial_value);
  let results = (outs Res<SimRefType, "", [MemAlloc<SimStorageResource>]>:$result);
  let assemblyFormat = "$initial_value attr-dict `:` type($initial_value) `->` qualified(type($result))";
  let hasVerifier = 1;
}

def SimRefReleaseOwnerOp : SimOp<"ref.release_owner"> {
  let summary = "end an automatic reference ownership lifetime";
  let description = [{
    Marks a control-flow point where the owner reference created by
    `obelisk_sim.ref.alloc` is no longer live. This internal operation is
    inserted by lifetime analysis and consumed by native lowering.
  }];
  let arguments =
      (ins Arg<SimRefType, "", [MemFree<SimStorageResource>]>:$reference);
  let assemblyFormat =
      "$reference attr-dict `:` qualified(type($reference))";
}

def SimRefLoadOp : SimOp<"ref.load", [
    DeclareOpInterfaceMethods<PromotableMemOpInterface>,
    DeclareOpInterfaceMethods<DestructurableAccessorOpInterface>,
    DeclareOpInterfaceMethods<SafeMemorySlotAccessOpInterface>,
    SimRefElementMatches<"reference", "result">
  ]> {
  let arguments = (ins Arg<SimRefType, "", [MemRead<SimStorageResource>]>:$reference);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$reference attr-dict `:` qualified(type($reference)) `->` type($result)";
}

def SimRefStoreOp : SimOp<"ref.store", [
    DeclareOpInterfaceMethods<PromotableMemOpInterface>,
    DeclareOpInterfaceMethods<DestructurableAccessorOpInterface>,
    DeclareOpInterfaceMethods<SafeMemorySlotAccessOpInterface>,
    SimRefElementMatches<"reference", "value">
  ]> {
  let arguments = (ins AnyType:$value,
                       Arg<SimRefType, "", [MemWrite<SimStorageResource>]>:$reference);
  let assemblyFormat = "$value `to` $reference attr-dict `:` type($value) `,` qualified(type($reference))";
}

def SimOverrideOp : SimOp<"override", [
    MemoryEffects<[MemWrite<SimSchedulerResource>]>
  ]> {
  let summary = "publish a whole static packed force or procedural assign";
  let arguments = (ins
    Arg<AnyType, "", [
      MemWrite<SimStorageResource>, MemWrite<SimNetResource>
    ]>:$target,
    AnyType:$value, BoolAttr:$is_assign);
  let assemblyFormat = "$target `=` $value `assign` $is_assign attr-dict `:` type($target) `,` type($value)";
  let hasVerifier = 1;
}

def SimReleaseOverrideOp : SimOp<"release_override", [
    MemoryEffects<[MemWrite<SimSchedulerResource>]>
  ]> {
  let summary = "release a whole static force or procedural assign";
  let arguments = (ins
    Arg<AnyType, "", [
      MemWrite<SimStorageResource>, MemWrite<SimNetResource>
    ]>:$target,
    BoolAttr:$is_assign);
  let assemblyFormat = "$target `assign` $is_assign attr-dict `:` type($target)";
  let hasVerifier = 1;
}

def SimNetExtractOp : SimOp<"net.extract", [Pure, ViewLikeOpInterface]> {
  let summary = "constant packed view into a built-in net";
  let arguments = (ins SimNetType:$input, I64Attr:$low_bit);
  let results = (outs SimNetType:$result);
  let assemblyFormat = "$input `from` $low_bit attr-dict `:` qualified(type($input)) `->` qualified(type($result))";
  let extraClassDeclaration = [{
    ::mlir::Value getViewSource() { return getInput(); }
  }];
  let hasVerifier = 1;
  let hasCanonicalizer = 1;
}

def SimRefExtractOp : SimOp<"ref.extract", [
    Pure, ViewLikeOpInterface,
    DeclareOpInterfaceMethods<SafeMemorySlotAccessOpInterface>
  ]> {
  let arguments = (ins SimRefType:$input, I64Attr:$low_bit);
  let results = (outs SimRefType:$result);
  let assemblyFormat = "$input `from` $low_bit attr-dict `:` qualified(type($input)) `->` qualified(type($result))";
  let extraClassDeclaration = [{
    ::mlir::Value getViewSource() { return getInput(); }
  }];
  let hasVerifier = 1;
  let hasCanonicalizer = 1;
}

def SimRefDynExtractOp : SimOp<"ref.dyn_extract", [
    Pure, ViewLikeOpInterface,
    DeclareOpInterfaceMethods<SafeMemorySlotAccessOpInterface>
  ]> {
  let summary = "dynamically select a packed storage slice";
  let description = [{
    The low-bit index may be a signless builtin integer or four-state logic.
    An unknown index denotes no storage, so stores through the result have no
    effect and loads produce X or zero according to the element domain.
    Out-of-range bits likewise ignore writes and use the domain fallback on
    loads, while in-range bits retain their normal storage effects.
  }];
  let arguments = (ins SimRefType:$input, AnyType:$low_bit);
  let results = (outs SimRefType:$result);
  let assemblyFormat = "$input `from` $low_bit attr-dict `:` functional-type(operands, results)";
  let extraClassDeclaration = [{
    ::mlir::Value getViewSource() { return getInput(); }
  }];
  let hasVerifier = 1;
  let hasCanonicalizer = 1;
}

def SimRefSubelementOp : SimOp<"ref.subelement", [
    Pure, ViewLikeOpInterface,
    DeclareOpInterfaceMethods<DestructurableAccessorOpInterface>,
    DeclareOpInterfaceMethods<SafeMemorySlotAccessOpInterface>
  ]> {
  let summary = "static declaration-order view into aggregate storage";
  let arguments = (ins SimRefType:$input, DenseI64ArrayAttr:$indices);
  let results = (outs SimRefType:$result);
  let assemblyFormat = "$input `[` $indices `]` attr-dict `:` qualified(type($input)) `->` qualified(type($result))";
  let extraClassDeclaration = [{
    ::mlir::Value getViewSource() { return getInput(); }
  }];
  let hasVerifier = 1;
  let hasCanonicalizer = 1;
}

def SimRefArrayElementOp : SimOp<"ref.array_element", [
    Pure, ViewLikeOpInterface,
    DeclareOpInterfaceMethods<SafeMemorySlotAccessOpInterface>
  ]> {
  let summary = "dynamic declared-range view into fixed array storage";
  let description = [{
    An unknown or out-of-range source index denotes no storage. Loads through
    the result return the recursive element default and stores have no effect.
  }];
  let arguments = (ins SimRefType:$input, AnyType:$index);
  let results = (outs SimRefType:$result);
  let assemblyFormat = "$input `[` $index `]` attr-dict `:` functional-type(operands, results)";
  let extraClassDeclaration = [{
    ::mlir::Value getViewSource() { return getInput(); }
  }];
  let hasVerifier = 1;
  let hasCanonicalizer = 1;
}

def SimNetReadOp : SimOp<"net.read", [
    MemoryEffects<[MemRead<SimNetResource>]>,
    SimNetElementMatches<"net", "result">
  ]> {
  let arguments = (ins SimNetType:$net);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$net attr-dict `:` qualified(type($net)) `->` type($result)";
}

def SimDriverDriveOp : SimOp<"driver.drive", [
    MemoryEffects<[MemWrite<SimNetResource>]>,
    SimDriverElementMatches<"driver", "value">
  ]> {
  let arguments = (ins SimDriverType:$driver, AnyType:$value);
  let assemblyFormat = "$driver `=` $value attr-dict `:` qualified(type($driver)) `,` type($value)";
}

def SimDriverDriveChangedOp : SimOp<"driver.drive_changed", [
    MemoryEffects<[MemWrite<SimNetResource>]>,
    SimDriverElementMatches<"driver", "value">
  ]> {
  let summary = "drive a net and return whether its resolved value changed";
  let description = [{
    This internal operation is the transition-producing form used by generated
    graph-region kernels. The result is true when any value or unknown-plane
    bit in the affected resolved net changes. Ordinary process bodies retain
    `driver.drive`; bytecode fallback bodies do not depend on this result.
  }];
  let arguments = (ins SimDriverType:$driver, AnyType:$value);
  let results = (outs I1:$changed);
  let assemblyFormat = "$driver `=` $value attr-dict `:` qualified(type($driver)) `,` type($value)";
}

def SimDriverExtractOp : SimOp<"driver.extract", [Pure, ViewLikeOpInterface]> {
  let arguments = (ins SimDriverType:$input, I64Attr:$low_bit);
  let results = (outs SimDriverType:$result);
  let assemblyFormat = "$input `from` $low_bit attr-dict `:` qualified(type($input)) `->` qualified(type($result))";
  let extraClassDeclaration = [{
    ::mlir::Value getViewSource() { return getInput(); }
  }];
  let hasVerifier = 1;
  let hasCanonicalizer = 1;
}

def SimDriverDynExtractOp : SimOp<"driver.dyn_extract", [Pure, ViewLikeOpInterface]> {
  let summary = "dynamically select a packed driver slice";
  let description = [{
    The low-bit index may be a signless builtin integer or four-state logic.
    An unknown index suppresses the drive. Out-of-range bits are ignored while
    in-range bits retain their normal net-driving effects.
  }];
  let arguments = (ins SimDriverType:$input, AnyType:$low_bit);
  let results = (outs SimDriverType:$result);
  let assemblyFormat = "$input `from` $low_bit attr-dict `:` functional-type(operands, results)";
  let extraClassDeclaration = [{
    ::mlir::Value getViewSource() { return getInput(); }
  }];
  let hasVerifier = 1;
  let hasCanonicalizer = 1;
}

def SimDriverSubelementOp : SimOp<"driver.subelement", [Pure, ViewLikeOpInterface]> {
  let summary = "static declaration-order view into an aggregate driver";
  let arguments = (ins SimDriverType:$input, DenseI64ArrayAttr:$indices);
  let results = (outs SimDriverType:$result);
  let assemblyFormat = "$input `[` $indices `]` attr-dict `:` qualified(type($input)) `->` qualified(type($result))";
  let extraClassDeclaration = [{
    ::mlir::Value getViewSource() { return getInput(); }
  }];
  let hasVerifier = 1;
  let hasCanonicalizer = 1;
}

def SimDriverArrayElementOp : SimOp<"driver.array_element", [Pure, ViewLikeOpInterface]> {
  let summary = "dynamic declared-range view into a fixed-array driver";
  let arguments = (ins SimDriverType:$input, AnyType:$index);
  let results = (outs SimDriverType:$result);
  let assemblyFormat = "$input `[` $index `]` attr-dict `:` functional-type(operands, results)";
  let extraClassDeclaration = [{
    ::mlir::Value getViewSource() { return getInput(); }
  }];
  let hasVerifier = 1;
  let hasCanonicalizer = 1;
}

def SimNBAEnqueueOp : SimOp<"nba.enqueue", [
    MemoryEffects<[MemWrite<SimSchedulerResource>]>,
    SimRefElementMatches<"destination", "value">
  ]> {
  let arguments = (ins AnyType:$value, SimRefType:$destination,
                       Optional<SimTimeType>:$delay,
                       OptionalAttr<SimNBASiteAttr>:$site);
  let assemblyFormat = [{$value `to` $destination (`after` $delay^)? attr-dict
    `:` functional-type(operands, results)}];
}

//===----------------------------------------------------------------------===//
// Packed values and time
//===----------------------------------------------------------------------===//

def SimLogicConstantOp : SimOp<"logic.constant", [Pure, ConstantLike]> {
  let summary = "constant four-state value given as a value and unknown plane";
  let description = [{
    Folds to a two-element `ArrayAttr` holding the value and unknown planes,
    which the dialect constant materializer turns back into this operation.
  }];
  let arguments = (ins APIntAttr:$value, APIntAttr:$unknown);
  let results = (outs SimLogicType:$result);
  let assemblyFormat = "$value `,` $unknown attr-dict `:` qualified(type($result))";
  let hasVerifier = 1;
  let hasFolder = 1;
}

def SimLogicFromBitsOp : SimOp<"logic.from_bits", [Pure]> {
  let arguments = (ins AnyInteger:$input);
  let results = (outs SimLogicType:$result);
  let assemblyFormat = "$input attr-dict `:` type($input) `->` qualified(type($result))";
  let hasVerifier = 1;
  let hasFolder = 1;
}

def SimLogicToBitsOp : SimOp<"logic.to_bits", [Pure]> {
  let summary = "convert four-state logic to two-state bits";
  let description = [{
    Performs the SystemVerilog four-state-to-two-state conversion. Every known
    bit is preserved and every X or Z bit becomes zero.
  }];
  let arguments = (ins SimLogicType:$input);
  let results = (outs AnyInteger:$result);
  let assemblyFormat = "$input attr-dict `:` qualified(type($input)) `->` type($result)";
  let hasVerifier = 1;
  let hasFolder = 1;
}

def SimLogicIsTrueOp : SimOp<"logic.is_true", [Pure]> {
  let summary = "evaluate four-state logic as a control-flow predicate";
  let description = [{
    Returns true exactly when at least one input bit is a known one. Zero and
    values containing only X and Z bits are false, matching SystemVerilog
    conditional-statement truth evaluation.
  }];
  let arguments = (ins SimLogicType:$input);
  let results = (outs I1:$result);
  let assemblyFormat = "$input attr-dict `:` qualified(type($input))";
  let hasFolder = 1;
}

def SimLogicMuxOp : SimOp<"logic.mux", [
    Pure, AllTypesMatch<["true_value", "false_value", "result"]>
  ]> {
  let summary = "conditional-expression selection with exact X/Z merging";
  let description = [{
    A known condition selects one operand. For an X or Z condition, each
    result symbol is preserved when both operands contain the same 0, 1, X,
    or Z symbol; every mismatch becomes X.
  }];
  let arguments = (ins SimLogic1Type:$condition, SimLogicType:$true_value,
                       SimLogicType:$false_value);
  let results = (outs SimLogicType:$result);
  let assemblyFormat = [{
    $condition `?` $true_value `:` $false_value attr-dict `:`
    functional-type(operands, results)
  }];
  let hasFolder = 1;
}

def SimLogicCountBitsOp : SimOp<"logic.count_bits", [Pure]> {
  let summary = "count selected four-state symbols in a fixed bitstream";
  let description = [{
    Each one-bit control selects exactly one of 0, 1, X, or Z using the
    dialect's value/unknown encoding. Duplicate controls select that state only
    once. The result is the count modulo 2^32, matching SystemVerilog `int`.
  }];
  let arguments = (ins AnyType:$input,
                       Variadic<SimLogic1Type>:$controls);
  let results = (outs I32:$result);
  let assemblyFormat =
      "$input `matching` $controls attr-dict `:` functional-type(operands, results)";
  let hasVerifier = 1;
  let hasFolder = 1;
}

def SimLogicClog2Op : SimOp<"logic.clog2", [Pure]> {
  let summary = "SystemVerilog ceiling base-two logarithm";
  let description = [{
    Converts X and Z bits to zero, then returns the minimum number of bits
    needed to represent the input minus one. Zero and one return zero. Signed
    inputs are intentionally interpreted by bit pattern, as required by
    SystemVerilog `$clog2`.
  }];
  let arguments = (ins SimLogicType:$input);
  let results = (outs I32:$result);
  let assemblyFormat = "$input attr-dict `:` qualified(type($input))";
  let hasFolder = 1;
}

def SimLogicResizeOp : SimOp<"logic.resize", [Pure]> {
  let summary = "widen or narrow a four-state value";
  let arguments = (ins SimLogicType:$input, BoolAttr:$is_signed);
  let results = (outs SimLogicType:$result);
  let assemblyFormat = "$input `signed` `=` $is_signed attr-dict `:` qualified(type($input)) `->` qualified(type($result))";
  let hasFolder = 1;
  let hasCanonicalizer = 1;
}

def SimLogicUnaryOp : SimOp<"logic.unary", [Pure]> {
  let arguments = (ins SimUnaryKind:$kind, SimLogicType:$input);
  let results = (outs SimLogicType:$result);
  let assemblyFormat =
      "$kind $input attr-dict `:` functional-type(operands, results)";
  let hasVerifier = 1;
  let hasFolder = 1;
}

def SimLogicReductionOp : SimOp<"logic.reduction", [Pure]> {
  let summary = "four-state packed reduction";
  let arguments = (ins SimReductionKind:$kind, SimLogicType:$input);
  let results = (outs SimLogic1Type:$result);
  let assemblyFormat = [{$kind $input attr-dict `:` qualified(type($input))
    `->` qualified(type($result))}];
  let hasFolder = 1;
}

def SimLogicBinaryOp
    : SimOp<"logic.binary", [Pure, SameOperandsAndResultType]> {
  let arguments = (ins SimBinaryKind:$kind, SimLogicType:$lhs,
                       SimLogicType:$rhs);
  let results = (outs SimLogicType : $result);
  let assemblyFormat =
      "$kind $lhs `,` $rhs attr-dict `:` qualified(type($result))";
  let hasFolder = 1;
  let hasCanonicalizer = 1;
}

def SimLogicLogicalOp : SimOp<"logic.logical", [Pure, Commutative]> {
  let summary = "four-state logical conjunction or disjunction";
  let arguments = (ins SimLogicalKind
                   : $kind, SimLogicType
                   : $lhs, SimLogicType
                   : $rhs);
  let results = (outs SimLogic1Type : $result);
  let assemblyFormat =
      "$kind $lhs `,` $rhs attr-dict `:` functional-type(operands, results)";
  let hasFolder = 1;
}

def SimLogicShiftOp
    : SimOp<"logic.shift", [Pure, AllTypesMatch<["input", "result"]>]> {
  let summary = "four-state shift with an independently typed amount";
  let arguments = (ins SimShiftKind
                   : $kind, SimLogicType
                   : $input, AnyType
                   : $amount);
  let results = (outs SimLogicType : $result);
  let assemblyFormat = "$kind $input `by` $amount attr-dict `:` "
                       "functional-type(operands, results)";
  let hasVerifier = 1;
  let hasFolder = 1;
}

def SimLogicCompareOp
    : SimOp<"logic.compare", [Pure, SameTypeOperands]> {
  let arguments = (ins SimCompareKind
                   : $kind, SimLogicType
                   : $lhs, SimLogicType
                   : $rhs);
  let results = (outs AnyType : $result);
  let assemblyFormat =
      "$kind $lhs `,` $rhs attr-dict `:` functional-type(operands, results)";
  let hasVerifier = 1;
  let hasFolder = 1;
  let hasCanonicalizer = 1;
}

def SimLogicConcatOp : SimOp<"logic.concat", [Pure]> {
  let arguments = (ins Variadic<SimLogicType> : $inputs);
  let results = (outs SimLogicType : $result);
  let assemblyFormat =
      "$inputs attr-dict `:` functional-type($inputs, $result)";
  let hasVerifier = 1;
  let hasFolder = 1;
  let hasCanonicalizer = 1;
}

def SimLogicReplicateOp : SimOp<"logic.replicate", [Pure]> {
  let arguments = (ins SimLogicType : $input, I64Attr : $count);
  let results = (outs SimLogicType : $result);
  let assemblyFormat = "$input `times` $count attr-dict `:` "
                       "qualified(type($input)) `->` qualified(type($result))";
  let hasVerifier = 1;
  let hasFolder = 1;
  let hasCanonicalizer = 1;
}

def SimLogicExtractOp : SimOp<"logic.extract", [Pure]> {
  let arguments = (ins SimLogicType : $input, I64Attr : $low_bit);
  let results = (outs SimLogicType : $result);
  let assemblyFormat = "$input `from` $low_bit attr-dict `:` "
                       "qualified(type($input)) `->` qualified(type($result))";
  let hasVerifier = 1;
  let hasFolder = 1;
  let hasCanonicalizer = 1;
}

def SimLogicDynExtractOp : SimOp<"logic.dyn_extract", [Pure]> {
  let summary = "dynamically select a four-state packed slice";
  let description = [{
    The low-bit index may be a signless builtin integer or four-state logic and
    its bit pattern is interpreted as a signed two's-complement value. Producers
    must therefore zero-extend unsigned source indices before normalization so
    that a positive index cannot be mistaken for a negative one. An unknown
    index produces all X. For a partially out-of-range selection, in-range bits
    are preserved and out-of-range bits are X.
  }];
  let arguments = (ins SimLogicType : $input, AnyType : $low_bit);
  let results = (outs SimLogicType : $result);
  let assemblyFormat =
      "$input `from` $low_bit attr-dict `:` functional-type(operands, results)";
  let hasVerifier = 1;
  let hasFolder = 1;
  let hasCanonicalizer = 1;
}

def SimBitsDynExtractOp : SimOp<"bits.dyn_extract", [Pure]> {
  let summary = "dynamically select a two-state packed slice";
  let description = [{
    The low-bit index may be a signless builtin integer or four-state logic and
    its bit pattern is interpreted as a signed two's-complement value. Producers
    must therefore zero-extend unsigned source indices before normalization so
    that a positive index cannot be mistaken for a negative one. An unknown
    index produces zero. For a partially out-of-range selection, in-range bits
    are preserved and out-of-range bits are zero. This operation defines bounds
    behavior without relying on poison-producing integer shifts.
  }];
  let arguments = (ins AnyInteger:$input, AnyType:$low_bit);
  let results = (outs AnyInteger:$result);
  let assemblyFormat =
      "$input `from` $low_bit attr-dict `:` functional-type(operands, results)";
  let hasVerifier = 1;
  let hasFolder = 1;
}

def SimLogicInsertOp
    : SimOp<"logic.insert", [Pure, AllTypesMatch<["input", "result"]>]> {
  let arguments = (ins SimLogicType
                   : $input, SimLogicType
                   : $replacement, I64Attr
                   : $low_bit);
  let results = (outs SimLogicType : $result);
  let assemblyFormat = "$replacement `into` $input `at` $low_bit attr-dict `:` "
                       "functional-type(operands, results)";
  let hasVerifier = 1;
  let hasFolder = 1;
  let hasCanonicalizer = 1;
}

def SimTimeConstantOp : SimOp<"time.constant", [Pure, ConstantLike]> {
  let summary = "nonnegative time measured in design-precision ticks";
  let arguments = (ins I64Attr : $value);
  let results = (outs SimTimeType : $result);
  let assemblyFormat = "$value attr-dict";
  let hasVerifier = 1;
  let hasFolder = 1;
}

def SimTimeNowOp : SimOp<"time.now", [
  MemoryEffects<[MemRead<SimSchedulerResource>]>
]> {
  let summary = "read the scheduler clock in design-precision ticks";
  let arguments = (ins SimContextType:$context);
  let results = (outs I64:$result);
  let assemblyFormat = "$context attr-dict";
}

def SimTimeToRealOp : SimOp<"time.to_real", [Pure]> {
  let summary = "convert design-precision ticks to a realtime value";
  let arguments = (ins I64:$input, I64Attr:$scale);
  let results = (outs F64:$result);
  let assemblyFormat = "$input `by` $scale attr-dict";
  let hasVerifier = 1;
}

def SimTimeFromRealOp : SimOp<"time.from_real", [Pure]> {
  let summary = "convert a realtime delay to design-precision ticks";
  let description = [{
    Rounds a nonnegative delay to the nearest source-precision quantum, with
    ties rounded upward, and then expresses it in design-precision ticks. NaN
    and negative inputs produce zero. Positive infinity and finite overflow
    saturate to the largest quantum-aligned value representable in 64 bits.
  }];
  let arguments = (ins F64:$input, I64Attr:$scale, I64Attr:$quantum);
  let results = (outs SimTimeType:$result);
  let assemblyFormat = "$input `by` $scale `quantum` $quantum attr-dict";
  let hasVerifier = 1;
}

def SimRealFromIntegerOp : SimOp<"real.from_integer", [Pure]> {
  let summary = "convert a two-state integer to an IEEE floating value";
  let description = [{
    Converts with IEEE round-to-nearest, ties-to-even semantics. Magnitudes
    outside the finite result range produce infinity with the source sign.
  }];
  let arguments = (ins AnyInteger:$input, BoolAttr:$is_signed);
  let results = (outs AnyFloat:$result);
  let assemblyFormat =
      "$input `signed` `=` $is_signed attr-dict `:` type($input) `->` type($result)";
}

def SimRealToIntegerOp : SimOp<"real.to_integer", [Pure]> {
  let summary = "round an IEEE double to a two-state integer";
  let arguments = (ins F64:$input, BoolAttr:$is_signed);
  let results = (outs AnyInteger:$result);
  let assemblyFormat =
      "$input `signed` `=` $is_signed attr-dict `:` type($result)";
}

def SimTimeScaleOp : SimOp<"time.scale", [Pure]> {
  let summary = "scale a dynamic packed delay into design-precision ticks";
  let description = [{
    The input is normalized to an unsigned i64 before this operation. Frontend
    lowering is responsible for mapping unknown and negative values to zero
    and for proving that multiplication by the positive scale cannot overflow
    the supported simulation-time range.
  }];
  let arguments = (ins I64:$input, I64Attr:$scale,
                       BoolAttr:$is_signed);
  let results = (outs SimTimeType:$result);
  let assemblyFormat = [{$input `by` $scale `signed` `=` $is_signed attr-dict
    `:` type($input)}];
  let hasVerifier = 1;
}

def SimTimeAddOp
    : SimOp<"time.add", [Pure, SameOperandsAndResultType, Commutative]> {
  let arguments = (ins SimTimeType : $lhs, SimTimeType : $rhs);
  let results = (outs SimTimeType : $result);
  let assemblyFormat = "$lhs `,` $rhs attr-dict";
  let hasFolder = 1;
}

//===----------------------------------------------------------------------===//
// Scheduler effects and suspension terminators
//===----------------------------------------------------------------------===//

def SimEventTriggerOp
    : SimOp<"event.trigger",
            [MemoryEffects<[MemWrite<SimSchedulerResource>]>]> {
  let arguments = (ins SimEventType : $event, Optional<SimTimeType>:$delay,
                       BoolAttr : $nonblocking,
                       OptionalAttr<SimEventSiteAttr>:$site);
  let assemblyFormat = "$event (`after` $delay^)? `nonblocking` `=` "
                       "$nonblocking attr-dict";
  let hasVerifier = 1;
}

def SimEventTriggeredOp
    : SimOp<"event.triggered",
            [MemoryEffects<[MemRead<SimSchedulerResource>]>]> {
  let summary = "test whether an event occurred in the current time slot";
  let arguments = (ins SimEventType:$event);
  let results = (outs I1:$result);
  let assemblyFormat = "$event attr-dict";
}

def SimEventEqualOp : SimOp<"event.equal", [Pure, SameTypeOperands]> {
  let summary = "compare two event handles for synchronization-object identity";
  let arguments = (ins SimEventType:$lhs, SimEventType:$rhs);
  let results = (outs I1:$result);
  let assemblyFormat = "$lhs `,` $rhs attr-dict";
}

def SimObserverBindOp : SimOp<"observer.bind", [
    DeclareOpInterfaceMethods<SymbolUserOpInterface>,
    MemoryEffects<[MemRead<SimStorageResource>, MemRead<SimNetResource>,
                   MemRead<SimSchedulerResource>]>
  ]> {
  let summary = "bind an outlined timing evaluator to captures and dependencies";
  let description = [{
    Captures match the evaluator arguments following its implicit context
    argument. Dependencies are canonical storage, net, or named-event handles
    whose publications cause the evaluator to run. The operation itself does
    not evaluate the expression.
  }];
  let arguments = (ins FlatSymbolRefAttr:$evaluator,
                       Variadic<AnyType>:$values,
                       I32Attr:$capture_count);
  let results = (outs SimObserverType:$result);
  let assemblyFormat = [{
    $evaluator `values` `(` $values `:` type($values) `)`
    `captures` $capture_count
    attr-dict `:` type($result)
  }];
  let extraClassDeclaration = [{
    ::mlir::Operation::operand_range getCaptures();
    ::mlir::Operation::operand_range getDependencies();
  }];
  let hasVerifier = 1;
}

class SimSuspendOp<string mnemonic, dag extraArgs = (ins),
                   list<Trait> extraTraits = []>
    : SimOp<mnemonic,
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

def SimSuspendDelayOp : SimSuspendOp<"suspend.delay", (ins SimTimeType:$delay,
    OptionalAttr<SimTimingSiteAttr>:$timing)> {
  let assemblyFormat = [{$delay `to` $continuation
    (`(` $continuation_operands^ `:` type($continuation_operands) `)`)? attr-dict}];
}

def SimSuspendChangeOp : SimSuspendOp<"suspend.change", (ins AnyType:$watched)> {
  let assemblyFormat = [{$watched `to` $continuation
    (`(` $continuation_operands^ `:` type($continuation_operands) `)`)? attr-dict `:` type($watched)}];
}

def SimSuspendEdgeOp : SimSuspendOp<"suspend.edge", (ins
    SimEdgeKind:$edge, AnyType:$watched)> {
  let assemblyFormat = [{$edge $watched `to` $continuation
    (`(` $continuation_operands^ `:` type($continuation_operands) `)`)?
    attr-dict `:` type($watched)}];
}

def SimSuspendEdgeIffOp : SimSuspendOp<"suspend.edge_iff", (ins
    SimEdgeKind:$edge, AnyType:$watched, AnyType:$condition)> {
  let summary = "suspend until an edge whose level condition is true";
  let description = [{
    The condition is a directly addressable packed object. The scheduler
    samples its complete four-state value when the primary edge occurs and
    latches a successful occurrence for the waiting process. Changes to the
    condition alone never resume the process.
  }];
  let assemblyFormat = [{$edge $watched `iff` $condition `to` $continuation
    (`(` $continuation_operands^ `:` type($continuation_operands) `)`)?
    attr-dict `:` type($watched) `,` type($condition)}];
}

def SimSuspendLevelOp : SimSuspendOp<"suspend.level", (ins AnyType:$watched)> {
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

def SimSuspendAnyOp : SimOp<"suspend.any", [
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

def SimSuspendEventOp : SimSuspendOp<"suspend.event", (ins SimEventType:$event)> {
  let assemblyFormat = [{$event `to` $continuation
    (`(` $continuation_operands^ `:` type($continuation_operands) `)`)? attr-dict}];
}

def SimSuspendObserveOp : SimOp<"suspend.observe", [
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

def SimSuspendForeverOp : SimSuspendOp<"suspend.forever"> {
  let summary = "permanently suspend the current logical process";
  let assemblyFormat = [{`to` $continuation
    (`(` $continuation_operands^ `:` type($continuation_operands) `)`)?
    attr-dict}];
}

def SimSuspendAwaitOp : SimSuspendOp<"suspend.await", (ins SimProcessType:$process)> {
  let summary = "suspend until the awaited process terminates";
  let description = [{
    Resumes once the target process has finished, whether it terminated
    normally or was killed. This is used for an explicit task/process await;
    fork/join synchronization uses `suspend.join`.
  }];
  let assemblyFormat = [{$process `to` $continuation
    (`(` $continuation_operands^ `:` type($continuation_operands) `)`)? attr-dict}];
}

def SimSuspendJoinOp : SimOp<"suspend.join", [
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

def SimSuspendChildrenOp : SimOp<"suspend.children", [
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

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONOPS_TD
