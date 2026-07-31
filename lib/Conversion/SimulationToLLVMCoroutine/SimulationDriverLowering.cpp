//===- SimulationDriverLowering.cpp - Native driver patterns ------------===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHandle.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Transforms/DialectConversion.h"

#include "llvm/ADT/STLExtras.h"

using namespace mlir;

namespace obelisk::detail {
namespace {

uint64_t encodeNativeStaticHandle(uint32_t id, int32_t offset = 0) {
  return obelisk_rt_stable_handle_encode(OBELISK_RT_STABLE_HANDLE_STATIC, id,
                                         offset);
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
    if (!function)
      return std::nullopt;
    auto descriptor = function.getArgAttrOfType<IntegerAttr>(
        argument.getArgNumber(), sim::metadata::descriptorId);
    return descriptor ? std::optional<uint64_t>(descriptor.getInt())
                      : std::nullopt;
  }
  return std::nullopt;
}

class DriverDriveConversion final
    : public OpConversionPattern<sim::SimDriverDriveOp> {
public:
  DriverDriveConversion(const TypeConverter &converter, MLIRContext *context,
                        const NativeStateLayout &layout)
      : OpConversionPattern(converter, context), layout(layout) {}

  LogicalResult
  matchAndRewrite(sim::SimDriverDriveOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getDriver().size() != 1 || adaptor.getValue().empty())
      return failure();
    storeStatePlane(rewriter, op.getLoc(), adaptor.getDriver().front(),
                    adaptor.getValue().front(), "__obelisk_state_value",
                    layout.bitCount, &layout);
    IntegerType i1 = rewriter.getI1Type();
    auto boolean = [&](bool value) {
      return arith::ConstantOp::create(rewriter, op.getLoc(), i1,
                                       rewriter.getBoolAttr(value));
    };
    if (adaptor.getValue().size() == 2) {
      storeStatePlane(rewriter, op.getLoc(), adaptor.getDriver().front(),
                      adaptor.getValue()[1], "__obelisk_state_unknown",
                      layout.bitCount, &layout);
    } else {
      storeStatePlane(rewriter, op.getLoc(), adaptor.getDriver().front(),
                      boolean(false), "__obelisk_state_unknown",
                      layout.bitCount, &layout);
    }
    Value changed = boolean(false);
    std::optional<uint64_t> affectedNet;
    if (auto netID = op->getAttrOfType<IntegerAttr>("obelisk.native.net_id"))
      affectedNet = netID.getInt();
    struct Publication {
      Value handle;
      Value oldValue;
      Value oldUnknown;
      Value value;
      Value unknown;
      bool fourState;
    };
    SmallVector<Publication> publications;
    SmallVector<std::pair<uint64_t, uint64_t>> resolvedComponents;
    for (const NativeStateLayout::Net &net : layout.netLayouts) {
      if (affectedNet && net.id != *affectedNet)
        continue;
      for (unsigned bit = 0; bit < net.width; ++bit) {
        std::pair<uint64_t, uint64_t> logical{net.id, bit};
        auto foundCanonical = layout.connectivityCanonical.find(logical);
        std::pair<uint64_t, uint64_t> canonical =
            foundCanonical == layout.connectivityCanonical.end()
                ? logical
                : foundCanonical->second;
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

        Value resolvedValue = boolean(true);
        Value resolvedUnknown = boolean(true);
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
            Value driverValue =
                loadStatePlane(rewriter, op.getLoc(), handle, i1,
                               "__obelisk_state_value", false, layout.bitCount,
                               &layout);
            Value driverUnknown = loadStatePlane(rewriter, op.getLoc(), handle,
                                                 i1, "__obelisk_state_unknown",
                                                 true, layout.bitCount,
                                                 &layout);
            Value currentZ = arith::AndIOp::create(
                rewriter, op.getLoc(), resolvedUnknown, resolvedValue);
            Value driverZ = arith::AndIOp::create(rewriter, op.getLoc(),
                                                  driverUnknown, driverValue);
            Value currentX = arith::AndIOp::create(
                rewriter, op.getLoc(), resolvedUnknown,
                arith::XOrIOp::create(rewriter, op.getLoc(), resolvedValue,
                                      boolean(true)));
            Value driverX = arith::AndIOp::create(
                rewriter, op.getLoc(), driverUnknown,
                arith::XOrIOp::create(rewriter, op.getLoc(), driverValue,
                                      boolean(true)));
            Value conflict = arith::OrIOp::create(
                rewriter, op.getLoc(), currentX,
                arith::OrIOp::create(
                    rewriter, op.getLoc(), driverX,
                    arith::CmpIOp::create(rewriter, op.getLoc(),
                                          arith::CmpIPredicate::ne,
                                          resolvedValue, driverValue)));
            Value mergedValue = arith::SelectOp::create(
                rewriter, op.getLoc(), conflict, boolean(false), resolvedValue);
            Value mergedUnknown = arith::SelectOp::create(
                rewriter, op.getLoc(), conflict, boolean(true), boolean(false));
            Value withoutCurrentZ = arith::SelectOp::create(
                rewriter, op.getLoc(), driverZ, resolvedValue, mergedValue);
            Value withoutCurrentZUnknown = arith::SelectOp::create(
                rewriter, op.getLoc(), driverZ, resolvedUnknown, mergedUnknown);
            resolvedValue = arith::SelectOp::create(
                rewriter, op.getLoc(), currentZ, driverValue, withoutCurrentZ);
            resolvedUnknown =
                arith::SelectOp::create(rewriter, op.getLoc(), currentZ,
                                        driverUnknown, withoutCurrentZUnknown);
          }
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
          Value oldResolvedValue =
              loadStatePlane(rewriter, op.getLoc(), netHandle, i1,
                             "__obelisk_state_value", false, layout.bitCount,
                             &layout);
          Value oldResolvedUnknown =
              loadStatePlane(rewriter, op.getLoc(), netHandle, i1,
                             "__obelisk_state_unknown", true, layout.bitCount,
                             &layout);
          Value publishValue = resolvedValue;
          Value publishUnknown = resolvedUnknown;
          if (!memberNet->fourState) {
            publishValue =
                arith::SelectOp::create(rewriter, op.getLoc(), resolvedUnknown,
                                        boolean(false), resolvedValue);
            publishUnknown = boolean(false);
          }
          publications.push_back({netHandle, oldResolvedValue,
                                  oldResolvedUnknown, publishValue,
                                  publishUnknown, memberNet->fourState});
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
    for (const Publication &publication : publications)
      notifySignal(rewriter, op.getLoc(), publication.handle, 1,
                   publication.oldValue, publication.oldUnknown,
                   publication.value,
                   publication.fourState ? publication.unknown : Value{},
                   resolveDirectStaticStateRange(publication.handle, 1,
                                                 &layout));
    (void)changed;
    rewriter.eraseOp(op);
    return success();
  }

private:
  const NativeStateLayout &layout;
};

} // namespace

void annotateStaticDriverNets(ModuleOp module,
                              const NativeStateLayout &layout) {
  module.walk([&](sim::SimDriverDriveOp drive) {
    std::optional<uint64_t> driverID = getStaticDriverID(drive.getDriver());
    if (!driverID)
      return;
    for (const NativeStateLayout::Driver &driver : layout.driverLayouts) {
      if (driver.id != *driverID)
        continue;
      drive->setAttr("obelisk.native.net_id",
                     IntegerAttr::get(IntegerType::get(module.getContext(), 64),
                                      driver.netId));
      return;
    }
  });
}

void populateDriverToLLVMConversionPatterns(RewritePatternSet &patterns,
                                            TypeConverter &converter,
                                            const NativeStateLayout &layout) {
  patterns.add<DriverDriveConversion>(converter, patterns.getContext(), layout);
}

} // namespace obelisk::detail
