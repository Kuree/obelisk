//===- SimulationFunctionTypeLowering.cpp - Function type rewrites -------===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Transforms/DialectConversion.h"

#include "llvm/ADT/STLExtras.h"

using namespace mlir;

namespace obelisk::detail {
namespace {

class ContextRuntimeLowering final : public ConversionPattern {
public:
  ContextRuntimeLowering(const TypeConverter &converter, MLIRContext *context)
      : ConversionPattern(converter,
                          sim::SimContextRuntimeOp::getOperationName(), 1,
                          context) {}

  LogicalResult
  matchAndRewrite(Operation *operation, ArrayRef<Value> operands,
                  ConversionPatternRewriter &rewriter) const override {
    if (operands.size() != 1)
      return rewriter.notifyMatchFailure(operation,
                                         "context projection must convert 1:1");
    rewriter.replaceOp(operation, operands.front());
    return success();
  }
};

class FuncSignatureConversion final
    : public OpConversionPattern<sim::SimFuncOp> {
public:
  FuncSignatureConversion(const TypeConverter &converter, MLIRContext *context,
                          const llvm::DenseSet<Value> &twoStateValues)
      : OpConversionPattern(converter, context),
        twoStateValues(&twoStateValues) {}

  LogicalResult
  matchAndRewrite(sim::SimFuncOp function, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    FunctionType type = function.getFunctionType();
    TypeConverter::SignatureConversion entry(type.getNumInputs());
    SmallVector<Type> results;
    SmallVector<SmallVector<int64_t>> zeroUnknownArguments;
    zeroUnknownArguments.reserve(function.getBody().getBlocks().size());
    for (Block &block : function.getBody()) {
      SmallVector<int64_t> unknowns;
      unsigned physical = 0;
      for (BlockArgument argument : block.getArguments()) {
        SmallVector<Type> converted;
        if (failed(
                getTypeConverter()->convertType(argument.getType(), converted)))
          return failure();
        if (isa<sim::LogicType>(argument.getType()) &&
            twoStateValues->contains(argument) && converted.size() == 2)
          unknowns.push_back(static_cast<int64_t>(physical + 1));
        physical += converted.size();
      }
      zeroUnknownArguments.push_back(std::move(unknowns));
    }
    if (failed(getTypeConverter()->convertSignatureArgs(type.getInputs(),
                                                        entry)) ||
        failed(getTypeConverter()->convertTypes(type.getResults(), results)) ||
        (!function.getBody().empty() &&
         failed(rewriter.convertRegionTypes(&function.getBody(),
                                            *getTypeConverter(), &entry))))
      return failure();
    SmallVector<Attribute> zeroUnknownAttr;
    zeroUnknownAttr.reserve(zeroUnknownArguments.size());
    for (ArrayRef<int64_t> indices : zeroUnknownArguments)
      zeroUnknownAttr.push_back(rewriter.getDenseI64ArrayAttr(indices));
    SmallVector<Attribute> argAttrs(entry.getConvertedTypes().size(),
                                    rewriter.getDictionaryAttr({}));
    SmallVector<Attribute> resultAttrs(results.size(),
                                       rewriter.getDictionaryAttr({}));
    rewriter.modifyOpInPlace(function, [&] {
      function.setType(FunctionType::get(rewriter.getContext(),
                                         entry.getConvertedTypes(), results));
      function.setArgAttrsAttr(rewriter.getArrayAttr(argAttrs));
      if (!resultAttrs.empty())
        function.setResAttrsAttr(rewriter.getArrayAttr(resultAttrs));
      function->setAttr(nativeTwoStateBlockUnknownsAttr,
                        rewriter.getArrayAttr(zeroUnknownAttr));
    });
    return success();
  }

private:
  const llvm::DenseSet<Value> *twoStateValues;
};

class ReturnTypeConversion final
    : public OpConversionPattern<sim::SimReturnOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimReturnOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    OperationState state(operation.getLoc(), operation->getName());
    state.addOperands(flatten(adaptor.getOperands()));
    state.addAttributes(operation->getAttrs());
    rewriter.replaceOp(operation, rewriter.create(state));
    return success();
  }
};

class SelectTypeConversion final : public OpConversionPattern<arith::SelectOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(arith::SelectOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto operands = adaptor.getOperands();
    if (operands.size() != 3 || operands[0].size() != 1 ||
        operands[1].size() != operands[2].size())
      return rewriter.notifyMatchFailure(
          operation, "select operands have incompatible converted arity");

    SmallVector<Value> results;
    results.reserve(operands[1].size());
    for (auto [trueValue, falseValue] :
         llvm::zip_equal(operands[1], operands[2])) {
      auto selected =
          arith::SelectOp::create(rewriter, operation.getLoc(),
                                  operands[0].front(), trueValue, falseValue);
      selected->setAttrs(operation->getAttrs());
      results.push_back(selected);
    }
    SmallVector<SmallVector<Value>> replacements;
    replacements.push_back(std::move(results));
    rewriter.replaceOpWithMultiple(operation, std::move(replacements));
    return success();
  }
};

class CallTypeConversion final : public OpConversionPattern<sim::SimCallOp> {
public:
  CallTypeConversion(const TypeConverter &converter, MLIRContext *context,
                     const llvm::DenseSet<Value> &twoStateValues)
      : OpConversionPattern(converter, context),
        twoStateValues(&twoStateValues) {}

