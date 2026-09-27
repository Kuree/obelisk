//===- SimulationManagedCoverageLowering.cpp - Covergroup patterns ---===//

#include "SimulationToLLVMCoroutinePrivate.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Transforms/DialectConversion.h"

#include "llvm/IR/DataLayout.h"

#include <type_traits>

using namespace mlir;

namespace obelisk::detail {

namespace {

class CovergroupCastConversion final
    : public OpConversionPattern<sim::SimCovergroupCastOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimCovergroupCastOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getInput().size() != 1)
      return failure();
    rewriter.replaceOp(op, adaptor.getInput());
    return success();
  }
};

struct FunctionalValueLayout {
  uint64_t pointerSize = 0;
  uint64_t unknown = 0;
  uint64_t owner = 0;
  uint64_t payload = 0;
  uint64_t kind = 0;
  uint64_t argumentRefKind = 0;
  uint64_t size = 0;
};

FunctionalValueLayout
getFunctionalValueLayout(const llvm::DataLayout &dataLayout) {
  FunctionalValueLayout layout;
  layout.pointerSize = dataLayout.getPointerSize();
  layout.unknown = 24 + layout.pointerSize;
  layout.owner = layout.unknown + layout.pointerSize;
  layout.payload = (layout.owner + layout.pointerSize + 7) & ~uint64_t{7};
  layout.kind = layout.payload + 8;
  layout.argumentRefKind = layout.kind + 4;
  layout.size = (layout.argumentRefKind + 4 + 7) & ~uint64_t{7};
  return layout;
}

void storeCoverageWord(ConversionPatternRewriter &rewriter, Location location,
                       Value base, uint64_t offset, Value value,
                       unsigned alignment = 8) {
  LLVM::StoreOp::create(rewriter, location, value,
                        byteGEP(rewriter, location, base, offset), alignment);
}

LogicalResult emitCoverageValueDescriptor(ConversionPatternRewriter &rewriter,
                                          Location location, Value descriptor,
                                          Type sourceType, ValueRange converted,
                                          uint64_t id,
                                          const FunctionalValueLayout &layout) {
  Type i32 = rewriter.getI32Type();
  Type i64 = rewriter.getI64Type();
  Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
  Value null = LLVM::ZeroOp::create(rewriter, location, pointer);
  Value valuePointer = null, unknownPointer = null, ownerPointer = null;
  Value payload = llvmConstant(rewriter, location, i64, 0);
  Value referenceKind = llvmConstant(rewriter, location, i32, 0);
  uint64_t width = 0, size = 0, kind = 0;
  if (auto argumentRef = dyn_cast<sim::ArgumentRefType>(sourceType)) {
    if (converted.size() != 1 || !converted.front().getType().isInteger(192))
      return failure();
    std::optional<unsigned> packedWidth =
        sim::getPackedWidth(argumentRef.getElementType());
    if (!packedWidth)
      return failure();
    width = *packedWidth;
    size = (width + 7) / 8;
    kind = OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF;
    Value packed = converted.front();
    Value ownerWord = LLVM::TruncOp::create(rewriter, location, i64, packed);
    ownerPointer = managedObjectPointer(rewriter, location, ownerWord);
    Value shiftedPayload = LLVM::LShrOp::create(
        rewriter, location, packed,
        llvmConstant(rewriter, location, packed.getType(), 64));
    payload = LLVM::TruncOp::create(rewriter, location, i64, shiftedPayload);
    Value shiftedTag = LLVM::LShrOp::create(
        rewriter, location, packed,
        llvmConstant(rewriter, location, packed.getType(), 128));
    referenceKind = LLVM::TruncOp::create(rewriter, location, i32, shiftedTag);
  } else if (isa<sim::DynamicArrayType, sim::QueueType>(sourceType)) {
    if (converted.size() != 1 || !converted.front().getType().isInteger(64))
      return failure();
    kind = OBELISK_RT_FUNCTIONAL_VALUE_MANAGED_CONTAINER;
    ownerPointer = managedObjectPointer(rewriter, location, converted.front());
  } else if (isa<sim::StringType>(sourceType)) {
    if (converted.size() != 1 || !converted.front().getType().isInteger(64))
      return failure();
    kind = OBELISK_RT_FUNCTIONAL_VALUE_STRING;
    payload = converted.front();
  } else if (sourceType.isF64()) {
    if (converted.size() != 1)
      return failure();
    width = 64;
    size = sizeof(double);
    kind = OBELISK_RT_FUNCTIONAL_VALUE_REAL;
    valuePointer =
        entryAlloca(rewriter, location, converted.front().getType(), 1, 8);
    LLVM::StoreOp::create(rewriter, location, converted.front(), valuePointer,
                          8);
  } else {
    std::optional<unsigned> packedWidth = sim::getPackedWidth(sourceType);
    if (!packedWidth || converted.empty() || converted.size() > 2)
      return failure();
    width = *packedWidth;
    size = (width + 7) / 8;
    kind = converted.size() == 2 ? OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE
                                 : OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL;
    valuePointer =
        entryAlloca(rewriter, location, converted.front().getType(), 1, 8);
    LLVM::StoreOp::create(rewriter, location, converted.front(), valuePointer,
                          8);
    if (converted.size() == 2) {
      unknownPointer =
          entryAlloca(rewriter, location, converted[1].getType(), 1, 8);
      LLVM::StoreOp::create(rewriter, location, converted[1], unknownPointer,
                            8);
    }
  }
  storeCoverageWord(rewriter, location, descriptor, 0,
                    llvmConstant(rewriter, location, i64, id));
  storeCoverageWord(rewriter, location, descriptor, 8,
                    llvmConstant(rewriter, location, i64, width));
  storeCoverageWord(rewriter, location, descriptor, 16,
                    llvmConstant(rewriter, location, i64, size));
  storeCoverageWord(rewriter, location, descriptor, 24, valuePointer);
  storeCoverageWord(rewriter, location, descriptor, layout.unknown,
                    unknownPointer, layout.pointerSize);
  storeCoverageWord(rewriter, location, descriptor, layout.owner, ownerPointer,
                    layout.pointerSize);
  storeCoverageWord(rewriter, location, descriptor, layout.payload, payload);
  storeCoverageWord(rewriter, location, descriptor, layout.kind,
                    llvmConstant(rewriter, location, i32, kind), 4);
  storeCoverageWord(rewriter, location, descriptor, layout.argumentRefKind,
                    referenceKind, 4);
  return success();
}

