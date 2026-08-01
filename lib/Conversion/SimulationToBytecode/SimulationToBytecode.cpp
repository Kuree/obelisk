//===- SimulationToBytecode.cpp - Strict design-wide bytecode encoder -----===//

#include "obelisk/Conversion/SimulationToBytecode.h"

#include "BytecodeEncoder.h"
#include "BytecodeSerialization.h"
#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Analysis/SimulationProcessFrameAnalysis.h"
#include "obelisk/Analysis/SimulationVPIAnalysis.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Analysis/Liveness.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/Error.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_ENCODEOBELISKSIMTOBYTECODEPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace bytecode {
namespace {

constexpr uint32_t kExecutionHasBytecode = OBELISK_RT_EXECUTION_HAS_BYTECODE;
constexpr uint32_t kExecutionHasDatabase =
    OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE;
constexpr uint32_t kExecutionVPIRead = OBELISK_RT_EXECUTION_VPI_READ;
constexpr uint32_t kExecutionVPIWrite = OBELISK_RT_EXECUTION_VPI_WRITE;
constexpr uint32_t kExecutionRequireBytecode =
    OBELISK_RT_EXECUTION_REQUIRE_BYTECODE;
constexpr uint32_t kDatabaseProfileRead = OBELISK_RT_DESIGN_PROFILE_READ;
constexpr uint32_t kDatabaseProfileWrite = OBELISK_RT_DESIGN_PROFILE_WRITE;

} // namespace

Encoder::Encoder(sim::SimDesignOp design,
                 const SimulationBytecodeOptions &options,
                 const llvm::DataLayout &dataLayout)
    : design(design), options(options), dataLayout(dataLayout) {}

FailureOr<EncodedSimulationDesign> Encoder::encode() {
  if (failed(prepareClassLayouts()) ||
      failed(prepareStaticSpecializationSites()))
    return failure();
  FailureOr<StateLayout> builtState = buildStateLayout(design);
  if (failed(builtState))
    return failure();
  state = *builtState;
  if (failed(planTwoStateRegisters()) || failed(planFunctions()) ||
      failed(planScheduleRanks()) || failed(encodeFunctions()))
    return failure();
  EncodedSimulationDesign result;
  result.bytecode = serializeBytecode();
  if (result.bytecode.empty())
    return failure();
  uint32_t profile = getVPIProfile();
  if (profile == UINT32_MAX)
    return failure();
  if (profile != 0) {
    result.designDatabase = serializeDatabase(profile);
    if (result.designDatabase.empty())
      return failure();
  }
  result.stateBitCount = state.bits;
  result.executionFlags = kExecutionHasBytecode;
  if (options.requireBytecode)
    result.executionFlags |= kExecutionRequireBytecode;
  if (profile != 0) {
    result.executionFlags |= kExecutionHasDatabase | kExecutionVPIRead;
    if (profile & kDatabaseProfileWrite)
      result.executionFlags |= kExecutionVPIWrite;
  }
  for (FunctionPlan &plan : plans)
    result.functions.push_back({plan.function.getSymName().str(), plan.index,
                                plan.scratchSize, plan.scratchAlignment,
                                plan.twoStateLogicRegisters});
  return result;
}

uint32_t Encoder::reg(const FunctionPlan &plan, Value value) const {
  auto found = plan.registers.find(value);
  return found == plan.registers.end() ? kInvalidRegister : found->second;
}

uint32_t Encoder::temporary(FunctionPlan &plan, Type type) {
  FailureOr<Layout> layout = getLayout(type);
  if (failed(layout))
    return kInvalidRegister;
  layout->offset = llvm::alignTo(plan.scratchSize, uint64_t{8});
  plan.scratchSize = layout->offset + layout->size;
  plan.layouts.push_back(*layout);
  return plan.layouts.size() - 1;
}

