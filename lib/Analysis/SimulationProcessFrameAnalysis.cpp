//===- SimulationProcessFrameAnalysis.cpp - Process frame facts --------===//

#include "obelisk/Analysis/SimulationProcessFrameAnalysis.h"

#include "obelisk/Analysis/SimulationStorageAnalysis.h"
#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHash.h"

#include "mlir/IR/BuiltinTypes.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/MathExtras.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <set>

using namespace mlir;

namespace obelisk {
namespace {

constexpr uint64_t kNoOffset = std::numeric_limits<uint64_t>::max();
constexpr uint64_t kWaitHeaderSize = sizeof(obelisk_rt_wait_record_v1);
constexpr uint64_t kWaitEntrySize = sizeof(obelisk_rt_wait_entry_v1);
constexpr uint64_t kWaitAlignment = alignof(obelisk_rt_wait_record_v1);
constexpr uint32_t kFrameChecksumReserved = 0;

bool alignUp(uint64_t value, uint64_t alignment, uint64_t &result) {
  if (value > std::numeric_limits<uint64_t>::max() - (alignment - 1))
    return false;
  result = llvm::alignTo(value, alignment);
  return true;
}

uint64_t appendHash(uint64_t hash, uint64_t value, unsigned bytes) {
  return obelisk_stable_hash_append_uint_le(hash, value, bytes);
}

FailureOr<uint64_t> getComputedWaitSize(sim::SimSuspendObserveOp operation) {
  uint64_t primaryCount = operation.getEdges().size();
  uint64_t conditionCount = operation.getConditionCount();
  uint64_t observerCount = primaryCount + conditionCount;
  uint64_t captureCount = 0;
  uint64_t dependencyCount = 0;
  uint64_t previousLimbs = 0;
  SmallVector<Value> observers(operation.getPrimaries());
  llvm::append_range(observers, operation.getConditions());
  if (observers.size() != observerCount)
    return failure();
  for (auto [index, value] : llvm::enumerate(observers)) {
    auto binding = value.getDefiningOp<sim::SimObserverBindOp>();
    if (!binding)
      return failure();
    captureCount += binding.getCaptures().size();
    dependencyCount += binding.getDependencies().size();
    if (index < primaryCount) {
      auto observerType = dyn_cast<sim::ObserverType>(value.getType());
      std::optional<unsigned> width =
          observerType ? isa<FloatType>(observerType.getResultType())
                             ? std::optional<unsigned>(
                                   cast<FloatType>(observerType.getResultType())
                                       .getWidth())
                             : sim::getPackedWidth(observerType.getResultType())
                       : std::nullopt;
      if (!width || *width == 0)
        return failure();
      previousLimbs += (*width + 63) / 64;
    }
  }
  auto addProduct = [](uint64_t &size, uint64_t count,
                       uint64_t stride) -> bool {
    if (count > (std::numeric_limits<uint64_t>::max() - size) / stride)
      return false;
    size += count * stride;
    return true;
  };
  uint64_t size = sizeof(obelisk_rt_computed_wait_record_v1);
  if (!addProduct(size, observerCount,
                  sizeof(obelisk_rt_computed_observer_v1)) ||
      !addProduct(size, captureCount, sizeof(obelisk_rt_computed_capture_v1)) ||
      !addProduct(size, dependencyCount,
                  sizeof(obelisk_rt_computed_dependency_v1)) ||
      !addProduct(size, primaryCount, sizeof(obelisk_rt_computed_clause_v1)) ||
      !addProduct(size, previousLimbs, sizeof(uint64_t) * 2))
    return failure();
  return size;
}

} // namespace

FailureOr<std::unique_ptr<SimulationProcessFrameAnalysis>>
SimulationProcessFrameAnalysis::create(sim::SimFuncOp function,
                                       const llvm::DataLayout &dataLayout) {
  if (function.isExternal() || function.getBody().empty()) {
    function.emitError("canonical process frame requires a definition");
    return failure();
  }
  auto result = std::make_unique<SimulationProcessFrameAnalysis>();
  // Do not leave cached layouts for types owned by this temporary context in
  // a DataLayout that is subsequently reused by another analysis.
  llvm::DataLayout analysisLayout(dataLayout.getStringRepresentation());
  llvm::LLVMContext llvmContext;
  uint64_t cursor = 0;
  auto allocate =
      [&](Type type, ProcessFrameFieldKind kind,
          SmallVectorImpl<ProcessFrameValue> &values) -> LogicalResult {
    FailureOr<analysis::SimulationStorageProperties> storage =
        analysis::getSimulationStorageProperties(type, analysisLayout,
                                                 llvmContext);
    if (failed(storage)) {
      function.emitError() << "cannot place type " << type
                           << " in the canonical process frame";
      return failure();
    }
    uint64_t valueOffset;
    if (!alignUp(cursor, storage->alignment, valueOffset))
      return function.emitError("canonical process frame offset overflow");
    if (valueOffset > std::numeric_limits<uint64_t>::max() - storage->size)
      return function.emitError("canonical process frame size overflow");
    uint64_t end = valueOffset + storage->size;
    uint64_t unknownOffset = kNoOffset;
    uint64_t auxiliaryOffset = kNoOffset;
    if (storage->managedRootOffsets.empty()) {
      result->fields.push_back(
          {kind,
           storage->fourState ? ProcessFrameFieldFlags::FourStateValue
                              : ProcessFrameFieldFlags::None,
           valueOffset, storage->size, storage->alignment});
    } else {
      for (uint64_t rootOffset : storage->managedRootOffsets)
        result->fields.push_back({kind, ProcessFrameFieldFlags::ManagedRoot,
                                  valueOffset + rootOffset,
                                  storage->managedRootSize,
                                  storage->managedRootAlignment});
    }
    if (storage->managedReference) {
      if (!alignUp(end, storage->alignment, auxiliaryOffset) ||
          auxiliaryOffset >
              std::numeric_limits<uint64_t>::max() - storage->size)
        return function.emitError("canonical process frame offset overflow");
      end = auxiliaryOffset + storage->size;
      result->fields.push_back({kind, ProcessFrameFieldFlags::None,
                                auxiliaryOffset, storage->size,
                                storage->alignment});
    } else if (storage->fourState) {
      // The transferred plane contains only the value's store bytes, but a
      // following typed LLVM load/store still requires its ABI alignment.
      if (!alignUp(end, storage->alignment, unknownOffset) ||
          unknownOffset >
              std::numeric_limits<uint64_t>::max() - storage->size)
        return function.emitError("canonical process frame size overflow");
      end = unknownOffset + storage->size;
      result->fields.push_back({kind, ProcessFrameFieldFlags::FourStateUnknown,
                                unknownOffset, storage->size,
                                storage->alignment});
    }
    cursor = end;
    result->frameAlignment =
        std::max<uint64_t>(result->frameAlignment, storage->alignment);
    values.push_back({valueOffset, unknownOffset, storage->size,
                      storage->alignment, auxiliaryOffset,
                      storage->managedRootOffsets});
    return success();
  };

  Block &entry = function.getBody().front();
  for (auto [index, argument] : llvm::enumerate(entry.getArguments())) {
    if (index == 0 && isa<sim::ContextType>(argument.getType())) {
      result->entryCaptureLayout.push_back({kNoOffset, kNoOffset, 0, 1});
      continue;
    }
    if (failed(allocate(argument.getType(), ProcessFrameFieldKind::Capture,
                        result->entryCaptureLayout)))
      return failure();
  }

  SmallVector<Operation *> suspensionOps;
  function.walk([&](Operation *operation) {
    if (sim::isSuspensionOp(operation))
      suspensionOps.push_back(operation);
  });
  llvm::SetVector<Block *> continuationBlocks;
  for (Operation *operation : suspensionOps)
    continuationBlocks.insert(operation->getSuccessor(0));

  // Exactly one semantic continuation is resident while a serial process is
  // suspended. Greedily color successor arguments into compatible liveness
  // lanes. A lane may be reused by distinct continuation targets, but managed
  // roots and managed references must never alias ordinary bits.
  struct ContinuationLane {
    uint64_t planeSize = 0;
    uint32_t alignment = 1;
    bool fourState = false;
    bool initialized = false;
    bool managedReference = false;
    SmallVector<uint64_t, 2> managedRootOffsets;
    uint64_t managedRootSize = 0;
    uint32_t managedRootAlignment = 1;
    uint64_t valueOffset = 0;
    uint64_t unknownOffset = kNoOffset;
    uint64_t auxiliaryOffset = kNoOffset;
  };
  SmallVector<ContinuationLane> lanes;
  DenseMap<Block *, SmallVector<unsigned>> continuationLaneAssignments;
  for (Block *block : continuationBlocks) {
    SmallVector<bool> used(lanes.size(), false);
    auto &assignments = continuationLaneAssignments[block];
    assignments.reserve(block->getNumArguments());
    for (BlockArgument argument : block->getArguments()) {
      FailureOr<analysis::SimulationStorageProperties> storage =
          analysis::getSimulationStorageProperties(argument.getType(),
                                                   analysisLayout, llvmContext);
      if (failed(storage)) {
        function.emitError() << "cannot place type " << argument.getType()
                             << " in the canonical process frame";
        return failure();
      }
      unsigned selected = lanes.size();
      for (auto [index, lane] : llvm::enumerate(lanes))
        if (!used[index] &&
            (!lane.initialized ||
             (lane.managedReference == storage->managedReference &&
              lane.managedRootOffsets == storage->managedRootOffsets))) {
          selected = index;
          break;
        }
      if (selected == lanes.size()) {
        lanes.emplace_back();
        used.push_back(false);
      }
      used[selected] = true;
      assignments.push_back(selected);
      ContinuationLane &lane = lanes[selected];
      lane.initialized = true;
      lane.managedReference = storage->managedReference;
      lane.managedRootOffsets = storage->managedRootOffsets;
      lane.managedRootSize = storage->managedRootSize;
      lane.managedRootAlignment = storage->managedRootAlignment;
      lane.planeSize = std::max(lane.planeSize, storage->size);
      lane.alignment = std::max(lane.alignment, storage->alignment);
      lane.fourState |= storage->fourState;
    }
  }
  for (ContinuationLane &lane : lanes) {
    if (!alignUp(lane.planeSize, lane.alignment, lane.planeSize) ||
        !alignUp(cursor, lane.alignment, lane.valueOffset) ||
        lane.valueOffset >
            std::numeric_limits<uint64_t>::max() - lane.planeSize)
      return function.emitError("canonical process frame size overflow");
    uint64_t end = lane.valueOffset + lane.planeSize;
    if (lane.managedRootOffsets.empty()) {
      result->fields.push_back(
          {ProcessFrameFieldKind::Continuation,
           lane.fourState ? ProcessFrameFieldFlags::FourStateValue
                          : ProcessFrameFieldFlags::None,
           lane.valueOffset, lane.planeSize, lane.alignment});
    } else {
      for (uint64_t rootOffset : lane.managedRootOffsets)
        result->fields.push_back(
            {ProcessFrameFieldKind::Continuation,
             ProcessFrameFieldFlags::ManagedRoot, lane.valueOffset + rootOffset,
             lane.managedRootSize, lane.managedRootAlignment});
    }
    if (lane.managedReference) {
      lane.auxiliaryOffset = end;
      if (end > std::numeric_limits<uint64_t>::max() - lane.planeSize)
        return function.emitError("canonical process frame size overflow");
      end += lane.planeSize;
      result->fields.push_back(
          {ProcessFrameFieldKind::Continuation, ProcessFrameFieldFlags::None,
           lane.auxiliaryOffset, lane.planeSize, lane.alignment});
    } else if (lane.fourState) {
      lane.unknownOffset = end;
      if (end > std::numeric_limits<uint64_t>::max() - lane.planeSize)
        return function.emitError("canonical process frame size overflow");
      end += lane.planeSize;
      result->fields.push_back({ProcessFrameFieldKind::Continuation,
                                ProcessFrameFieldFlags::FourStateUnknown,
                                lane.unknownOffset, lane.planeSize,
                                lane.alignment});
    }
    cursor = end;
    result->frameAlignment =
        std::max<uint64_t>(result->frameAlignment, lane.alignment);
  }
  for (Block *block : continuationBlocks) {
    auto &layout = result->continuationLayouts[block];
    auto assignmentsIt = continuationLaneAssignments.find(block);
    if (assignmentsIt == continuationLaneAssignments.end())
      return function.emitError("continuation lane assignment is missing");
    ArrayRef<unsigned> assignments = assignmentsIt->second;
    if (assignments.size() != block->getNumArguments())
      return function.emitError("continuation lane assignment is incomplete");
    for (auto [index, argument] : llvm::enumerate(block->getArguments())) {
      FailureOr<analysis::SimulationStorageProperties> storage =
          analysis::getSimulationStorageProperties(argument.getType(),
                                                   analysisLayout, llvmContext);
      if (failed(storage))
        return failure();
      const ContinuationLane &lane = lanes[assignments[index]];
      layout.push_back(
          {lane.valueOffset,
           storage->fourState ? lane.unknownOffset : kNoOffset, storage->size,
           storage->alignment,
           storage->managedReference ? lane.auxiliaryOffset : kNoOffset,
           storage->managedRootOffsets});
    }
  }

  uint64_t nextID = 1;
  // LLVM's DenseSet reserves the two largest unsigned values as sentinels,
  // but both are valid continuation IDs in the runtime ABI.
  std::set<uint32_t> usedIDs;
  DenseMap<uint32_t, Block *> continuationTargets;
  uint64_t maxWaitSize = kWaitHeaderSize;
  for (Operation *operation : suspensionOps) {
    if (auto site =
            operation->getAttrOfType<sim::ContinuationSiteAttr>("site")) {
      if (site.getId() == 0) {
        operation->emitError("requires a nonzero continuation ID");
        return failure();
      }
      auto [target, inserted] = continuationTargets.try_emplace(
          site.getId(), operation->getSuccessor(0));
      if (!inserted && target->second != operation->getSuccessor(0)) {
        operation->emitError("continuation ID names multiple successor blocks");
        return failure();
      }
      usedIDs.insert(site.getId());
    }
    uint64_t waitSize;
    if (auto observe = dyn_cast<sim::SimSuspendObserveOp>(operation)) {
      FailureOr<uint64_t> computed = getComputedWaitSize(observe);
      if (failed(computed))
        return operation->emitError(
            "cannot size the computed observer wait record");
      waitSize = *computed;
    } else {
      uint64_t entries = sim::getWaitEntryCount(operation);
      if (entries > (std::numeric_limits<uint64_t>::max() - kWaitHeaderSize) /
                        kWaitEntrySize)
        return operation->emitError("wait record size overflow");
      waitSize = kWaitHeaderSize + entries * kWaitEntrySize;
    }
    maxWaitSize = std::max(maxWaitSize, waitSize);
  }
  uint64_t waitOffset = kNoOffset;
  if (!suspensionOps.empty()) {
    if (!alignUp(cursor, kWaitAlignment, waitOffset) ||
        waitOffset > std::numeric_limits<uint64_t>::max() - maxWaitSize)
      return function.emitError("canonical wait record offset overflow");
    result->fields.push_back(
        {ProcessFrameFieldKind::Wait, ProcessFrameFieldFlags::None, waitOffset,
         maxWaitSize, static_cast<uint32_t>(kWaitAlignment)});
    result->frameAlignment =
        std::max<uint64_t>(result->frameAlignment, kWaitAlignment);
    cursor = waitOffset + maxWaitSize;
  }

  for (Operation *operation : suspensionOps) {
    uint32_t id;
    if (auto site = operation->getAttrOfType<sim::ContinuationSiteAttr>("site"))
      id = site.getId();
    else {
      while (nextID <= std::numeric_limits<uint32_t>::max() &&
             usedIDs.count(static_cast<uint32_t>(nextID)) != 0)
        ++nextID;
      if (nextID > std::numeric_limits<uint32_t>::max())
        return operation->emitError("continuation ID space is exhausted");
      id = static_cast<uint32_t>(nextID++);
      usedIDs.insert(id);
    }
    result->suspensions.push_back(
        {operation, operation->getSuccessor(0), id, waitOffset, maxWaitSize});
    result->continuationLayoutsByID[id] =
        result->continuationLayouts.lookup(operation->getSuccessor(0));
  }
  result->continuations.push_back(0);
  for (uint32_t id : usedIDs)
    result->continuations.push_back(id);
  llvm::sort(result->continuations);
  if (result->frameAlignment > 4096 ||
      result->fields.size() > std::numeric_limits<uint32_t>::max() ||
      result->continuations.size() > std::numeric_limits<uint32_t>::max())
    return function.emitError(
        "canonical process frame exceeds the runtime ABI limits");
  if (!alignUp(cursor, result->frameAlignment, result->frameSize))
    return function.emitError("canonical process frame size overflow");

  uint64_t hash = OBELISK_STABLE_HASH_OFFSET_BASIS;
  hash = appendHash(hash, OBELISK_RT_VERSION, 4);
  hash = appendHash(hash, kFrameChecksumReserved, 4);
  hash = appendHash(hash, result->frameSize, 8);
  hash = appendHash(hash, result->frameAlignment, 8);
  hash = appendHash(hash, result->fields.size(), 4);
  hash = appendHash(hash, result->continuations.size(), 4);
  for (const ProcessFrameField &field : result->fields) {
    hash = appendHash(hash, static_cast<uint32_t>(field.kind), 4);
    hash = appendHash(hash, static_cast<uint32_t>(field.flags), 4);
    hash = appendHash(hash, field.offset, 8);
    hash = appendHash(hash, field.size, 8);
    hash = appendHash(hash, field.alignment, 4);
    hash = appendHash(hash, kFrameChecksumReserved, 4);
  }
  for (uint32_t continuation : result->continuations)
    hash = appendHash(hash, continuation, 4);
  result->checksum = hash;
  return result;
}

ArrayRef<ProcessFrameValue>
SimulationProcessFrameAnalysis::getContinuationLayout(Block *block) const {
  auto found = continuationLayouts.find(block);
  return found == continuationLayouts.end()
             ? ArrayRef<ProcessFrameValue>{}
             : ArrayRef<ProcessFrameValue>(found->second);
}

ArrayRef<ProcessFrameValue>
SimulationProcessFrameAnalysis::getContinuationLayout(
    uint32_t continuationID) const {
  auto found = continuationLayoutsByID.find(continuationID);
  return found == continuationLayoutsByID.end()
             ? ArrayRef<ProcessFrameValue>{}
             : ArrayRef<ProcessFrameValue>(found->second);
}

const ProcessSuspension *
SimulationProcessFrameAnalysis::getSuspension(Operation *operation) const {
  for (const ProcessSuspension &suspension : suspensions)
    if (suspension.operation == operation)
      return &suspension;
  return nullptr;
}

} // namespace obelisk