class CoveragePointHitConversion final
    : public OpConversionPattern<sim::SimCoveragePointHitOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimCoveragePointHitOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getEnabled().size() != 1)
      return failure();
    Value context = managedContext(rewriter, op.getLoc());
    Value enabled =
        LLVM::ZExtOp::create(rewriter, op.getLoc(), rewriter.getI32Type(),
                             adaptor.getEnabled().front());
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_coverage_point_hit"),
            ValueRange{context,
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI64Type(), op.getPoint()),
                       enabled})
            .getResult();
    // Generated eval bodies are admitted only after the point index has been
    // verified against the module inventory, and the context is their first
    // ABI capture. The runtime operation is therefore a nonfailing relaxed
    // atomic increment. Avoid introducing a scheduler-failure edge into the
    // otherwise closed hot body; ordinary coroutine code keeps the defensive
    // status check below.
    auto function = op->getParentOfType<sim::SimFuncOp>();
    if (!function ||
        !::obelisk::schedule::has<::obelisk::schedule::Field::EvalRawCaptures>(
            function))
      reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

template <typename Op>
class CoverageControlConversion final : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;
  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getControl().size() != 1 || adaptor.getMetric().size() != 1 ||
        adaptor.getScope().size() != 1)
      return failure();
    constexpr bool isDefinition =
        std::is_same_v<Op, sim::SimCoverageControlDefinitionOp>;
    if constexpr (isDefinition)
      if (adaptor.getDefinition().size() != 1)
        return failure();

    Type i32 = rewriter.getI32Type();
    Value output = entryAlloca(rewriter, op.getLoc(), i32, 1, 4);
    Value context = managedContext(rewriter, op.getLoc());
    SmallVector<Value> arguments{context, adaptor.getControl().front(),
                                 adaptor.getMetric().front(),
                                 adaptor.getScope().front()};
    StringRef symbol;
    if constexpr (isDefinition) {
      symbol = "obelisk_rt_v1_coverage_control_definition";
      arguments.push_back(adaptor.getDefinition().front());
    } else {
      symbol = "obelisk_rt_v1_coverage_control_instance";
      arguments.push_back(llvmConstant(rewriter, op.getLoc(),
                                       rewriter.getI64Type(),
                                       op.getCoverageScopeId()));
    }
    arguments.push_back(output);
    Value status =
        LLVM::CallOp::create(rewriter, op.getLoc(), TypeRange{i32},
                             SymbolRefAttr::get(rewriter.getContext(), symbol),
                             arguments)
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.replaceOp(
        op, LLVM::LoadOp::create(rewriter, op.getLoc(), i32, output, 4));
    return success();
  }
};