uint32_t Encoder::temporaryLike(FunctionPlan &plan, Type type, Value model) {
  FailureOr<Layout> layout = getLayout(type);
  uint32_t modelRegister = reg(plan, model);
  if (failed(layout) || modelRegister == kInvalidRegister)
    return kInvalidRegister;
  if (isa<sim::LogicType>(type)) {
    layout->kind = plan.layouts[modelRegister].kind;
    layout->size = ((uint64_t{layout->width} + 63) / 64) *
                   (layout->kind == Logic ? 16 : 8);
    if (layout->kind == Bits)
      ++plan.twoStateLogicRegisters;
  }
  layout->offset = llvm::alignTo(plan.scratchSize, uint64_t{8});
  plan.scratchSize = layout->offset + layout->size;
  plan.layouts.push_back(*layout);
  return plan.layouts.size() - 1;
}

uint64_t Encoder::addConstant(const Layout &layout, const APInt &value,
                              const APInt *unknown) {
  uint64_t offset = constants.size();
  uint64_t limbs = (uint64_t{layout.width} + 63) / 64;
  auto appendPlane = [&](const APInt &plane) {
    for (uint64_t limb = 0; limb != limbs; ++limb) {
      unsigned bits = std::min<uint64_t>(64, layout.width - limb * 64);
      append64(constants, plane.extractBitsAsZExtValue(bits, limb * 64));
    }
  };
  appendPlane(value);
  if (layout.kind == Logic) {
    if (unknown)
      appendPlane(*unknown);
    else
      constants.resize(constants.size() + limbs * 8, 0);
  }
  return offset;
}

uint64_t Encoder::addZeroConstant(const Layout &layout) {
  if (auto found = zeroConstants.find(layout.size);
      found != zeroConstants.end())
    return found->second;
  uint64_t offset = constants.size();
  constants.resize(constants.size() + layout.size, 0);
  zeroConstants.try_emplace(layout.size, offset);
  return offset;
}

uint64_t Encoder::addRawConstant(ArrayRef<uint8_t> bytes) {
  uint64_t offset = constants.size();
  llvm::append_range(constants, bytes);
  return offset;
}

uint64_t Encoder::addBytesConstant(ArrayRef<uint8_t> bytes) {
  uint64_t dataOffset = constants.size();
  llvm::append_range(constants, bytes);
  uint64_t descriptorOffset = constants.size();
  append64(constants, dataOffset);
  append64(constants, bytes.size());
  return descriptorOffset;
}

uint32_t Encoder::addIntrinsicSignature(uint32_t id, uint32_t inputCount,
                                        uint32_t outputCount, uint32_t flags) {
  for (auto [index, signature] : llvm::enumerate(intrinsicSignatures))
    if (signature.id == id && signature.inputCount == inputCount &&
        signature.outputCount == outputCount && signature.flags == flags)
      return static_cast<uint32_t>(index);
  intrinsicSignatures.push_back({id, inputCount, outputCount, flags});
  return intrinsicSignatures.size() - 1;
}

LogicalResult Encoder::emitIntrinsicRegisters(FunctionPlan &plan, uint32_t id,
                                              ArrayRef<uint32_t> inputs,
                                              ArrayRef<uint32_t> outputs,
                                              uint32_t flags) {
  if (inputs.size() > UINT32_MAX || outputs.size() > UINT32_MAX ||
      operandMaps.size() > UINT32_MAX || intrinsicSites.size() > UINT32_MAX)
    return plan.function.emitOpError("bytecode intrinsic table is too large");
  uint32_t firstOperand = static_cast<uint32_t>(operandMaps.size());
  for (uint32_t input : inputs) {
    if (input == kInvalidRegister || input >= plan.layouts.size())
      return plan.function.emitOpError("bytecode intrinsic input is unmapped");
    operandMaps.push_back({0, input});
  }
  for (uint32_t output : outputs) {
    if (output == kInvalidRegister || output >= plan.layouts.size())
      return plan.function.emitOpError("bytecode intrinsic output is unmapped");
    operandMaps.push_back({output, 0});
  }
  uint32_t signature =
      addIntrinsicSignature(id, static_cast<uint32_t>(inputs.size()),
                            static_cast<uint32_t>(outputs.size()), flags);
  uint32_t site = intrinsicSites.size();
  intrinsicSites.push_back({signature, firstOperand,
                            static_cast<uint32_t>(inputs.size()),
                            static_cast<uint32_t>(outputs.size())});
  emit({Intrinsic, 0, 0, 0, 0, 0, 0, site});
  return success();
}

