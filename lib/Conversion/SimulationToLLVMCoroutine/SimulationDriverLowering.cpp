//===- SimulationDriverLowering.cpp - Native driver patterns ------------===//

#include "SimulationToLLVMCoroutinePrivate.h"
#include "obelisk/Analysis/ParallelWalk.h"
#include "obelisk/Dialect/Native/NativeDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleEnums.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHandle.h"
#include "obelisk/Runtime/StrengthResolution.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Matchers.h"
#include "mlir/Transforms/DialectConversion.h"

#include "llvm/ADT/STLExtras.h"

#include <algorithm>
#include <limits>
#include <type_traits>

using namespace mlir;

namespace obelisk::detail {
namespace {

uint64_t encodeNativeStaticHandle(uint32_t id, int32_t offset = 0) {
  return obelisk_rt_stable_handle_encode(OBELISK_RT_STABLE_HANDLE_STATIC, id,
                                         offset);
}

// Keep ordinary narrow driver resolution straight-line.  Wide port aliases
// are the case where expanding the identical one-driver proof once per bit
// dominates compile time and memory.
constexpr unsigned bulkConnectedDriverMinWidth = 65;

// A run of identically configured bits resolves through a truth table over
// every driver state combination: 4^n entries for n contributions per bit.
constexpr unsigned maximumRunContributions = 4;

using NetByID = DenseMap<uint64_t, const NativeStateLayout::Net *>;
using DriverByID = DenseMap<uint64_t, const NativeStateLayout::Driver *>;
using DriversByNet =
    DenseMap<uint64_t, SmallVector<const NativeStateLayout::Driver *, 1>>;
using ConnectedNets = DenseSet<uint64_t>;

struct DriverLookupIndex {
  explicit DriverLookupIndex(const NativeStateLayout &layout) {
    for (const NativeStateLayout::Net &net : layout.netLayouts)
      netByID.try_emplace(net.id, &net);
    for (const NativeStateLayout::Driver &driver : layout.driverLayouts)
      driverByID.try_emplace(driver.id, &driver);
    for (const NativeStateLayout::Driver &driver : layout.driverLayouts)
      driversByNet[driver.netId].push_back(&driver);
    for (const auto &entry : layout.connectivityCanonical)
      connectedNets.insert(entry.first.first);
    for (const auto &[net, drivers] : driversByNet)
      indexSegments(net, drivers);
  }

  /// The drivers whose driven range covers bit `offset` of `net`, in layout
  /// order.
  ArrayRef<const NativeStateLayout::Driver *>
  coveringDrivers(uint64_t net, uint64_t offset) const {
    auto found = segmentsByNet.find(net);
    if (found == segmentsByNet.end())
      return {};
    const auto &segments = found->second;
    auto next = llvm::upper_bound(segments, offset,
                                  [](uint64_t bit, const DriverSegment &segment) {
                                    return bit < segment.low;
                                  });
    if (next == segments.begin() || offset >= std::prev(next)->high)
      return {};
    return std::prev(next)->drivers;
  }

  NetByID netByID;
  DriverByID driverByID;
  DriversByNet driversByNet;
  ConnectedNets connectedNets;

private:
  /// A maximal range of a net's bits covered by the same drivers.
  struct DriverSegment {
    uint64_t low;
    uint64_t high;
    SmallVector<const NativeStateLayout::Driver *, 2> drivers;
  };
  static uint64_t drivenEnd(const NativeStateLayout::Driver &driver) {
    return std::min<uint64_t>(uint64_t{driver.drivenLow} + driver.drivenWidth,
                              driver.width);
  }
  // Sweep the driven ranges once, so finding a bit's drivers is a binary
  // search instead of a scan over every driver of the net.
  void indexSegments(uint64_t net,
                     ArrayRef<const NativeStateLayout::Driver *> drivers) {
    SmallVector<const NativeStateLayout::Driver *> starts;
    SmallVector<uint64_t> bounds;
    for (const NativeStateLayout::Driver *driver : drivers)
      if (driver->drivenLow < drivenEnd(*driver)) {
        starts.push_back(driver);
        bounds.push_back(driver->drivenLow);
        bounds.push_back(drivenEnd(*driver));
      }
    llvm::sort(bounds);
    bounds.erase(std::unique(bounds.begin(), bounds.end()), bounds.end());
    llvm::stable_sort(starts, [](const auto *lhs, const auto *rhs) {
      return lhs->drivenLow < rhs->drivenLow;
    });
    SmallVector<const NativeStateLayout::Driver *> active;
    size_t next = 0;
    auto &segments = segmentsByNet[net];
    for (size_t bound = 0; bound + 1 < bounds.size(); ++bound) {
      uint64_t low = bounds[bound];
      llvm::erase_if(active, [&](const NativeStateLayout::Driver *driver) {
        return drivenEnd(*driver) <= low;
      });
      for (; next < starts.size() && starts[next]->drivenLow <= low; ++next)
        active.push_back(starts[next]);
      if (active.empty())
        continue;
      DriverSegment segment{low, bounds[bound + 1], {}};
      segment.drivers.assign(active.begin(), active.end());
      llvm::sort(segment.drivers);
      segments.push_back(std::move(segment));
    }
  }