template <typename Op>
class CoverageQueryConversion final : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;
  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getMetric().size() != 1 || adaptor.getScope().size() != 1)
      return failure();
    constexpr bool isDefinition =
        std::is_same_v<Op, sim::SimCoverageQueryDefinitionOp>;
    if constexpr (isDefinition)
      if (adaptor.getDefinition().size() != 1)
        return failure();

    Type i32 = rewriter.getI32Type();
    Value output = entryAlloca(rewriter, op.getLoc(), i32, 1, 4);
    Value context = managedContext(rewriter, op.getLoc());
    SmallVector<Value> arguments{context, adaptor.getMetric().front(),
                                 adaptor.getScope().front()};
    StringRef symbol;
    if constexpr (isDefinition) {
      symbol = "obelisk_rt_v1_coverage_query_definition";
      arguments.push_back(adaptor.getDefinition().front());
    } else {
      symbol = "obelisk_rt_v1_coverage_query_instance";
      arguments.push_back(llvmConstant(rewriter, op.getLoc(),
                                       rewriter.getI64Type(),
                                       op.getCoverageScopeId()));
    }
    arguments.push_back(
        llvmConstant(rewriter, op.getLoc(), i32, op.getMaximum() ? 1 : 0));
    arguments.push_back(output);
    Value status =
        LLVM::CallOp::create(rewriter, op.getLoc(), TypeRange{i32},
                             SymbolRefAttr::get(rewriter.getContext(), symbol),
                             arguments)
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.replaceOp(
        op, LLVM::LoadOp::create(rewriter, op.getLoc(), i32, output, 4));
    return success();
  }
};

template <typename Op>
class CoverageDatabaseConversion final : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;
  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getMetric().size() != 1 || adaptor.getName().size() != 1)
      return failure();
    Type i32 = rewriter.getI32Type();
    Value output = entryAlloca(rewriter, op.getLoc(), i32, 1, 4);
    Value context = managedContext(rewriter, op.getLoc());
    StringRef symbol = std::is_same_v<Op, sim::SimCoverageSaveOp>
                           ? "obelisk_rt_v1_coverage_database_save"
                           : "obelisk_rt_v1_coverage_database_merge";
    Value status =
        LLVM::CallOp::create(rewriter, op.getLoc(), TypeRange{i32},
                             SymbolRefAttr::get(rewriter.getContext(), symbol),
                             ValueRange{context, adaptor.getMetric().front(),
                                        adaptor.getName().front(), output})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.replaceOp(
        op, LLVM::LoadOp::create(rewriter, op.getLoc(), i32, output, 4));
    return success();
  }
};

class FunctionalCoverageGetConversion final
    : public OpConversionPattern<sim::SimFunctionalCoverageGetOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimFunctionalCoverageGetOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type f64 = rewriter.getF64Type();
    Value output = entryAlloca(rewriter, op.getLoc(), f64, 1, 8);
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_functional_coverage_get"),
            ValueRange{context, output})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.replaceOp(
        op, LLVM::LoadOp::create(rewriter, op.getLoc(), f64, output, 8));
    return success();
  }
};

template <typename Op>
class FunctionalCoverageDatabaseConversion final
    : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;
  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getName().size() != 1)
      return failure();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    StringRef symbol;
    if constexpr (std::is_same_v<Op, sim::SimFunctionalCoverageSetDbNameOp>)
      symbol = "obelisk_rt_v1_functional_coverage_set_db_name";
    else
      symbol = "obelisk_rt_v1_functional_coverage_load_db";
    Value status = LLVM::CallOp::create(
                       rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(), symbol),
                       ValueRange{context, adaptor.getName().front()})
                       .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

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
  CovergroupCreateConversion(TypeConverter &converter, MLIRContext *context,
                             FunctionalValueLayout layout)
      : OpConversionPattern(converter, context), layout(layout) {}
  LogicalResult
  matchAndRewrite(sim::SimCovergroupCreateOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto declaration =
        SymbolTable::lookupNearestSymbolFrom<sim::SimCovergroupDeclOp>(
            op, op.getDeclarationAttr());
    if (!declaration || adaptor.getPayloads().size() != op.getPayloads().size())
      return failure();
    uint64_t formalCount = op.getArgumentCount();
    uint64_t expressionCount = op.getExpressionIds().size();
    Type i8 = rewriter.getI8Type();
    Type i64 = rewriter.getI64Type();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value null = LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer);
    Value formals = formalCount ? entryAlloca(rewriter, op.getLoc(), i8,
                                              formalCount * layout.size, 8)
                                : null;
    Value expressions = expressionCount
                            ? entryAlloca(rewriter, op.getLoc(), i8,
                                          expressionCount * layout.size, 8)
                            : null;
    for (uint64_t index = 0; index != formalCount; ++index)
      if (failed(emitCoverageValueDescriptor(
              rewriter, op.getLoc(),
              byteGEP(rewriter, op.getLoc(), formals, index * layout.size),
              op.getPayloads()[index].getType(), adaptor.getPayloads()[index],
              static_cast<uint64_t>(op.getFormalIds()[index]), layout)))
        return failure();
    for (uint64_t index = 0; index != expressionCount; ++index) {
      uint64_t payloadIndex = formalCount + index;
      if (failed(emitCoverageValueDescriptor(
              rewriter, op.getLoc(),
              byteGEP(rewriter, op.getLoc(), expressions, index * layout.size),
              op.getPayloads()[payloadIndex].getType(),
              adaptor.getPayloads()[payloadIndex],
              static_cast<uint64_t>(op.getExpressionIds()[index]), layout)))
        return failure();
    }
    Value output = entryAlloca(rewriter, op.getLoc(), i64, 1, 8);
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_covergroup_create"),
            ValueRange{
                context,
                llvmConstant(rewriter, op.getLoc(), i64,
                             declaration.getSchemaType()),
                formals, llvmConstant(rewriter, op.getLoc(), i64, formalCount),
                expressions,
                llvmConstant(rewriter, op.getLoc(), i64, expressionCount),
                output})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.replaceOp(
        op, LLVM::LoadOp::create(rewriter, op.getLoc(), i64, output, 8));
    return success();
  }