LogicalResult Encoder::emitIntrinsic(FunctionPlan &plan, uint32_t id,
                                     ArrayRef<Value> inputs,
                                     ArrayRef<Value> outputs, uint32_t flags) {
  SmallVector<uint32_t> inputRegisters, outputRegisters;
  llvm::transform(inputs, std::back_inserter(inputRegisters),
                  [&](Value value) { return reg(plan, value); });
  llvm::transform(outputs, std::back_inserter(outputRegisters),
                  [&](Value value) { return reg(plan, value); });
  return emitIntrinsicRegisters(plan, id, inputRegisters, outputRegisters,
                                flags);
}

uint32_t Encoder::emitBytesConstant(FunctionPlan &plan,
                                    ArrayRef<uint8_t> bytes) {
  uint32_t result =
      temporary(plan, sim::BytesType::get(plan.function.getContext()));
  if (result != kInvalidRegister)
    emit({Constant, 0, result, 0, 0, 0, 0, addBytesConstant(bytes)});
  return result;
}

uint32_t Encoder::emitU64Constant(FunctionPlan &plan, uint64_t value) {
  Type i64 = IntegerType::get(plan.function.getContext(), 64);
  uint32_t result = temporary(plan, i64);
  if (result != kInvalidRegister)
    emit({Constant, 0, result, 0, 0, 0, 0,
          addConstant(plan.layouts[result], APInt(64, value))});
  return result;
}

LogicalResult Encoder::encodeDisplay(FunctionPlan &plan, sim::SimDisplayOp op) {
  SmallVector<uint8_t> metadata;
  append32(metadata, 1);
  append32(metadata, op.getAppendNewline() ? 1 : 0);
  append32(metadata, op.getDefaultRadix());
  append32(metadata, op.getItemFlags().size());
  StringRef scope = op.getScope().value_or("");
  if (scope.empty())
    scope = plan.function.getSymName();
  StringRef library = op.getLibraryCell().value_or("");
  append64(metadata, scope.size());
  append64(metadata, library.size());
  append64(metadata, op.getTimeMultiplier().value_or(1));
  append32(metadata, static_cast<uint32_t>(op.getTimePrecision().value_or(0)));
  for (int32_t flag : op.getItemFlags())
    append32(metadata, static_cast<uint32_t>(flag));
  llvm::append_range(metadata, scope.bytes());
  llvm::append_range(metadata, library.bytes());
  uint32_t metadataRegister = emitBytesConstant(plan, metadata);
  if (metadataRegister == kInvalidRegister)
    return op.emitOpError("cannot allocate display metadata register");
  SmallVector<uint32_t> inputs{metadataRegister, reg(plan, op.getDescriptor())};
  for (Value item : op.getItems())
    inputs.push_back(reg(plan, item));
  return emitIntrinsicRegisters(plan, kIntrinsicDisplay, inputs, {});
}

uint64_t Encoder::emit(Instruction instruction) {
  instructions.push_back(instruction);
  return instructions.size() - 1;
}

std::pair<uint64_t, uint64_t> Encoder::addMap(FunctionPlan &destinationPlan,
                                              ValueRange destination,
                                              FunctionPlan &sourcePlan,
                                              ValueRange source) {
  uint64_t first = operandMaps.size();
  for (auto [destinationValue, sourceValue] :
       llvm::zip_equal(destination, source))
    operandMaps.push_back(
        {reg(destinationPlan, destinationValue), reg(sourcePlan, sourceValue)});
  return {first, destination.size()};
}

