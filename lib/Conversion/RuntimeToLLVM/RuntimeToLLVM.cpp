//===- RuntimeToLLVM.cpp - Lower typed runtime operations to C ABI calls -===//

#include "obelisk/Conversion/RuntimeToLLVM.h"

#include "RuntimeToLLVMABI.h"
#include "RuntimeToLLVMPatterns.h"

#include "obelisk/Dialect/Runtime/RuntimeABI.h"
#include "obelisk/Dialect/Runtime/RuntimeOps.h"

#include "mlir/Conversion/LLVMCommon/LoweringOptions.h"
#include "mlir/Conversion/LLVMCommon/TypeConverter.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Func/Transforms/FuncConversions.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/LoopLikeInterface.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/Error.h"
#include "llvm/TargetParser/Triple.h"

#include <limits>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_CONVERTOBELISKRUNTIMETOLLVMPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

using namespace obelisk::runtimelowering;
class FuncValueTypeConversion : public ConversionPattern {
public:
  FuncValueTypeConversion(const TypeConverter &converter, StringRef name,
                          MLIRContext *context)
      : ConversionPattern(converter, name, 1, context) {}

  LogicalResult
  matchAndRewrite(Operation *operation, ArrayRef<Value> operands,
                  ConversionPatternRewriter &rewriter) const override {
    SmallVector<Type> results;
    if (failed(getTypeConverter()->convertTypes(operation->getResultTypes(),
                                                results)))
      return failure();
    OperationState state(operation->getLoc(), operation->getName());
    state.addOperands(operands);
    state.addTypes(results);
    state.addAttributes(operation->getAttrs());
    Operation *replacement = rewriter.create(state);
    rewriter.replaceOp(operation, replacement->getResults());
    return success();
  }
};

/// Convert every block signature in a function, including unreachable and
/// non-entry blocks. The generic function-interface pattern only owns the
/// callable signature; CFG successor arguments otherwise retain runtime types.
class RuntimeFunctionSignatureConversion
    : public OpConversionPattern<func::FuncOp> {
public:
  RuntimeFunctionSignatureConversion(const TypeConverter &converter,
                                     MLIRContext *context)
      : OpConversionPattern(converter, context, /*benefit=*/2) {}

  LogicalResult
  matchAndRewrite(func::FuncOp function, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    FunctionType type = function.getFunctionType();
    TypeConverter::SignatureConversion entry(type.getNumInputs());
    SmallVector<Type> results;
    if (failed(getTypeConverter()->convertSignatureArgs(type.getInputs(),
                                                        entry)) ||
        failed(getTypeConverter()->convertTypes(type.getResults(), results)))
      return failure();
    if (!function.getBody().empty() &&
        failed(rewriter.convertRegionTypes(&function.getBody(),
                                           *getTypeConverter(), &entry)))
      return failure();
    FunctionType converted = FunctionType::get(
        rewriter.getContext(), entry.getConvertedTypes(), results);
    rewriter.modifyOpInPlace(function, [&] { function.setType(converted); });
    return success();
  }
};

bool containsBufferType(Type type) {
  bool found = false;
  type.walk([&](runtime::BufferType) { found = true; });
  return found;
}

