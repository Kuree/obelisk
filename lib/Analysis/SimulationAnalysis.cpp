//===- SimulationAnalysis.cpp - Shared simulation optimization facts -----===//

#include "obelisk/Analysis/SimulationAnalysis.h"

#include "mlir/Interfaces/ControlFlowInterfaces.h"

#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/MathExtras.h"

#include <algorithm>
#include <limits>

using namespace mlir;

namespace obelisk::analysis {
namespace {

std::optional<uint64_t> getPackedValueWidth(Type type) {
  return sim::getProvenanceSpan(type);
}

sim::ComputeResourceKind getHandleResourceKind(Type type) {
  if (isa<sim::RefType>(type))
    return sim::ComputeResourceKind::Storage;
  if (isa<sim::NetType, sim::DriverType>(type))
    return sim::ComputeResourceKind::Net;
  if (isa<sim::EventType>(type))
    return sim::ComputeResourceKind::Event;
  return sim::ComputeResourceKind::Unknown;
}

DescriptorProvenance joinProvenance(DescriptorProvenance lhs,
                                    const DescriptorProvenance &rhs) {
  if (lhs.resource != rhs.resource || lhs.descriptor != rhs.descriptor ||
      lhs.formal != rhs.formal)
    return {};
  uint64_t rootWidth = std::max(lhs.rootWidth, rhs.rootWidth);
  auto checkedEnd = [&](uint64_t low, uint64_t width) {
    return low <= rootWidth && width <= rootWidth - low
               ? std::optional<uint64_t>(low + width)
               : std::nullopt;
  };
  std::optional<uint64_t> lhsEnd = checkedEnd(lhs.low, lhs.width);
  std::optional<uint64_t> rhsEnd = checkedEnd(rhs.low, rhs.width);
  if (!lhsEnd || !rhsEnd) {
    lhs.low = 0;
    lhs.width = rootWidth;
    lhs.rootWidth = rootWidth;
    lhs.dynamic = true;
    return lhs;
  }
  uint64_t low = std::min(lhs.low, rhs.low);
  uint64_t end = std::max(*lhsEnd, *rhsEnd);
  lhs.low = low;
  lhs.width = end - low;
  lhs.rootWidth = rootWidth;
  lhs.dynamic |= rhs.dynamic;
  return lhs;
}

DescriptorProvenance widenDynamic(DescriptorProvenance provenance) {
  if (provenance.resource != sim::ComputeResourceKind::Unknown)
    provenance.dynamic = true;
  return provenance;
}

DescriptorProvenance narrowProvenance(DescriptorProvenance provenance,
                                      uint64_t low, uint64_t width) {
  if (provenance.resource == sim::ComputeResourceKind::Unknown)
    return provenance;
  if (provenance.low > provenance.rootWidth ||
      low > provenance.rootWidth - provenance.low ||
      width > provenance.rootWidth - provenance.low - low)
    return widenDynamic(provenance);
  provenance.low += low;
  provenance.width = width;
  return provenance;
}

void setIfChanged(DescriptorProvenanceMap &map, Value value,
                  DescriptorProvenance provenance, bool &changed) {
  auto found = map.find(value);
  if (found == map.end()) {
    map.try_emplace(value, provenance);
    changed = true;
    return;
  }
  DescriptorProvenance joined = joinProvenance(found->second, provenance);
  if (!(joined == found->second)) {
    found->second = joined;
    changed = true;
  }
}

template <typename Callback>
void forEachIncoming(Block &block, Callback &&callback) {
  for (Block *predecessor : block.getPredecessors()) {
    Operation *terminator = predecessor->getTerminator();
    auto branch = dyn_cast<BranchOpInterface>(terminator);
    if (!branch) {
      callback(nullptr, OperandRange(nullptr, 0));
      continue;
    }
    for (unsigned successor = 0; successor != terminator->getNumSuccessors();
         ++successor) {
      if (terminator->getSuccessor(successor) != &block)
        continue;
      callback(terminator,
               branch.getSuccessorOperands(successor).getForwardedOperands());
    }
  }
}

} // namespace

std::optional<unsigned> getSimulationStorageBitWidth(Type type) {
  if (isa<sim::CovergroupHandleType>(type) || sim::isManagedHandleType(type))
    return 64;
  if (std::optional<unsigned> packed = sim::getPackedWidth(type))
    return packed;
  std::optional<uint64_t> span = sim::getProvenanceSpan(type);
  if (auto unionType = dyn_cast<sim::UnpackedUnionType>(type);
      unionType && unionType.getIsTagged() && span) {
    uint64_t tagBits = llvm::Log2_64_Ceil(
        static_cast<uint64_t>(sim::getAggregateNumElements(type)) + 1);
    if (tagBits > std::numeric_limits<uint64_t>::max() - *span)
      return std::nullopt;
    *span += tagBits;
  }
  if (!span || *span == 0 || *span > std::numeric_limits<unsigned>::max())
    return std::nullopt;
  return static_cast<unsigned>(*span);
}

bool containsFourStateLogic(Type type) {
  if (sim::isManagedHandleType(type))
    return false;
  bool result = false;
  type.walk([&](sim::LogicType) { result = true; });
  return result;
}

DescriptorProvenanceMap deriveDescriptorProvenance(sim::SimFuncOp function) {
  DescriptorProvenanceMap provenanceMap;
  if (function.isExternal() || function.getBody().empty())
    return provenanceMap;

  DenseMap<uint64_t, uint64_t> driverNets;
  if (auto design = function->getParentOfType<sim::SimDesignOp>())
    for (sim::SimDriverDeclOp driver :
         design.getBody().front().getOps<sim::SimDriverDeclOp>())
      driverNets[driver.getId()] = driver.getNetId();

  Block &entry = function.getBody().front();
  for (BlockArgument argument : entry.getArguments()) {
    unsigned index = argument.getArgNumber();
    auto capture = function.getArgAttrOfType<sim::CaptureKindAttr>(
        index, sim::metadata::captureKind);
    auto descriptor = function.getArgAttrOfType<IntegerAttr>(
        index, sim::metadata::descriptorId);
    auto descriptorLow = function.getArgAttrOfType<IntegerAttr>(
        index, sim::metadata::descriptorLow);
    auto descriptorRootType = function.getArgAttrOfType<TypeAttr>(
        index, sim::metadata::descriptorRootType);
    sim::ComputeResourceKind kind = getHandleResourceKind(argument.getType());
    auto width = getPackedValueWidth(argument.getType());
    if (kind == sim::ComputeResourceKind::Unknown || !width)
      continue;
    DescriptorProvenance provenance;
    provenance.resource = kind;
    provenance.width = *width;
    provenance.rootWidth = *width;
    if (descriptorRootType) {
      std::optional<uint64_t> rootWidth =
          sim::getProvenanceSpan(descriptorRootType.getValue());
      if (!rootWidth || !descriptorLow || descriptorLow.getValue().isNegative())
        continue;
      provenance.rootWidth = *rootWidth;
      provenance.low = descriptorLow.getValue().getZExtValue();
      if (provenance.low > provenance.rootWidth ||
          provenance.width > provenance.rootWidth - provenance.low)
        continue;
    }
    // Observer handle captures are serialized as ordinary captured values in
    // the execution ABI, but remain parametric from the compute graph's point
    // of view: observer.bind supplies the concrete handle at each wait site.
    // Preserve a formal provenance here so evaluator effects can be
    // substituted into the waiting process without manufacturing a concrete
    // resource that has neither a descriptor nor a formal target.
    bool observerHandleCapture =
        function.getEntryKind() == sim::EntryKind::Observer && index != 0 &&
        capture && capture.getValue() == sim::CaptureKind::Value;
    if (capture && (capture.getValue() == sim::CaptureKind::Formal ||
                    observerHandleCapture)) {
      provenance.formal = index;
    } else if (descriptor) {
      if (!descriptor.getValue().isNegative() &&
          descriptor.getValue().getBitWidth() <= 64)
        provenance.descriptor = descriptor.getValue().getZExtValue();
      else
        provenance = {};
      if (isa<sim::DriverType>(argument.getType())) {
        auto net = provenance.descriptor
                       ? driverNets.find(*provenance.descriptor)
                       : driverNets.end();
        if (net == driverNets.end()) {
          provenance.resource = sim::ComputeResourceKind::Unknown;
          provenance.descriptor.reset();
        } else {
          provenance.descriptor = net->second;
        }
      }
    }
    provenanceMap[argument] = provenance;
  }

  while (true) {
    bool changed = false;
    for (Block &block : function.getBody()) {
      if (&block != &entry) {
        for (BlockArgument argument : block.getArguments()) {
          if (getHandleResourceKind(argument.getType()) ==
              sim::ComputeResourceKind::Unknown)
            continue;
          bool allIncomingReady = true;
          bool hasIndependentIncoming = false;
          std::optional<DescriptorProvenance> joined;
          forEachIncoming(block, [&](Operation *terminator,
                                     OperandRange forwarded) {
            if (!terminator || argument.getArgNumber() >= forwarded.size()) {
              allIncomingReady = false;
              return;
            }
            Value incoming = forwarded[argument.getArgNumber()];
            if (incoming == argument)
              return;
            hasIndependentIncoming = true;
            auto provenance = provenanceMap.find(incoming);
            if (provenance == provenanceMap.end()) {
              allIncomingReady = false;
              return;
            }
            joined = joined ? joinProvenance(*joined, provenance->second)
                            : provenance->second;
          });
          if (allIncomingReady && hasIndependentIncoming && joined)
            setIfChanged(provenanceMap, argument, *joined, changed);
        }
      }

      for (Operation &operation : block) {
        auto declare = [&](Value result, sim::ComputeResourceKind resource,
                           std::optional<uint64_t> descriptor) {
          auto width = getPackedValueWidth(result.getType());
          if (!width)
            return;
          DescriptorProvenance provenance;
          provenance.resource = resource;
          provenance.descriptor = descriptor;
          provenance.width = *width;
          provenance.rootWidth = *width;
          setIfChanged(provenanceMap, result, provenance, changed);
        };
        auto forward = [&](Value input, Value result,
                           std::optional<uint64_t> low) {
          auto found = provenanceMap.find(input);
          auto width = getPackedValueWidth(result.getType());
          if (found == provenanceMap.end() || !width)
            return;
          setIfChanged(provenanceMap, result,
                       low ? narrowProvenance(found->second, *low, *width)
                           : widenDynamic(found->second),
                       changed);
        };
        auto forwardSubelement = [&](Value input, Value result,
                                     ArrayRef<int64_t> indices) {
          auto found = provenanceMap.find(input);
          if (found == provenanceMap.end())
            return;
          Type current = input.getType();
          if (auto reference = dyn_cast<sim::RefType>(current))
            current = reference.getElementType();
          else if (auto driver = dyn_cast<sim::DriverType>(current))
            current = driver.getElementType();
          uint64_t offset = 0;
          for (int64_t index : indices) {
            if (index < 0 || static_cast<uint64_t>(index) >
                                 std::numeric_limits<unsigned>::max()) {
              setIfChanged(provenanceMap, result, widenDynamic(found->second),
                           changed);
              return;
            }
            auto child = sim::getAggregateProvenanceSubelement(
                current, static_cast<unsigned>(index));
            if (!child ||
                child->first > std::numeric_limits<uint64_t>::max() - offset) {
              setIfChanged(provenanceMap, result, widenDynamic(found->second),
                           changed);
              return;
            }
            offset += child->first;
            current = sim::getAggregateElementType(
                current, static_cast<unsigned>(index));
          }
          std::optional<uint64_t> width = sim::getProvenanceSpan(current);
          setIfChanged(provenanceMap, result,
                       width ? narrowProvenance(found->second, offset, *width)
                             : widenDynamic(found->second),
                       changed);
        };
        llvm::TypeSwitch<Operation *>(&operation)
            .Case<sim::SimContextStorageOp>([&](auto op) {
              declare(op.getResult(), sim::ComputeResourceKind::Storage,
                      op.getId());
            })
            .Case<sim::SimContextNetOp>([&](auto op) {
              declare(op.getResult(), sim::ComputeResourceKind::Net,
                      op.getId());
            })
            .Case<sim::SimContextDriverOp>([&](auto op) {
              auto net = driverNets.find(op.getId());
              declare(op.getResult(),
                      net == driverNets.end()
                          ? sim::ComputeResourceKind::Unknown
                          : sim::ComputeResourceKind::Net,
                      net == driverNets.end()
                          ? std::optional<uint64_t>{}
                          : std::optional<uint64_t>{net->second});
            })
            .Case<sim::SimContextEventOp>([&](auto op) {
              declare(op.getResult(), sim::ComputeResourceKind::Event,
                      op.getId());
            })
            .Case<sim::SimRefAllocOp>([&](auto op) {
              declare(op.getResult(), sim::ComputeResourceKind::Local,
                      std::nullopt);
            })
            .Case<sim::SimRefExtractOp, sim::SimNetExtractOp,
                  sim::SimDriverExtractOp>([&](auto op) {
              forward(op.getInput(), op.getResult(), op.getLowBit());
            })
            .Case<sim::SimRefSubelementOp, sim::SimDriverSubelementOp>(
                [&](auto op) {
                  forwardSubelement(op.getInput(), op.getResult(),
                                    op.getIndices());
                })
            .Case<sim::SimRefDynExtractOp, sim::SimDriverDynExtractOp>(
                [&](auto op) {
                  forward(op.getInput(), op.getResult(), std::nullopt);
                })
            .Case<sim::SimRefArrayElementOp, sim::SimDriverArrayElementOp>(
                [&](auto op) {
                  forward(op.getInput(), op.getResult(), std::nullopt);
                })
            .Default([&](Operation *op) {
              for (Value result : op->getResults())
                if (getHandleResourceKind(result.getType()) !=
                    sim::ComputeResourceKind::Unknown)
                  setIfChanged(provenanceMap, result, DescriptorProvenance{},
                               changed);
            });
      }
    }
    if (!changed)
      break;
  }
  return provenanceMap;
}

uint64_t getSimulationOperationCost(Operation &operation) {
  if (operation.hasAttr("obelisk_sim.rematerialized") &&
      operation.hasTrait<OpTrait::ConstantLike>())
    return 0;
  if (isa<sim::SimRefLoadOp, sim::SimRefStoreOp, sim::SimNetReadOp,
          sim::SimDriverDriveOp, sim::SimNBAEnqueueOp,
          sim::SimManagedNBAEnqueueOp, sim::SimReferencePathNBAEnqueueOp>(
          operation))
    return 3;
  if (isa<sim::SimCallOp>(operation))
    return 5;
  if (isa<sim::SimSuspendDelayOp, sim::SimSuspendChangeOp,
          sim::SimSuspendEdgeOp, sim::SimSuspendEdgeIffOp,
          sim::SimSuspendLevelOp, sim::SimSuspendAnyOp, sim::SimSuspendEventOp,
          sim::SimSuspendForeverOp, sim::SimSuspendAwaitOp,
          sim::SimSuspendJoinOp>(operation))
    return 1;
  return operation.hasTrait<OpTrait::IsTerminator>() ? 0 : 1;
}

uint64_t getSimulationOperationCost(Operation *operation) {
  uint64_t cost = 0;
  operation->walk([&](Operation *nested) {
    if (nested != operation)
      cost += getSimulationOperationCost(*nested);
  });
  return cost;
}

uint64_t getSimulationRegionCost(Region &region) {
  uint64_t cost = 0;
  region.walk([&](Operation *operation) {
    cost += getSimulationOperationCost(*operation);
  });
  return cost;
}

} // namespace obelisk::analysis
