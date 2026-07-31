//===- SimulationNBALowering.cpp - Native NBA rewrite patterns ----------===//

#include "SimulationNBALowering.h"
#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Conversion/SimulationRuntime.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/StableHandle.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Transforms/DialectConversion.h"

#include "llvm/ADT/STLExtras.h"

using namespace mlir;

namespace obelisk::detail {
namespace {

class ImmediateNBAConversion final
    : public OpConversionPattern<sim::SimNBAEnqueueOp> {
public:
  ImmediateNBAConversion(const TypeConverter &converter, MLIRContext *context,
                         uint64_t stateBitCount,
                         const NativeStaticNBAPlan *staticPlan,
                         bool staticSitesEnabled, bool guardedClaims)
      : OpConversionPattern(converter, context), stateBitCount(stateBitCount),
        staticPlan(staticPlan), staticSitesEnabled(staticSitesEnabled),
        guardedClaims(guardedClaims) {}

  LogicalResult
  matchAndRewrite(sim::SimNBAEnqueueOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getDestination().size() != 1 || adaptor.getValue().empty())
      return failure();
    std::optional<unsigned> width = nativeStateWidth(op.getValue().getType());
    if (!width)
      return failure();
    Location location = op.getLoc();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i32 = rewriter.getI32Type();
    Type i64 = rewriter.getI64Type();

    sim::NBASiteAttr site = op.getSiteAttr();
    auto staticRoot =
        site && staticPlan
            ? staticPlan->siteRoots.find(site.getId())
            : llvm::DenseMap<uint64_t, uint32_t>::const_iterator{};
    std::optional<uint64_t> destinationValue =
        resolveCFGConstantInteger(adaptor.getDestination().front());
    obelisk_rt_stable_handle_v1 decoded{};
    bool packedStaticStage =
        site && staticPlan && staticRoot != staticPlan->siteRoots.end() &&
        staticRoot->second < staticPlan->roots.size() &&
        adaptor.getDelay().empty() && !site.getTiming() &&
        site.getStorage() != sim::ComputeNBAStorageKind::DynamicFrontier &&
        *width <= 64 && destinationValue &&
        obelisk_rt_stable_handle_decode(*destinationValue, &decoded) &&
        decoded.kind == OBELISK_RT_STABLE_HANDLE_STATIC &&
        decoded.id == staticPlan->roots[staticRoot->second].static_state &&
        decoded.offset >= 0 &&
        static_cast<uint64_t>(decoded.offset) <=
            staticPlan->roots[staticRoot->second].bit_width &&
        *width <= staticPlan->roots[staticRoot->second].bit_width -
                      static_cast<uint64_t>(decoded.offset) &&
        (llvm::all_of(
             adaptor.getValue(),
             [](Value value) { return isa<IntegerType>(value.getType()); }) ||
         (*width <= 64 && adaptor.getValue().size() == 1 &&
          isa<LLVM::LLVMPointerType>(adaptor.getValue().front().getType())));
    if (packedStaticStage) {
      bool assumeClean = op->hasAttr(assumeCleanSpecializationAttr);
      StringRef generatedAccumulator =
          staticRoot->second < staticPlan->generatedAccumulators.size()
              ? staticPlan->generatedAccumulators[staticRoot->second]
              : StringRef{};
      bool useGuardedClaim =
          !assumeClean && (guardedClaims || generatedAccumulator.empty());
      auto widen = [&](Value value) {
        if (isa<LLVM::LLVMPointerType>(value.getType()))
          value = LLVM::LoadOp::create(
              rewriter, location,
              IntegerType::get(rewriter.getContext(), *width), value, 1);
        auto type = cast<IntegerType>(value.getType());
        return type.getWidth() == 64
                   ? value
                   : LLVM::ZExtOp::create(rewriter, location, i64, value)
                         .getResult();
      };
      Value unknown = llvmConstant(rewriter, location, i64, 0);
      if (adaptor.getValue().size() == 2)
        unknown = widen(adaptor.getValue()[1]);
      Value value = widen(adaptor.getValue().front());
      sim::SimFuncOp function = op->getParentOfType<sim::SimFuncOp>();
      uint32_t homeRegion =
          function ? getRuntimeEventRegion(function.getHomeRegion())
                   : UINT32_MAX;
      uint32_t commitRegion = homeRegion == OBELISK_RT_REGION_ACTIVE ||
                                      homeRegion == OBELISK_RT_REGION_REACTIVE
                                  ? homeRegion + 2
                                  : UINT32_MAX;
      uint64_t rootWidth = staticPlan->roots[staticRoot->second].bit_width;
      bool directScalarStage = !generatedAccumulator.empty() &&
                               rootWidth <= 64 && decoded.offset == 0 &&
                               *width == rootWidth;
      bool directLaneStage = !generatedAccumulator.empty() && *width == 32 &&
                             adaptor.getValue().size() == 1 &&
                             (decoded.offset & 31) == 0;
      bool directGeneratedStage =
          commitRegion != UINT32_MAX && (directScalarStage || directLaneStage);
      auto emitDirectGeneratedStage = [&] {
        Value base = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                               generatedAccumulator);
        if (directScalarStage) {
          LLVM::StoreOp::create(
              rewriter, location, value,
              byteGEP(rewriter, location, base,
                      offsetof(obelisk_rt_generated_nba_accumulator_256,
                               value)),
              8);
          LLVM::StoreOp::create(
              rewriter, location, unknown,
              byteGEP(rewriter, location, base,
                      offsetof(obelisk_rt_generated_nba_accumulator_256,
                               unknown)),
              8);
          uint64_t mask = rootWidth == 64
                              ? UINT64_MAX
                              : (uint64_t{1} << rootWidth) - 1;
          LLVM::StoreOp::create(
              rewriter, location,
              llvmConstant(rewriter, location, i64, mask),
              byteGEP(rewriter, location, base,
                      offsetof(obelisk_rt_generated_nba_accumulator_256,
                               write_mask)),
              8);
        } else {
          uint64_t laneOffset = static_cast<uint64_t>(decoded.offset / 8);
          Value laneValue =
              LLVM::TruncOp::create(rewriter, location, i32, value);
          LLVM::StoreOp::create(
              rewriter, location, laneValue,
              byteGEP(
                  rewriter, location, base,
                  offsetof(obelisk_rt_generated_nba_accumulator_256, value) +
                      laneOffset),
              4);
          // This lane form is restricted to two-state values. The generated
          // record is zero-initialized and no other path writes its unknown
          // lanes, so repeatedly storing zero here only adds hot-path traffic.
          LLVM::StoreOp::create(
              rewriter, location,
              llvmConstant(rewriter, location, i32, UINT32_MAX),
              byteGEP(rewriter, location, base,
                      offsetof(obelisk_rt_generated_nba_accumulator_256,
                               write_mask) +
                          laneOffset),
              4);
        }
        LLVM::StoreOp::create(
            rewriter, location, llvmConstant(rewriter, location, i32, 1),
            byteGEP(rewriter, location, base,
                    offsetof(obelisk_rt_generated_nba_accumulator_256, valid)),
            4);
        LLVM::StoreOp::create(
            rewriter, location,
            llvmConstant(rewriter, location, i32, commitRegion),
            byteGEP(rewriter, location, base,
                    offsetof(obelisk_rt_generated_nba_accumulator_256,
                             exec_region)),
            4);
        Value dirtyBase = LLVM::AddressOfOp::create(
            rewriter, location, pointer,
            "__obelisk_aot_nba_dirty_roots_v1");
        Value dirtyWord = byteGEP(
            rewriter, location, dirtyBase,
            static_cast<uint64_t>(staticRoot->second / 64) * sizeof(uint64_t));
        Value previous =
            LLVM::LoadOp::create(rewriter, location, i64, dirtyWord, 8);
        Value marked = LLVM::OrOp::create(
            rewriter, location, previous,
            llvmConstant(rewriter, location, i64,
                         uint64_t{1} << (staticRoot->second % 64)));
        LLVM::StoreOp::create(rewriter, location, marked, dirtyWord, 8);
        Value summaryBase = LLVM::AddressOfOp::create(
            rewriter, location, pointer,
            "__obelisk_aot_nba_dirty_summary_v1");
        uint32_t dirtyWordIndex = staticRoot->second / 64;
        Value summaryWord = byteGEP(
            rewriter, location, summaryBase,
            static_cast<uint64_t>(dirtyWordIndex / 64) * sizeof(uint64_t));
        Value previousSummary =
            LLVM::LoadOp::create(rewriter, location, i64, summaryWord, 8);
        Value markedSummary = LLVM::OrOp::create(
            rewriter, location, previousSummary,
            llvmConstant(rewriter, location, i64,
                         uint64_t{1} << (dirtyWordIndex % 64)));
        LLVM::StoreOp::create(rewriter, location, markedSummary, summaryWord,
                              8);
      };
      if (!useGuardedClaim && directGeneratedStage) {
        emitDirectGeneratedStage();
        rewriter.eraseOp(op);
        return success();
      }
      if (useGuardedClaim && directGeneratedStage) {
        Value useDirect = staticNBASpecializationGuard(rewriter, location,
                                                       staticRoot->second);
        Block *head = rewriter.getInsertionBlock();
        Block *continuation =
            rewriter.splitBlock(head, rewriter.getInsertionPoint());
        Region *region = head->getParent();
        Block *directBlock =
            rewriter.createBlock(region, continuation->getIterator());
        Block *claimBlock =
            rewriter.createBlock(region, continuation->getIterator());
        recordStaticSpecializationCFGBlocks(rewriter, head, 3);

        rewriter.setInsertionPointToEnd(head);
        markLikelyTrue(cf::CondBranchOp::create(rewriter, location, useDirect,
                                                directBlock, ValueRange{},
                                                claimBlock, ValueRange{}));

        rewriter.setInsertionPointToEnd(directBlock);
        emitDirectGeneratedStage();
        cf::BranchOp::create(rewriter, location, continuation);

        rewriter.setInsertionPointToEnd(claimBlock);
        Value contextAddress = LLVM::AddressOfOp::create(
            rewriter, location, pointer, "__obelisk_current_context");
        Value runtimeContext = LLVM::LoadOp::create(rewriter, location, pointer,
                                                    contextAddress, 8);
        Value status =
            LLVM::CallOp::create(
                rewriter, location, TypeRange{i32},
                SymbolRefAttr::get(rewriter.getContext(),
                                   "obelisk_rt_v1_static_nba_claim"),
                ValueRange{
                    runtimeContext,
                    llvmConstant(rewriter, location, i32, staticRoot->second),
                    LLVM::AddressOfOp::create(rewriter, location, pointer,
                                              "__obelisk_state_value"),
                    LLVM::ZeroOp::create(rewriter, location, pointer),
                    llvmConstant(rewriter, location, i64, stateBitCount),
                    llvmConstant(rewriter, location, i64,
                                 static_cast<uint64_t>(decoded.offset)),
                    llvmConstant(rewriter, location, i64, *width), value,
                    unknown})
                .getResult();
        LLVM::CallOp::create(rewriter, location, TypeRange{},
                             SymbolRefAttr::get(rewriter.getContext(),
                                                "obelisk_rt_v1_scheduler_fail"),
                             ValueRange{runtimeContext, status});
        cf::BranchOp::create(rewriter, location, continuation);

        rewriter.setInsertionPointToStart(continuation);
        rewriter.eraseOp(op);
        return success();
      }
      Value contextAddress = LLVM::AddressOfOp::create(
          rewriter, location, pointer, "__obelisk_current_context");
      Value runtimeContext =
          LLVM::LoadOp::create(rewriter, location, pointer, contextAddress, 8);
      if (!useGuardedClaim) {
        LLVM::CallOp::create(
            rewriter, location, TypeRange{},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_static_nba_stage_wide"),
            ValueRange{
                runtimeContext,
                llvmConstant(rewriter, location, i32, staticRoot->second),
                llvmConstant(rewriter, location, i64,
                             static_cast<uint64_t>(decoded.offset)),
                llvmConstant(rewriter, location, i64, *width), value, unknown,
                llvmConstant(rewriter, location, i32,
                             adaptor.getValue().size() == 2 ? 1 : 0)});
        rewriter.eraseOp(op);
        return success();
      }
      Value status =
          LLVM::CallOp::create(
              rewriter, location, TypeRange{i32},
              SymbolRefAttr::get(rewriter.getContext(),
                                 "obelisk_rt_v1_static_nba_claim"),
              ValueRange{
                  runtimeContext,
                  llvmConstant(rewriter, location, i32, staticRoot->second),
                  LLVM::AddressOfOp::create(rewriter, location, pointer,
                                            "__obelisk_state_value"),
                  adaptor.getValue().size() == 2
                      ? LLVM::AddressOfOp::create(rewriter, location, pointer,
                                                  "__obelisk_state_unknown")
                            .getResult()
                      : LLVM::ZeroOp::create(rewriter, location, pointer)
                            .getResult(),
                  llvmConstant(rewriter, location, i64, stateBitCount),
                  llvmConstant(rewriter, location, i64,
                               static_cast<uint64_t>(decoded.offset)),
                  llvmConstant(rewriter, location, i64, *width), value,
                  unknown})
              .getResult();
      LLVM::CallOp::create(rewriter, location, TypeRange{},
                           SymbolRefAttr::get(rewriter.getContext(),
                                              "obelisk_rt_v1_scheduler_fail"),
                           ValueRange{runtimeContext, status});
      rewriter.eraseOp(op);
      return success();
    }