private:
  FunctionalValueLayout layout;
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
                                    rewriter.getI64Type(), op.getItem()),
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

FailureOr<Value>
serializeCovergroupSamplerCaptures(Operation *operation,
                                   Operation::operand_range captures,
                                   ConversionPatternRewriter &rewriter) {
  if (captures.size() > UINT32_MAX)
    return operation->emitError(
        "sampler observer capture count exceeds v1 ABI");

  Location location = operation->getLoc();
  Type i8 = rewriter.getI8Type();
  Type i64 = rewriter.getI64Type();
  Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
  Value null = LLVM::ZeroOp::create(rewriter, location, pointer);
  constexpr uint64_t captureSize = sizeof(obelisk_rt_computed_capture_v1);
  static_assert(captureSize % sizeof(uint64_t) == 0);
  Value records =
      captures.empty()
          ? null
          : entryAlloca(rewriter, location, i8, captures.size() * captureSize,
                        alignof(obelisk_rt_computed_capture_v1));

  for (auto [index, capture] : llvm::enumerate(captures)) {
    Value record = byteGEP(rewriter, location, records, index * captureSize);
    auto integer = dyn_cast<IntegerType>(capture.getType());
    if (!integer || (integer.getWidth() != 64 && integer.getWidth() != 192))
      return operation->emitError(
          "sampler capture is neither a stable handle nor argument ref");
    LLVM::StoreOp::create(rewriter, location, capture, record, 8);
    uint64_t firstZeroWord = integer.getWidth() == 192 ? 3 : 1;
    for (uint64_t word = firstZeroWord; word != captureSize / sizeof(uint64_t);
         ++word)
      LLVM::StoreOp::create(
          rewriter, location, llvmConstant(rewriter, location, i64, 0),
          byteGEP(rewriter, location, record, word * sizeof(uint64_t)), 8);
  }
  return records;
}

