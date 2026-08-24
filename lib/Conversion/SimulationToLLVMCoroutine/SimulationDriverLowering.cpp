//===- SimulationDriverLowering.cpp - Native driver patterns ------------===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHandle.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Transforms/DialectConversion.h"

#include "llvm/ADT/STLExtras.h"

#include <type_traits>

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

template <typename DriveOp>
class DriverDriveConversion final : public OpConversionPattern<DriveOp> {
public:
  using Base = OpConversionPattern<DriveOp>;
  using OneToNOpAdaptor = typename Base::OneToNOpAdaptor;

  DriverDriveConversion(const TypeConverter &converter, MLIRContext *context,
                        const NativeStateLayout &layout)
      : Base(converter, context), layout(layout) {}

  LogicalResult
  matchAndRewrite(DriveOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getDriver().size() != 1 || adaptor.getValue().empty())
      return failure();
    Type sourceType = op.getValue().getType();
    Value driveValue = adaptor.getValue().front();
    std::optional<unsigned> sourceWidth = nativeStateWidth(sourceType);
    if (!sourceWidth)
      return failure();
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
    storeStatePlane(rewriter, op.getLoc(), adaptor.getDriver().front(),
                    driveValue, "__obelisk_state_value", layout.bitCount,
                    storeLayout);
    storeStatePlane(rewriter, op.getLoc(), adaptor.getDriver().front(),
                    driveUnknown, "__obelisk_state_unknown", layout.bitCount,
                    storeLayout);
    IntegerType i1 = rewriter.getI1Type();
    auto boolean = [&](bool value) {
      return arith::ConstantOp::create(rewriter, op.getLoc(), i1,
                                       rewriter.getBoolAttr(value));
    };
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
              SymbolRefAttr::get(
                  rewriter.getContext(),
                  "obelisk_rt_v1_scheduler_real_transition"),
              ValueRange{runtimeContext, adaptor.getDriver().front(),
                         llvmConstant(rewriter, op.getLoc(),
                                      rewriter.getI32Type(), *sourceWidth),
                         save(oldRawValue), save(driveValue)});
        } else {
          notifySignal(rewriter, op.getLoc(), adaptor.getDriver().front(),
                       *sourceWidth, oldRawValue, oldRawUnknown, driveValue,
                       driveUnknown, std::nullopt);
        }
      }
      if constexpr (std::is_same_v<DriveOp, sim::SimDriverDriveChangedOp>)
        rewriter.replaceOp(op, changed);
      else
        rewriter.eraseOp(op);
      return success();
    }

    if constexpr (std::is_same_v<DriveOp, sim::SimDriverDriveDelayedNetOp>) {
      auto driverID =
          op->template getAttrOfType<IntegerAttr>("obelisk.native.driver_id");
      auto driver =
          driverID
              ? llvm::find_if(layout.driverLayouts,
                              [&](const auto &candidate) {
                                return candidate.id ==
                                       static_cast<uint64_t>(driverID.getInt());
                              })
              : layout.driverLayouts.end();
      if (driver == layout.driverLayouts.end())
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

    // A full-width drive into an isolated net with one driver needs no
    // bitwise resolution: the resolved value is the driver value. Keep this
    // vector-shaped through LLVM lowering so very wide constants do not turn
    // into millions of scalar loads, selects, and stores.
    const NativeStateLayout::Net *bulkNet = nullptr;
    if (op->hasAttr("obelisk.native.whole_driver")) {
      auto driverID =
          op->template getAttrOfType<IntegerAttr>("obelisk.native.driver_id");
      auto driver =
          driverID
              ? llvm::find_if(layout.driverLayouts,
                              [&](const auto &candidate) {
                                return candidate.id ==
                                       static_cast<uint64_t>(driverID.getInt());
                              })
              : layout.driverLayouts.end();
      if (driver != layout.driverLayouts.end()) {
        auto net = llvm::find_if(layout.netLayouts, [&](const auto &candidate) {
          return candidate.id == driver->netId;
        });
        bool onlyDriver =
            llvm::count_if(layout.driverLayouts, [&](const auto &candidate) {
              return candidate.netId == driver->netId;
            }) == 1;
        bool connected =
            llvm::any_of(layout.connectivityCanonical, [&](const auto &entry) {
              return entry.first.first == driver->netId;
            });
        if (net != layout.netLayouts.end() && onlyDriver && !connected &&
            driver->drivenLow == 0 && driver->drivenWidth == driver->width &&
            driver->width == net->width && driveType.getWidth() == net->width &&
            static_cast<uint32_t>(net->resolution) <
                static_cast<uint32_t>(sim::NetResolutionKind::Tri0) &&
            driver->strength0 != sim::Strength::HighZ &&
            driver->strength1 != sim::Strength::HighZ)
          bulkNet = &*net;
      }
    }
    if (bulkNet) {
      Value netHandle = arith::ConstantOp::create(
          rewriter, op.getLoc(), rewriter.getI64Type(),
          rewriter.getI64IntegerAttr(
              encodeNativeStaticHandle(bulkNet->handleID)));
      Value oldValue = loadStatePlane(rewriter, op.getLoc(), netHandle,
                                      driveType, "__obelisk_state_value", false,
                                      layout.bitCount, &layout);
      Value oldUnknown = loadStatePlane(rewriter, op.getLoc(), netHandle,
                                        driveType, "__obelisk_state_unknown",
                                        true, layout.bitCount, &layout);
      Value publishValue = driveValue;
      Value publishUnknown = driveUnknown;
      if (!bulkNet->fourState) {
        Value allOnes =
            integerConstant(APInt::getAllOnes(driveType.getWidth()));
        publishValue =
            arith::AndIOp::create(rewriter, op.getLoc(), driveValue,
                                  arith::XOrIOp::create(rewriter, op.getLoc(),
                                                        driveUnknown, allOnes));
        publishUnknown = integerConstant(APInt::getZero(driveType.getWidth()));
      }
      Value valueChanged =
          storeStatePlane(rewriter, op.getLoc(), netHandle, publishValue,
                          "__obelisk_state_value", layout.bitCount, &layout);
      Value unknownChanged =
          storeStatePlane(rewriter, op.getLoc(), netHandle, publishUnknown,
                          "__obelisk_state_unknown", layout.bitCount, &layout);
      changed = arith::OrIOp::create(rewriter, op.getLoc(), valueChanged,
                                     unknownChanged);
      notifySignal(
          rewriter, op.getLoc(), netHandle, bulkNet->width, oldValue,
          oldUnknown, publishValue,
          bulkNet->fourState ? publishUnknown : Value{},
          resolveDirectStaticStateRange(netHandle, bulkNet->width, &layout));
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
                     publication.directRange);
      } else {
        notifySignal(rewriter, op.getLoc(), run.front().handle, run.size(),
                     packBits(run, &Publication::oldValue),
                     packBits(run, &Publication::oldUnknown),
                     packBits(run, &Publication::value),
                     packBits(run, &Publication::unknown), firstRange);
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
};

} // namespace

void annotateStaticDriverNets(ModuleOp module,
                              const NativeStateLayout &layout) {
  auto annotate = [&](auto drive) {
    if (drive.getDriver().template getDefiningOp<sim::SimContextDriverOp>())
      drive->setAttr("obelisk.native.whole_driver",
                     UnitAttr::get(module.getContext()));
    std::optional<uint64_t> driverID = getStaticDriverID(drive.getDriver());
    if (!driverID)
      return;
    drive->setAttr(
        "obelisk.native.driver_id",
        IntegerAttr::get(IntegerType::get(module.getContext(), 64), *driverID));
    for (const NativeStateLayout::Driver &driver : layout.driverLayouts) {
      if (driver.id != *driverID)
        continue;
      drive->setAttr("obelisk.native.net_id",
                     IntegerAttr::get(IntegerType::get(module.getContext(), 64),
                                      driver.netId));
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
  patterns.add<DriverDriveConversion<sim::SimDriverDriveOp>,
               DriverDriveConversion<sim::SimDriverDriveDelayedNetOp>,
               DriverDriveConversion<sim::SimDriverDriveChangedOp>>(
      converter, patterns.getContext(), layout);
}

} // namespace obelisk::detail