LogicalResult verifyRuntimeBufferOwnership(ModuleOp module) {
  WalkResult result = module.walk([&](Operation *operation) -> WalkResult {
    if (auto function = dyn_cast<func::FuncOp>(operation)) {
      for (Type type : function.getFunctionType().getInputs())
        if (containsBufferType(type)) {
          function.emitOpError()
              << "owned runtime buffers cannot be function arguments";
          return WalkResult::interrupt();
        }
      for (Type type : function.getFunctionType().getResults())
        if (containsBufferType(type)) {
          function.emitOpError()
              << "owned runtime buffers cannot be function results";
          return WalkResult::interrupt();
        }
    }
    for (Region &region : operation->getRegions())
      for (Block &block : region)
        for (BlockArgument argument : block.getArguments())
          if (containsBufferType(argument.getType())) {
            operation->emitOpError()
                << "owned runtime buffers cannot be block arguments";
            return WalkResult::interrupt();
          }
    for (Value resultValue : operation->getResults()) {
      if (!containsBufferType(resultValue.getType()))
        continue;
      if (!isa<runtime::BufferType>(resultValue.getType()) ||
          !isa<runtime::RTLastErrorOp, runtime::RTFormatOp,
               runtime::RTFileGetlineOp, runtime::RTFileErrorOp>(operation)) {
        operation->emitOpError()
            << "owned runtime buffers must be produced directly by a typed "
               "runtime buffer operation";
        return WalkResult::interrupt();
      }
      unsigned releases = 0;
      unsigned sizes = 0;
      unsigned packedReads = 0;
      Operation *release = nullptr;
      bool invalid = false;
      for (Operation *user : resultValue.getUsers()) {
        releases += isa<runtime::RTBufferReleaseOp>(user);
        sizes += isa<runtime::RTBytesSizeOp>(user);
        packedReads += isa<runtime::RTPackedFromBytesOp>(user);
        if (isa<runtime::RTBufferReleaseOp>(user))
          release = user;
        if (!isa<runtime::RTBufferReleaseOp, runtime::RTBytesSizeOp,
                 runtime::RTPackedFromBytesOp>(user) ||
            user->getBlock() != operation->getBlock())
          invalid = true;
      }
      if (release)
        for (Operation *user : resultValue.getUsers())
          if (user != release && !user->isBeforeInBlock(release))
            invalid = true;
      if (invalid || releases != 1 || sizes > 1 || packedReads > 1) {
        operation->emitOpError()
            << "owned runtime buffer requires one same-block release and at "
               "most one size and one packed-byte consumer, both before the "
               "release";
        return WalkResult::interrupt();
      }
    }
    return WalkResult::advance();
  });
  return result.wasInterrupted() ? failure() : success();
}

LogicalResult verifyNoRuntimeTypes(ModuleOp module) {
  WalkResult result = module.walk([&](Operation *operation) -> WalkResult {
    auto reject = [&](Type type) {
      if (!containsRuntimeType(type))
        return false;
      operation->emitOpError()
          << "contains an obelisk_rt type after runtime-to-LLVM conversion";
      return true;
    };
    for (Type type : operation->getResultTypes())
      if (reject(type))
        return WalkResult::interrupt();
    for (Region &region : operation->getRegions())
      for (Block &block : region)
        for (BlockArgument argument : block.getArguments())
          if (reject(argument.getType()))
            return WalkResult::interrupt();
    for (NamedAttribute named : operation->getAttrs()) {
      bool found = false;
      named.getValue().walk([&](Type type) {
        if (containsRuntimeType(type))
          found = true;
      });
      if (found) {
        operation->emitOpError()
            << "attribute '" << named.getName().getValue()
            << "' contains an obelisk_rt type after runtime-to-LLVM "
               "conversion";
        return WalkResult::interrupt();
      }
    }
    return WalkResult::advance();
  });
  return result.wasInterrupted() ? failure() : success();
}

