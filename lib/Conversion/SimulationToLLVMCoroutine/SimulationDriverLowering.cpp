//===- SimulationDriverLowering.cpp - Native driver patterns ------------===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHandle.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Transforms/DialectConversion.h"

#include "llvm/ADT/STLExtras.h"

#include <algorithm>
#include <limits>
#include <type_traits>

using namespace mlir;

namespace obelisk::detail {
namespace {

constexpr StringLiteral guardedBulkDriveAttr =
    "obelisk.native.guarded_bulk_drive";
constexpr StringLiteral cleanBulkDriveAttr = "obelisk.native.clean_bulk_drive";

uint64_t encodeNativeStaticHandle(uint32_t id, int32_t offset = 0) {
  return obelisk_rt_stable_handle_encode(OBELISK_RT_STABLE_HANDLE_STATIC, id,
                                         offset);
}

constexpr StringLiteral nativeDriverLowAttr = "obelisk.native.driver_low";

// Keep ordinary narrow driver resolution straight-line.  Wide port aliases
// are the case where expanding the identical one-driver proof once per bit
// dominates compile time and memory.
constexpr unsigned bulkConnectedDriverMinWidth = 65;

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
  }

  NetByID netByID;
  DriverByID driverByID;
  DriversByNet driversByNet;
  ConnectedNets connectedNets;
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
  if (width < minimumWidth || layout.hasPassSwitch || driver.drivenLow != 0 ||
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
      argument.getArgNumber(), sim::metadata::descriptorId);
  auto low = function.getArgAttrOfType<IntegerAttr>(
      argument.getArgNumber(), sim::metadata::descriptorLow);
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
        argument.getArgNumber(), sim::metadata::descriptorId);
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
          argument.getArgNumber(), sim::metadata::descriptorId);
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
                        std::shared_ptr<const DriverLookupIndex> cleanIndex)
      : Base(converter, context), layout(layout), index(std::move(index)),
        cleanLayout(std::move(cleanLayout)), cleanIndex(std::move(cleanIndex)) {
    // Guard materialization creates two marked drives, each lowered once.
    this->setHasBoundedRewriteRecursion();
  }

  LogicalResult
  matchAndRewrite(DriveOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    bool clean = op->hasAttr(cleanBulkDriveAttr);
    if (clean && !cleanLayout)
      return failure();
    const NativeStateLayout &layout = clean ? *cleanLayout : this->layout;
    const auto &index = clean ? cleanIndex : this->index;
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
      if (cleanLayout && !op->hasAttr(guardedBulkDriveAttr) &&
          !op->hasAttr("obelisk_sim.defer_net_resolution") &&
          !op->hasAttr("obelisk_sim.user_net_raw_drive")) {
        auto id =
            op->template getAttrOfType<IntegerAttr>("obelisk.native.driver_id");
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
            clone->setAttr(guardedBulkDriveAttr, rewriter.getUnitAttr());
            if (useClean)
              clone->setAttr(cleanBulkDriveAttr, rewriter.getUnitAttr());
            cf::BranchOp::create(rewriter, op.getLoc(), tail,
                                 clone->getResults());
          }
          rewriter.setInsertionPointToEnd(head);
          Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
          Value address = LLVM::AddressOfOp::create(
              rewriter, op.getLoc(), pointer,
              "__obelisk_static_specialization_fast_v1");
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
      uint64_t value = static_cast<uint64_t>(id.getInt());
      if (index) {
        auto found = index->driverByID.find(value);
        return found == index->driverByID.end() ? nullptr : found->second;
      }
      auto found = llvm::find_if(layout.driverLayouts, [&](const auto &driver) {
        return driver.id == value;
      });
      return found == layout.driverLayouts.end() ? nullptr : &*found;
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
    bool userRaw = op->hasAttr("obelisk_sim.user_net_raw_drive");
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
    bool deferResolution = op->hasAttr("obelisk_sim.defer_net_resolution");
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
          notifySignal(rewriter, op.getLoc(), adaptor.getDriver().front(),
                       *sourceWidth, oldRawValue, oldRawUnknown, driveValue,
                       driveUnknown, std::nullopt,
                       op->getAttr(sim::metadata::evalSourceOwner));
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
        auto driverID =
            op->template getAttrOfType<IntegerAttr>("obelisk.native.driver_id");
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
      auto driverID =
          op->template getAttrOfType<IntegerAttr>("obelisk.native.driver_id");
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
    if (auto netID =
            op->template getAttrOfType<IntegerAttr>("obelisk.native.net_id"))
      affectedNet = netID.getInt();
    const NativeStateLayout::Driver *exactDriver = nullptr;
    if (op->hasAttr("obelisk.native.exact_driver_range")) {
      auto driverID =
          op->template getAttrOfType<IntegerAttr>("obelisk.native.driver_id");
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
    auto driverID =
        op->template getAttrOfType<IntegerAttr>("obelisk.native.driver_id");
    if (driverID)
      affectedDriver = getDriver(driverID);
    std::optional<uint64_t> updateLow;
    if (auto low = op->template getAttrOfType<IntegerAttr>(nativeDriverLowAttr);
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
    if (bulkNets.empty() && op->hasAttr("obelisk.native.whole_driver")) {
      auto driverID =
          op->template getAttrOfType<IntegerAttr>("obelisk.native.driver_id");
      const NativeStateLayout::Driver *driver = getDriver(driverID);
      if (driver) {
        const NativeStateLayout::Net *net = nullptr;
        bool onlyDriver = false;
        bool connected = false;
        if (index) {
          auto foundNet = index->netByID.find(driver->netId);
          if (foundNet != index->netByID.end())
            net = foundNet->second;
          auto foundDrivers = index->driversByNet.find(driver->netId);
          onlyDriver = foundDrivers != index->driversByNet.end() &&
                       foundDrivers->second.size() == 1;
          connected = index->connectedNets.contains(driver->netId);
        } else {
          auto foundNet =
              llvm::find_if(layout.netLayouts, [&](const auto &candidate) {
                return candidate.id == driver->netId;
              });
          if (foundNet != layout.netLayouts.end())
            net = &*foundNet;
          onlyDriver =
              llvm::count_if(layout.driverLayouts, [&](const auto &candidate) {
                return candidate.netId == driver->netId;
              }) == 1;
          connected = llvm::any_of(layout.connectivityCanonical,
                                   [&](const auto &entry) {
                                     return entry.first.first == driver->netId;
                                   });
        }
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
      };
      SmallVector<BulkPublication, 2> bulkPublications;
      for (const NativeStateLayout::Net *net : bulkNets) {
        Value netHandle = arith::ConstantOp::create(
            rewriter, op.getLoc(), rewriter.getI64Type(),
            rewriter.getI64IntegerAttr(
                encodeNativeStaticHandle(net->handleID)));
        Value oldValue = loadStatePlane(rewriter, op.getLoc(), netHandle,
                                        driveType, "__obelisk_state_value",
                                        false, layout.bitCount, &layout);
        Value oldUnknown = loadStatePlane(rewriter, op.getLoc(), netHandle,
                                          driveType, "__obelisk_state_unknown",
                                          true, layout.bitCount, &layout);
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
                                    publishValue, publishUnknown});
      }
      // Publish every collapsed-net member before notifying any observer, just
      // like the scalar resolver below, so an alias transition is atomic.
      for (const BulkPublication &publication : bulkPublications) {
        Value valueChanged = storeStatePlane(
            rewriter, op.getLoc(), publication.handle, publication.value,
            "__obelisk_state_value", layout.bitCount, &layout);
        Value unknownChanged = storeStatePlane(
            rewriter, op.getLoc(), publication.handle, publication.unknown,
            "__obelisk_state_unknown", layout.bitCount, &layout);
        changed = arith::OrIOp::create(
            rewriter, op.getLoc(), changed,
            arith::OrIOp::create(rewriter, op.getLoc(), valueChanged,
                                 unknownChanged));
      }
      for (const BulkPublication &publication : bulkPublications)
        notifySignal(rewriter, op.getLoc(), publication.handle,
                     publication.net->width, publication.oldValue,
                     publication.oldUnknown, publication.value,
                     publication.net->fourState ? publication.unknown : Value{},
                     resolveDirectStaticStateRange(
                         publication.handle, publication.net->width, &layout),
                     op->getAttr(sim::metadata::evalSourceOwner));
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
    };
    SmallVector<Publication> publications;
    SmallVector<std::pair<uint64_t, uint64_t>> resolvedComponents;
    for (const NativeStateLayout::Net &net : layout.netLayouts) {
      if (affectedNet && net.id != *affectedNet)
        continue;
      unsigned firstBit = exactDriver ? exactDriver->drivenLow : 0;
      unsigned endBit = exactDriver
                            ? exactDriver->drivenLow + exactDriver->drivenWidth
                            : net.width;
      if (endBit > net.width)
        return failure();
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
        if (llvm::is_contained(resolvedComponents, canonical))
          continue;
        resolvedComponents.push_back(canonical);

        IntegerType strengthType = rewriter.getIntegerType(16);
        auto strengthConstant = [&](uint16_t value) {
          return arith::ConstantOp::create(
              rewriter, op.getLoc(), strengthType,
              rewriter.getIntegerAttr(strengthType, value));
        };
        auto strengthBit = [](unsigned index) -> uint16_t {
          return static_cast<uint16_t>(uint16_t{1} << index);
        };
        sim::NetResolutionKind resolution = net.resolution;
        auto foundResolution = layout.connectivityResolutions.find(canonical);
        if (foundResolution != layout.connectivityResolutions.end())
          resolution = foundResolution->second;
        unsigned implicitStrengthIndex = 7;
        switch (resolution) {
        case sim::NetResolutionKind::Tri0:
          implicitStrengthIndex = 2;
          break;
        case sim::NetResolutionKind::Tri1:
          implicitStrengthIndex = 12;
          break;
        case sim::NetResolutionKind::Supply0:
          implicitStrengthIndex = 0;
          break;
        case sim::NetResolutionKind::Supply1:
          implicitStrengthIndex = 14;
          break;
        default:
          break;
        }
        Value resolvedStrengths =
            strengthConstant(strengthBit(implicitStrengthIndex));
        Value resolutionValue = arith::ConstantOp::create(
            rewriter, op.getLoc(), rewriter.getI32Type(),
            rewriter.getI32IntegerAttr(static_cast<uint32_t>(resolution)));
        for (const NativeStateLayout::Driver &driver : layout.driverLayouts) {
          for (const analysis::NetBit &member : component) {
            if (member.net != driver.netId ||
                member.offset < driver.drivenLow ||
                member.offset - driver.drivenLow >= driver.drivenWidth ||
                member.offset >= driver.width)
              continue;
            // Every bit is an independent driver contribution. A topology
            // component may contain several bits from the same vector driver.
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
            uint16_t zeroMask = strengthBit(7 - strength0);
            uint16_t oneMask = strengthBit(7 + strength1);
            uint16_t xMask = 0;
            for (unsigned index = 7 - strength0; index <= 7 + strength1;
                 ++index)
              xMask |= strengthBit(index);
            Value knownStrengths = arith::SelectOp::create(
                rewriter, op.getLoc(), driverValue, strengthConstant(oneMask),
                strengthConstant(zeroMask));
            Value unknownStrengths = arith::SelectOp::create(
                rewriter, op.getLoc(), driverValue,
                strengthConstant(strengthBit(7)), strengthConstant(xMask));
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
        Value hasZero = hasStrength(resolvedStrengths, strengthBit(7));
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
          Value chargeStrengths = strengthConstant(strengthBit(7));
          for (const analysis::NetBit &member : component) {
            auto memberNet =
                llvm::find_if(layout.netLayouts, [&](const auto &candidate) {
                  return candidate.id == member.net;
                });
            if (memberNet == layout.netLayouts.end() ||
                member.offset >= memberNet->width || !memberNet->chargeStrength)
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
            uint16_t zeroMask = strengthBit(7 - strength);
            uint16_t oneMask = strengthBit(7 + strength);
            uint16_t xMask = 0;
            for (unsigned index = 7 - strength; index <= 7 + strength; ++index)
              xMask |= strengthBit(index);
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
          Value chargeZero = hasStrength(chargeStrengths, strengthBit(7));
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
          auto memberNet =
              llvm::find_if(layout.netLayouts, [&](const auto &candidate) {
                return candidate.id == member.net;
              });
          if (memberNet == layout.netLayouts.end() ||
              member.offset >= memberNet->width)
            return failure();
          Value netHandle = arith::ConstantOp::create(
              rewriter, op.getLoc(), rewriter.getI64Type(),
              rewriter.getI64IntegerAttr(encodeNativeStaticHandle(
                  memberNet->handleID, static_cast<int32_t>(member.offset))));
          Value oldResolvedValue = loadStatePlane(
              rewriter, op.getLoc(), netHandle, i1, "__obelisk_state_value",
              false, layout.bitCount, &layout);
          Value oldResolvedUnknown = loadStatePlane(
              rewriter, op.getLoc(), netHandle, i1, "__obelisk_state_unknown",
              true, layout.bitCount, &layout);
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
               resolveDirectStaticStateRange(netHandle, 1, &layout)});
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
      if (firstRange) {
        while (end < publications.size() && end - begin < 64) {
          const std::optional<DirectStaticStateRange> &nextRange =
              publications[end].directRange;
          uint64_t relativeOffset = end - begin;
          if (!nextRange || nextRange->staticID != firstRange->staticID ||
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
        notifySignal(rewriter, op.getLoc(), publication.handle, 1,
                     publication.oldValue, publication.oldUnknown,
                     publication.value,
                     publication.fourState ? publication.unknown : Value{},
                     publication.directRange,
                     op->getAttr(sim::metadata::evalSourceOwner));
      } else {
        notifySignal(rewriter, op.getLoc(), run.front().handle, run.size(),
                     packBits(run, &Publication::oldValue),
                     packBits(run, &Publication::oldUnknown),
                     packBits(run, &Publication::value),
                     packBits(run, &Publication::unknown), firstRange,
                     op->getAttr(sim::metadata::evalSourceOwner));
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
};

} // namespace

void annotateStaticDriverNets(ModuleOp module,
                              const NativeStateLayout &layout) {
  auto annotate = [&](auto drive) {
    if (drive.getDriver().template getDefiningOp<sim::SimContextDriverOp>())
      drive->setAttr("obelisk.native.whole_driver",
                     UnitAttr::get(module.getContext()));
    std::optional<ExactStaticDriverTarget> exactTarget;
    auto frozenID = drive->template getAttrOfType<IntegerAttr>(
        "obelisk_sim.exact_driver_id");
    auto frozenLow = drive->template getAttrOfType<IntegerAttr>(
        "obelisk_sim.exact_driver_low");
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
    drive->setAttr(
        "obelisk.native.driver_id",
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
        drive->setAttr(
            nativeDriverLowAttr,
            IntegerAttr::get(IntegerType::get(module.getContext(), 64), *low));
      drive->setAttr("obelisk.native.net_id",
                     IntegerAttr::get(IntegerType::get(module.getContext(), 64),
                                      driver.netId));
      if (exactTarget && exactTarget->id == *driverID && valueWidth &&
          *valueWidth == driver.drivenWidth &&
          exactTarget->lowBit == driver.drivenLow)
        drive->setAttr("obelisk.native.exact_driver_range",
                       UnitAttr::get(module.getContext()));
      return;
    }
  };
  module.walk([&](sim::SimDriverDriveOp drive) { annotate(drive); });
  module.walk([&](sim::SimDriverDriveDelayedNetOp drive) { annotate(drive); });
  module.walk([&](sim::SimDriverDriveChangedOp drive) { annotate(drive); });
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
  patterns.add<DriverDriveConversion<sim::SimDriverDriveOp>,
               DriverDriveConversion<sim::SimDriverDriveDelayedNetOp>,
               DriverDriveConversion<sim::SimDriverDriveChangedOp>>(
      converter, patterns.getContext(), layout, index, cleanLayout, cleanIndex);
}

} // namespace obelisk::detail