  DenseMap<uint64_t, SmallVector<DriverSegment, 1>> segmentsByNet;
};

std::optional<SmallVector<const NativeStateLayout::Net *, 2>>
getBulkConnectedDriverNets(
    const NativeStateLayout &layout, const NativeStateLayout::Driver &driver,
    unsigned width, const NetByID &netByID, const DriversByNet &driversByNet,
    unsigned minimumWidth = bulkConnectedDriverMinWidth) {
  if (width < minimumWidth || layout.hasPassSwitch || driver.drivenLow != 0 ||
      driver.drivenWidth != width || driver.width != width ||
      driver.strength0 != sim::Strength::Strong ||
      driver.strength1 != sim::Strength::Strong ||
      !layout.directHandles.contains(driver.handleID) ||
      layout.guardedHandles.contains(driver.handleID))
    return std::nullopt;

  SmallVector<const NativeStateLayout::Net *, 2> nets;
  for (unsigned bit = 0; bit != width; ++bit) {
    std::pair<uint64_t, uint64_t> logical{driver.netId, bit};
    auto canonical = layout.connectivityCanonical.find(logical);
    if (canonical == layout.connectivityCanonical.end())
      return std::nullopt;
    auto foundComponent = layout.connectivityComponents.find(canonical->second);
    if (foundComponent == layout.connectivityComponents.end() ||
        foundComponent->second.size() < 2)
      return std::nullopt;
    ArrayRef<analysis::NetBit> component = foundComponent->second;

    auto resolution = layout.connectivityResolutions.find(canonical->second);
    if (resolution == layout.connectivityResolutions.end() ||
        resolution->second != sim::NetResolutionKind::Wire)
      return std::nullopt;

    if (bit == 0) {
      for (const analysis::NetBit &member : component) {
        if (member.offset != 0 || llvm::any_of(nets, [&](const auto *net) {
              return net->id == member.net;
            }))
          return std::nullopt;
        auto foundNet = netByID.find(member.net);
        if (foundNet == netByID.end() || foundNet->second->width != width ||
            foundNet->second->resolution != sim::NetResolutionKind::Wire ||
            !layout.directHandles.contains(foundNet->second->handleID) ||
            layout.guardedHandles.contains(foundNet->second->handleID) ||
            llvm::any_of(foundNet->second->propagationDelays,
                         [](const auto &delay) { return delay.has_value(); }))
          return std::nullopt;
        const NativeStateLayout::Net *net = foundNet->second;
        nets.push_back(net);
      }
    } else {
      if (component.size() != nets.size())
        return std::nullopt;
      for (const NativeStateLayout::Net *net : nets)
        if (!llvm::is_contained(component, analysis::NetBit{net->id, bit}))
          return std::nullopt;
    }

    // The component must have exactly one effective source, namely this
    // driver.  This excludes multi-driver nets, strength-pair banks, and any
    // alias whose apparently simple shape hides a competing contribution.
    unsigned contributions = 0;
    for (const analysis::NetBit &member : component) {
      auto foundDrivers = driversByNet.find(member.net);
      if (foundDrivers == driversByNet.end())
        continue;
      for (const NativeStateLayout::Driver *candidate : foundDrivers->second)
        if (member.offset >= candidate->drivenLow &&
            member.offset - candidate->drivenLow < candidate->drivenWidth) {
          if (candidate->id != driver.id)
            return std::nullopt;
          ++contributions;
        }
    }
    if (contributions != 1)
      return std::nullopt;
  }
  return nets;
}

const NativeStateLayout::Net *getBulkCapturedIsolatedDriverNet(
    const NativeStateLayout &layout, const NativeStateLayout::Driver &driver,
    unsigned width, const NetByID &netByID, const DriversByNet &driversByNet,
    const ConnectedNets &connectedNets,
    unsigned minimumWidth = bulkConnectedDriverMinWidth) {
  if (!layout.isolatedSingleDriverNets.contains(driver.netId) ||
      width < minimumWidth || layout.hasPassSwitch || driver.drivenLow != 0 ||
      driver.drivenWidth != width || driver.width != width ||
      driver.strength0 != sim::Strength::Strong ||
      driver.strength1 != sim::Strength::Strong ||
      !layout.directHandles.contains(driver.handleID) ||
      layout.guardedHandles.contains(driver.handleID) ||
      connectedNets.contains(driver.netId))
    return nullptr;
  auto foundNet = netByID.find(driver.netId);
  auto foundDrivers = driversByNet.find(driver.netId);
  if (foundNet == netByID.end() || foundNet->second->width != width ||
      foundDrivers == driversByNet.end() || foundDrivers->second.size() != 1 ||
      foundNet->second->resolution != sim::NetResolutionKind::Wire ||
      !layout.directHandles.contains(foundNet->second->handleID) ||
      layout.guardedHandles.contains(foundNet->second->handleID) ||
      llvm::any_of(foundNet->second->propagationDelays,
                   [](const auto &delay) { return delay.has_value(); }))
    return nullptr;
  return foundNet->second;
}

std::optional<uint64_t> getStaticDriverOffset(Value value, uint64_t driverID,
                                              DenseSet<Value> &active) {
  if (!value || !active.insert(value).second)
    return std::nullopt;
  auto finish = [&](std::optional<uint64_t> result) {
    active.erase(value);
    return result;
  };
  if (auto context = value.getDefiningOp<sim::SimContextDriverOp>())
    return finish(context.getId() == driverID ? std::optional<uint64_t>(0)
                                              : std::nullopt);
  if (auto extract = value.getDefiningOp<sim::SimDriverExtractOp>()) {
    std::optional<uint64_t> base =
        getStaticDriverOffset(extract.getInput(), driverID, active);
    int64_t rawLow = extract.getLowBit();
    if (!base || rawLow < 0 ||
        static_cast<uint64_t>(rawLow) > UINT64_MAX - *base)
      return finish(std::nullopt);
    return finish(*base + static_cast<uint64_t>(rawLow));
  }
  if (auto subelement = value.getDefiningOp<sim::SimDriverSubelementOp>()) {
    std::optional<uint64_t> base =
        getStaticDriverOffset(subelement.getInput(), driverID, active);
    if (!base)
      return finish(std::nullopt);
    Type current = subelement.getInput().getType().getElementType();
    uint64_t offset = 0;
    for (int64_t rawIndex : subelement.getIndices()) {
      if (rawIndex < 0 || static_cast<uint64_t>(rawIndex) >=
                              sim::getAggregateNumElements(current))
        return finish(std::nullopt);
      auto child = sim::getAggregateProvenanceSubelement(
          current, static_cast<unsigned>(rawIndex));
      if (!child || child->first > UINT64_MAX - offset)
        return finish(std::nullopt);
      offset += child->first;
      current = sim::getAggregateElementType(current,
                                             static_cast<unsigned>(rawIndex));
    }
    if (offset > UINT64_MAX - *base)
      return finish(std::nullopt);
    return finish(*base + offset);
  }
  auto argument = dyn_cast<BlockArgument>(value);
  if (!argument)
    return finish(std::nullopt);
  Block *block = argument.getOwner();
  auto function = dyn_cast<sim::SimFuncOp>(block->getParentOp());
  if (!function || block != &function.getBody().front())
    return finish(std::nullopt);
  auto descriptor = function.getArgAttrOfType<IntegerAttr>(
      argument.getArgNumber(), sim::metadata::DescriptorId::getNameStr());
  auto low = function.getArgAttrOfType<IntegerAttr>(
      argument.getArgNumber(), sim::metadata::DescriptorLow::getNameStr());
  if (!descriptor || descriptor.getValue().isNegative() ||
      descriptor.getValue().getActiveBits() > 64 ||
      descriptor.getValue().getZExtValue() != driverID ||
      (low &&
       (low.getValue().isNegative() || low.getValue().getActiveBits() > 64)))
    return finish(std::nullopt);
  return finish(low ? low.getValue().getZExtValue() : uint64_t{0});
}

std::optional<uint64_t> getStaticDriverOffset(Value value, uint64_t driverID) {
  DenseSet<Value> active;
  return getStaticDriverOffset(value, driverID, active);
}

std::optional<uint64_t> getStaticDriverID(Value value) {
  while (value) {
    if (auto context = value.getDefiningOp<sim::SimContextDriverOp>())
      return context.getId();
    Operation *definition = value.getDefiningOp();
    if (auto extract = dyn_cast_or_null<sim::SimDriverExtractOp>(definition)) {
      value = extract.getInput();
      continue;
    }
    if (auto extract =
            dyn_cast_or_null<sim::SimDriverDynExtractOp>(definition)) {
      value = extract.getInput();
      continue;
    }
    if (auto subelement =
            dyn_cast_or_null<sim::SimDriverSubelementOp>(definition)) {
      value = subelement.getInput();
      continue;
    }
    if (auto element =
            dyn_cast_or_null<sim::SimDriverArrayElementOp>(definition)) {
      value = element.getInput();
      continue;
    }
    auto argument = dyn_cast<BlockArgument>(value);
    if (!argument)
      return std::nullopt;
    auto function =
        dyn_cast<sim::SimFuncOp>(argument.getOwner()->getParentOp());
    if (!function || argument.getOwner() != &function.getBody().front())
      return std::nullopt;
    auto descriptor = function.getArgAttrOfType<IntegerAttr>(
        argument.getArgNumber(), sim::metadata::DescriptorId::getNameStr());
    return descriptor ? std::optional<uint64_t>(descriptor.getInt())
                      : std::nullopt;
  }
  return std::nullopt;
}

struct ExactStaticDriverTarget {
  uint64_t id;
  uint64_t lowBit;

  bool operator==(const ExactStaticDriverTarget &other) const {
    return id == other.id && lowBit == other.lowBit;
  }
};

std::optional<ExactStaticDriverTarget>
getExactStaticDriverTarget(Value value, DenseSet<Value> &active, bool &cycle) {
  if (!active.insert(value).second) {
    cycle = true;
    return std::nullopt;
  }
  auto done = [&](std::optional<ExactStaticDriverTarget> result) {
    active.erase(value);
    return result;
  };
  if (auto context = value.getDefiningOp<sim::SimContextDriverOp>())
    return done(ExactStaticDriverTarget{context.getId(), 0});

  Operation *definition = value.getDefiningOp();
  uint64_t addedLowBit = 0;
  Value input;
  if (auto extract = dyn_cast_or_null<sim::SimDriverExtractOp>(definition)) {
    addedLowBit = extract.getLowBit();
    input = extract.getInput();
  } else if (auto subelement =
                 dyn_cast_or_null<sim::SimDriverSubelementOp>(definition)) {
    Type current = subelement.getInput().getType().getElementType();
    for (int64_t rawIndex : subelement.getIndices()) {
      if (rawIndex < 0 || static_cast<uint64_t>(rawIndex) >=
                              sim::getAggregateNumElements(current))
        return done(std::nullopt);
      auto item = sim::getAggregateProvenanceSubelement(
          current, static_cast<unsigned>(rawIndex));
      if (!item ||
          item->first > std::numeric_limits<uint64_t>::max() - addedLowBit)
        return done(std::nullopt);
      addedLowBit += item->first;
      current = sim::getAggregateElementType(current,
                                             static_cast<unsigned>(rawIndex));
    }
    input = subelement.getInput();
  }
  if (input) {
    auto target = getExactStaticDriverTarget(input, active, cycle);
    if (!target ||
        target->lowBit > std::numeric_limits<uint64_t>::max() - addedLowBit)
      return done(std::nullopt);
    target->lowBit += addedLowBit;
    return done(target);
  }

  auto argument = dyn_cast<BlockArgument>(value);
  if (!argument)
    return done(std::nullopt);
  if (auto function =
          dyn_cast<sim::SimFuncOp>(argument.getOwner()->getParentOp())) {
    if (argument.getOwner() == &function.getBody().front()) {
      auto descriptor = function.getArgAttrOfType<IntegerAttr>(
          argument.getArgNumber(), sim::metadata::DescriptorId::getNameStr());
      if (descriptor)
        return done(ExactStaticDriverTarget{
            static_cast<uint64_t>(descriptor.getInt()), 0});
    }
  }

  // CFG cleanup may forward a concrete driver through conditional block
  // arguments after compute fusion. Preserve exactness only when every
  // incoming edge names the same statically-derived driver slice.
  std::optional<ExactStaticDriverTarget> resolved;
  bool sawIncoming = false;
  bool sawCycle = false;
  Block *block = argument.getOwner();
  for (Block *predecessor : block->getPredecessors()) {
    Operation *terminator = predecessor->getTerminator();
    auto branch = dyn_cast<BranchOpInterface>(terminator);
    if (!branch)
      return done(std::nullopt);
    for (unsigned successor = 0; successor != terminator->getNumSuccessors();
         ++successor) {
      if (terminator->getSuccessor(successor) != block)
        continue;
      SuccessorOperands operands = branch.getSuccessorOperands(successor);
      unsigned index = argument.getArgNumber();
      if (index >= operands.size() || operands.isOperandProduced(index))
        return done(std::nullopt);
      bool incomingCycle = false;
      auto incoming =
          getExactStaticDriverTarget(operands[index], active, incomingCycle);
      if (!incoming && incomingCycle) {
        sawCycle = true;
        continue;
      }
      if (!incoming || (resolved && !(*resolved == *incoming)))
        return done(std::nullopt);
      resolved = incoming;
      sawIncoming = true;
    }
  }
  if (!resolved && sawCycle)
    cycle = true;
  return done(sawIncoming ? resolved : std::nullopt);
}

std::optional<ExactStaticDriverTarget> getExactStaticDriverTarget(Value value) {
  DenseSet<Value> active;
  bool cycle = false;
  return getExactStaticDriverTarget(value, active, cycle);
}

template <typename DriveOp>
class DriverDriveConversion final : public OpConversionPattern<DriveOp> {
public:
  using Base = OpConversionPattern<DriveOp>;
  using OneToNOpAdaptor = typename Base::OneToNOpAdaptor;