std::pair<uint64_t, uint64_t>
Encoder::addRegistersMap(ArrayRef<uint32_t> destinations,
                         FunctionPlan &sourcePlan, ValueRange source) {
  uint64_t first = operandMaps.size();
  for (auto [destination, sourceValue] : llvm::zip_equal(destinations, source))
    operandMaps.push_back({destination, reg(sourcePlan, sourceValue)});
  return {first, destinations.size()};
}

LogicalResult Encoder::encodeFunctions() {
  for (FunctionPlan &plan : plans) {
    plan.firstInstruction = instructions.size();
    for (Block &block : plan.function.getBody()) {
      plan.blockPCs[&block] = instructions.size();
      for (Operation &operation : block) {
        if (mayCollect(&operation)) {
          if (failed(emitAggregateManagedRoots(plan, &operation)))
            return failure();
          emitDeadManagedClears(plan, &operation);
        }
        if (failed(encodeOperation(plan, &operation)))
          return failure();
      }
    }
    for (auto [instruction, target] : plan.branches) {
      auto found = plan.blockPCs.find(target);
      if (found == plan.blockPCs.end())
        return plan.function.emitOpError("bytecode branch target is missing");
      instructions[instruction].immediate = found->second;
    }
    if (failed(emitContinuationEntries(plan)))
      return failure();
    plan.instructionCount = instructions.size() - plan.firstInstruction;
    plan.scratchSize = llvm::alignTo(plan.scratchSize, uint64_t{8});
  }
  return success();
}

bool Encoder::mayCollect(Operation *operation) {
  return isa<
      sim::SimClassAllocOp, sim::SimClassCopyOp, sim::SimWeakCreateOp,
      sim::SimReferencePathIndexOp, sim::SimReferencePathAssocOp,
      sim::SimContainerCreateLikeOp, sim::SimContainerCreateOp,
      sim::SimContainerCloneOp, sim::SimContainerWriteOp, sim::SimQueueInsertOp,
      sim::SimAssocCreateOp, sim::SimAssocWriteOp, sim::SimAssocSetDefaultOp,
      sim::SimAssocTraverseOp, sim::SimArgumentRefStoreOp,
      sim::SimReferencePathNBAEnqueueOp, sim::SimGCSafepointOp,
      sim::SimStringLiteralOp, sim::SimStringFromPackedOp,
      sim::SimStringConcatOp, sim::SimStringRepeatOp, sim::SimStringPutcOp,
      sim::SimStringSubstrOp, sim::SimStringCaseConvertOp,
      sim::SimStringFormatIntegerOp, sim::SimStringFormatRealOp,
      sim::SimStringScanFieldOp,
      sim::SimFileGetlineStringOp, sim::SimFileErrorStringOp,
      sim::SimPlusargValueOp, sim::SimCallOp,
      sim::SimClassDirectCallOp,
      sim::SimClassVirtualCallOp, sim::SimDPICallOp>(operation);
}

