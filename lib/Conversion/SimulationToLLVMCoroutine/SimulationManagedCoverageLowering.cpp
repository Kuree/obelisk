//===- SimulationManagedCoverageLowering.cpp - Covergroup patterns ---===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Transforms/DialectConversion.h"

using namespace mlir;

namespace obelisk::detail {

namespace {

class CovergroupNullConversion final
    : public OpConversionPattern<sim::SimCovergroupNullOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimCovergroupNullOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    rewriter.replaceOp(
        op, LLVM::ZeroOp::create(rewriter, op.getLoc(), rewriter.getI64Type()));
    return success();
  }
};

class CovergroupCreateConversion final
    : public OpConversionPattern<sim::SimCovergroupCreateOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimCovergroupCreateOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto declaration =
        SymbolTable::lookupNearestSymbolFrom<sim::SimCovergroupDeclOp>(
            op, op.getDeclarationAttr());
    if (!declaration)
      return failure();
    Type i64 = rewriter.getI64Type();
    Value bins = entryAlloca(rewriter, op.getLoc(), i64,
                             declaration.getCoverpointBins().size(), 8);
    for (auto [index, count] : llvm::enumerate(declaration.getCoverpointBins()))
      LLVM::StoreOp::create(
          rewriter, op.getLoc(),
          llvmConstant(rewriter, op.getLoc(), i64,
                       static_cast<uint64_t>(count)),
          byteGEP(rewriter, op.getLoc(), bins, index * sizeof(uint64_t)), 8);
    Value output = entryAlloca(rewriter, op.getLoc(), i64, 1, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), i64),
                          output, 8);
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_covergroup_create"),
            ValueRange{
                context,
                llvmConstant(rewriter, op.getLoc(), i64, declaration.getId()),
                bins,
                llvmConstant(rewriter, op.getLoc(), i64,
                             declaration.getCoverpointBins().size()),
                output})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.replaceOp(
        op, LLVM::LoadOp::create(rewriter, op.getLoc(), i64, output, 8));
    return success();
  }
};

template <typename Op, bool Enabled>
class CovergroupControlConversion final : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;
  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getHandle().size() != 1)
      return failure();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_covergroup_set_enabled"),
            ValueRange{context, adaptor.getHandle().front(),
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI32Type(), Enabled)})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class CovergroupEnabledConversion final
    : public OpConversionPattern<sim::SimCovergroupSampleEnabledOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimCovergroupSampleEnabledOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getHandle().size() != 1)
      return failure();
    Type i32 = rewriter.getI32Type();
    Value output = entryAlloca(rewriter, op.getLoc(), i32, 1, 4);
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_covergroup_sample_enabled"),
            ValueRange{context, adaptor.getHandle().front(), output})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    Value enabled = LLVM::LoadOp::create(rewriter, op.getLoc(), i32, output, 4);
    rewriter.replaceOp(op,
                       LLVM::TruncOp::create(rewriter, op.getLoc(),
                                             rewriter.getI1Type(), enabled));
    return success();
  }
};

class CovergroupBinHitConversion final
    : public OpConversionPattern<sim::SimCovergroupBinHitOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimCovergroupBinHitOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getHandle().size() != 1)
      return failure();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_covergroup_bin_hit"),
            ValueRange{context, adaptor.getHandle().front(),
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI32Type(), op.getCoverpoint()),
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI32Type(), op.getBin())})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class CovergroupSampleConversion final
    : public OpConversionPattern<sim::SimCovergroupSampleOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimCovergroupSampleOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getHandle().size() != 1)
      return failure();
    SmallVector<Value> hitValues = flatten(adaptor.getHits());
    Type i8 = rewriter.getI8Type();
    Value hits = entryAlloca(rewriter, op.getLoc(), i8, hitValues.size(), 1);
    for (auto [index, hit] : llvm::enumerate(hitValues))
      LLVM::StoreOp::create(
          rewriter, op.getLoc(),
          LLVM::ZExtOp::create(rewriter, op.getLoc(), i8, hit),
          byteGEP(rewriter, op.getLoc(), hits, index), 1);
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_covergroup_sample"),
            ValueRange{context, adaptor.getHandle().front(), hits,
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI64Type(), hitValues.size())})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

template <typename Op, bool IsType>
class CovergroupQueryConversion final : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;
  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type f64 = rewriter.getF64Type();
    Type i32 = rewriter.getI32Type();
    Value percentage = entryAlloca(rewriter, op.getLoc(), f64, 1, 8);
    Value covered = entryAlloca(rewriter, op.getLoc(), i32, 1, 4);
    Value total = entryAlloca(rewriter, op.getLoc(), i32, 1, 4);
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value key;
    StringRef symbol;
    SmallVector<Value> arguments{context};
    if constexpr (IsType) {
      auto declaration =
          SymbolTable::lookupNearestSymbolFrom<sim::SimCovergroupDeclOp>(
              op, op.getDeclarationAttr());
      if (!declaration)
        return failure();
      key = llvmConstant(rewriter, op.getLoc(), rewriter.getI64Type(),
                         declaration.getId());
      symbol = "obelisk_rt_v1_covergroup_type_query";
      arguments.push_back(key);
      Type i64 = rewriter.getI64Type();
      Value bins = entryAlloca(rewriter, op.getLoc(), i64,
                               declaration.getCoverpointBins().size(), 8);
      for (auto [index, count] :
           llvm::enumerate(declaration.getCoverpointBins()))
        LLVM::StoreOp::create(
            rewriter, op.getLoc(),
            llvmConstant(rewriter, op.getLoc(), i64,
                         static_cast<uint64_t>(count)),
            byteGEP(rewriter, op.getLoc(), bins, index * sizeof(uint64_t)), 8);
      arguments.push_back(bins);
      arguments.push_back(llvmConstant(rewriter, op.getLoc(), i64,
                                       declaration.getCoverpointBins().size()));
    } else {
      if (adaptor.getHandle().size() != 1)
        return failure();
      key = adaptor.getHandle().front();
      symbol = "obelisk_rt_v1_covergroup_instance_query";
      arguments.push_back(key);
    }
    arguments.append({percentage, covered, total});
    Value status =
        LLVM::CallOp::create(rewriter, op.getLoc(), TypeRange{i32},
                             SymbolRefAttr::get(rewriter.getContext(), symbol),
                             arguments)
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.replaceOp(
        op, ValueRange{
                LLVM::LoadOp::create(rewriter, op.getLoc(), f64, percentage, 8),
                LLVM::LoadOp::create(rewriter, op.getLoc(), i32, covered, 4),
                LLVM::LoadOp::create(rewriter, op.getLoc(), i32, total, 4)});
    return success();
  }
};

} // namespace

void populateManagedCoverageToLLVMConversionPatterns(
    RewritePatternSet &patterns, TypeConverter &converter) {
  MLIRContext *context = patterns.getContext();
  patterns.add<
      CovergroupNullConversion, CovergroupCreateConversion,
      CovergroupEnabledConversion, CovergroupBinHitConversion,
      CovergroupSampleConversion,
      CovergroupControlConversion<sim::SimCovergroupStartOp, true>,
      CovergroupControlConversion<sim::SimCovergroupStopOp, false>,
      CovergroupQueryConversion<sim::SimCovergroupInstanceQueryOp, false>,
      CovergroupQueryConversion<sim::SimCovergroupTypeQueryOp, true>>(
      converter, context);
}

} // namespace obelisk::detail