  DriverDriveConversion(const TypeConverter &converter, MLIRContext *context,
                        const NativeStateLayout &layout,
                        std::shared_ptr<const DriverLookupIndex> index,
                        std::shared_ptr<const NativeStateLayout> cleanLayout,
                        std::shared_ptr<const DriverLookupIndex> cleanIndex,
                        std::shared_ptr<const DriverLookupIndex> resolutionIndex)
      : Base(converter, context), layout(layout), index(std::move(index)),
        cleanLayout(std::move(cleanLayout)), cleanIndex(std::move(cleanIndex)),
        resolutionIndex(std::move(resolutionIndex)) {
    // Guard materialization creates two marked drives, each lowered once.
    this->setHasBoundedRewriteRecursion();
  }

  LogicalResult
  matchAndRewrite(DriveOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    bool clean = native::has<native::CleanBulkDrive>(op);
    if (clean && !cleanLayout)
      return failure();
    const NativeStateLayout &layout = clean ? *cleanLayout : this->layout;
    const auto &index = clean ? cleanIndex : this->index;
    // Driver and net lookups; `index` alone also gates the bulk proofs.
    const DriverLookupIndex &lookup = index ? *index : *resolutionIndex;
    if (adaptor.getDriver().size() != 1 || adaptor.getValue().empty())
      return failure();
    Type sourceType = op.getValue().getType();
    Value driveValue = adaptor.getValue().front();
    std::optional<unsigned> sourceWidth = nativeStateWidth(sourceType);
    if (!sourceWidth)
      return failure();
    if constexpr (!std::is_same_v<DriveOp, sim::SimDriverDriveDelayedNetOp>) {
      // A single clean-boundary guard covers the driver contribution AND all
      // resolved aliases. A per-driver root guard is insufficient: forcing a
      // net leaves its driver unforced, but release needs the retained driver
      // contribution in canonical storage.
      if (cleanLayout && !native::has<native::GuardedBulkDrive>(op) &&
          !::obelisk::schedule::has<
              ::obelisk::schedule::Field::DeferNetResolution>(op) &&
          !sim::has<sim::metadata::UserNetRawDrive>(op)) {
        auto id = native::get<native::DriverId>(op);
        auto found = id ? cleanIndex->driverByID.find(id.getUInt())
                        : cleanIndex->driverByID.end();
        std::optional<uint64_t> handle =
            resolveCFGConstantInteger(adaptor.getDriver().front());
        obelisk_rt_stable_handle_v1 decoded{};
        bool candidate = found != cleanIndex->driverByID.end() && handle &&
                         obelisk_rt_stable_handle_decode(*handle, &decoded) &&
                         decoded.kind == OBELISK_RT_STABLE_HANDLE_STATIC &&
                         decoded.id == found->second->handleID &&
                         decoded.offset == 0;
        if (candidate) {
          const auto &driver = *found->second;
          candidate = getBulkCapturedIsolatedDriverNet(
                          *cleanLayout, driver, *sourceWidth,
                          cleanIndex->netByID, cleanIndex->driversByNet,
                          cleanIndex->connectedNets, 1) != nullptr ||
                      getBulkConnectedDriverNets(
                          *cleanLayout, driver, *sourceWidth,
                          cleanIndex->netByID, cleanIndex->driversByNet, 1)
                          .has_value();
        }
        if (candidate) {
          Block *head = rewriter.getInsertionBlock();
          Block *tail = rewriter.splitBlock(head, op->getIterator());
          Region *region = head->getParent();
          Block *fast = rewriter.createBlock(region, tail->getIterator());
          Block *slow = rewriter.createBlock(region, tail->getIterator());
          SmallVector<Value> results;
          for (Type type : op->getResultTypes())
            results.push_back(tail->addArgument(type, op.getLoc()));
          recordStaticSpecializationCFGBlocks(rewriter, head, 3);
          for (auto [block, useClean] :
               {std::pair{fast, true}, std::pair{slow, false}}) {
            rewriter.setInsertionPointToEnd(block);
            Operation *clone = rewriter.clone(*op);
            native::mark<native::GuardedBulkDrive>(clone);
            if (useClean)
              native::mark<native::CleanBulkDrive>(clone);
            cf::BranchOp::create(rewriter, op.getLoc(), tail,
                                 clone->getResults());
          }
          rewriter.setInsertionPointToEnd(head);
          Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
          Value address = LLVM::AddressOfOp::create(
              rewriter, op.getLoc(), pointer,
              "__obelisk_state_specialization_fast_v1");
          Value flag = LLVM::LoadOp::create(rewriter, op.getLoc(),
                                            rewriter.getI32Type(), address, 4);
          Value allowed = arith::CmpIOp::create(
              rewriter, op.getLoc(), arith::CmpIPredicate::ne, flag,
              llvmConstant(rewriter, op.getLoc(), rewriter.getI32Type(), 0));
          markLikelyTrue(cf::CondBranchOp::create(rewriter, op.getLoc(),
                                                  allowed, fast, slow));
          rewriter.replaceOp(op, results);
          return success();
        }
      }
    }
    auto getDriver = [&](IntegerAttr id) -> const NativeStateLayout::Driver * {
      if (!id)
        return nullptr;
      return lookup.driverByID.lookup(static_cast<uint64_t>(id.getInt()));
    };
    IntegerType driveType = rewriter.getIntegerType(*sourceWidth);
    if (isa<FloatType>(sourceType))
      driveValue = arith::BitcastOp::create(rewriter, op.getLoc(), driveType,
                                            driveValue);
    auto integerConstant = [&](const APInt &value) {
      return arith::ConstantOp::create(
          rewriter, op.getLoc(), driveType,
          rewriter.getIntegerAttr(driveType, value));
    };
    Value driveUnknown =
        adaptor.getValue().size() == 2
            ? adaptor.getValue()[1]
            : integerConstant(APInt::getZero(driveType.getWidth()));
    // Delayed-net resolution runs in the runtime against the canonical state
    // planes. Route these driver stores through the generic state ABI so the
    // native globals and canonical image are updated together before the
    // resolver reads the contribution.
    const NativeStateLayout *storeLayout = &layout;
    if constexpr (std::is_same_v<DriveOp, sim::SimDriverDriveDelayedNetOp>)
      storeLayout = nullptr;
    bool userRaw = sim::has<sim::metadata::UserNetRawDrive>(op);
    Value oldRawValue;
    Value oldRawUnknown;
    if (userRaw) {
      oldRawValue = loadStatePlane(
          rewriter, op.getLoc(), adaptor.getDriver().front(), driveType,
          "__obelisk_state_value", false, layout.bitCount, storeLayout);
      oldRawUnknown = loadStatePlane(
          rewriter, op.getLoc(), adaptor.getDriver().front(), driveType,
          "__obelisk_state_unknown", true, layout.bitCount, storeLayout);
    }
    Value driveValueChanged = storeStatePlane(
        rewriter, op.getLoc(), adaptor.getDriver().front(), driveValue,
        "__obelisk_state_value", layout.bitCount, storeLayout);
    Value driveUnknownChanged = storeStatePlane(
        rewriter, op.getLoc(), adaptor.getDriver().front(), driveUnknown,
        "__obelisk_state_unknown", layout.bitCount, storeLayout);
    IntegerType i1 = rewriter.getI1Type();
    auto boolean = [&](bool value) {
      return arith::ConstantOp::create(rewriter, op.getLoc(), i1,
                                       rewriter.getBoolAttr(value));
    };
    Value rawChanged = arith::OrIOp::create(
        rewriter, op.getLoc(), driveValueChanged, driveUnknownChanged);
    Value changed = boolean(false);
    // A conditional gate stores its complementary low-polarity bank before
    // its high-polarity bank. The first store deliberately stops here so the
    // second drive resolves and publishes one atomic logical transition.
    bool deferResolution = ::obelisk::schedule::has<
        ::obelisk::schedule::Field::DeferNetResolution>(op);
    if constexpr (std::is_same_v<DriveOp, sim::SimDriverDriveDelayedNetOp>)
      deferResolution = op.getDeferResolution();
    if (deferResolution) {
      if (userRaw) {
        if (isa<FloatType>(sourceType)) {
          Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
          auto save = [&](Value value) {
            Value storage =
                entryAlloca(rewriter, op.getLoc(), value.getType(), 1, 1);
            LLVM::StoreOp::create(rewriter, op.getLoc(), value, storage, 1);
            return storage;
          };
          Value contextAddress = LLVM::AddressOfOp::create(
              rewriter, op.getLoc(), pointer, "__obelisk_current_context");
          Value runtimeContext = LLVM::LoadOp::create(
              rewriter, op.getLoc(), pointer, contextAddress, 8);
          LLVM::CallOp::create(
              rewriter, op.getLoc(), TypeRange{},
              SymbolRefAttr::get(rewriter.getContext(),
                                 "obelisk_rt_v1_scheduler_real_transition"),
              ValueRange{runtimeContext, adaptor.getDriver().front(),
                         llvmConstant(rewriter, op.getLoc(),
                                      rewriter.getI32Type(), *sourceWidth),
                         save(oldRawValue), save(driveValue)});
        } else {
          notifySignal(
              rewriter, op.getLoc(), adaptor.getDriver().front(), *sourceWidth,
              oldRawValue, oldRawUnknown, driveValue, driveUnknown,
              std::nullopt,
              ::obelisk::schedule::get<schedule::metadata::evalSourceOwner>(
                  op));
        }
      }
      if constexpr (std::is_same_v<DriveOp, sim::SimDriverDriveChangedOp>)
        rewriter.replaceOp(op, changed);
      else
        rewriter.eraseOp(op);
      return success();
    }

    if constexpr (!std::is_same_v<DriveOp, sim::SimDriverDriveDelayedNetOp>) {
      if (layout.hasPassSwitch) {
        auto driverID = native::get<native::DriverId>(op);
        const NativeStateLayout::Driver *driver = getDriver(driverID);
        if (!driver)
          return failure();
        uint64_t begin = driver->offset + driver->drivenLow;
        uint64_t end = begin + driver->drivenWidth;
        Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
        Type i32 = rewriter.getI32Type();
        Type i64 = rewriter.getI64Type();
        Value contextAddress = LLVM::AddressOfOp::create(
            rewriter, op.getLoc(), pointer, "__obelisk_current_context");
        Value runtimeContext = LLVM::LoadOp::create(rewriter, op.getLoc(),
                                                    pointer, contextAddress, 8);
        Value status =
            LLVM::CallOp::create(
                rewriter, op.getLoc(), TypeRange{i32},
                SymbolRefAttr::get(rewriter.getContext(),
                                   "obelisk_rt_v1_scheduler_resolve_drivers"),
                ValueRange{runtimeContext,
                           llvmConstant(rewriter, op.getLoc(), i64, begin),
                           llvmConstant(rewriter, op.getLoc(), i64, end)})
                .getResult();
        LLVM::CallOp::create(rewriter, op.getLoc(), TypeRange{},
                             SymbolRefAttr::get(rewriter.getContext(),
                                                "obelisk_rt_v1_scheduler_fail"),
                             ValueRange{runtimeContext, status});
        if constexpr (std::is_same_v<DriveOp, sim::SimDriverDriveChangedOp>)
          rewriter.replaceOp(op, rawChanged);
        else
          rewriter.eraseOp(op);
        return success();
      }
    }

    if constexpr (std::is_same_v<DriveOp, sim::SimDriverDriveDelayedNetOp>) {
      auto driverID = native::get<native::DriverId>(op);
      const NativeStateLayout::Driver *driver = getDriver(driverID);
      if (!driver)
        return failure();
      uint64_t begin = driver->offset + driver->drivenLow;
      uint64_t end = begin + driver->drivenWidth;
      Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
      Type i32 = rewriter.getI32Type();
      Type i64 = rewriter.getI64Type();
      Value contextAddress = LLVM::AddressOfOp::create(
          rewriter, op.getLoc(), pointer, "__obelisk_current_context");
      Value runtimeContext = LLVM::LoadOp::create(rewriter, op.getLoc(),
                                                  pointer, contextAddress, 8);
      Value status =
          LLVM::CallOp::create(
              rewriter, op.getLoc(), TypeRange{i32},
              SymbolRefAttr::get(rewriter.getContext(),
                                 "obelisk_rt_v1_scheduler_resolve_drivers"),
              ValueRange{runtimeContext,
                         llvmConstant(rewriter, op.getLoc(), i64, begin),
                         llvmConstant(rewriter, op.getLoc(), i64, end)})
              .getResult();
      LLVM::CallOp::create(rewriter, op.getLoc(), TypeRange{},
                           SymbolRefAttr::get(rewriter.getContext(),
                                              "obelisk_rt_v1_scheduler_fail"),
                           ValueRange{runtimeContext, status});
      rewriter.eraseOp(op);
      return success();
    }
    std::optional<uint64_t> affectedNet;
    if (auto netID = native::get<native::NetId>(op))
      affectedNet = netID.getInt();
    const NativeStateLayout::Driver *exactDriver = nullptr;
    if (native::has<native::ExactDriverRange>(op)) {
      auto driverID = native::get<native::DriverId>(op);
      if (!driverID)
        return failure();
      const NativeStateLayout::Driver *driver = getDriver(driverID);
      if (!driver || driver->drivenWidth != *sourceWidth)
        return failure();
      exactDriver = driver;
    }

    // A statically addressed partial driver update can only change the
    // connectivity components reached by the bits written by this operation.
    // Keep that exact component set in the compiler: expanding every scalar
    // gate drive across every bit of its packed destination makes native IR
    // quadratic in a generated primitive array.  If either the descriptor or
    // the address remains dynamic, leave the set absent and retain the
    // conservative whole-net lowering below.
    std::optional<DenseSet<std::pair<uint64_t, uint64_t>>> affectedComponents;
    const NativeStateLayout::Driver *affectedDriver = nullptr;
    auto driverID = native::get<native::DriverId>(op);
    if (driverID)
      affectedDriver = getDriver(driverID);
    std::optional<uint64_t> updateLow;
    if (auto low = native::get<native::DriverLow>(op);
        low && !low.getValue().isNegative() &&
        low.getValue().getActiveBits() <= 64) {
      updateLow = low.getValue().getZExtValue();
    } else {
      std::optional<uint64_t> encodedHandle =
          resolveCFGConstantInteger(adaptor.getDriver().front());
      obelisk_rt_stable_handle_v1 decodedHandle{};
      if (affectedDriver && encodedHandle &&
          obelisk_rt_stable_handle_decode(*encodedHandle, &decodedHandle) &&
          decodedHandle.kind == OBELISK_RT_STABLE_HANDLE_STATIC &&
          decodedHandle.id == affectedDriver->handleID &&
          decodedHandle.offset >= 0)
        updateLow = static_cast<uint64_t>(decodedHandle.offset);
    }
    if (affectedDriver && updateLow) {
      if (*updateLow <= affectedDriver->width &&
          *sourceWidth <= affectedDriver->width - *updateLow) {
        uint64_t drivenLow = affectedDriver->drivenLow;
        uint64_t drivenEnd = drivenLow + affectedDriver->drivenWidth;
        uint64_t updateEnd = *updateLow + *sourceWidth;
        uint64_t begin = std::max(*updateLow, drivenLow);
        uint64_t end = std::min(updateEnd, drivenEnd);
        affectedComponents.emplace();
        for (uint64_t bit = begin; bit != end; ++bit) {
          std::pair<uint64_t, uint64_t> component{affectedDriver->netId, bit};
          auto canonical = layout.connectivityCanonical.find(component);
          if (canonical != layout.connectivityCanonical.end())
            component = canonical->second;
          affectedComponents->insert(component);
        }
      }
    }

    // A full-width drive into an isolated net with one driver needs no
    // bitwise resolution: the resolved value is the driver value. Keep this
    // vector-shaped through LLVM lowering so very wide constants do not turn
    // into millions of scalar loads, selects, and stores.
    SmallVector<const NativeStateLayout::Net *, 2> bulkNets;
    if (index && exactDriver)
      if (auto connected = getBulkConnectedDriverNets(
              layout, *exactDriver, *sourceWidth, index->netByID,
              index->driversByNet, clean ? 1 : bulkConnectedDriverMinWidth))
        bulkNets = std::move(*connected);
    if (bulkNets.empty() && index && exactDriver)
      if (const NativeStateLayout::Net *isolated =
              getBulkCapturedIsolatedDriverNet(
                  layout, *exactDriver, *sourceWidth, index->netByID,
                  index->driversByNet, index->connectedNets,
                  clean ? 1 : bulkConnectedDriverMinWidth))
        bulkNets.push_back(isolated);
    if (bulkNets.empty() && native::has<native::WholeDriver>(op)) {
      auto driverID = native::get<native::DriverId>(op);
      const NativeStateLayout::Driver *driver = getDriver(driverID);
      if (driver) {
        const NativeStateLayout::Net *net = lookup.netByID.lookup(driver->netId);
        auto foundDrivers = lookup.driversByNet.find(driver->netId);
        bool onlyDriver = foundDrivers != lookup.driversByNet.end() &&
                          foundDrivers->second.size() == 1;
        bool connected = lookup.connectedNets.contains(driver->netId);
        if (net && onlyDriver && !connected && driver->drivenLow == 0 &&
            driver->drivenWidth == driver->width &&
            driver->width == net->width && driveType.getWidth() == net->width &&
            static_cast<uint32_t>(net->resolution) <
                static_cast<uint32_t>(sim::NetResolutionKind::Tri0) &&
            driver->strength0 != sim::Strength::HighZ &&
            driver->strength1 != sim::Strength::HighZ)
          bulkNets.push_back(net);
      }
    }
    if (!bulkNets.empty()) {
      struct BulkPublication {
        const NativeStateLayout::Net *net;
        Value handle;
        Value oldValue;
        Value oldUnknown;
        Value value;
        Value unknown;
        bool known;
      };
      SmallVector<BulkPublication, 2> bulkPublications;
      for (const NativeStateLayout::Net *net : bulkNets) {
        // IEEE 1800-2023 6.3.1, 6.6.1: the activation input certificate proves
        // this isolated net and its new value known. Its zero unknown plane
        // remains valid without another check at each drive. Connected or
        // competing contributions have no such implication. Keep the driver
        // contribution stores above for canonical state and force/release
        // (10.6.2); writable VPI retains the activation's clean-state guard.
        bool known =
            !net->fourState ||
            (schedule::has<schedule::Field::EvalInductiveTwoStateAccess>(op) &&
             affectedDriver && affectedDriver->netId == net->id &&
             layout.isolatedSingleDriverNets.contains(net->id));
        Value netHandle = arith::ConstantOp::create(
            rewriter, op.getLoc(), rewriter.getI64Type(),
            rewriter.getI64IntegerAttr(
                encodeNativeStaticHandle(net->handleID)));
        Value oldValue = loadStatePlane(rewriter, op.getLoc(), netHandle,
                                        driveType, "__obelisk_state_value",
                                        false, layout.bitCount, &layout);
        Value oldUnknown =
            !known
                ? loadStatePlane(rewriter, op.getLoc(), netHandle, driveType,
                                 "__obelisk_state_unknown", true,
                                 layout.bitCount, &layout)
                : integerConstant(APInt::getZero(driveType.getWidth()));
        Value publishValue = driveValue;
        Value publishUnknown = driveUnknown;
        if (!net->fourState) {
          Value allOnes =
              integerConstant(APInt::getAllOnes(driveType.getWidth()));
          publishValue = arith::AndIOp::create(
              rewriter, op.getLoc(), driveValue,
              arith::XOrIOp::create(rewriter, op.getLoc(), driveUnknown,
                                    allOnes));
          publishUnknown =
              integerConstant(APInt::getZero(driveType.getWidth()));
        }
        bulkPublications.push_back({net, netHandle, oldValue, oldUnknown,
                                    publishValue, publishUnknown, known});
      }
      // Publish every collapsed-net member before notifying any observer, just
      // like the scalar resolver below, so an alias transition is atomic.
      for (const BulkPublication &publication : bulkPublications) {
        Value valueChanged = storeStatePlane(
            rewriter, op.getLoc(), publication.handle, publication.value,
            "__obelisk_state_value", layout.bitCount, &layout);
        // A two-state net's unknown plane is permanently zero. A certified
        // four-state net stays known for this activation; its fallback still
        // publishes X/Z transitions and invalidates the certificate.
        Value unknownChanged =
            !publication.known
                ? storeStatePlane(rewriter, op.getLoc(), publication.handle,
                                  publication.unknown,
                                  "__obelisk_state_unknown", layout.bitCount,
                                  &layout)
                : boolean(false);
        changed = arith::OrIOp::create(
            rewriter, op.getLoc(), changed,
            arith::OrIOp::create(rewriter, op.getLoc(), valueChanged,
                                 unknownChanged));
      }
      for (const BulkPublication &publication : bulkPublications)
        notifySignal(
            rewriter, op.getLoc(), publication.handle, publication.net->width,
            publication.oldValue, publication.oldUnknown, publication.value,
            publication.net->fourState ? publication.unknown : Value{},
            resolveDirectStaticStateRange(publication.handle,
                                          publication.net->width, &layout),
            ::obelisk::schedule::get<schedule::metadata::evalSourceOwner>(op));
      if constexpr (std::is_same_v<DriveOp, sim::SimDriverDriveChangedOp>)
        rewriter.replaceOp(op, changed);
      else
        rewriter.eraseOp(op);
      return success();
    }

    struct Publication {
      Value handle;
      Value oldValue;
      Value oldUnknown;
      Value value;
      Value unknown;
      bool fourState;
      std::optional<DirectStaticStateRange> directRange;
      unsigned width = 1;
      bool storeUnknown = true;
    };
    SmallVector<Publication> publications;
    DenseSet<std::pair<uint64_t, uint64_t>> resolvedComponents;

    // IEEE 1800-2017 6.7.1 and 28.12 resolve each net bit on its own, so bits
    // whose drivers, strengths and net kind agree share one resolution
    // function. Evaluate it once per run of such bits, as bitwise logic over
    // word-sized slices of the driver and net planes, instead of emitting the
    // scalar resolver below once per bit. Trireg charge and bits with more
    // driver contributions than the table bound keep the scalar resolver.
    struct Contribution {
      const NativeStateLayout::Driver *driver;
      unsigned member;
      bool operator==(const Contribution &other) const {
        return driver == other.driver && member == other.member;
      }
    };
    struct BitShape {
      unsigned bit;
      SmallVector<analysis::NetBit, 2> members;
      SmallVector<Contribution, 2> contributions;
      sim::NetResolutionKind resolution;
    };
    auto canonicalOf = [&](const NativeStateLayout::Net &net, unsigned bit) {
      std::pair<uint64_t, uint64_t> logical{net.id, bit};
      auto found = layout.connectivityCanonical.find(logical);
      return found == layout.connectivityCanonical.end() ? logical
                                                         : found->second;
    };
    auto shapeBit = [&](const NativeStateLayout::Net &net, unsigned bit,
                        std::pair<uint64_t, uint64_t> canonical)
        -> std::optional<BitShape> {
      BitShape shape;
      shape.bit = bit;
      auto foundComponent = layout.connectivityComponents.find(canonical);
      if (foundComponent == layout.connectivityComponents.end() ||
          foundComponent->second.empty())
        shape.members.push_back({net.id, bit});
      else
        shape.members.assign(foundComponent->second.begin(),
                             foundComponent->second.end());
      shape.resolution = net.resolution;
      auto foundResolution = layout.connectivityResolutions.find(canonical);
      if (foundResolution != layout.connectivityResolutions.end())
        shape.resolution = foundResolution->second;
      if (shape.resolution == sim::NetResolutionKind::TriReg)
        return std::nullopt;
      for (auto [memberIndex, member] : llvm::enumerate(shape.members)) {
        const NativeStateLayout::Net *memberNet =
            lookup.netByID.lookup(member.net);
        if (!memberNet || member.offset >= memberNet->width ||
            member.offset > static_cast<uint64_t>(INT32_MAX))
          return std::nullopt;
        for (const NativeStateLayout::Driver *driver :
             lookup.coveringDrivers(member.net, member.offset))
          shape.contributions.push_back(
              {driver, static_cast<unsigned>(memberIndex)});
      }
      if (shape.contributions.size() > maximumRunContributions)
        return std::nullopt;
      // Strength combination is commutative and associative; a fixed order
      // keeps the generated IR deterministic.
      llvm::sort(shape.contributions, [](const Contribution &lhs,
                                         const Contribution &rhs) {
        return std::pair(lhs.driver, lhs.member) <
               std::pair(rhs.driver, rhs.member);
      });
      return shape;
    };
    // The next bit continues a run when its whole component is the previous
    // bit's component moved by one bit, each member in a fixed direction, with
    // the same contributions. A reversed port alias walks its member down.
    auto continues = [](const BitShape &previous, const BitShape &next,
                        SmallVectorImpl<int> &directions) {
      if (next.bit != previous.bit + 1 ||
          next.resolution != previous.resolution ||
          next.members.size() != previous.members.size() ||
          next.contributions != previous.contributions)
        return false;
      SmallVector<int, 2> steps;
      for (auto [lhs, rhs] : llvm::zip_equal(previous.members, next.members)) {
        if (rhs.net != lhs.net)
          return false;
        if (rhs.offset == lhs.offset + 1)
          steps.push_back(1);
        else if (lhs.offset != 0 && rhs.offset == lhs.offset - 1)
          steps.push_back(-1);
        else
          return false;
      }
      if (directions.empty()) {
        directions.assign(steps.begin(), steps.end());
        return true;
      }
      return llvm::equal(steps, directions);
    };
    // Per-bit resolution of every driver state combination, using the
    // runtime's strength algebra: table index bit 2c is contribution c's
    // value plane and bit 2c+1 its unknown plane.
    auto resolutionTables = [&](const BitShape &shape) {
      unsigned entries = 1u << (2 * shape.contributions.size());
      std::pair<APInt, APInt> tables{APInt::getZero(entries),
                                     APInt::getZero(entries)};
      auto kind = static_cast<uint8_t>(shape.resolution);
      for (unsigned entry = 0; entry != entries; ++entry) {
        uint16_t strengths = runtime::implicitNetStrength(kind);
        for (auto [index, contribution] :
             llvm::enumerate(shape.contributions))
          strengths = runtime::combineStrengthRanges(
              strengths,
              runtime::driverBitStrengths(
                  (entry >> (2 * index)) & 1, (entry >> (2 * index + 1)) & 1,
                  static_cast<unsigned>(contribution.driver->strength0),
                  static_cast<unsigned>(contribution.driver->strength1)),
              kind);
        runtime::ResolvedBit resolved = runtime::resolvedBit(strengths);
        if (resolved.value)
          tables.first.setBit(entry);
        if (resolved.unknown)
          tables.second.setBit(entry);
      }
      return tables;
    };
    auto resolveRun = [&](const BitShape &first, unsigned length,
                          ArrayRef<int> directions) {
      auto [valueTable, unknownTable] = resolutionTables(first);
      auto reversed = [&](unsigned member) {
        return !directions.empty() && directions[member] < 0;
      };
      for (unsigned delta = 0; delta < length; delta += 64) {
        unsigned width = std::min(64u, length - delta);
        IntegerType type = rewriter.getIntegerType(width);
        Value zero = arith::ConstantOp::create(
            rewriter, op.getLoc(), type, rewriter.getIntegerAttr(type, 0));
        Value ones = arith::ConstantOp::create(
            rewriter, op.getLoc(), type,
            rewriter.getIntegerAttr(type, APInt::getAllOnes(width)));
        auto constantOf = [&](Value plane) -> std::optional<bool> {
          APInt bits;
          if (!matchPattern(plane, m_ConstantInt(&bits)))
            return std::nullopt;
          if (bits.isZero())
            return false;
          if (bits.isAllOnes())
            return true;
          return std::nullopt;
        };
        // The storage slice holding this chunk of a member's bits. A reversed
        // member holds them in descending order, so its slice is reflected
        // into run order on load and back on publication.
        auto sliceLow = [&](unsigned member) -> uint64_t {
          uint64_t start = first.members[member].offset;
          return reversed(member) ? start - delta - (width - 1)
                                  : start + delta;
        };
        auto inRunOrder = [&](unsigned member, Value slice) -> Value {
          if (!reversed(member) || width == 1 || constantOf(slice))
            return slice;
          return LLVM::BitReverseOp::create(rewriter, op.getLoc(), type, slice);
        };
        auto handleAt = [&](uint32_t handleID, uint64_t offset) {
          return arith::ConstantOp::create(
              rewriter, op.getLoc(), rewriter.getI64Type(),
              rewriter.getI64IntegerAttr(encodeNativeStaticHandle(
                  handleID, static_cast<int32_t>(offset))));
        };
        // This drive's own contribution is the value just stored. Using it
        // directly lets a two-state drive, proven at compile time or running
        // in a known-state copy, cofactor its unknown plane away.
        auto ownSlice = [&](Value plane, uint64_t offset) -> Value {
          Value shifted = plane;
          if (offset != *updateLow)
            shifted = rewriter.createOrFold<arith::ShRUIOp>(
                op.getLoc(), plane,
                arith::ConstantOp::create(
                    rewriter, op.getLoc(), driveType,
                    rewriter.getIntegerAttr(driveType, offset - *updateLow)));
          if (driveType.getWidth() == width)
            return shifted;
          return rewriter.createOrFold<arith::TruncIOp>(op.getLoc(), type,
                                                        shifted);
        };
        SmallVector<Value> planes;
        for (const Contribution &contribution : first.contributions) {
          const NativeStateLayout::Driver &driver = *contribution.driver;
          uint64_t offset = sliceLow(contribution.member);
          if (&driver == affectedDriver && updateLow &&
              offset >= *updateLow &&
              offset + width <= *updateLow + *sourceWidth) {
            planes.push_back(inRunOrder(contribution.member,
                                        ownSlice(driveValue, offset)));
            planes.push_back(inRunOrder(contribution.member,
                                        ownSlice(driveUnknown, offset)));
            continue;
          }
          Value handle = handleAt(driver.handleID, offset);
          planes.push_back(inRunOrder(
              contribution.member,
              loadStatePlane(rewriter, op.getLoc(), handle, type,
                             "__obelisk_state_value", false, layout.bitCount,
                             &layout)));
          planes.push_back(inRunOrder(
              contribution.member,
              loadStatePlane(rewriter, op.getLoc(), handle, type,
                             "__obelisk_state_unknown", true, layout.bitCount,
                             &layout)));
        }
        // Project the constant planes out of the tables, so the expansion
        // below sees every remaining independence: known inputs cost nothing.
        SmallVector<Value> variables;
        SmallVector<unsigned> positions;
        unsigned fixedEntry = 0;
        for (auto [position, plane] : llvm::enumerate(planes)) {
          if (std::optional<bool> known = constantOf(plane)) {
            if (*known)
              fixedEntry |= 1u << position;
            continue;
          }
          variables.push_back(plane);
          positions.push_back(static_cast<unsigned>(position));
        }
        auto project = [&](const APInt &table) {
          APInt projected = APInt::getZero(1u << variables.size());
          for (unsigned entry = 0; entry != projected.getBitWidth(); ++entry) {
            unsigned index = fixedEntry;
            for (auto [variable, position] : llvm::enumerate(positions))
              if ((entry >> variable) & 1)
                index |= 1u << position;
            if (table[index])
              projected.setBit(entry);
          }
          return projected;
        };
        DenseMap<Value, Value> inverted;
        auto invert = [&](Value value) {
          Value &result = inverted[value];
          if (!result)
            result = arith::XOrIOp::create(rewriter, op.getLoc(), value, ones);
          return result;
        };
        // Shannon expansion, most significant variable first.
        DenseMap<std::pair<unsigned, APInt>, Value> memo;
        auto synthesize = [&](auto &self, const APInt &table,
                              unsigned count) -> Value {
          if (table.isZero())
            return zero;
          if (table.isAllOnes())
            return ones;
          if (Value found = memo.lookup({count, table}))
            return found;
          unsigned half = 1u << (count - 1);
          APInt low = table.trunc(half);
          APInt high = table.lshr(half).trunc(half);
          Value plane = variables[count - 1];
          Value result;
          if (low == high) {
            result = self(self, low, count - 1);
          } else {
            Value set = self(self, high, count - 1);
            Value clear = self(self, low, count - 1);
            if (set == ones && clear == zero)
              result = plane;
            else if (set == zero && clear == ones)
              result = invert(plane);
            else if (clear == zero)
              result = arith::AndIOp::create(rewriter, op.getLoc(), plane, set);
            else if (set == zero)
              result = arith::AndIOp::create(rewriter, op.getLoc(),
                                             invert(plane), clear);
            else if (set == ones)
              result = arith::OrIOp::create(rewriter, op.getLoc(), plane, clear);
            else if (clear == ones)
              result = arith::OrIOp::create(rewriter, op.getLoc(),
                                            invert(plane), set);
            else
              result = arith::OrIOp::create(
                  rewriter, op.getLoc(),
                  arith::AndIOp::create(rewriter, op.getLoc(), plane, set),
                  arith::AndIOp::create(rewriter, op.getLoc(), invert(plane),
                                        clear));
          }
          memo[{count, table}] = result;
          return result;
        };
        unsigned count = static_cast<unsigned>(variables.size());
        Value resolvedValue = synthesize(synthesize, project(valueTable), count);
        Value resolvedUnknown =
            synthesize(synthesize, project(unknownTable), count);
        for (auto [memberIndex, member] : llvm::enumerate(first.members)) {
          const NativeStateLayout::Net &memberNet =
              *lookup.netByID.lookup(member.net);
          Value netHandle = handleAt(memberNet.handleID, sliceLow(memberIndex));
          Value oldValue = loadStatePlane(rewriter, op.getLoc(), netHandle,
                                          type, "__obelisk_state_value", false,
                                          layout.bitCount, &layout);
          // The unknown plane of an integer net is permanently zero.
          Value oldUnknown =
              memberNet.fourState
                  ? loadStatePlane(rewriter, op.getLoc(), netHandle, type,
                                   "__obelisk_state_unknown", true,
                                   layout.bitCount, &layout)
                  : zero;
          Value publishValue =
              memberNet.fourState
                  ? resolvedValue
                  : arith::AndIOp::create(rewriter, op.getLoc(), resolvedValue,
                                          invert(resolvedUnknown))
                        .getResult();
          Value publishUnknown = memberNet.fourState ? resolvedUnknown : zero;
          unsigned index = static_cast<unsigned>(memberIndex);
          publications.push_back(
              {netHandle, oldValue, oldUnknown,
               inRunOrder(index, publishValue),
               inRunOrder(index, publishUnknown), memberNet.fourState,
               resolveDirectStaticStateRange(netHandle, width, &layout), width,
               memberNet.fourState});
        }
      }
    };
    auto resolveRuns = [&](const NativeStateLayout::Net &net, unsigned firstBit,
                           unsigned endBit) {
      std::optional<BitShape> first, last;
      unsigned length = 0;
      SmallVector<int> directions;
      auto flush = [&]() {
        if (first)
          resolveRun(*first, length, directions);
        first.reset();
        last.reset();
        length = 0;
        directions.clear();
      };
      for (unsigned bit = firstBit; bit < endBit; ++bit) {
        // Filter on the component before building its shape: a partial drive
        // into a wide net touches only a few of its bits.
        std::pair<uint64_t, uint64_t> canonical = canonicalOf(net, bit);
        if ((affectedComponents && !affectedComponents->contains(canonical)) ||
            resolvedComponents.contains(canonical)) {
          flush();
          continue;
        }
        std::optional<BitShape> shape = shapeBit(net, bit, canonical);
        if (!shape) {
          flush();
          continue;
        }
        if (last && !continues(*last, *shape, directions))
          flush();
        resolvedComponents.insert(canonical);
        if (!first)
          first = *shape;
        last = std::move(shape);
        ++length;
      }
      flush();
    };
    // Reject before emitting anything for an earlier net.
    if (exactDriver)
      for (const NativeStateLayout::Net &net : layout.netLayouts)
        if ((!affectedNet || net.id == *affectedNet) &&
            exactDriver->drivenLow + exactDriver->drivenWidth > net.width)
          return failure();
    for (const NativeStateLayout::Net &net : layout.netLayouts) {
      if (affectedNet && net.id != *affectedNet)
        continue;
      unsigned firstBit = exactDriver ? exactDriver->drivenLow : 0;
      unsigned endBit = exactDriver
                            ? exactDriver->drivenLow + exactDriver->drivenWidth
                            : net.width;
      resolveRuns(net, firstBit, endBit);
      for (unsigned bit = firstBit; bit < endBit; ++bit) {
        std::pair<uint64_t, uint64_t> logical{net.id, bit};
        auto foundCanonical = layout.connectivityCanonical.find(logical);
        std::pair<uint64_t, uint64_t> canonical =
            foundCanonical == layout.connectivityCanonical.end()
                ? logical
                : foundCanonical->second;
        if (affectedComponents && !affectedComponents->contains(canonical))
          continue;
        auto foundComponent = layout.connectivityComponents.find(canonical);
        SmallVector<analysis::NetBit> fallback;
        ArrayRef<analysis::NetBit> component;
        if (foundComponent == layout.connectivityComponents.end() ||
            foundComponent->second.empty()) {
          fallback.push_back({net.id, bit});
          component = fallback;
        } else {
          component = foundComponent->second;
        }
        if (!resolvedComponents.insert(canonical).second)
          continue;

        IntegerType strengthType = rewriter.getIntegerType(16);
        auto strengthConstant = [&](uint16_t value) {
          return arith::ConstantOp::create(
              rewriter, op.getLoc(), strengthType,
              rewriter.getIntegerAttr(strengthType, value));
        };
        // Bit `index` of a strength set, 7 being high impedance.
        auto strengthIndexBit = [](unsigned index) -> uint16_t {
          return static_cast<uint16_t>(uint16_t{1} << index);
        };
        sim::NetResolutionKind resolution = net.resolution;
        auto foundResolution = layout.connectivityResolutions.find(canonical);
        if (foundResolution != layout.connectivityResolutions.end())
          resolution = foundResolution->second;
        Value resolvedStrengths = strengthConstant(
            runtime::implicitNetStrength(static_cast<uint8_t>(resolution)));
        Value resolutionValue = arith::ConstantOp::create(
            rewriter, op.getLoc(), rewriter.getI32Type(),
            rewriter.getI32IntegerAttr(static_cast<uint32_t>(resolution)));
        // Every bit is an independent driver contribution. A topology
        // component may contain several bits from the same vector driver.
        SmallVector<std::pair<const NativeStateLayout::Driver *, unsigned>, 4>
            contributions;
        for (auto [index, member] : llvm::enumerate(component))
          for (const NativeStateLayout::Driver *driver :
               lookup.coveringDrivers(member.net, member.offset))
            contributions.push_back({driver, static_cast<unsigned>(index)});
        llvm::sort(contributions);
        for (auto [contributor, memberIndex] : contributions) {
          {
            const NativeStateLayout::Driver &driver = *contributor;
            const analysis::NetBit &member = component[memberIndex];
            Value handle = arith::ConstantOp::create(
                rewriter, op.getLoc(), rewriter.getI64Type(),
                rewriter.getI64IntegerAttr(encodeNativeStaticHandle(
                    driver.handleID, static_cast<int32_t>(member.offset))));
            Value driverValue = loadStatePlane(rewriter, op.getLoc(), handle,
                                               i1, "__obelisk_state_value",
                                               false, layout.bitCount, &layout);
            Value driverUnknown = loadStatePlane(
                rewriter, op.getLoc(), handle, i1, "__obelisk_state_unknown",
                true, layout.bitCount, &layout);
            unsigned strength0 = static_cast<unsigned>(driver.strength0);
            unsigned strength1 = static_cast<unsigned>(driver.strength1);
            uint16_t zeroMask = strengthIndexBit(7 - strength0);
            uint16_t oneMask = strengthIndexBit(7 + strength1);
            uint16_t xMask = 0;
            for (unsigned index = 7 - strength0; index <= 7 + strength1;
                 ++index)
              xMask |= strengthIndexBit(index);
            Value knownStrengths = arith::SelectOp::create(
                rewriter, op.getLoc(), driverValue, strengthConstant(oneMask),
                strengthConstant(zeroMask));
            Value unknownStrengths = arith::SelectOp::create(
                rewriter, op.getLoc(), driverValue,
                strengthConstant(strengthIndexBit(7)), strengthConstant(xMask));
            Value driverStrengths =
                arith::SelectOp::create(rewriter, op.getLoc(), driverUnknown,
                                        unknownStrengths, knownStrengths);
            resolvedStrengths =
                LLVM::CallOp::create(
                    rewriter, op.getLoc(), TypeRange{strengthType},
                    SymbolRefAttr::get(rewriter.getContext(),
                                       "obelisk_rt_v1_strength_resolve_kind"),
                    ValueRange{resolvedStrengths, driverStrengths,
                               resolutionValue})
                    .getResult();
          }
        }
        auto hasStrength = [&](Value strengths, uint16_t mask) {
          Value masked = arith::AndIOp::create(rewriter, op.getLoc(), strengths,
                                               strengthConstant(mask));
          return arith::CmpIOp::create(rewriter, op.getLoc(),
                                       arith::CmpIPredicate::ne, masked,
                                       strengthConstant(0));
        };
        Value hasNegative =
            hasStrength(resolvedStrengths, (uint16_t{1} << 7) - 1);
        Value hasZero = hasStrength(resolvedStrengths, strengthIndexBit(7));
        Value hasPositive =
            hasStrength(resolvedStrengths,
                        static_cast<uint16_t>(((uint16_t{1} << 15) - 1) &
                                              ~((uint16_t{1} << 8) - 1)));
        auto logicalNot = [&](Value value) {
          return arith::XOrIOp::create(rewriter, op.getLoc(), value,
                                       boolean(true));
        };
        Value resolvedZ = arith::AndIOp::create(
            rewriter, op.getLoc(), hasZero,
            arith::AndIOp::create(rewriter, op.getLoc(),
                                  logicalNot(hasNegative),
                                  logicalNot(hasPositive)));
        Value knownZero = arith::AndIOp::create(
            rewriter, op.getLoc(), hasNegative,
            arith::AndIOp::create(rewriter, op.getLoc(), logicalNot(hasZero),
                                  logicalNot(hasPositive)));
        Value knownOne =
            arith::AndIOp::create(rewriter, op.getLoc(), hasPositive,
                                  arith::AndIOp::create(rewriter, op.getLoc(),
                                                        logicalNot(hasNegative),
                                                        logicalNot(hasZero)));
        Value resolvedUnknown = arith::OrIOp::create(
            rewriter, op.getLoc(), resolvedZ,
            logicalNot(arith::OrIOp::create(rewriter, op.getLoc(), knownZero,
                                            knownOne)));
        Value resolvedValue =
            arith::OrIOp::create(rewriter, op.getLoc(), resolvedZ, knownOne);

        // IEEE 1800-2017 28.16.2: when every active driver is high
        // impedance, a connected trireg component resolves its retained
        // charges at their declared small, medium, or large charge strengths.
        // Compute that component value once so publication remains atomic.
        Value chargeValue;
        Value chargeUnknown;
        if (resolution == sim::NetResolutionKind::TriReg) {
          Value chargeStrengths = strengthConstant(strengthIndexBit(7));
          for (const analysis::NetBit &member : component) {
            const NativeStateLayout::Net *memberNet =
                lookup.netByID.lookup(member.net);
            if (!memberNet || member.offset >= memberNet->width ||
                !memberNet->chargeStrength)
              continue;
            Value memberHandle = arith::ConstantOp::create(
                rewriter, op.getLoc(), rewriter.getI64Type(),
                rewriter.getI64IntegerAttr(encodeNativeStaticHandle(
                    memberNet->handleID, static_cast<int32_t>(member.offset))));
            Value memberValue = loadStatePlane(
                rewriter, op.getLoc(), memberHandle, i1,
                "__obelisk_state_value", false, layout.bitCount, &layout);
            Value memberUnknown = loadStatePlane(
                rewriter, op.getLoc(), memberHandle, i1,
                "__obelisk_state_unknown", true, layout.bitCount, &layout);
            unsigned strength =
                static_cast<unsigned>(*memberNet->chargeStrength);
            uint16_t zeroMask = strengthIndexBit(7 - strength);
            uint16_t oneMask = strengthIndexBit(7 + strength);
            uint16_t xMask = 0;
            for (unsigned index = 7 - strength; index <= 7 + strength; ++index)
              xMask |= strengthIndexBit(index);
            Value knownStrengths = arith::SelectOp::create(
                rewriter, op.getLoc(), memberValue, strengthConstant(oneMask),
                strengthConstant(zeroMask));
            Value storedStrengths = arith::SelectOp::create(
                rewriter, op.getLoc(), memberUnknown, strengthConstant(xMask),
                knownStrengths);
            chargeStrengths =
                LLVM::CallOp::create(
                    rewriter, op.getLoc(), TypeRange{strengthType},
                    SymbolRefAttr::get(rewriter.getContext(),
                                       "obelisk_rt_v1_strength_resolve_kind"),
                    ValueRange{chargeStrengths, storedStrengths,
                               resolutionValue})
                    .getResult();
          }
          Value chargeNegative =
              hasStrength(chargeStrengths, (uint16_t{1} << 7) - 1);
          Value chargeZero = hasStrength(chargeStrengths, strengthIndexBit(7));
          Value chargePositive =
              hasStrength(chargeStrengths,
                          static_cast<uint16_t>(((uint16_t{1} << 15) - 1) &
                                                ~((uint16_t{1} << 8) - 1)));
          Value chargeKnownZero = arith::AndIOp::create(
              rewriter, op.getLoc(), chargeNegative,
              arith::AndIOp::create(rewriter, op.getLoc(),
                                    logicalNot(chargeZero),
                                    logicalNot(chargePositive)));
          Value chargeKnownOne = arith::AndIOp::create(
              rewriter, op.getLoc(), chargePositive,
              arith::AndIOp::create(rewriter, op.getLoc(),
                                    logicalNot(chargeNegative),
                                    logicalNot(chargeZero)));
          Value chargeZ = arith::AndIOp::create(
              rewriter, op.getLoc(), chargeZero,
              arith::AndIOp::create(rewriter, op.getLoc(),
                                    logicalNot(chargeNegative),
                                    logicalNot(chargePositive)));
          chargeUnknown = arith::OrIOp::create(
              rewriter, op.getLoc(), chargeZ,
              logicalNot(arith::OrIOp::create(
                  rewriter, op.getLoc(), chargeKnownZero, chargeKnownOne)));
          chargeValue = arith::OrIOp::create(rewriter, op.getLoc(), chargeZ,
                                             chargeKnownOne);
        }
        for (const analysis::NetBit &member : component) {
          const NativeStateLayout::Net *memberNet =
              lookup.netByID.lookup(member.net);
          if (!memberNet || member.offset >= memberNet->width)
            return failure();
          Value netHandle = arith::ConstantOp::create(
              rewriter, op.getLoc(), rewriter.getI64Type(),
              rewriter.getI64IntegerAttr(encodeNativeStaticHandle(
                  memberNet->handleID, static_cast<int32_t>(member.offset))));
          Value oldResolvedValue = loadStatePlane(
              rewriter, op.getLoc(), netHandle, i1, "__obelisk_state_value",
              false, layout.bitCount, &layout);
          // The unknown plane of an integer net is permanently zero.
          Value oldResolvedUnknown =
              memberNet->fourState
                  ? loadStatePlane(rewriter, op.getLoc(), netHandle, i1,
                                   "__obelisk_state_unknown", true,
                                   layout.bitCount, &layout)
                  : boolean(false);
          Value publishValue = resolvedValue;
          Value publishUnknown = resolvedUnknown;
          if (resolution == sim::NetResolutionKind::TriReg) {
            publishValue = arith::SelectOp::create(
                rewriter, op.getLoc(), resolvedZ, chargeValue, resolvedValue);
            publishUnknown =
                arith::SelectOp::create(rewriter, op.getLoc(), resolvedZ,
                                        chargeUnknown, resolvedUnknown);
          }
          if (!memberNet->fourState) {
            publishValue =
                arith::SelectOp::create(rewriter, op.getLoc(), resolvedUnknown,
                                        boolean(false), resolvedValue);
            publishUnknown = boolean(false);
          }
          publications.push_back(
              {netHandle, oldResolvedValue, oldResolvedUnknown, publishValue,
               publishUnknown, memberNet->fourState,
               resolveDirectStaticStateRange(netHandle, 1, &layout), 1,
               memberNet->fourState});
        }
      }
    }
    // Publish every component affected by this vector drive before emitting
    // any transition notification. This matches bytecode atomic publication
    // and prevents observers from seeing a partially updated topology.
    for (const Publication &publication : publications) {
      changed = arith::OrIOp::create(
          rewriter, op.getLoc(), changed,
          storeStatePlane(rewriter, op.getLoc(), publication.handle,
                          publication.value, "__obelisk_state_value",
                          layout.bitCount, &layout));
      if (publication.storeUnknown)
        changed = arith::OrIOp::create(
            rewriter, op.getLoc(), changed,
            storeStatePlane(rewriter, op.getLoc(), publication.handle,
                            publication.unknown, "__obelisk_state_unknown",
                            layout.bitCount, &layout));
    }
    auto packBits = [&](ArrayRef<Publication> run, Value Publication::*member) {
      Value packed = llvmConstant(rewriter, op.getLoc(), rewriter.getI64Type(),
                                  uint64_t{0});
      for (auto [bit, publication] : llvm::enumerate(run)) {
        Value extended = LLVM::ZExtOp::create(
            rewriter, op.getLoc(), rewriter.getI64Type(), publication.*member);
        if (bit != 0)
          extended = arith::ShLIOp::create(
              rewriter, op.getLoc(), extended,
              llvmConstant(rewriter, op.getLoc(), rewriter.getI64Type(), bit));
        packed = arith::OrIOp::create(rewriter, op.getLoc(), packed, extended);
      }
      return packed;
    };
    for (size_t begin = 0; begin < publications.size();) {
      size_t end = begin + 1;
      const std::optional<DirectStaticStateRange> &firstRange =
          publications[begin].directRange;
      if (firstRange && publications[begin].width == 1) {
        while (end < publications.size() && end - begin < 64) {
          const std::optional<DirectStaticStateRange> &nextRange =
              publications[end].directRange;
          uint64_t relativeOffset = end - begin;
          if (publications[end].width != 1 || !nextRange ||
              nextRange->staticID != firstRange->staticID ||
              nextRange->guarded != firstRange->guarded ||
              nextRange->offset != firstRange->offset + relativeOffset ||
              nextRange->localOffset !=
                  firstRange->localOffset + relativeOffset)
            break;
          ++end;
        }
      }
      ArrayRef<Publication> run(publications.data() + begin, end - begin);
      if (run.size() == 1) {
        const Publication &publication = run.front();
        notifySignal(
            rewriter, op.getLoc(), publication.handle, publication.width,
            publication.oldValue,
            publication.oldUnknown, publication.value,
            publication.fourState ? publication.unknown : Value{},
            publication.directRange,
            ::obelisk::schedule::get<schedule::metadata::evalSourceOwner>(op));
      } else {
        notifySignal(
            rewriter, op.getLoc(), run.front().handle, run.size(),
            packBits(run, &Publication::oldValue),
            packBits(run, &Publication::oldUnknown),
            packBits(run, &Publication::value),
            packBits(run, &Publication::unknown), firstRange,
            ::obelisk::schedule::get<schedule::metadata::evalSourceOwner>(op));
      }
      begin = end;
    }
    if constexpr (std::is_same_v<DriveOp, sim::SimDriverDriveChangedOp>)
      rewriter.replaceOp(op, changed);
    else
      rewriter.eraseOp(op);
    return success();
  }

private:
  const NativeStateLayout &layout;
  std::shared_ptr<const DriverLookupIndex> index;
  std::shared_ptr<const NativeStateLayout> cleanLayout;
  std::shared_ptr<const DriverLookupIndex> cleanIndex;
  // Indexes the unspecialized layout for run resolution even when no driver
  // qualifies for a bulk drive.
  std::shared_ptr<const DriverLookupIndex> resolutionIndex;
};

} // namespace

void annotateStaticDriverNets(ModuleOp module,
                              const NativeStateLayout &layout) {
  auto annotate = [&](auto drive) {
    if (drive.getDriver().template getDefiningOp<sim::SimContextDriverOp>())
      native::mark<native::WholeDriver>(drive);
    std::optional<ExactStaticDriverTarget> exactTarget;
    auto frozenID = schedule::get<schedule::Field::ExactDriverId>(drive);
    auto frozenLow = schedule::get<schedule::Field::ExactDriverLow>(drive);
    if (frozenID && frozenLow && !frozenID.getValue().isNegative() &&
        !frozenLow.getValue().isNegative() &&
        frozenID.getValue().getActiveBits() <= 64 &&
        frozenLow.getValue().getActiveBits() <= 64)
      exactTarget =
          ExactStaticDriverTarget{frozenID.getValue().getZExtValue(),
                                  frozenLow.getValue().getZExtValue()};
    else if (!frozenID && !frozenLow)
      exactTarget = getExactStaticDriverTarget(drive.getDriver());
    std::optional<uint64_t> driverID =
        exactTarget ? std::optional<uint64_t>(exactTarget->id)
                    : getStaticDriverID(drive.getDriver());
    if (!driverID)
      return;
    native::set<native::DriverId>(
        drive,
        IntegerAttr::get(IntegerType::get(module.getContext(), 64), *driverID));
    std::optional<unsigned> valueWidth =
        nativeStateWidth(drive.getValue().getType());
    for (const NativeStateLayout::Driver &driver : layout.driverLayouts) {
      if (driver.id != *driverID)
        continue;
      std::optional<uint64_t> low =
          exactTarget ? std::optional<uint64_t>(exactTarget->lowBit)
                      : getStaticDriverOffset(drive.getDriver(), *driverID);
      if (low)
        native::set<native::DriverLow>(
            drive,
            IntegerAttr::get(IntegerType::get(module.getContext(), 64), *low));
      native::set<native::NetId>(
          drive, IntegerAttr::get(IntegerType::get(module.getContext(), 64),
                                  driver.netId));
      if (exactTarget && exactTarget->id == *driverID && valueWidth &&
          *valueWidth == driver.drivenWidth &&
          exactTarget->lowBit == driver.drivenLow)
        native::mark<native::ExactDriverRange>(drive);
      return;
    }
  };
  for (auto drive : analysis::collectInWalkOrder<sim::SimDriverDriveOp>(module))
    annotate(drive);
  for (auto drive :
       analysis::collectInWalkOrder<sim::SimDriverDriveDelayedNetOp>(module))
    annotate(drive);
  for (auto drive :
       analysis::collectInWalkOrder<sim::SimDriverDriveChangedOp>(module))
    annotate(drive);
}

void populateDriverToLLVMConversionPatterns(RewritePatternSet &patterns,
                                            TypeConverter &converter,
                                            const NativeStateLayout &layout) {
  std::shared_ptr<const DriverLookupIndex> index;
  bool hasBulkCandidate =
      !layout.hasPassSwitch && llvm::any_of(layout.driverLayouts, [&](auto &d) {
        return d.width >= bulkConnectedDriverMinWidth && d.drivenLow == 0 &&
               d.drivenWidth == d.width &&
               d.strength0 == sim::Strength::Strong &&
               d.strength1 == sim::Strength::Strong &&
               layout.directHandles.contains(d.handleID) &&
               !layout.guardedHandles.contains(d.handleID);
      });
  if (hasBulkCandidate)
    index = std::make_shared<DriverLookupIndex>(layout);
  std::shared_ptr<const NativeStateLayout> cleanLayout;
  std::shared_ptr<const DriverLookupIndex> cleanIndex;
  if (llvm::any_of(layout.netLayouts, [&](const auto &net) {
        return layout.guardedHandles.contains(net.handleID);
      })) {
    cleanLayout =
        std::make_shared<NativeStateLayout>(makeCleanEvalStateLayout(layout));
    cleanIndex = std::make_shared<DriverLookupIndex>(*cleanLayout);
  }
  std::shared_ptr<const DriverLookupIndex> resolutionIndex =
      index ? index : std::make_shared<DriverLookupIndex>(layout);
  patterns.add<DriverDriveConversion<sim::SimDriverDriveOp>,
               DriverDriveConversion<sim::SimDriverDriveDelayedNetOp>,
               DriverDriveConversion<sim::SimDriverDriveChangedOp>>(
      converter, patterns.getContext(), layout, index, cleanLayout, cleanIndex,
      resolutionIndex);
}

} // namespace obelisk::detail