LogicalResult Encoder::emitAggregateManagedRoots(FunctionPlan &plan,
                                                 Operation *operation) {
  const LivenessBlockInfo *blockInfo =
      plan.liveness->getLiveness(operation->getBlock());
  if (!blockInfo)
    return success();
  Liveness::ValueSetT live = blockInfo->currentlyLiveValues(operation);
  for (const auto &registerEntry : plan.registers) {
    Value value = registerEntry.first;
    uint32_t source = registerEntry.second;
    SmallVector<uint64_t, 2> offsets;
    if (!sim::getManagedHandleOffsets(value.getType(), offsets))
      return operation->emitError(
          "value has no fixed bytecode managed root layout");
    // Scalar managed registers and tagged string words are enumerated
    // directly from the live bytecode frame. Only aggregate payloads need
    // object-pointer shadows for their embedded managed slots.
    if (offsets.empty() || plan.layouts[source].kind == Managed ||
        plan.layouts[source].kind == String)
      continue;
    for (uint64_t bitOffset : offsets) {
      auto found = llvm::find_if(
          plan.managedRootShadows,
          [&](const FunctionPlan::ManagedRootShadow &shadow) {
            return shadow.value == value && shadow.bitOffset == bitOffset;
          });
      uint32_t shadow;
      if (found == plan.managedRootShadows.end()) {
        Layout layout;
        layout.kind = Managed;
        layout.width = 64;
        layout.size = 8;
        layout.offset = llvm::alignTo(plan.scratchSize, uint64_t{8});
        plan.scratchSize = layout.offset + layout.size;
        plan.layouts.push_back(layout);
        shadow = plan.layouts.size() - 1;
        plan.managedRootShadows.push_back({value, bitOffset, shadow});
      } else {
        shadow = found->reg;
      }
      if (!live.contains(value) || value.getDefiningOp() == operation) {
        emit({Constant, 0, shadow, 0, 0, 0, 0,
              addZeroConstant(plan.layouts[shadow])});
        continue;
      }
      if (failed(emitIntrinsicRegisters(
              plan, kIntrinsicManagedRootExtract,
              {source, emitU64Constant(plan, bitOffset)}, {shadow})))
        return failure();
    }
  }
  return success();
}

void Encoder::emitDeadManagedClears(FunctionPlan &plan, Operation *operation) {
  const LivenessBlockInfo *blockInfo =
      plan.liveness->getLiveness(operation->getBlock());
  if (!blockInfo)
    return;
  Liveness::ValueSetT live = blockInfo->currentlyLiveValues(operation);
  for (auto [value, reg] : plan.registers) {
    const Layout &layout = plan.layouts[reg];
    if (layout.kind != Managed && layout.kind != ManagedRef &&
        layout.kind != ArgumentRef)
      continue;
    if (live.contains(value) && value.getDefiningOp() != operation)
      continue;
    emit({Constant, 0, reg, 0, 0, 0, 0, addZeroConstant(layout)});
  }
}

LogicalResult Encoder::emitContinuationEntries(FunctionPlan &plan) {
  auto emitEntry = [&](uint32_t id, Block *block,
                       ArrayRef<ProcessFrameValue> slots) -> LogicalResult {
    uint64_t entryPC = instructions.size();
    auto restore = [&](ValueRange values,
                       ArrayRef<ProcessFrameValue> valueSlots,
                       bool consumeRoots) {
      if (values.size() != valueSlots.size())
        return failure();
      for (auto [argument, slot] : llvm::zip_equal(values, valueSlots)) {
        if (slot.valueOffset == UINT64_MAX) {
          uint32_t destination = reg(plan, argument);
          emit({Constant, 0, destination, 0, 0, 0, 0,
                addZeroConstant(plan.layouts[destination])});
          continue;
        }
        if (slot.storageSize > UINT32_MAX ||
            (slot.hasSecondaryStorage() && slot.storageSize > UINT32_MAX / 2))
          return failure();
        uint64_t transferSize =
            slot.storageSize * (slot.hasSecondaryStorage() ? 2 : 1);
        emitFrameTransfer(plan, LoadFrame, argument, slot.valueOffset,
                          static_cast<uint32_t>(transferSize));
        if (consumeRoots)
          for (uint64_t rootOffset : slot.managedRootOffsets)
            emit({ClearFrameRoot, 0, 0, 0, 0, 0, 0,
                  slot.valueOffset + rootOffset});
      }
      return success();
    };
    Block *functionEntry = &plan.function.getBody().front();
    // Scratch registers are intentionally cleared for every dispatch. Entry
    // captures are immutable SSA values and may remain live in any resumed
    // block even after suspension-live threading has made block operands
    // explicit, so reconstruct them at every non-entry continuation.
    if (id != 0 && failed(restore(functionEntry->getArguments(),
                                  plan.frame->getEntryCaptureLayout(),
                                  /*consumeRoots=*/false)))
      return plan.function.emitOpError(
          "canonical entry capture exceeds the bytecode ABI limit");
    if (failed(restore(block->getArguments(), slots,
                       /*consumeRoots=*/id != 0)))
      return plan.function.emitOpError(
          "canonical frame transfer exceeds the bytecode ABI limit");
    uint64_t jump = emit({Jump});
    instructions[jump].immediate = plan.blockPCs.lookup(block);
    auto rank = plan.blockScheduleRanks.find(block);
    if (rank == plan.blockScheduleRanks.end())
      return plan.function.emitOpError(
          "continuation block has no schedule rank");
    plan.continuations.push_back({plan.index, id, entryPC, rank->second});
    return success();
  };
  Block *entry = &plan.function.getBody().front();
  if (!plan.frame) {
    plan.continuations.push_back(
        {plan.index, 0, plan.blockPCs.lookup(entry), UINT32_MAX});
    return success();
  }
  if (failed(emitEntry(0, entry, plan.frame->getEntryCaptureLayout())))
    return failure();
  for (uint32_t id : plan.frame->getContinuations()) {
    if (id == 0)
      continue;
    Block *block = nullptr;
    for (const ProcessSuspension &suspension : plan.frame->getSuspensions())
      if (suspension.continuationID == id) {
        block = suspension.continuation;
        break;
      }
    if (!block)
      return plan.function.emitOpError("missing continuation block");
    if (failed(emitEntry(id, block, plan.frame->getContinuationLayout(id))))
      return failure();
  }
  llvm::sort(plan.continuations,
             [](const Continuation &left, const Continuation &right) {
               return left.id < right.id;
             });
  return success();
}