class CovergroupClockEventRegisterConversion final
    : public OpConversionPattern<sim::SimCovergroupClockEventRegisterOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimCovergroupClockEventRegisterOp op,
                  OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    const uint64_t primaryCount = op.getPrimaries().size();
    if (adaptor.getValues().size() != op.getValues().size() ||
        adaptor.getHandle().size() != 1)
      return failure();
    for (uint64_t index = 0; index != primaryCount; ++index) {
      if (adaptor.getValues()[index].size() != 1)
        return failure();
      ValueRange initial = adaptor.getValues()[primaryCount + index];
      if (initial.empty() || initial.size() > 2)
        return failure();
    }
    for (uint64_t index = primaryCount * 2; index != adaptor.getValues().size();
         ++index)
      if (adaptor.getValues()[index].size() != 1)
        return failure();

    Value convertedSampler = adaptor.getValues().back().front();
    Operation *binding = getConvertedObserverBinding(convertedSampler);
    auto observerID =
        binding ? ::obelisk::schedule::get<
                      ::obelisk::schedule::Field::NativeObserverId>(binding)
                : IntegerAttr{};
    if (!binding || !observerID ||
        !getConvertedObserverDependencies(binding).empty())
      return failure();
    Operation::operand_range captures = getConvertedObserverCaptures(binding);
    if (captures.size() > UINT32_MAX)
      return op.emitOpError("sampler observer capture count exceeds v1 ABI");

    Location location = op.getLoc();
    Type i8 = rewriter.getI8Type();
    Type i32 = rewriter.getI32Type();
    Type i64 = rewriter.getI64Type();
    FailureOr<Value> captureRecords =
        serializeCovergroupSamplerCaptures(op, captures, rewriter);
    if (failed(captureRecords))
      return failure();

    SmallVector<Operation *> eventBindings;
    SmallVector<ValueRange> initialPlanes;
    eventBindings.reserve(primaryCount + op.getConditions().size());
    initialPlanes.reserve(primaryCount);
    for (uint64_t index = 0; index != primaryCount; ++index) {
      Operation *primary =
          getConvertedObserverBinding(adaptor.getValues()[index].front());
      if (!primary)
        return op.emitOpError(
            "primary token has malformed converted observer metadata");
      eventBindings.push_back(primary);
      initialPlanes.push_back(adaptor.getValues()[primaryCount + index]);
    }
    for (uint64_t index = 0; index != op.getConditions().size(); ++index) {
      Operation *condition = getConvertedObserverBinding(
          adaptor.getValues()[primaryCount * 2 + index].front());
      if (!condition)
        return op.emitOpError(
            "condition token has malformed converted observer metadata");
      eventBindings.push_back(condition);
    }
    uint64_t observerCount = eventBindings.size();
    uint64_t eventCaptureCount = 0;
    uint64_t dependencyCount = 0;
    uint64_t previousLimbs = 0;
    for (auto [index, eventBinding] : llvm::enumerate(eventBindings)) {
      uint64_t captures = getConvertedObserverCaptureCount(eventBinding);
      uint64_t dependencies =
          getConvertedObserverDependencies(eventBinding).size();
      if (captures > UINT32_MAX - eventCaptureCount ||
          dependencies > UINT32_MAX - dependencyCount)
        return eventBinding->emitOpError(
            "converted observer inventory exceeds v1 count fields");
      eventCaptureCount += captures;
      dependencyCount += dependencies;
      if (index < primaryCount) {
        auto width = ::obelisk::schedule::get<
            ::obelisk::schedule::Field::NativeObserverWidth>(eventBinding);
        if (!width || width.getValue().isNegative() ||
            width.getValue().isZero() || width.getValue().getActiveBits() > 32)
          return eventBinding->emitOpError("missing converted observer width");
        uint64_t limbs = (width.getValue().getZExtValue() + uint64_t{63}) / 64;
        if (limbs > UINT32_MAX - previousLimbs)
          return eventBinding->emitOpError(
              "computed observer history exceeds v1 limb count");
        previousLimbs += limbs;
      }
    }
    auto addProduct = [&](uint64_t &size, uint64_t count,
                          uint64_t stride) -> LogicalResult {
      if (count > (UINT64_MAX - size) / stride)
        return op.emitOpError("computed event record size overflow");
      size += count * stride;
      return success();
    };
    uint64_t recordSize = sizeof(obelisk_rt_computed_wait_record_v1);
    if (failed(addProduct(recordSize, observerCount,
                          sizeof(obelisk_rt_computed_observer_v1))) ||
        failed(addProduct(recordSize, eventCaptureCount,
                          sizeof(obelisk_rt_computed_capture_v1))) ||
        failed(addProduct(recordSize, dependencyCount,
                          sizeof(obelisk_rt_computed_dependency_v1))) ||
        failed(addProduct(recordSize, primaryCount,
                          sizeof(obelisk_rt_computed_clause_v1))) ||
        failed(addProduct(recordSize, previousLimbs, sizeof(uint64_t) * 2)))
      return failure();
    if (recordSize > UINT32_MAX)
      return op.emitOpError("computed event record exceeds v1 offset fields");
    Value record = entryAlloca(rewriter, location, i8, recordSize,
                               alignof(obelisk_rt_computed_wait_record_v1));
    SmallVector<Operation *> serializedBindings;
    if (failed(serializeComputedObserverRecord(
            op, eventBindings, initialPlanes, op.getEdges(),
            op.getConditionIndices(), record, recordSize, rewriter,
            serializedBindings)))
      return failure();

    auto [context, lane] = managedContextAndLane(rewriter, location);
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_covergroup_clock_event_register"),
            ValueRange{
                context, record,
                llvmConstant(rewriter, location, i64, recordSize),
                llvmConstant(rewriter, location, i32, op.getStrobe() ? 1 : 0),
                llvmConstant(rewriter, location, i64,
                             observerID.getValue().getZExtValue()),
                *captureRecords,
                llvmConstant(rewriter, location, i32,
                             static_cast<uint32_t>(captures.size()))})
            .getResult();
    reportManagedStatus(rewriter, location, context, status);
    rewriter.eraseOp(op);
    for (Operation *eventBinding : serializedBindings)
      if (eventBinding->use_empty())
        rewriter.eraseOp(eventBinding);
    if (binding->use_empty())
      rewriter.eraseOp(binding);
    return success();
  }
};

