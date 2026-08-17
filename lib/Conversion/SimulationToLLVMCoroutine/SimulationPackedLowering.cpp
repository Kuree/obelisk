//===- SimulationPackedLowering.cpp - Packed simulation conversion ----===//

#include "SimulationPackedLowering.h"
#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Analysis/SimulationStorageAnalysis.h"
#include "obelisk/Analysis/StateDomainAnalysis.h"
#include "obelisk/Conversion/RuntimeToLLVM.h"
#include "obelisk/Conversion/SimulationToRuntime.h"
#include "obelisk/Conversion/SimulationToStandard.h"
#include "obelisk/Dialect/Runtime/RuntimeOps.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHash.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Threading.h"
#include "mlir/Transforms/DialectConversion.h"

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
#include <chrono>
#include <cstring>
#include <limits>

using namespace mlir;

namespace obelisk::detail {

namespace {

constexpr StringLiteral inductiveTwoStateAccessAttr =
    "obelisk.eval.inductive_two_state_access";
constexpr StringLiteral inductiveTwoStateAttr =
    "obelisk.eval.inductive_two_state";
constexpr StringLiteral conditionalTwoStateAttr =
    "obelisk.eval.conditionally_two_state";

LogicalResult convertNativeAggregateType(Type type,
                                         SmallVectorImpl<Type> &results) {
  std::optional<unsigned> width = nativeStateWidth(type);
  if (!width)
    return failure();
  Type plane = IntegerType::get(type.getContext(), *width);
  results.push_back(plane);
  if (containsLogic(type))
    results.push_back(plane);
  return success();
}

bool hasNoLogic(Operation *operation) {
  for (Type type : operation->getOperandTypes())
    if (containsLogic(type))
      return false;
  for (Type type : operation->getResultTypes())
    if (containsLogic(type))
      return false;
  for (Region &region : operation->getRegions())
    for (Block &block : region)
      for (BlockArgument argument : block.getArguments())
        if (containsLogic(argument.getType()))
          return false;
  return true;
}

std::string makeElementTraceBytes(ArrayRef<int64_t> offsets,
                                  ArrayRef<int32_t> kinds) {
  std::string bytes(offsets.size() * sizeof(obelisk_rt_element_trace_slot_v1),
                    '\0');
  for (auto [index, offset, kind] : llvm::enumerate(offsets, kinds)) {
    obelisk_rt_element_trace_slot_v1 slot{
        static_cast<uint64_t>(offset),
        static_cast<obelisk_rt_managed_slot_kind_v1>(kind), 0};
    std::memcpy(bytes.data() + index * sizeof(slot), &slot, sizeof(slot));
  }
  return bytes;
}

uint64_t appendStableHash(uint64_t hash, uint64_t value, unsigned bytes) {
  for (unsigned index = 0; index != bytes; ++index) {
    hash ^= (value >> (index * 8)) & 0xff;
    hash *= OBELISK_STABLE_HASH_PRIME;
  }
  return hash;
}

void fuseWideManagedBitStores(ModuleOp module) {
  SmallVector<sim::SimManagedStoreOp> stores;
  module.walk([&](sim::SimManagedStoreOp store) { stores.push_back(store); });
  IRRewriter rewriter(module.getContext());
  for (sim::SimManagedStoreOp store : stores) {
    auto unflatten =
        store.getValue().getDefiningOp<sim::SimPackedUnflattenOp>();
    auto insert =
        unflatten
            ? unflatten.getInput().getDefiningOp<sim::SimBitsDynInsertOp>()
            : sim::SimBitsDynInsertOp{};
    auto flatten =
        insert ? insert.getInput().getDefiningOp<sim::SimPackedFlattenOp>()
               : sim::SimPackedFlattenOp{};
    auto load = flatten
                    ? flatten.getInput().getDefiningOp<sim::SimManagedLoadOp>()
                    : sim::SimManagedLoadOp{};
    auto inputType = insert ? dyn_cast<IntegerType>(insert.getInput().getType())
                            : IntegerType{};
    auto replacementType =
        insert ? dyn_cast<IntegerType>(insert.getReplacement().getType())
               : IntegerType{};
    auto lowType = insert ? dyn_cast<IntegerType>(insert.getLowBit().getType())
                          : IntegerType{};
    if (!unflatten || !insert || !flatten || !load || !inputType ||
        !replacementType || !lowType || inputType.getWidth() <= 256 ||
        replacementType.getWidth() > 64 ||
        load.getReference() != store.getReference() ||
        !unflatten->hasOneUse() || !insert->hasOneUse() ||
        !flatten->hasOneUse() || !load->hasOneUse())
      continue;
    if (load->getBlock() != store->getBlock())
      continue;
    bool uninterrupted = false;
    for (Operation *operation = load->getNextNode(); operation;
         operation = operation->getNextNode()) {
      if (operation == store.getOperation()) {
        uninterrupted = true;
        break;
      }
      if (!isMemoryEffectFree(operation) || !isSpeculatable(operation))
        break;
    }
    if (!uninterrupted)
      continue;
    rewriter.setInsertionPoint(store);
    sim::SimManagedBitsDynStoreOp::create(
        rewriter, store.getLoc(), insert.getReplacement(), store.getReference(),
        insert.getLowBit());
    rewriter.eraseOp(store);
    rewriter.eraseOp(unflatten);
    rewriter.eraseOp(insert);
    rewriter.eraseOp(flatten);
    rewriter.eraseOp(load);
  }
}

} // namespace

LogicalResult lowerPackedSimulationOperations(
    ModuleOp module, const llvm::DataLayout &dataLayout,
    const NativeStateLayout &stateLayout, bool enableDirectStaticState,
    const NativeStaticNBAPlan *staticNBAPlan, bool vpiAllowsWrite,
    bool experimentalTwoState) {
  MLIRContext *context = module.getContext();
  bool detailedTiming = module->hasAttr("obelisk.debug.native_timing");
  auto lastTiming = std::chrono::steady_clock::now();
  auto markTiming = [&](StringRef name) {
    if (!detailedTiming)
      return;
    auto now = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(now - lastTiming).count();
    llvm::errs() << "obelisk packed timing: " << name << ": " << seconds
                 << " s\n";
    lastTiming = now;
  };
  // Avoid lowering a narrow update of a wide managed packed field into one
  // enormous LLVM integer. Besides copying the whole field, i32k shifts make
  // SelectionDAG instruction selection superlinear. Bytecode has already been
  // frozen before this native-only rewrite.
  fuseWideManagedBitStores(module);
  // Consume the whole-design X/Z proof in the AOT path after suspension
  // threading has reached its final SSA shape. Signatures and canonical frames
  // remain two-plane ABI objects, but proven block arguments, call results,
  // and local producers expose a constant-zero unknown plane to LLVM.
  DenseSet<Value> nativeTwoStateValues;
  DenseSet<Operation *> nativeTwoStateOperations;
  uint64_t stateDomainFunctionLimit = std::numeric_limits<uint64_t>::max();
  auto optimizationLevel =
      module->getAttrOfType<IntegerAttr>("obelisk.native.optimization_level");
  if (optimizationLevel && optimizationLevel.getInt() >= 3)
    stateDomainFunctionLimit = 4096;
  if (auto limit = module->getAttrOfType<IntegerAttr>(
          "obelisk.native.max_state_domain_functions")) {
    if (limit.getInt() < 0)
      return module.emitError(
          "native state-domain function limit cannot be negative");
    stateDomainFunctionLimit = limit.getUInt();
  }
  WalkResult stateDomainsComputed = module.walk([&](sim::SimDesignOp design) {
    if (design.getBody().empty()) {
      design.emitOpError("cannot analyze a design with no body");
      return WalkResult::interrupt();
    }
    bool needsInductiveFacts = false;
    bool needsKnownStateFacts = false;
    uint64_t functionCount = 0;
    for (sim::SimFuncOp function :
         design.getBody().front().getOps<sim::SimFuncOp>()) {
      if (function.isExternal())
        continue;
      ++functionCount;
      bool guarded = function->hasAttr(inductiveTwoStateAttr);
      bool conditional = function->hasAttr(conditionalTwoStateAttr);
      needsInductiveFacts |= guarded && !conditional;
      needsKnownStateFacts |= conditional;
    }
    // Whole-design state-domain propagation is an optional code-size/runtime
    // optimization. On very large library-heavy O3 inputs, retain exact
    // four-state lowering instead of spending an unbounded cold-compile budget
    // proving local unknown planes dead. Guarded native variants still require
    // their proof and are never skipped.
    if (!needsInductiveFacts && !needsKnownStateFacts &&
        functionCount > stateDomainFunctionLimit)
      return WalkResult::advance();
    FailureOr<StateDomainAnalysis> stateDomains =
        StateDomainAnalysis::compute(design, needsInductiveFacts);
    if (failed(stateDomains))
      return WalkResult::interrupt();
    std::optional<StateDomainAnalysis> knownStateDomains;
    if (needsKnownStateFacts) {
      FailureOr<StateDomainAnalysis> computed =
          StateDomainAnalysis::computeAssumingKnownState(design);
      if (failed(computed))
        return WalkResult::interrupt();
      knownStateDomains.emplace(std::move(*computed));
    }
    for (sim::SimFuncOp function :
         design.getBody().front().getOps<sim::SimFuncOp>()) {
      if (function.isExternal())
        continue;
      bool guardedTwoState = function->hasAttr(inductiveTwoStateAttr);
      bool conditionalTwoState = function->hasAttr(conditionalTwoStateAttr);
      const StateDomainAnalysis &guardedDomains =
          conditionalTwoState ? *knownStateDomains : *stateDomains;
      auto isTwoState = [&](Value value) {
        return experimentalTwoState ||
               (guardedTwoState
                    ? guardedDomains.isTwoStateWithInductiveRoots(value)
                    : stateDomains->isTwoState(value));
      };
      analysis::DescriptorProvenanceMap provenance =
          analysis::deriveDescriptorProvenance(function);
      auto isPromotableAccess = [&](Value handle, Value result) {
        if (!guardedDomains.isTwoStateWithInductiveRoots(result))
          return false;
        auto root = provenance.find(handle);
        return root != provenance.end() && root->second.descriptor &&
               !root->second.dynamic && root->second.width != 0 &&
               guardedDomains.isInductivelyTwoState(root->second.resource,
                                                    *root->second.descriptor);
      };
      for (Block &block : function.getBody()) {
        for (BlockArgument argument : block.getArguments())
          if (isa<sim::LogicType>(argument.getType()) && isTwoState(argument))
            nativeTwoStateValues.insert(argument);
        for (Operation &operation : block) {
          for (Value result : operation.getResults())
            // Preponed snapshots retain the source's four-state domain, and a
            // history value has an IEEE default of X before its ring contains
            // enough enabled clock ticks, even when live Active-region state
            // is inductively two-state.
            if (isa<sim::LogicType>(result.getType()) &&
                !isa<sim::SimSampledReadOp, sim::SimSampledHistoryOp,
                     sim::SimClockedSampleReadOp>(operation) &&
                isTwoState(result))
              nativeTwoStateValues.insert(result);
          if (!guardedTwoState)
            continue;
          if (auto load = dyn_cast<sim::SimRefLoadOp>(operation)) {
            if (isPromotableAccess(load.getReference(), load.getResult()))
              operation.setAttr(inductiveTwoStateAccessAttr,
                                UnitAttr::get(context));
            continue;
          }
          if (auto read = dyn_cast<sim::SimNetReadOp>(operation)) {
            if (isPromotableAccess(read.getNet(), read.getResult()))
              operation.setAttr(inductiveTwoStateAccessAttr,
                                UnitAttr::get(context));
            continue;
          }
          Value destination;
          Value stored;
          if (auto store = dyn_cast<sim::SimRefStoreOp>(operation)) {
            destination = store.getReference();
            stored = store.getValue();
          } else if (auto nba = dyn_cast<sim::SimNBAEnqueueOp>(operation)) {
            destination = nba.getDestination();
            stored = nba.getValue();
          }
          if (!destination ||
              !guardedDomains.isTwoStateWithInductiveRoots(stored))
            continue;
          auto root = provenance.find(destination);
          if (root == provenance.end() || !root->second.descriptor ||
              root->second.dynamic ||
              !guardedDomains.isInductivelyTwoState(root->second.resource,
                                                    *root->second.descriptor))
            continue;
          operation.setAttr(inductiveTwoStateAccessAttr,
                            UnitAttr::get(context));
        }
      }
    }
    return WalkResult::advance();
  });
  if (stateDomainsComputed.wasInterrupted())
    return failure();
  markTiming("state-domain analysis");
  for (Value value : nativeTwoStateValues) {
    auto result = dyn_cast<OpResult>(value);
    if (!result || result.getOwner()->getNumResults() != 1)
      continue;
    nativeTwoStateOperations.insert(result.getOwner());
  }

  // Record the net driven by each operation before dialect conversion starts
  // rewriting function signatures and their block arguments.  Conversion
  // patterns should inspect stable operation metadata instead of chasing the
  // source SSA graph while it is being replaced.
  annotateStaticDriverNets(module, stateLayout);

  // Managed string lowering materializes one private byte global per literal
  // or scan prefix. Assign their names once in deterministic IR order. The
  // old patterns searched suffixes from zero for every operation, turning a
  // large library's literal inventory into quadratic symbol-table traffic.
  uint64_t stringOrdinal = 0;
  uint64_t scanPrefixOrdinal = 0;
  uint64_t fileScanPrefixOrdinal = 0;
  llvm::StringSet<> reservedSymbols;
  llvm::StringMap<Operation *> existingSymbols;
  for (Operation &operation : *module.getBody())
    if (StringAttr symbol = SymbolTable::getSymbolName(&operation)) {
      reservedSymbols.insert(symbol.getValue());
      existingSymbols.try_emplace(symbol.getValue(), &operation);
    }
  auto allocateGlobalName = [&](StringRef base, uint64_t &ordinal) {
    std::string name;
    do {
      name = (base + Twine(ordinal++)).str();
    } while (!reservedSymbols.insert(name).second);
    return StringAttr::get(context, name);
  };
  struct ByteGlobal {
    Location location;
    std::string name;
    std::string bytes;
  };
  SmallVector<ByteGlobal> byteGlobals;
  llvm::StringMap<unsigned> byteGlobalIndices;
  auto reserveByteGlobal = [&](Location location, StringRef name,
                               StringRef bytes) -> LogicalResult {
    auto [entry, inserted] =
        byteGlobalIndices.try_emplace(name, byteGlobals.size());
    if (inserted) {
      byteGlobals.push_back({location, name.str(), bytes.str()});
      return success();
    }
    if (byteGlobals[entry->second].bytes != bytes)
      return emitError(location)
             << "native byte global @" << name << " has conflicting payloads";
    return success();
  };
  auto reserveTrace = [&](Location location, StringRef prefix, uint64_t typeID,
                          ArrayRef<int64_t> offsets,
                          ArrayRef<int32_t> kinds) -> LogicalResult {
    if (offsets.empty())
      return success();
    std::string name = (prefix + Twine(typeID)).str();
    return reserveByteGlobal(location, name,
                             makeElementTraceBytes(offsets, kinds));
  };
  WalkResult globalInventory = module.walk([&](Operation *operation) {
    auto reserve = [&](StringRef name, StringRef bytes) {
      if (failed(reserveByteGlobal(operation->getLoc(), name, bytes)))
        return WalkResult::interrupt();
      return WalkResult::advance();
    };
    if (auto literal = dyn_cast<sim::SimStringLiteralOp>(operation)) {
      if (literal.getValue().empty())
        return WalkResult::advance();
      StringAttr name =
          allocateGlobalName("__obelisk_string_literal.", stringOrdinal);
      operation->setAttr(nativeStringGlobalAttr, name);
      return reserve(name.getValue(), literal.getValue());
    }
    if (auto scan = dyn_cast<sim::SimStringScanFieldOp>(operation)) {
      if (scan.getPrefix().empty())
        return WalkResult::advance();
      StringAttr name =
          allocateGlobalName("__obelisk_scan_prefix.", scanPrefixOrdinal);
      operation->setAttr(nativeScanPrefixGlobalAttr, name);
      return reserve(name.getValue(), scan.getPrefix());
    }
    if (auto scan = dyn_cast<sim::SimFileScanFieldOp>(operation)) {
      if (scan.getPrefix().empty())
        return WalkResult::advance();
      StringAttr name = allocateGlobalName("__obelisk_file_scan_prefix.",
                                           fileScanPrefixOrdinal);
      operation->setAttr(nativeFileScanPrefixGlobalAttr, name);
      return reserve(name.getValue(), scan.getPrefix());
    }
    if (auto create = dyn_cast<sim::SimContainerCreateOp>(operation)) {
      if (failed(reserveTrace(create.getLoc(), "__obelisk_element_trace_",
                              create.getTypeId(), create.getTraceOffsets(),
                              create.getTraceKinds())))
        return WalkResult::interrupt();
      return WalkResult::advance();
    }
    if (auto create = dyn_cast<sim::SimMailboxCreateOp>(operation)) {
      if (failed(reserveTrace(create.getLoc(),
                              "__obelisk_mailbox_element_trace_",
                              create.getTypeId(), create.getTraceOffsets(),
                              create.getTraceKinds())))
        return WalkResult::interrupt();
      return WalkResult::advance();
    }
    if (auto create = dyn_cast<sim::SimAssocCreateOp>(operation)) {
      if (failed(reserveTrace(create.getLoc(), "__obelisk_element_trace_",
                              create.getTypeId(), create.getTraceOffsets(),
                              create.getTraceKinds())))
        return WalkResult::interrupt();
      return WalkResult::advance();
    }
    StringRef program;
    if (auto solve = dyn_cast<sim::SimRandomSolveOp>(operation))
      program = solve.getProgram();
    else if (auto solve = dyn_cast<sim::SimRandomSolveWideOp>(operation))
      program = solve.getProgram();
    if (!program.empty()) {
      std::string name = "__obelisk_random_program_" +
                         llvm::utohexstr(llvm::hash_value(program));
      return reserve(name, program);
    }
    if (auto call = dyn_cast<sim::SimDPICallOp>(operation)) {
      if (call.getSourceFile().empty())
        return WalkResult::advance();
      uint64_t hash = OBELISK_STABLE_HASH_OFFSET_BASIS;
      for (unsigned char byte : call.getSourceFile().bytes())
        hash = appendStableHash(hash, byte, 1);
      std::string name =
          (Twine("__obelisk_dpi_source_") + Twine(call.getImportId()) + "_" +
           Twine(call.getSourceLine()) + "_" + Twine(call.getSourceColumn()) +
           "_" + Twine(hash))
              .str();
      return reserve(name, call.getSourceFile());
    }
    return WalkResult::advance();
  });
  if (globalInventory.wasInterrupted())
    return failure();
  for (const ByteGlobal &global : byteGlobals) {
    auto existingIt = existingSymbols.find(global.name);
    if (existingIt != existingSymbols.end()) {
      auto existing = dyn_cast<LLVM::GlobalOp>(existingIt->second);
      if (!existing)
        return emitError(global.location)
               << "native byte global @" << global.name
               << " conflicts with a pre-existing symbol";
      Type expectedType = LLVM::LLVMArrayType::get(IntegerType::get(context, 8),
                                                   global.bytes.size());
      auto value = existing->getAttrOfType<StringAttr>("value");
      if (existing.getGlobalType() != expectedType || !existing.getConstant() ||
          existing.getLinkage() != LLVM::Linkage::Internal ||
          existing.getAlignment().value_or(0) != 1 || !value ||
          value.getValue() != global.bytes)
        return emitError(global.location)
               << "native byte global @" << global.name
               << " conflicts with a pre-existing symbol";
    } else {
      LLVM::GlobalOp created = makeByteArrayGlobal(module, global.location,
                                                   global.name, global.bytes);
      existingSymbols.try_emplace(global.name, created.getOperation());
    }
  }
  markTiming("byte-global inventory and materialization");

  auto configurePackedConverter = [&](SimulationToStandardTypeConverter &c) {
    addSimulationPackedAggregateTypeConversions(c);
    c.addConversion(
        [](sim::UnpackedArrayType type, SmallVectorImpl<Type> &results) {
          return convertNativeAggregateType(type, results);
        });
    c.addConversion(
        [](sim::UnpackedStructType type, SmallVectorImpl<Type> &results) {
          return convertNativeAggregateType(type, results);
        });
    c.addConversion(
        [](sim::UnpackedUnionType type, SmallVectorImpl<Type> &results) {
          return convertNativeAggregateType(type, results);
        });
    addSimulationToRuntimeTypeConversions(c);
    c.addConversion([context](Type type) -> std::optional<Type> {
      if (sim::isSimulationHandleType(type) || sim::isManagedHandleType(type))
        return IntegerType::get(context, sim::simulationHandleBitWidth);
      return std::nullopt;
    });
    c.addConversion([context](sim::ArgumentRefType) -> Type {
      return IntegerType::get(context, 192);
    });
    c.addConversion(
        [context](sim::ManagedRefType, SmallVectorImpl<Type> &results) {
          results.push_back(IntegerType::get(context, 64));
          results.push_back(IntegerType::get(context, 64));
          return success();
        });
  };
  SimulationToStandardTypeConverter packedConverter;
  configurePackedConverter(packedConverter);
  llvm::DataLayout localDataLayout(dataLayout.getStringRepresentation());
  llvm::LLVMContext llvmContext;
  WalkResult virtualTaskABI =
      module.walk([&](sim::SimClassVirtualTaskCallOp call) {
        SmallVector<int64_t> sizes;
        SmallVector<int64_t> roots;
        SmallVector<int64_t> references;
        unsigned physicalIndex = 0;
        for (Value argument : call.getArguments()) {
          FailureOr<analysis::SimulationStorageProperties> storage =
              analysis::getSimulationStorageProperties(
                  argument.getType(), localDataLayout, llvmContext);
          if (failed(storage)) {
            call.emitOpError("argument has no canonical native ABI");
            return WalkResult::interrupt();
          }
          unsigned planes =
              analysis::getSimulationPhysicalStorageCount(*storage);
          for (const sim::ManagedHandleSlot &root : storage->managedRootSlots) {
            roots.push_back(physicalIndex);
            roots.push_back(root.bitOffset);
            roots.push_back(root.kindMask);
            roots.push_back(root.conditional ? 1 : 0);
          }
          for (unsigned plane = 0; plane != planes; ++plane)
            sizes.push_back(storage->size);
          physicalIndex += planes;
        }
        physicalIndex = 0;
        for (Value value : call.getValues()) {
          FailureOr<analysis::SimulationStorageProperties> storage =
              analysis::getSimulationStorageProperties(
                  value.getType(), localDataLayout, llvmContext);
          if (failed(storage)) {
            call.emitOpError("value has no canonical native ABI");
            return WalkResult::interrupt();
          }
          if (isa<sim::RefType>(value.getType()))
            references.push_back(physicalIndex);
          physicalIndex +=
              analysis::getSimulationPhysicalStorageCount(*storage);
        }
        call->setAttr(nativeMethodArgumentSizesAttr,
                      DenseI64ArrayAttr::get(context, sizes));
        call->setAttr(nativeMethodArgumentRootsAttr,
                      DenseI64ArrayAttr::get(context, roots));
        call->setAttr(nativeTransferredReferencesAttr,
                      DenseI64ArrayAttr::get(context, references));
        return WalkResult::advance();
      });
  if (virtualTaskABI.wasInterrupted())
    return failure();
  ReferenceArgumentMap referenceArguments;
  WalkResult lifetimeInputs = module.walk([&](sim::SimFuncOp function) {
    if (function.getBody().empty())
      return WalkResult::advance();
    // Observer captures are borrowed from the persistent computed-wait
    // record. Unlike an ordinary direct call, invoking an observer does not
    // transfer one retained reference per argument, so its return must not
    // consume captured automatic state. The waiting activation owns that
    // state across suspension and releases it on resumption or cancellation.
    if (function.getEntryKind() == sim::EntryKind::Observer ||
        function->hasAttr("obelisk.eval.borrowed_captures"))
      return WalkResult::advance();
    unsigned physical = 0;
    for (BlockArgument argument : function.getBody().front().getArguments()) {
      SmallVector<Type> converted;
      if (failed(packedConverter.convertType(argument.getType(), converted)))
        return WalkResult::interrupt();
      if (isa<sim::RefType>(argument.getType())) {
        if (converted.size() != 1)
          return WalkResult::interrupt();
        referenceArguments[function.getOperation()].push_back(physical);
      }
      physical += converted.size();
    }
    SmallVector<int64_t> referenceIndices;
    for (unsigned index : referenceArguments[function.getOperation()])
      referenceIndices.push_back(index);
    function->setAttr(nativeTransferredReferencesAttr,
                      DenseI64ArrayAttr::get(context, referenceIndices));
    return WalkResult::advance();
  });
  if (lifetimeInputs.wasInterrupted())
    return failure();
  markTiming("managed lifetime inventory");
  // This is transaction-local metadata produced only by the AOT signature
  // pattern below. Never consume a same-named source attribute.
  module.walk([](sim::SimFuncOp function) {
    function->removeAttr(nativeTwoStateBlockUnknownsAttr);
  });
  auto populatePackedPatterns = [&](SimulationToStandardTypeConverter &c,
                                    RewritePatternSet &patterns) {
    populateSimulationToStandardPatterns(c, patterns, nativeTwoStateOperations);
    populateSimulationPackedAggregateViewPatterns(c, patterns);
    populateSimulationToRuntimePatterns(c, patterns);
    populateFunctionTypeConversionPatterns(patterns, c, nativeTwoStateValues);
    populateAggregateToLLVMConversionPatterns(patterns, c);
    populateControlToLLVMConversionPatterns(patterns, c);
    populateEventToLLVMConversionPatterns(patterns, c);
    populateSuspensionTypeConversionPatterns(patterns, c);
    populateReferenceLifetimeToLLVMConversionPatterns(patterns, c);
    populateNativeHandleConversionPatterns(patterns, c, stateLayout.storage,
                                           stateLayout.nets,
                                           stateLayout.drivers);
    populateSchedulerToLLVMConversionPatterns(patterns, c);
    populateStateReadWriteToLLVMConversionPatterns(
        patterns, c, stateLayout.bitCount,
        enableDirectStaticState ? &stateLayout : nullptr, experimentalTwoState);
    populateOverrideToLLVMConversionPatterns(patterns, c, stateLayout.bitCount);
    populateManagedToLLVMConversionPatterns(patterns, c, dataLayout,
                                            stateLayout.bitCount);
    populateDriverToLLVMConversionPatterns(patterns, c, stateLayout);
    populateNBAToLLVMConversionPatterns(patterns, c, stateLayout.bitCount,
                                        staticNBAPlan, staticNBAPlan != nullptr,
                                        vpiAllowsWrite, experimentalTwoState);
  };
  RewritePatternSet packedPatterns(context);
  populatePackedPatterns(packedConverter, packedPatterns);
  auto configurePackedTarget = [&](ConversionTarget &target,
                                   SimulationToStandardTypeConverter &c) {
    target.addIllegalOp<
        sim::SimBytesConstantOp, sim::SimFinishOp, sim::SimStopOp,
        sim::SimFatalOp, sim::SimErrorOp, sim::SimTerminationRequestedOp,
        sim::SimTimeNowOp, sim::SimDisplayOp, sim::SimStringOutputFormatOp,
        sim::SimFileOpenMCDOp, sim::SimFileOpenOp, sim::SimFileCloseOp,
        sim::SimFileFlushOp, sim::SimFileGetcOp, sim::SimFileUngetcOp,
        sim::SimFileGetlineOp, sim::SimFileReadPackedOp, sim::SimFileEofOp,
        sim::SimFileSeekOp, sim::SimFileTellOp, sim::SimFileRewindOp,
        sim::SimDumpOpenOp, sim::SimDumpOpenStringOp, sim::SimDumpTimescaleOp,
        sim::SimDumpVarsOp, sim::SimDumpAllOp, sim::SimDumpControlOp,
        sim::SimDumpLimitOp, sim::SimDumpFlushOp, sim::SimDumpPortsOp,
        sim::SimDumpPortsControlOp>();
    target.addIllegalOp<
        sim::SimContextStorageOp, sim::SimContextNetOp, sim::SimContextDriverOp,
        sim::SimContextEventOp, sim::SimRefAllocOp, sim::SimRefReleaseOwnerOp,
        sim::SimRefLoadOp, sim::SimRefStoreOp, sim::SimOverrideOp,
        sim::SimReleaseOverrideOp, sim::SimNetExtractOp, sim::SimRefExtractOp,
        sim::SimRefDynExtractOp, sim::SimRefSubelementOp,
        sim::SimRefArrayElementOp, sim::SimNetReadOp, sim::SimDriverDriveOp,
        sim::SimDriverDriveChangedOp, sim::SimDriverExtractOp,
        sim::SimDriverDynExtractOp, sim::SimDriverSubelementOp,
        sim::SimDriverArrayElementOp, sim::SimNBAEnqueueOp,
        sim::SimEventCreateOp, sim::SimEventTriggerOp, sim::SimEventTriggeredOp,
        sim::SimEventEqualOp, sim::SimDisableChildrenOp, sim::SimControlEnterOp,
        sim::SimControlLeaveOp, sim::SimControlDisableOp, sim::SimStaticOnceOp,
        sim::SimDeferredOnceOp, sim::SimDeferredEnqueueOp,
        sim::SimDeferredMatureOp, sim::SimAssertionControlOp,
        sim::SimAssertionEnabledOp, sim::SimAssertionActionStateOp,
        sim::SimSampledReadOp, sim::SimSampledHistoryOp,
        sim::SimClockedSampleUpdateOp, sim::SimClockedSampleReadOp,
        sim::SimMonitorRegisterOp, sim::SimMonitorControlOp,
        sim::SimMonitorCurrentOp, sim::SimBitsDynExtractOp,
        sim::SimBitsDynInsertOp, sim::SimClassNullOp, sim::SimCovergroupNullOp,
        sim::SimCovergroupCreateOp, sim::SimCovergroupSampleEnabledOp,
        sim::SimCovergroupBinHitOp, sim::SimCovergroupStartOp,
        sim::SimCovergroupStopOp, sim::SimCovergroupInstanceQueryOp,
        sim::SimCovergroupTypeQueryOp, sim::SimManagedNullOp,
        sim::SimManagedIsNullOp, sim::SimEventNullOp, sim::SimContainerSizeOp,
        sim::SimContainerCreateLikeOp, sim::SimContainerCreateOp,
        sim::SimContainerCloneOp, sim::SimContainerDeleteOp,
        sim::SimQueueDeleteOp, sim::SimQueueInsertOp, sim::SimContainerReadOp,
        sim::SimContainerWriteOp, sim::SimAssocCreateOp, sim::SimAssocReadOp,
        sim::SimAssocWriteOp, sim::SimAssocExistsOp, sim::SimAssocDeleteOp,
        sim::SimAssocSetDefaultOp, sim::SimAssocTraverseOp,
        sim::SimRandomNextOp, sim::SimRandomSeedOp, sim::SimRandomBoundedOp,
        sim::SimRandomDistributionOp, sim::SimRandomCycleNextOp,
        sim::SimRandomSolveOp, sim::SimRandomSolveWideOp,
        sim::SimStringLiteralOp, sim::SimStringFromPackedOp,
        sim::SimStringToPackedOp, sim::SimStringConcatOp,
        sim::SimStringRepeatOp, sim::SimStringLengthOp, sim::SimStringGetcOp,
        sim::SimStringPutcOp, sim::SimStringSubstrOp, sim::SimStringCompareOp,
        sim::SimStringCaseConvertOp, sim::SimStringParseIntegerOp,
        sim::SimStringParseLogicOp,
        sim::SimStringParseRealOp, sim::SimStringScanFieldOp,
        sim::SimStringFormatIntegerOp, sim::SimStringFormatRealOp,
        sim::SimFileOpenStringMCDOp, sim::SimFileOpenStringOp,
        sim::SimFileGetlineStringOp, sim::SimFileErrorStringOp,
        sim::SimTimeFormatOp, sim::SimPlusargTestOp, sim::SimPlusargValueOp,
        sim::SimClassAllocOp, sim::SimClassCopyOp, sim::SimClassIsInstanceOp,
        sim::SimClassIdOp, sim::SimClassCastOp, sim::SimClassFieldRefOp,
        sim::SimManagedWatchOp, sim::SimClassRootBindOp, sim::SimManagedLoadOp,
        sim::SimManagedStoreOp, sim::SimManagedBitsDynStoreOp,
        sim::SimManagedNBAEnqueueOp, sim::SimReferencePathNBAEnqueueOp,
        sim::SimArgumentRefFromRefOp, sim::SimArgumentRefFromManagedOp,
        sim::SimReferencePathIndexOp, sim::SimReferencePathAssocOp,
        sim::SimArgumentRefFromPathOp, sim::SimArgumentRefLoadOp,
        sim::SimArgumentRefStoreOp, sim::SimClassDirectCallOp,
        sim::SimClassVirtualCallOp, sim::SimWeakCreateOp, sim::SimWeakGetOp,
        sim::SimWeakClearOp, sim::SimGCSafepointOp>();
    target
        .addIllegalOp<sim::SimAggregateDefaultOp, sim::SimAggregateConstructOp,
                      sim::SimAggregateExtractOp, sim::SimAggregateInsertOp,
                      sim::SimArrayDynExtractOp, sim::SimUnionConstructOp,
                      sim::SimUnionExtractOp, sim::SimUnionIsActiveOp>();
    target.addLegalDialect<runtime::ObeliskRuntimeDialect>();
    target.addLegalOp<sim::SimContextRuntimeOp, sim::SimStatusCheckOp>();
    target.addDynamicallyLegalOp<sim::SimFuncOp>([&](sim::SimFuncOp function) {
      return c.isSignatureLegal(function.getFunctionType()) &&
             c.isLegal(&function.getBody());
    });
    target.addDynamicallyLegalOp<
        sim::SimCallOp, sim::SimDPICallOp, sim::SimSpawnOp, sim::SimReturnOp,
        sim::SimTaskCallOp, sim::SimClassVirtualTaskCallOp,
        sim::SimObserverBindOp, sim::SimPackedFlattenOp,
        sim::SimPackedUnflattenOp, sim::SimSuspendDelayOp,
        sim::SimSuspendChangeOp, sim::SimSuspendEdgeOp,
        sim::SimSuspendEdgeIffOp, sim::SimSuspendLevelOp, sim::SimSuspendAnyOp,
        sim::SimSuspendEventOp, sim::SimSuspendMailboxOp,
        sim::SimSuspendSemaphoreOp, sim::SimSuspendForeverOp,
        sim::SimSuspendAwaitOp, sim::SimSuspendJoinOp,
        sim::SimSuspendChildrenOp, sim::SimSuspendObserveOp,
        sim::SimProcessControlOp>(
        [&](Operation *operation) { return c.isLegal(operation); });
    target.addDynamicallyLegalDialect<
        sim::ObeliskSimulationDialect, arith::ArithDialect,
        cf::ControlFlowDialect, func::FuncDialect>([&](Operation *operation) {
      return hasNoLogic(operation) && c.isLegal(operation);
    });
    target.addDynamicallyLegalOp<cf::BranchOp, cf::CondBranchOp>(
        [&](Operation *operation) { return c.isLegal(operation); });
    target.addDynamicallyLegalOp<ModuleOp>(hasNoLogic);
    target.markUnknownOpDynamicallyLegal(hasNoLogic);
  };
  ConversionTarget packedTarget(*context);
  configurePackedTarget(packedTarget, packedConverter);

  SmallVector<SmallVector<Operation *>> functionChunks;
  constexpr size_t functionsPerChunk = 64;
  module.walk([&](sim::SimFuncOp function) {
    if (functionChunks.empty() ||
        functionChunks.back().size() == functionsPerChunk)
      functionChunks.emplace_back();
    functionChunks.back().push_back(function);
  });
  if (failed(failableParallelForEach(
          context, functionChunks, [&](ArrayRef<Operation *> functions) {
            SimulationToStandardTypeConverter workerConverter;
            configurePackedConverter(workerConverter);
            RewritePatternSet workerPatterns(context);
            populatePackedPatterns(workerConverter, workerPatterns);
            FrozenRewritePatternSet workerFrozen(std::move(workerPatterns));
            ConversionTarget workerTarget(*context);
            configurePackedTarget(workerTarget, workerConverter);
            return applyFullConversion(functions, workerTarget, workerFrozen);
          })))
    return failure();
  markTiming("parallel function conversion");
  SmallVector<Operation *> nonFunctionRoots;
  for (Operation &operation : *module.getBody()) {
    if (auto design = dyn_cast<sim::SimDesignOp>(operation)) {
      for (Operation &nested : design.getBody().front())
        if (!isa<sim::SimFuncOp>(nested))
          nonFunctionRoots.push_back(&nested);
      continue;
    }
    if (!isa<FunctionOpInterface>(operation))
      nonFunctionRoots.push_back(&operation);
  }
  if (failed(applyFullConversion(nonFunctionRoots, packedTarget,
                                 std::move(packedPatterns))))
    return failure();
  if (failed(materializeDPIThunks(module)))
    return failure();
  markTiming("non-function conversion and DPI thunks");

  // Region signature conversion records the physical unknown-plane block
  // arguments that the whole-design proof made redundant. Replace them only
  // after dialect conversion has finished remapping every original logic use;
  // doing this inside the signature pattern would not update future one-to-N
  // operand adaptors owned by the conversion driver.
  WalkResult specializedBlockArguments =
      module.walk([&](sim::SimFuncOp function) {
        auto mappings =
            function->getAttrOfType<ArrayAttr>(nativeTwoStateBlockUnknownsAttr);
        if (!mappings)
          return WalkResult::advance();
        if (mappings.size() != function.getBody().getBlocks().size()) {
          function.emitOpError(
              "has invalid native two-state block-argument metadata");
          return WalkResult::interrupt();
        }
        OpBuilder builder(context);
        for (auto [block, mapping] :
             llvm::zip_equal(function.getBody(), mappings)) {
          auto indices = dyn_cast<DenseI64ArrayAttr>(mapping);
          if (!indices) {
            function.emitOpError(
                "has malformed native two-state block-argument metadata");
            return WalkResult::interrupt();
          }
          builder.setInsertionPointToStart(&block);
          for (int64_t index : indices.asArrayRef()) {
            if (index < 0 ||
                static_cast<uint64_t>(index) >= block.getNumArguments()) {
              function.emitOpError(
                  "has out-of-range native two-state block argument");
              return WalkResult::interrupt();
            }
            BlockArgument argument =
                block.getArgument(static_cast<unsigned>(index));
            auto type = dyn_cast<IntegerType>(argument.getType());
            if (!type) {
              function.emitOpError(
                  "has non-integer native two-state unknown plane");
              return WalkResult::interrupt();
            }
            Value zero = arith::ConstantOp::create(
                builder, function.getLoc(), type,
                builder.getIntegerAttr(type, APInt::getZero(type.getWidth())));
            argument.replaceAllUsesWith(zero);
          }
        }
        function->removeAttr(nativeTwoStateBlockUnknownsAttr);
        return WalkResult::advance();
      });
  if (specializedBlockArguments.wasInterrupted())
    return failure();
  markTiming("two-state block specialization");
  if (failed(threadRuntimeStatuses(module)))
    return failure();
  markTiming("runtime status threading");
  if (failed(releaseNativeAutomaticState(module, referenceArguments)))
    return failure();
  markTiming("automatic-state release");
  if (failed(validateRuntimeToLLVMPreconditions(module, dataLayout)))
    return failure();
  markTiming("runtime precondition validation");
  return success();
}

} // namespace obelisk::detail
