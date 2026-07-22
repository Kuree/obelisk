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
                       OptionalAttr<SimComputeObservabilityKind>:$observability);
  let assemblyFormat = [{$id `in` $scope_id `:` $type $lifetime
    (`hierarchy` $hierarchical_name^)? (`debug` $debug_name^)? attr-dict}];
  let hasVerifier = 1;
}

def SimDriverDeclOp : SimOp<"driver.decl", [MemoryEffects<[MemWrite<SimInventoryResource>]>]> {
  let summary = "immutable static net-driver descriptor";
  let arguments = (ins I64Attr:$id, I64Attr:$scope_id, I64Attr:$net_id,
                       TypeAttr:$type, SimLifetime:$lifetime,
                       OptionalAttr<StrAttr>:$hierarchical_name,
                       OptionalAttr<StrAttr>:$debug_name);
  let assemblyFormat = [{$id `in` $scope_id `drives` $net_id `:` $type
    $lifetime (`hierarchy` $hierarchical_name^)?
    (`debug` $debug_name^)? attr-dict}];
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
                       OptionalAttr<StrAttr>:$sym_visibility,
                       OptionalAttr<DictArrayAttr>:$arg_attrs,
                       OptionalAttr<DictArrayAttr>:$res_attrs,
                       OptionalAttr<SimComputeEffectArrayAttr>:$effect_summary,
                       OptionalAttr<SimFragmentABIAttr>:$fragment_abi);
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

def SimDisplayOp : SimOp<"display", [
  MemoryEffects<[MemWrite<SimIOResource>]>
]> {
  let summary = "formatted display or write to a SystemVerilog descriptor";
  let description = [{
    `item_flags` describes the logical argument sequence. Bit 0 marks a
    nonempty packed operand signed; bit 1 represents an omitted argument and
    consumes no entry from `items`. `scope` and `library_cell` carry the
    elaborated call-site information used by `%m` and `%l`. `time_multiplier`
    converts a numeric `%t` argument from the invoking scope's time unit to the
    design precision used by the runtime clock.
  }];
  let arguments = (ins SimContextType:$context, I32:$descriptor,
                       Variadic<AnyType>:$items, BoolAttr:$append_newline,
                       I32Attr:$default_radix,
                       DenseI32ArrayAttr:$item_flags,
                       OptionalAttr<StrAttr>:$scope,
                       OptionalAttr<StrAttr>:$library_cell,
                       OptionalAttr<I64Attr>:$time_multiplier);
  let assemblyFormat = [{$context `to` $descriptor `(` $items `)`
    `newline` `=` $append_newline `radix` `=` $default_radix
    `flags` `=` $item_flags attr-dict
    `:` type($items)}];
  let hasVerifier = 1;
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
    The low-bit index may be a signless builtin integer or four-state logic.
    An unknown index produces all X. For a partially out-of-range selection,
    in-range bits are preserved and out-of-range bits are X.
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
    The low-bit index may be a signless builtin integer or four-state logic.
    An unknown index produces zero. For a partially out-of-range selection,
    in-range bits are preserved and out-of-range bits are zero. This operation
    defines bounds behavior without relying on poison-producing integer shifts.
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

def SimTimeScaleOp : SimOp<"time.scale", [Pure]> {
  let summary = "scale a dynamic packed delay into design-precision ticks";
  let arguments = (ins AnyType:$input, I64Attr:$scale,
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
  let arguments = (ins SimEventType : $event, BoolAttr : $nonblocking,
                       OptionalAttr<SimEventSiteAttr>:$site);
  let assemblyFormat = "$event `nonblocking` `=` $nonblocking attr-dict";
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
                                      OptionalAttr<SimContinuationSiteAttr>:$site));
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
                       OptionalAttr<SimContinuationSiteAttr>:$site);
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
                       OptionalAttr<SimContinuationSiteAttr>:$site);
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

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONOPS_TD