class CovergroupBlockEventRegisterConversion final
    : public OpConversionPattern<sim::SimCovergroupBlockEventRegisterOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimCovergroupBlockEventRegisterOp op,
                  OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getHandle().size() != 1 || adaptor.getSampler().size() != 1)
      return failure();
    if (op.getReceiver() && adaptor.getReceiver().size() != 1)
      return failure();

    Operation *binding =
        getConvertedObserverBinding(adaptor.getSampler().front());
    auto observerID =
        binding ? ::obelisk::schedule::get<
                      ::obelisk::schedule::Field::NativeObserverId>(binding)
                : IntegerAttr{};
    if (!binding || !observerID ||
        !getConvertedObserverDependencies(binding).empty())
      return failure();
    Operation::operand_range captures = getConvertedObserverCaptures(binding);
    FailureOr<Value> captureRecords =
        serializeCovergroupSamplerCaptures(op, captures, rewriter);
    if (failed(captureRecords))
      return failure();

    ArrayRef<int64_t> targetIDs = op.getTargetIds();
    ArrayRef<int32_t> eventKinds = op.getEventKinds();
    if (targetIDs.empty() || targetIDs.size() != eventKinds.size() ||
        targetIDs.size() > UINT32_MAX)
      return op.emitOpError("block-event inventory exceeds v1 ABI");

    Location location = op.getLoc();
    Type i32 = rewriter.getI32Type();
    Type i64 = rewriter.getI64Type();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value null = LLVM::ZeroOp::create(rewriter, location, pointer);
    Value targetRecords = entryAlloca(rewriter, location, i64, targetIDs.size(),
                                      alignof(uint64_t));
    Value kindRecords = entryAlloca(rewriter, location, i32, eventKinds.size(),
                                    alignof(uint32_t));
    for (auto [index, targetID] : llvm::enumerate(targetIDs))
      LLVM::StoreOp::create(
          rewriter, location,
          llvmConstant(rewriter, location, i64,
                       static_cast<uint64_t>(targetID)),
          byteGEP(rewriter, location, targetRecords, index * sizeof(uint64_t)),
          alignof(uint64_t));
    for (auto [index, eventKind] : llvm::enumerate(eventKinds))
      LLVM::StoreOp::create(
          rewriter, location,
          llvmConstant(rewriter, location, i32,
                       static_cast<uint32_t>(eventKind)),
          byteGEP(rewriter, location, kindRecords, index * sizeof(uint32_t)),
          alignof(uint32_t));

    Value receiver = null;
    if (op.getReceiver())
      receiver = managedObjectPointer(rewriter, location,
                                      adaptor.getReceiver().front());
    auto [context, lane] = managedContextAndLane(rewriter, location);
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_covergroup_block_event_register"),
            ValueRange{context, adaptor.getHandle().front(), receiver,
                       llvmConstant(rewriter, location, i64,
                                    observerID.getValue().getZExtValue()),
                       *captureRecords,
                       llvmConstant(rewriter, location, i32,
                                    static_cast<uint32_t>(captures.size())),
                       targetRecords, kindRecords,
                       llvmConstant(rewriter, location, i32,
                                    static_cast<uint32_t>(targetIDs.size()))})
            .getResult();
    reportManagedStatus(rewriter, location, context, status);
    rewriter.eraseOp(op);
    if (binding->use_empty())
      rewriter.eraseOp(binding);
    return success();
  }
};

