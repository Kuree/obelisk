//===- SimulationOps.td - Executable simulation operations ----*- td -*-===//

#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONOPS_TD
#define OBELISK_DIALECT_SIMULATION_SIMULATIONOPS_TD

include "obelisk/Dialect/Simulation/SimulationDialect.td"
include "obelisk/Dialect/Simulation/SimulationEnums.td"
include "obelisk/Dialect/Simulation/SimulationAttrs.td"
include "obelisk/Dialect/Simulation/SimulationTypes.td"
include "mlir/IR/SymbolInterfaces.td"
include "mlir/Interfaces/CallInterfaces.td"
include "mlir/Interfaces/ControlFlowInterfaces.td"
include "mlir/Interfaces/FunctionInterfaces.td"
include "mlir/Interfaces/MemorySlotInterfaces.td"
include "mlir/Interfaces/SideEffectInterfaces.td"

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
                       OptionalAttr<ArrayAttr>:$effect_summary,
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

def SimRefAllocOp : SimOp<"ref.alloc", [
    MemoryEffects<[MemAlloc<SimStorageResource>]>,
    DeclareOpInterfaceMethods<PromotableAllocationOpInterface>
  ]> {
  let summary = "promotable automatic local allocation";
  let arguments = (ins AnyType:$initial_value);
  let results = (outs SimRefType:$result);
  let assemblyFormat = "$initial_value attr-dict `:` type($initial_value) `->` qualified(type($result))";
  let hasVerifier = 1;
}

def SimRefLoadOp : SimOp<"ref.load", [
    MemoryEffects<[MemRead<SimStorageResource>]>,
    DeclareOpInterfaceMethods<PromotableMemOpInterface>,
    SimRefElementMatches<"reference", "result">
  ]> {
  let arguments = (ins SimRefType:$reference);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$reference attr-dict `:` qualified(type($reference)) `->` type($result)";
}

def SimRefStoreOp : SimOp<"ref.store", [
    MemoryEffects<[MemWrite<SimStorageResource>]>,
    DeclareOpInterfaceMethods<PromotableMemOpInterface>,
    SimRefElementMatches<"reference", "value">
  ]> {
  let arguments = (ins AnyType:$value, SimRefType:$reference);
  let assemblyFormat = "$value `to` $reference attr-dict `:` type($value) `,` qualified(type($reference))";
}

def SimRefExtractOp : SimOp<"ref.extract", [Pure]> {
  let arguments = (ins SimRefType:$input, I64Attr:$low_bit);
  let results = (outs SimRefType:$result);
  let assemblyFormat = "$input `from` $low_bit attr-dict `:` qualified(type($input)) `->` qualified(type($result))";
  let hasVerifier = 1;
}

def SimRefDynExtractOp : SimOp<"ref.dyn_extract", [Pure]> {
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
  let hasVerifier = 1;
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

def SimDriverExtractOp : SimOp<"driver.extract", [Pure]> {
  let arguments = (ins SimDriverType:$input, I64Attr:$low_bit);
  let results = (outs SimDriverType:$result);
  let assemblyFormat = "$input `from` $low_bit attr-dict `:` qualified(type($input)) `->` qualified(type($result))";
  let hasVerifier = 1;
}

def SimDriverDynExtractOp : SimOp<"driver.dyn_extract", [Pure]> {
  let summary = "dynamically select a packed driver slice";
  let description = [{
    The low-bit index may be a signless builtin integer or four-state logic.
    An unknown index suppresses the drive. Out-of-range bits are ignored while
    in-range bits retain their normal net-driving effects.
  }];
  let arguments = (ins SimDriverType:$input, AnyType:$low_bit);
  let results = (outs SimDriverType:$result);
  let assemblyFormat = "$input `from` $low_bit attr-dict `:` functional-type(operands, results)";
  let hasVerifier = 1;
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
}

def SimLogicUnaryOp : SimOp<"logic.unary", [Pure]> {
  let arguments = (ins SimUnaryKind:$kind, SimLogicType:$input);
  let results = (outs SimLogicType:$result);
  let assemblyFormat =
      "$kind $input attr-dict `:` functional-type(operands, results)";
  let hasVerifier = 1;
}

def SimLogicReductionOp : SimOp<"logic.reduction", [Pure]> {
  let summary = "four-state packed reduction";
  let arguments = (ins SimReductionKind:$kind, SimLogicType:$input);
  let results = (outs SimLogic1Type:$result);
  let assemblyFormat = [{$kind $input attr-dict `:` qualified(type($input))
    `->` qualified(type($result))}];
}

def SimLogicBinaryOp : SimOp<"logic.binary", [Pure,
    AllTypesMatch<["lhs", "rhs", "result"]>]> {
  let arguments = (ins SimBinaryKind:$kind, SimLogicType:$lhs,
                       SimLogicType:$rhs);
  let results = (outs SimLogicType : $result);
  let assemblyFormat =
      "$kind $lhs `,` $rhs attr-dict `:` qualified(type($result))";
}

def SimLogicLogicalOp : SimOp<"logic.logical", [Pure]> {
  let summary = "four-state logical conjunction or disjunction";
  let arguments = (ins SimLogicalKind
                   : $kind, SimLogicType
                   : $lhs, SimLogicType
                   : $rhs);
  let results = (outs SimLogic1Type : $result);
  let assemblyFormat =
      "$kind $lhs `,` $rhs attr-dict `:` functional-type(operands, results)";
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
}

def SimLogicCompareOp
    : SimOp<"logic.compare", [Pure, AllTypesMatch<["lhs", "rhs"]>]> {
  let arguments = (ins SimCompareKind
                   : $kind, SimLogicType
                   : $lhs, SimLogicType
                   : $rhs);
  let results = (outs AnyType : $result);
  let assemblyFormat =
      "$kind $lhs `,` $rhs attr-dict `:` functional-type(operands, results)";
  let hasVerifier = 1;
}

def SimLogicConcatOp : SimOp<"logic.concat", [Pure]> {
  let arguments = (ins Variadic<SimLogicType> : $inputs);
  let results = (outs SimLogicType : $result);
  let assemblyFormat =
      "$inputs attr-dict `:` functional-type($inputs, $result)";
  let hasVerifier = 1;
}

def SimLogicReplicateOp : SimOp<"logic.replicate", [Pure]> {
  let arguments = (ins SimLogicType : $input, I64Attr : $count);
  let results = (outs SimLogicType : $result);
  let assemblyFormat = "$input `times` $count attr-dict `:` "
                       "qualified(type($input)) `->` qualified(type($result))";
  let hasVerifier = 1;
}

def SimLogicExtractOp : SimOp<"logic.extract", [Pure]> {
  let arguments = (ins SimLogicType : $input, I64Attr : $low_bit);
  let results = (outs SimLogicType : $result);
  let assemblyFormat = "$input `from` $low_bit attr-dict `:` "
                       "qualified(type($input)) `->` qualified(type($result))";
  let hasVerifier = 1;
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
    : SimOp<"time.add", [Pure, AllTypesMatch<["lhs", "rhs", "result"]>]> {
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