  LogicalResult
  matchAndRewrite(sim::SimCallOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    for (auto [operand, converted] :
         llvm::zip_equal(operation.getOperands(), adaptor.getOperands()))
      if (isa<sim::RefType>(operand.getType()) && converted.size() == 1)
        emitNativeStateRetain(rewriter, operation.getLoc(), converted.front());
    SmallVector<Type> results;
    SmallVector<size_t> resultSizes;
    for (Type type : operation.getResultTypes()) {
      size_t start = results.size();
      if (failed(getTypeConverter()->convertType(type, results)))
        return failure();
      resultSizes.push_back(results.size() - start);
    }
    OperationState state(operation.getLoc(), operation->getName());
    state.addOperands(flatten(adaptor.getOperands()));
    state.addTypes(results);
    state.addAttributes(operation->getAttrs());
    Operation *replacement = rewriter.create(state);
    SmallVector<SmallVector<Value>> replacements;
    replacements.reserve(resultSizes.size());
    size_t offset = 0;
    for (auto [index, size] : llvm::enumerate(resultSizes)) {
      SmallVector<Value> values(replacement->getResults().slice(offset, size));
      if (size == 2 && twoStateValues->contains(operation.getResult(index))) {
        auto type = dyn_cast<IntegerType>(values[1].getType());
        if (!type)
          return failure();
        values[1] = arith::ConstantOp::create(
            rewriter, operation.getLoc(), type,
            rewriter.getIntegerAttr(type, APInt::getZero(type.getWidth())));
      }
      replacements.push_back(std::move(values));
      offset += size;
    }
    rewriter.replaceOpWithMultiple(operation, std::move(replacements));
    return success();
  }

private:
  const llvm::DenseSet<Value> *twoStateValues;
};

class TaskCallTypeConversion final
    : public OpConversionPattern<sim::SimTaskCallOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimTaskCallOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    uint64_t logicalArguments = operation.getArgumentCount();
    if (logicalArguments > adaptor.getOperands().size())
      return failure();
    uint64_t physicalArguments = 0;
    for (ValueRange values :
         ArrayRef(adaptor.getOperands()).take_front(logicalArguments))
      physicalArguments += values.size();
    for (auto [operand, converted] :
         llvm::zip_equal(operation.getOperands(), adaptor.getOperands()))
      if (isa<sim::RefType>(operand.getType()) && converted.size() == 1)
        emitNativeStateRetain(rewriter, operation.getLoc(), converted.front());
    OperationState state(operation.getLoc(), operation->getName());
    state.addOperands(flatten(adaptor.getOperands()));
    state.addSuccessors(operation->getSuccessors());
    state.addAttributes(operation->getAttrs());
    state.attributes.set(operation.getArgumentCountAttrName(),
                         rewriter.getI64IntegerAttr(physicalArguments));
    rewriter.replaceOp(operation, rewriter.create(state));
    return success();
  }
};

class ClassVirtualTaskCallTypeConversion final
    : public OpConversionPattern<sim::SimClassVirtualTaskCallOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimClassVirtualTaskCallOp operation,
                  OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    uint64_t logicalArguments = operation.getArgumentCount();
    if (adaptor.getReceiver().size() != 1 ||
        logicalArguments > adaptor.getValues().size())
      return failure();
    uint64_t physicalArguments = 0;
    for (ValueRange values :
         ArrayRef(adaptor.getValues()).take_front(logicalArguments))
      physicalArguments += values.size();
    for (auto [operand, converted] :
         llvm::zip_equal(operation.getValues(), adaptor.getValues()))
      if (isa<sim::RefType>(operand.getType()) && converted.size() == 1)
        emitNativeStateRetain(rewriter, operation.getLoc(), converted.front());
    OperationState state(operation.getLoc(), operation->getName());
    state.addOperands(flatten(adaptor.getOperands()));
    state.addSuccessors(operation->getSuccessors());
    state.addAttributes(operation->getAttrs());
    state.attributes.set(operation.getArgumentCountAttrName(),
                         rewriter.getI64IntegerAttr(physicalArguments));
    rewriter.replaceOp(operation, rewriter.create(state));
    return success();
  }
};

class DPICallTypeConversion final
    : public OpConversionPattern<sim::SimDPICallOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimDPICallOp operation, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    SmallVector<Type> results;
    SmallVector<size_t> resultSizes;
    for (Type type : operation.getResultTypes()) {
      size_t start = results.size();
      if (failed(getTypeConverter()->convertType(type, results)))
        return failure();
      resultSizes.push_back(results.size() - start);
    }
    OperationState state(operation.getLoc(), operation->getName());
    state.addOperands(flatten(adaptor.getOperands()));
    state.addTypes(results);
    state.addAttributes(operation->getAttrs());
    state.addAttribute(
        "obelisk.dpi.logical_operand_count",
        rewriter.getI32IntegerAttr(operation.getArguments().size()));
    Operation *replacement = rewriter.create(state);
    SmallVector<SmallVector<Value>> replacements;
    replacements.reserve(resultSizes.size());
    size_t offset = 0;
    for (size_t size : resultSizes) {
      replacements.emplace_back(replacement->getResults().slice(offset, size));
      offset += size;
    }
    rewriter.replaceOpWithMultiple(operation, std::move(replacements));
    return success();
  }
};

} // namespace

void populateFunctionTypeConversionPatterns(
    RewritePatternSet &patterns, TypeConverter &converter,
    const llvm::DenseSet<Value> &twoStateValues) {
  MLIRContext *context = patterns.getContext();
  patterns.add<FuncSignatureConversion, CallTypeConversion>(converter, context,
                                                            twoStateValues);
  patterns.add<ReturnTypeConversion, SelectTypeConversion,
               TaskCallTypeConversion, ClassVirtualTaskCallTypeConversion,
               DPICallTypeConversion>(converter, context);
}

void populateContextRuntimeToLLVMConversionPattern(
    RewritePatternSet &patterns, const TypeConverter &converter) {
  patterns.add<ContextRuntimeLowering>(converter, patterns.getContext());
}

} // namespace obelisk::detail