class CovergroupBlockEventFireConversion final
    : public OpConversionPattern<sim::SimCovergroupBlockEventFireOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimCovergroupBlockEventFireOp op,
                  OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (op.getReceiver() && adaptor.getReceiver().size() != 1)
      return failure();
    Location location = op.getLoc();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value receiver = LLVM::ZeroOp::create(rewriter, location, pointer);
    if (op.getReceiver())
      receiver = managedObjectPointer(rewriter, location,
                                      adaptor.getReceiver().front());
    auto [context, lane] = managedContextAndLane(rewriter, location);
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_covergroup_block_event_fire"),
            ValueRange{context,
                       llvmConstant(rewriter, location, rewriter.getI64Type(),
                                    op.getTargetId()),
                       llvmConstant(rewriter, location, rewriter.getI32Type(),
                                    static_cast<uint32_t>(op.getEventKind())),
                       receiver})
            .getResult();
    reportManagedStatus(rewriter, location, context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class CovergroupSetNameConversion final
    : public OpConversionPattern<sim::SimCovergroupSetNameOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimCovergroupSetNameOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getHandle().size() != 1 || adaptor.getName().size() != 1)
      return failure();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status = LLVM::CallOp::create(
                       rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_covergroup_set_name"),
                       ValueRange{context, adaptor.getHandle().front(),
                                  adaptor.getName().front()})
                       .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class CovergroupSetIntegerOptionConversion final
    : public OpConversionPattern<sim::SimCovergroupSetIntegerOptionOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimCovergroupSetIntegerOptionOp op,
                  OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getHandle().size() != 1 || adaptor.getValue().size() != 1)
      return failure();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_covergroup_set_integer_option"),
            ValueRange{context, adaptor.getHandle().front(),
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI64Type(), op.getItem()),
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI32Type(),
                                    static_cast<uint32_t>(op.getOption())),
                       adaptor.getValue().front()})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class CovergroupGetIntegerOptionConversion final
    : public OpConversionPattern<sim::SimCovergroupGetIntegerOptionOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimCovergroupGetIntegerOptionOp op,
                  OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getHandle().size() != 1)
      return failure();
    Value value =
        entryAlloca(rewriter, op.getLoc(), rewriter.getI64Type(), 1, 8);
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_covergroup_get_integer_option"),
            ValueRange{context, adaptor.getHandle().front(),
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI64Type(), op.getItem()),
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI32Type(),
                                    static_cast<uint32_t>(op.getOption())),
                       value})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.replaceOp(op,
                       LLVM::LoadOp::create(rewriter, op.getLoc(),
                                            rewriter.getI64Type(), value, 8));
    return success();
  }
};

class CovergroupSetStringOptionConversion final
    : public OpConversionPattern<sim::SimCovergroupSetStringOptionOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimCovergroupSetStringOptionOp op,
                  OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getHandle().size() != 1 || adaptor.getValue().size() != 1)
      return failure();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_covergroup_set_string_option"),
            ValueRange{context, adaptor.getHandle().front(),
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI64Type(), op.getItem()),
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI32Type(),
                                    static_cast<uint32_t>(op.getOption())),
                       adaptor.getValue().front()})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class CovergroupSetTypeIntegerOptionConversion final
    : public OpConversionPattern<sim::SimCovergroupSetTypeIntegerOptionOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimCovergroupSetTypeIntegerOptionOp op,
                  OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getValue().size() != 1)
      return failure();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(
                rewriter.getContext(),
                "obelisk_rt_v1_covergroup_set_type_integer_option"),
            ValueRange{context,
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI64Type(), op.getTypeId()),
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI64Type(), op.getItem()),
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI32Type(),
                                    static_cast<uint32_t>(op.getOption())),
                       adaptor.getValue().front()})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class CovergroupSetTypeStringOptionConversion final
    : public OpConversionPattern<sim::SimCovergroupSetTypeStringOptionOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimCovergroupSetTypeStringOptionOp op,
                  OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getValue().size() != 1)
      return failure();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(
                rewriter.getContext(),
                "obelisk_rt_v1_covergroup_set_type_string_option"),
            ValueRange{context,
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI64Type(), op.getTypeId()),
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI64Type(), op.getItem()),
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI32Type(),
                                    static_cast<uint32_t>(op.getOption())),
                       adaptor.getValue().front()})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class CovergroupSampleConversion final
    : public OpConversionPattern<sim::SimCovergroupSampleOp> {
public:
  CovergroupSampleConversion(TypeConverter &converter, MLIRContext *context,
                             FunctionalValueLayout layout)
      : OpConversionPattern(converter, context), layout(layout) {}
  LogicalResult
  matchAndRewrite(sim::SimCovergroupSampleOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getHandle().size() != 1 ||
        adaptor.getValues().size() != op.getValues().size())
      return failure();
    uint64_t count = op.getValues().size();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value descriptors =
        count ? entryAlloca(rewriter, op.getLoc(), rewriter.getI8Type(),
                            count * layout.size, 8)
              : LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer);
    for (uint64_t index = 0; index != count; ++index)
      if (failed(emitCoverageValueDescriptor(
              rewriter, op.getLoc(),
              byteGEP(rewriter, op.getLoc(), descriptors, index * layout.size),
              op.getValues()[index].getType(), adaptor.getValues()[index],
              static_cast<uint64_t>(op.getExpressionIds()[index]), layout)))
        return failure();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_covergroup_sample"),
            ValueRange{context, adaptor.getHandle().front(), descriptors,
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI64Type(), count)})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.eraseOp(op);
    return success();
  }