LogicalResult Encoder::emitConstant(FunctionPlan &plan, Value result,
                                    const APInt &value, const APInt *unknown) {
  uint32_t destination = reg(plan, result);
  Layout layout = plan.layouts[destination];
  emit({Constant, 0, destination, 0, 0, 0, 0,
        addConstant(layout, value, unknown)});
  return success();
}

uint32_t Encoder::getVPIProfile() {
  analysis::SimulationVPIAnalysis vpi =
      analysis::SimulationVPIAnalysis::compute(design);
  if (options.vpi != "auto") {
    std::optional<sim::ComputeVPIMode> mode =
        sim::symbolizeComputeVPIMode(options.vpi);
    if (!mode) {
      design.emitOpError(
          "bytecode VPI profile must be auto, off, read, or full");
      return UINT32_MAX;
    }
    vpi = analysis::SimulationVPIAnalysis::forMode(*mode);
  }
  uint32_t profile = 0;
  if (vpi.allowsRead())
    profile |= kDatabaseProfileRead;
  if (vpi.allowsWrite())
    profile |= kDatabaseProfileWrite;
  return profile;
}

SmallVector<uint8_t> Encoder::serializeBytecode() {
  return serializeBytecodeImage(plans, instructions, operandMaps, constants,
                                intrinsicSignatures, intrinsicSites,
                                captureRecords, state);
}

SmallVector<uint8_t> Encoder::serializeDatabase(uint32_t profile) {
  return serializeDesignDatabase(design, profile, state.storageOffsets,
                                 state.netOffsets, state.driverOffsets);
}

} // namespace bytecode