    Value contextAddress = LLVM::AddressOfOp::create(
        rewriter, location, pointer, "__obelisk_current_context");
    Value runtimeContext =
        LLVM::LoadOp::create(rewriter, location, pointer, contextAddress, 8);
    auto savePlane = [&](Value value) {
      Value address = entryAlloca(rewriter, location, value.getType(), 1, 1);
      LLVM::StoreOp::create(rewriter, location, value, address, 1);
      return address;
    };
    Value value = savePlane(adaptor.getValue().front());
    Value unknown = LLVM::ZeroOp::create(rewriter, location, pointer);
    Value unknownPlane = LLVM::ZeroOp::create(rewriter, location, pointer);
    if (adaptor.getValue().size() == 2) {
      unknown = savePlane(adaptor.getValue()[1]);
      unknownPlane = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                               "__obelisk_state_unknown");
    }
    Value valuePlane = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                                 "__obelisk_state_value");
    Value delay = adaptor.getDelay().empty()
                      ? llvmConstant(rewriter, location, i64, 0)
                      : adaptor.getDelay().front();
    if (isa<sim::StringType>(op.getValue().getType())) {
      Value status =
          LLVM::CallOp::create(
              rewriter, location, TypeRange{i32},
              SymbolRefAttr::get(rewriter.getContext(),
                                 "obelisk_rt_v1_scheduler_string_nba"),
              ValueRange{runtimeContext, valuePlane,
                         llvmConstant(rewriter, location, i64, stateBitCount),
                         adaptor.getDestination().front(), delay,
                         adaptor.getValue().front()})
              .getResult();
      LLVM::CallOp::create(rewriter, location, TypeRange{},
                           SymbolRefAttr::get(rewriter.getContext(),
                                              "obelisk_rt_v1_scheduler_fail"),
                           ValueRange{runtimeContext, status});
    } else {
      bool staticallyStaged =
          staticSitesEnabled && staticPlan && site &&
          staticPlan->siteRoots.contains(site.getId()) &&
          adaptor.getDelay().empty() && !site.getTiming() &&
          site.getStorage() != sim::ComputeNBAStorageKind::DynamicFrontier;
      SmallVector<Value> arguments{
          runtimeContext,
          valuePlane,
          unknownPlane,
          llvmConstant(rewriter, location, i64, stateBitCount),
          adaptor.getDestination().front(),
          llvmConstant(rewriter, location, i64, *width)};
      if (staticallyStaged)
        arguments.insert(arguments.begin() + 1,
                         llvmConstant(rewriter, location, i64, site.getId()));
      else
        arguments.push_back(delay);
      arguments.push_back(value);
      arguments.push_back(unknown);
      Value status =
          LLVM::CallOp::create(
              rewriter, location, TypeRange{i32},
              SymbolRefAttr::get(rewriter.getContext(),
                                 staticallyStaged
                                     ? "obelisk_rt_v1_scheduler_static_nba"
                                     : "obelisk_rt_v1_scheduler_nba"),
              arguments)
              .getResult();
      LLVM::CallOp::create(rewriter, location, TypeRange{},
                           SymbolRefAttr::get(rewriter.getContext(),
                                              "obelisk_rt_v1_scheduler_fail"),
                           ValueRange{runtimeContext, status});
    }
    rewriter.eraseOp(op);
    return success();
  }

private:
  uint64_t stateBitCount;
  const NativeStaticNBAPlan *staticPlan;
  bool staticSitesEnabled;
  bool guardedClaims;
};

} // namespace

void populateNBAToLLVMConversionPatterns(RewritePatternSet &patterns,
                                         TypeConverter &converter,
                                         uint64_t stateBitCount,
                                         const NativeStaticNBAPlan *staticPlan,
                                         bool staticSitesEnabled,
                                         bool guardedClaims) {
  patterns.add<ImmediateNBAConversion>(converter, patterns.getContext(),
                                       stateBitCount, staticPlan,
                                       staticSitesEnabled, guardedClaims);
}

} // namespace obelisk::detail