private:
  FunctionalValueLayout layout;
};

class CovergroupFormalReadConversion final
    : public OpConversionPattern<sim::SimCovergroupFormalReadOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimCovergroupFormalReadOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getHandle().size() != 1)
      return failure();
    SmallVector<Type> convertedTypes;
    if (failed(getTypeConverter()->convertType(op.getResult().getType(),
                                               convertedTypes)) ||
        convertedTypes.empty() || convertedTypes.size() > 2)
      return failure();
    std::optional<unsigned> width =
        sim::getPackedWidth(op.getResult().getType());
    uint64_t bitWidth =
        op.getResult().getType().isF64() ? 64 : width.value_or(0);
    if (!bitWidth)
      return failure();
    uint64_t size = (bitWidth + 7) / 8;
    Value value = entryAlloca(rewriter, op.getLoc(), convertedTypes[0], 1, 8);
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value unknown = LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer);
    if (convertedTypes.size() == 2)
      unknown = entryAlloca(rewriter, op.getLoc(), convertedTypes[1], 1, 8);
    uint64_t kind =
        op.getResult().getType().isF64() ? OBELISK_RT_FUNCTIONAL_VALUE_REAL
        : convertedTypes.size() == 2 ? OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE
                                     : OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL;
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_covergroup_formal_read"),
            ValueRange{context, adaptor.getHandle().front(),
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI64Type(), op.getFormalId()),
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI32Type(), kind),
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI64Type(), bitWidth),
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI64Type(), size),
                       value, unknown})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    SmallVector<Value> results;
    results.push_back(LLVM::LoadOp::create(rewriter, op.getLoc(),
                                           convertedTypes[0], value, 8));
    if (convertedTypes.size() == 2)
      results.push_back(LLVM::LoadOp::create(rewriter, op.getLoc(),
                                             convertedTypes[1], unknown, 8));
    SmallVector<SmallVector<Value>> replacements;
    replacements.push_back(std::move(results));
    rewriter.replaceOpWithMultiple(op, std::move(replacements));
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
                         declaration.getSchemaType());
      symbol = "obelisk_rt_v1_covergroup_type_query";
      arguments.push_back(key);
    } else {
      if (adaptor.getHandle().size() != 1)
        return failure();
      key = adaptor.getHandle().front();
      symbol = "obelisk_rt_v1_covergroup_instance_query";
      arguments.push_back(key);
    }
    arguments.push_back(llvmConstant(rewriter, op.getLoc(),
                                     rewriter.getI64Type(), op.getItem()));
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
    RewritePatternSet &patterns, TypeConverter &converter,
    const llvm::DataLayout &dataLayout) {
  MLIRContext *context = patterns.getContext();
  FunctionalValueLayout layout = getFunctionalValueLayout(dataLayout);
  patterns.add<CovergroupCreateConversion, CovergroupSampleConversion>(
      converter, context, layout);
  patterns.add<
      CoveragePointHitConversion,
      CoverageControlConversion<sim::SimCoverageControlDefinitionOp>,
      CoverageControlConversion<sim::SimCoverageControlInstanceOp>,
      CoverageQueryConversion<sim::SimCoverageQueryDefinitionOp>,
      CoverageQueryConversion<sim::SimCoverageQueryInstanceOp>,
      CoverageDatabaseConversion<sim::SimCoverageSaveOp>,
      CoverageDatabaseConversion<sim::SimCoverageMergeOp>,
      CovergroupNullConversion, CovergroupCastConversion,
      FunctionalCoverageGetConversion,
      FunctionalCoverageDatabaseConversion<
          sim::SimFunctionalCoverageSetDbNameOp>,
      FunctionalCoverageDatabaseConversion<sim::SimFunctionalCoverageLoadDbOp>,
      CovergroupEnabledConversion, CovergroupClockEventRegisterConversion,
      CovergroupBlockEventRegisterConversion,
      CovergroupBlockEventFireConversion, CovergroupSetNameConversion,
      CovergroupSetIntegerOptionConversion,
      CovergroupGetIntegerOptionConversion, CovergroupSetStringOptionConversion,
      CovergroupSetTypeIntegerOptionConversion,
      CovergroupSetTypeStringOptionConversion, CovergroupFormalReadConversion,
      CovergroupControlConversion<sim::SimCovergroupStartOp, true>,
      CovergroupControlConversion<sim::SimCovergroupStopOp, false>,
      CovergroupQueryConversion<sim::SimCovergroupInstanceQueryOp, false>,
      CovergroupQueryConversion<sim::SimCovergroupTypeQueryOp, true>>(converter,
                                                                      context);
}

} // namespace obelisk::detail