namespace {

class EncodeObeliskSimToBytecodePass final
    : public impl::EncodeObeliskSimToBytecodePassBase<
          EncodeObeliskSimToBytecodePass> {
public:
  using Base =
      impl::EncodeObeliskSimToBytecodePassBase<EncodeObeliskSimToBytecodePass>;
  using Base::Base;

  void runOnOperation() override {
    ModuleOp module = getOperation();
    auto layoutAttr = module->getAttrOfType<StringAttr>("llvm.data_layout");
    if (!layoutAttr) {
      module.emitError(
          "bytecode encoding requires an explicit llvm.data_layout");
      return signalPassFailure();
    }
    llvm::Expected<llvm::DataLayout> parsed =
        llvm::DataLayout::parse(layoutAttr.getValue());
    if (!parsed) {
      module.emitError() << "invalid LLVM data layout: "
                         << llvm::toString(parsed.takeError());
      return signalPassFailure();
    }
    if (!parsed->isLittleEndian() || parsed->getPointerSizeInBits() != 64) {
      module.emitError(
          "bytecode encoding requires a 64-bit little-endian target");
      return signalPassFailure();
    }
    SmallVector<sim::SimDesignOp> designs;
    module.walk([&](sim::SimDesignOp design) { designs.push_back(design); });
    if (designs.size() != 1) {
      module.emitError(
          "bytecode encoding requires exactly one simulation design");
      return signalPassFailure();
    }
    SimulationBytecodeOptions options;
    options.vpi = vpi;
    options.requireBytecode = requireBytecode;
    FailureOr<EncodedSimulationDesign> encoded =
        encodeSimulationDesign(designs.front(), options);
    if (failed(encoded))
      return signalPassFailure();
    OpBuilder builder(module.getContext());
    auto asI8 = [](ArrayRef<uint8_t> bytes) {
      return ArrayRef<int8_t>(reinterpret_cast<const int8_t *>(bytes.data()),
                              bytes.size());
    };
    module->setAttr("obelisk.bytecode.image",
                    builder.getDenseI8ArrayAttr(asI8(encoded->bytecode)));
    module->setAttr("obelisk.execution.flags",
                    builder.getI32IntegerAttr(encoded->executionFlags));
    module->setAttr("obelisk.execution.state_bits",
                    builder.getI64IntegerAttr(encoded->stateBitCount));
    if (!encoded->designDatabase.empty())
      module->setAttr(
          "obelisk.design.database",
          builder.getDenseI8ArrayAttr(asI8(encoded->designDatabase)));
    else
      module->removeAttr("obelisk.design.database");
    llvm::StringMap<sim::SimFuncOp> functions;
    designs.front().walk([&](sim::SimFuncOp function) {
      functions[function.getSymName()] = function;
    });
    for (const SimulationBytecodeFunction &function : encoded->functions) {
      sim::SimFuncOp source = functions.lookup(function.symbol);
      source->setAttr("obelisk.bytecode.function",
                      builder.getI32IntegerAttr(function.index));
      source->setAttr("obelisk.bytecode.scratch_size",
                      builder.getI64IntegerAttr(function.scratchSize));
      source->setAttr("obelisk.bytecode.scratch_alignment",
                      builder.getI64IntegerAttr(function.scratchAlignment));
      source->setAttr(
          "obelisk.bytecode.two_state_logic_registers",
          builder.getI32IntegerAttr(function.twoStateLogicRegisters));
    }
  }
};

} // namespace

FailureOr<EncodedSimulationDesign>
encodeSimulationDesign(sim::SimDesignOp design,
                       const SimulationBytecodeOptions &options) {
  ModuleOp module = design->getParentOfType<ModuleOp>();
  if (!module)
    return design.emitOpError("requires a containing module");
  auto layoutAttr = module->getAttrOfType<StringAttr>("llvm.data_layout");
  if (!layoutAttr)
    return design.emitOpError("requires an explicit llvm.data_layout");
  llvm::Expected<llvm::DataLayout> parsed =
      llvm::DataLayout::parse(layoutAttr.getValue());
  if (!parsed) {
    design.emitOpError() << "invalid LLVM data layout: "
                         << llvm::toString(parsed.takeError());
    return failure();
  }
  if (!parsed->isLittleEndian() || parsed->getPointerSizeInBits() != 64)
    return design.emitOpError(
        "bytecode encoding requires a 64-bit little-endian target");
  bytecode::Encoder encoder(design, options, *parsed);
  return encoder.encode();
}

} // namespace obelisk