class ConvertObeliskRuntimeToLLVMPass
    : public impl::ConvertObeliskRuntimeToLLVMPassBase<
          ConvertObeliskRuntimeToLLVMPass> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ConvertObeliskRuntimeToLLVMPass)

  void runOnOperation() override {
    ModuleOp module = getOperation();
    auto layout = module->getAttrOfType<StringAttr>("llvm.data_layout");
    if (!layout) {
      module.emitError()
          << "runtime lowering requires an explicit llvm.data_layout";
      return signalPassFailure();
    }
    llvm::Expected<llvm::DataLayout> parsed =
        llvm::DataLayout::parse(layout.getValue());
    if (!parsed) {
      module.emitError() << "invalid LLVM data layout: "
                         << llvm::toString(parsed.takeError());
      return signalPassFailure();
    }
    if (failed(validateRuntimeToLLVMPreconditions(module, *parsed)))
      return signalPassFailure();
    if (failed(materializeEmbeddedSimulationDesign(module)))
      return signalPassFailure();

    ABITypes abi(&getContext(), getABIAlignments(*parsed), *parsed);
    TypeConverter converter;
    converter.addConversion([&](Type type) -> std::optional<Type> {
      return convertRuntimeType(type, abi);
    });

    RewritePatternSet patterns(&getContext());
    populateRuntimePatterns(converter, patterns, abi);
    patterns.add<RuntimeFunctionSignatureConversion>(converter, &getContext());
    patterns.add<FuncValueTypeConversion>(
        converter, func::ConstantOp::getOperationName(), &getContext());
    patterns.add<FuncValueTypeConversion>(
        converter, func::CallIndirectOp::getOperationName(), &getContext());
    populateCallOpTypeConversionPattern(patterns, converter);
    populateReturnOpTypeConversionPattern(patterns, converter);
    populateBranchOpInterfaceTypeConversionPattern(patterns, converter);

    ConversionTarget target(getContext());
    target.addIllegalDialect<runtime::ObeliskRuntimeDialect>();
    target.addLegalDialect<LLVM::LLVMDialect>();
    target.addLegalOp<ModuleOp>();
    target.addDynamicallyLegalDialect<func::FuncDialect>(
        [&](Operation *operation) { return converter.isLegal(operation); });
    target.addDynamicallyLegalOp<func::FuncOp>([&](func::FuncOp function) {
      return converter.isSignatureLegal(function.getFunctionType()) &&
             converter.isLegal(&function.getBody());
    });
    target.addDynamicallyLegalOp<func::ReturnOp>(
        [&](func::ReturnOp operation) { return converter.isLegal(operation); });
    target.addDynamicallyLegalOp<func::CallOp>(
        [&](func::CallOp operation) { return converter.isLegal(operation); });
    target.markUnknownOpDynamicallyLegal([&](Operation *operation) {
      if (isa<BranchOpInterface>(operation))
        return isLegalForBranchOpInterfaceTypeConversionPattern(operation,
                                                                converter);
      return converter.isLegal(operation);
    });
    if (failed(applyPartialConversion(module, target, std::move(patterns))))
      return signalPassFailure();
    if (failed(verifyNoRuntimeTypes(module)))
      signalPassFailure();
  }
};

} // namespace

LogicalResult
validateRuntimeToLLVMPreconditions(ModuleOp module,
                                   const llvm::DataLayout &dataLayout) {
  if (failed(validateTargetABI(module, dataLayout)))
    return failure();
  if (auto tripleAttr =
          module->getAttrOfType<StringAttr>("llvm.target_triple")) {
    llvm::Triple triple(tripleAttr.getValue());
    if (!triple.isArch64Bit() || !triple.isLittleEndian())
      return module.emitError()
             << "llvm.target_triple is inconsistent with the supported "
                "64-bit little-endian runtime ABI";
  }
  return verifyRuntimeBufferOwnership(module);
}

void addRuntimeToLLVMTypeConversions(LLVMTypeConverter &converter) {
  ABITypes abi(&converter.getContext(),
               getABIAlignments(converter.getDataLayout()),
               converter.getDataLayout());
  converter.addConversion([abi](Type type) -> std::optional<Type> {
    if (type.getDialect().getNamespace() != "obelisk_rt")
      return std::nullopt;
    return convertRuntimeType(type, abi);
  });
  converter.addConversion([&converter](TupleType type) -> std::optional<Type> {
    if (!containsRuntimeType(type))
      return std::nullopt;
    SmallVector<Type> elements;
    for (Type element : type.getTypes()) {
      Type converted = converter.convertType(element);
      if (!converted)
        return std::nullopt;
      elements.push_back(converted);
    }
    return TupleType::get(type.getContext(), elements);
  });
  converter.addConversion(
      [&converter](FunctionType type) -> std::optional<Type> {
        if (!containsRuntimeType(type))
          return std::nullopt;
        SmallVector<Type> inputs;
        SmallVector<Type> results;
        for (Type input : type.getInputs()) {
          Type converted = converter.convertType(input);
          if (!converted)
            return std::nullopt;
          inputs.push_back(converted);
        }
        for (Type result : type.getResults()) {
          Type converted = converter.convertType(result);
          if (!converted)
            return std::nullopt;
          results.push_back(converted);
        }
        return FunctionType::get(type.getContext(), inputs, results);
      });
}

void populateRuntimeToLLVMPatterns(const LLVMTypeConverter &converter,
                                   RewritePatternSet &patterns) {
  MLIRContext *context = patterns.getContext();
  ABITypes abi(context, getABIAlignments(converter.getDataLayout()),
               converter.getDataLayout());
  populateRuntimePatterns(converter, patterns, abi);
}

} // namespace obelisk
