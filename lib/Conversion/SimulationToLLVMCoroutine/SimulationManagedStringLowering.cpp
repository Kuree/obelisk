//===- SimulationManagedStringLowering.cpp - Managed string patterns -===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Transforms/DialectConversion.h"

#include <string>
#include <type_traits>

using namespace mlir;

namespace obelisk::detail {

namespace {

static Value finishStringAllocation(ConversionPatternRewriter &rewriter,
                                    Location location, Value context,
                                    Value status, Value output) {
  reportManagedStatus(rewriter, location, context, status);
  return LLVM::LoadOp::create(rewriter, location, rewriter.getI64Type(), output,
                              8);
}

class StringLiteralConversion final
    : public OpConversionPattern<sim::SimStringLiteralOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimStringLiteralOp op, OneToNOpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ModuleOp module = op->getParentOfType<ModuleOp>();
    if (!module)
      return failure();
    std::string name;
    for (uint64_t suffix = 0;; ++suffix) {
      name = ("__obelisk_string_literal." + Twine(suffix)).str();
      if (!module.lookupSymbol(name))
        break;
    }
    StringRef bytes = op.getValue();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value data = LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer);
    if (!bytes.empty()) {
      LLVM::GlobalOp global =
          makeByteArrayGlobal(module, op.getLoc(), name, bytes);
      data = LLVM::AddressOfOp::create(rewriter, op.getLoc(), global);
    }
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value output =
        entryAlloca(rewriter, op.getLoc(), rewriter.getI64Type(), 1, 8);
    LLVM::StoreOp::create(
        rewriter, op.getLoc(),
        LLVM::ZeroOp::create(rewriter, op.getLoc(), rewriter.getI64Type()),
        output, 8);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_string_create"),
            ValueRange{lane, data,
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI64Type(), bytes.size()),
                       output})
            .getResult();
    rewriter.replaceOp(op, finishStringAllocation(rewriter, op.getLoc(),
                                                  context, status, output));
    return success();
  }
};

class StringFromPackedConversion final
    : public OpConversionPattern<sim::SimStringFromPackedOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimStringFromPackedOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    std::optional<unsigned> width =
        sim::getPackedWidth(op.getInput().getType());
    if (!width ||
        (adaptor.getInput().size() != 1 && adaptor.getInput().size() != 2))
      return failure();
    Type planeType = adaptor.getInput().front().getType();
    Value value = entryAlloca(rewriter, op.getLoc(), planeType, 1, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(), adaptor.getInput().front(),
                          value, 8);
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value unknown = LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer);
    if (adaptor.getInput().size() == 2) {
      unknown = entryAlloca(rewriter, op.getLoc(), planeType, 1, 8);
      LLVM::StoreOp::create(rewriter, op.getLoc(), adaptor.getInput()[1],
                            unknown, 8);
    }
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value output =
        entryAlloca(rewriter, op.getLoc(), rewriter.getI64Type(), 1, 8);
    LLVM::StoreOp::create(
        rewriter, op.getLoc(),
        LLVM::ZeroOp::create(rewriter, op.getLoc(), rewriter.getI64Type()),
        output, 8);
    Value status = LLVM::CallOp::create(
                       rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_string_from_packed"),
                       ValueRange{lane, value, unknown,
                                  llvmConstant(rewriter, op.getLoc(),
                                               rewriter.getI64Type(), *width),
                                  output})
                       .getResult();
    rewriter.replaceOp(op, finishStringAllocation(rewriter, op.getLoc(),
                                                  context, status, output));
    return success();
  }
};

class StringToPackedConversion final
    : public OpConversionPattern<sim::SimStringToPackedOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimStringToPackedOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto resultType = dyn_cast<IntegerType>(
        getTypeConverter()->convertType(op.getResult().getType()));
    if (!resultType || adaptor.getInput().size() != 1)
      return failure();
    Value output = entryAlloca(rewriter, op.getLoc(), resultType, 1, 8);
    LLVM::StoreOp::create(
        rewriter, op.getLoc(),
        LLVM::ZeroOp::create(rewriter, op.getLoc(), resultType), output, 8);
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_string_to_packed"),
            ValueRange{adaptor.getInput().front(), output,
                       LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer),
                       llvmConstant(rewriter, op.getLoc(),
                                    rewriter.getI64Type(),
                                    resultType.getWidth())})
            .getResult();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.replaceOp(
        op, LLVM::LoadOp::create(rewriter, op.getLoc(), resultType, output, 8));
    return success();
  }
};

class StringConcatConversion final
    : public OpConversionPattern<sim::SimStringConcatOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimStringConcatOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type i64 = rewriter.getI64Type();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Value spans = LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer);
    if (!adaptor.getInputs().empty()) {
      spans = entryAlloca(rewriter, op.getLoc(), i64,
                          adaptor.getInputs().size(), 8);
      for (auto [index, input] : llvm::enumerate(adaptor.getInputs())) {
        if (input.size() != 1)
          return failure();
        LLVM::StoreOp::create(
            rewriter, op.getLoc(), input.front(),
            byteGEP(rewriter, op.getLoc(), spans, index * sizeof(uint64_t)), 8);
      }
    }
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value output = entryAlloca(rewriter, op.getLoc(), i64, 1, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), i64),
                          output, 8);
    Value status = LLVM::CallOp::create(
                       rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_string_concat_many"),
                       ValueRange{lane, spans,
                                  llvmConstant(rewriter, op.getLoc(), i64,
                                               adaptor.getInputs().size()),
                                  output})
                       .getResult();
    rewriter.replaceOp(op, finishStringAllocation(rewriter, op.getLoc(),
                                                  context, status, output));
    return success();
  }
};

template <typename Op>
class StringAllocatingCallConversion final : public OpConversionPattern<Op> {
public:
  StringAllocatingCallConversion(const TypeConverter &converter,
                                 MLIRContext *context, StringRef symbol)
      : OpConversionPattern<Op>(converter, context), symbol(symbol) {}

  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    SmallVector<Value> operands;
    for (ValueRange range : adaptor.getOperands()) {
      if (range.size() != 1)
        return failure();
      operands.push_back(range.front());
    }
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    operands.insert(operands.begin(), lane);
    if constexpr (std::is_same_v<Op, sim::SimStringPutcOp>)
      operands.back() = arith::ExtUIOp::create(
          rewriter, op.getLoc(), rewriter.getI32Type(), operands.back());
    if constexpr (std::is_same_v<Op, sim::SimStringCaseConvertOp>)
      operands.push_back(llvmConstant(rewriter, op.getLoc(),
                                      rewriter.getI32Type(),
                                      op.getToUpper() ? 1 : 0));
    if constexpr (std::is_same_v<Op, sim::SimStringFormatIntegerOp>) {
      operands.push_back(llvmConstant(rewriter, op.getLoc(),
                                      rewriter.getI32Type(), op.getRadix()));
      operands.push_back(llvmConstant(rewriter, op.getLoc(),
                                      rewriter.getI32Type(),
                                      op.getIsSigned() ? 1 : 0));
    }
    Value output =
        entryAlloca(rewriter, op.getLoc(), rewriter.getI64Type(), 1, 8);
    LLVM::StoreOp::create(
        rewriter, op.getLoc(),
        LLVM::ZeroOp::create(rewriter, op.getLoc(), rewriter.getI64Type()),
        output, 8);
    operands.push_back(output);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(), symbol), operands)
            .getResult();
    rewriter.replaceOp(op, finishStringAllocation(rewriter, op.getLoc(),
                                                  context, status, output));
    return success();
  }

private:
  std::string symbol;
};

class StringLengthConversion final
    : public OpConversionPattern<sim::SimStringLengthOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimStringLengthOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    rewriter.replaceOp(
        op, LLVM::CallOp::create(
                rewriter, op.getLoc(), TypeRange{rewriter.getI64Type()},
                SymbolRefAttr::get(rewriter.getContext(),
                                   "obelisk_rt_v1_string_length"),
                adaptor.getInput().front())
                .getResult());
    return success();
  }
};

class StringGetcConversion final
    : public OpConversionPattern<sim::SimStringGetcOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimStringGetcOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Value result =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_string_getc"),
            ValueRange{adaptor.getInput().front(), adaptor.getIndex().front()})
            .getResult();
    rewriter.replaceOp(op,
                       arith::TruncIOp::create(rewriter, op.getLoc(),
                                               rewriter.getI8Type(), result));
    return success();
  }
};

class StringCompareConversion final
    : public OpConversionPattern<sim::SimStringCompareOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimStringCompareOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    StringRef symbol = op.getCaseInsensitive()
                           ? "obelisk_rt_v1_string_compare_insensitive"
                           : "obelisk_rt_v1_string_compare";
    rewriter.replaceOp(
        op, LLVM::CallOp::create(
                rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
                SymbolRefAttr::get(rewriter.getContext(), symbol),
                ValueRange{adaptor.getLhs().front(), adaptor.getRhs().front()})
                .getResult());
    return success();
  }
};

template <typename Op>
class StringFileOpenConversion final : public OpConversionPattern<Op> {
public:
  using OpConversionPattern<Op>::OpConversionPattern;
  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Value output =
        entryAlloca(rewriter, op.getLoc(), rewriter.getI32Type(), 1, 4);
    LLVM::StoreOp::create(
        rewriter, op.getLoc(),
        LLVM::ZeroOp::create(rewriter, op.getLoc(), rewriter.getI32Type()),
        output, 4);
    SmallVector<Value> operands{context, adaptor.getPath().front()};
    StringRef symbol = "obelisk_rt_v1_file_open_string_mcd";
    if constexpr (std::is_same_v<Op, sim::SimFileOpenStringOp>) {
      operands.push_back(adaptor.getMode().front());
      symbol = "obelisk_rt_v1_file_open_string";
    }
    operands.push_back(output);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(), symbol), operands)
            .getResult();
    Value descriptor = LLVM::LoadOp::create(rewriter, op.getLoc(),
                                            rewriter.getI32Type(), output, 4);
    Value ok = arith::CmpIOp::create(
        rewriter, op.getLoc(), arith::CmpIPredicate::eq, status,
        llvmConstant(rewriter, op.getLoc(), rewriter.getI32Type(), 0));
    rewriter.replaceOp(op, arith::SelectOp::create(
                               rewriter, op.getLoc(), ok, descriptor,
                               LLVM::ZeroOp::create(rewriter, op.getLoc(),
                                                    rewriter.getI32Type())));
    return success();
  }
};

class StringScanFieldConversion final
    : public OpConversionPattern<sim::SimStringScanFieldOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimStringScanFieldOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ModuleOp module = op->getParentOfType<ModuleOp>();
    if (!module)
      return failure();
    Type i64 = rewriter.getI64Type();
    Type i32 = rewriter.getI32Type();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    StringRef prefix = op.getPrefix();
    Value prefixData = LLVM::ZeroOp::create(rewriter, op.getLoc(), pointer);
    if (!prefix.empty()) {
      std::string name;
      for (uint64_t suffix = 0;; ++suffix) {
        name = ("__obelisk_scan_prefix." + Twine(suffix)).str();
        if (!module.lookupSymbol(name))
          break;
      }
      prefixData = LLVM::AddressOfOp::create(
          rewriter, op.getLoc(),
          makeByteArrayGlobal(module, op.getLoc(), name, prefix));
    }
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Value fieldOutput = entryAlloca(rewriter, op.getLoc(), i64, 1, 8);
    Value cursorOutput = entryAlloca(rewriter, op.getLoc(), i32, 1, 4);
    Value okOutput = entryAlloca(rewriter, op.getLoc(), i32, 1, 4);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), i64),
                          fieldOutput, 8);
    for (Value output : {cursorOutput, okOutput})
      LLVM::StoreOp::create(rewriter, op.getLoc(),
                            LLVM::ZeroOp::create(rewriter, op.getLoc(), i32),
                            output, 4);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_string_scan_field"),
            ValueRange{lane, adaptor.getInput().front(),
                       adaptor.getCursor().front(), prefixData,
                       llvmConstant(rewriter, op.getLoc(), i64, prefix.size()),
                       llvmConstant(rewriter, op.getLoc(), i32,
                                    op.getSpecifier()),
                       fieldOutput, cursorOutput, okOutput})
            .getResult();
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.replaceOp(
        op, ValueRange{
                LLVM::LoadOp::create(rewriter, op.getLoc(), i64, fieldOutput, 8),
                LLVM::LoadOp::create(rewriter, op.getLoc(), i32, cursorOutput,
                                     4),
                LLVM::LoadOp::create(rewriter, op.getLoc(), i32, okOutput, 4)});
    return success();
  }
};

class PlusargTestConversion final
    : public OpConversionPattern<sim::SimPlusargTestOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimPlusargTestOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    Type i32 = rewriter.getI32Type();
    Value output = entryAlloca(rewriter, op.getLoc(), i32, 1, 4);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), i32),
                          output, 4);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_plusarg_test"),
            ValueRange{context, adaptor.getName().front(), output})
            .getResult();
    Value found = LLVM::LoadOp::create(rewriter, op.getLoc(), i32, output, 4);
    Value ok = arith::CmpIOp::create(
        rewriter, op.getLoc(), arith::CmpIPredicate::eq, status,
        llvmConstant(rewriter, op.getLoc(), i32, 0));
    rewriter.replaceOp(
        op, arith::SelectOp::create(
                rewriter, op.getLoc(), ok, found,
                LLVM::ZeroOp::create(rewriter, op.getLoc(), i32)));
    return success();
  }
};

class PlusargValueConversion final
    : public OpConversionPattern<sim::SimPlusargValueOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(sim::SimPlusargValueOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Type i64 = rewriter.getI64Type();
    Type i32 = rewriter.getI32Type();
    Value tailOutput = entryAlloca(rewriter, op.getLoc(), i64, 1, 8);
    Value foundOutput = entryAlloca(rewriter, op.getLoc(), i32, 1, 4);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), i64),
                          tailOutput, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), i32),
                          foundOutput, 4);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_plusarg_value"),
            ValueRange{context, lane, adaptor.getPrefix().front(), tailOutput,
                       foundOutput})
            .getResult();
    Value ok = arith::CmpIOp::create(
        rewriter, op.getLoc(), arith::CmpIPredicate::eq, status,
        llvmConstant(rewriter, op.getLoc(), i32, 0));
    Value tail = LLVM::LoadOp::create(rewriter, op.getLoc(), i64, tailOutput, 8);
    Value found =
        LLVM::LoadOp::create(rewriter, op.getLoc(), i32, foundOutput, 4);
    rewriter.replaceOp(
        op, ValueRange{arith::SelectOp::create(
                           rewriter, op.getLoc(), ok, tail,
                           LLVM::ZeroOp::create(rewriter, op.getLoc(), i64)),
                       arith::SelectOp::create(
                           rewriter, op.getLoc(), ok, found,
                           LLVM::ZeroOp::create(rewriter, op.getLoc(), i32))});
    return success();
  }
};

// File queries that yield a managed string plus an i32 companion: the runtime
// entry point takes the descriptor and two output pointers, and both results
// fall back to zero when the call reports failure.
template <typename Op>
class StringFileQueryConversion final : public OpConversionPattern<Op> {
public:
  StringFileQueryConversion(const TypeConverter &converter,
                            MLIRContext *context, StringRef symbol)
      : OpConversionPattern<Op>(converter, context), symbol(symbol) {}

  LogicalResult
  matchAndRewrite(Op op, typename OpConversionPattern<Op>::OneToNOpAdaptor
                             adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    Type i64 = rewriter.getI64Type();
    Type i32 = rewriter.getI32Type();
    Value stringOutput = entryAlloca(rewriter, op.getLoc(), i64, 1, 8);
    Value countOutput = entryAlloca(rewriter, op.getLoc(), i32, 1, 4);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), i64),
                          stringOutput, 8);
    LLVM::StoreOp::create(rewriter, op.getLoc(),
                          LLVM::ZeroOp::create(rewriter, op.getLoc(), i32),
                          countOutput, 4);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(), symbol),
            ValueRange{context, lane, adaptor.getDescriptor().front(),
                       stringOutput, countOutput})
            .getResult();
    Value ok = arith::CmpIOp::create(
        rewriter, op.getLoc(), arith::CmpIPredicate::eq, status,
        llvmConstant(rewriter, op.getLoc(), i32, 0));
    Value string =
        LLVM::LoadOp::create(rewriter, op.getLoc(), i64, stringOutput, 8);
    Value count =
        LLVM::LoadOp::create(rewriter, op.getLoc(), i32, countOutput, 4);
    rewriter.replaceOp(
        op, ValueRange{arith::SelectOp::create(
                           rewriter, op.getLoc(), ok, string,
                           LLVM::ZeroOp::create(rewriter, op.getLoc(), i64)),
                       arith::SelectOp::create(
                           rewriter, op.getLoc(), ok, count,
                           LLVM::ZeroOp::create(rewriter, op.getLoc(), i32))});
    return success();
  }

private:
  std::string symbol;
};

template <typename Op>
class StringParseConversion final : public OpConversionPattern<Op> {
public:
  StringParseConversion(const TypeConverter &converter, MLIRContext *context,
                        StringRef symbol)
      : OpConversionPattern<Op>(converter, context), symbol(symbol) {}

  LogicalResult
  matchAndRewrite(Op op,
                  typename OpConversionPattern<Op>::OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type resultType = std::is_same_v<Op, sim::SimStringParseIntegerOp>
                          ? Type(rewriter.getI64Type())
                          : Type(rewriter.getF64Type());
    Value output = entryAlloca(rewriter, op.getLoc(), resultType, 1, 8);
    LLVM::StoreOp::create(
        rewriter, op.getLoc(),
        LLVM::ZeroOp::create(rewriter, op.getLoc(), resultType), output, 8);
    SmallVector<Value> operands{adaptor.getInput().front()};
    if constexpr (std::is_same_v<Op, sim::SimStringParseIntegerOp>)
      operands.push_back(llvmConstant(rewriter, op.getLoc(),
                                      rewriter.getI32Type(), op.getRadix()));
    operands.push_back(output);
    Value status =
        LLVM::CallOp::create(
            rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
            SymbolRefAttr::get(rewriter.getContext(), symbol), operands)
            .getResult();
    auto [context, lane] = managedContextAndLane(rewriter, op.getLoc());
    (void)lane;
    reportManagedStatus(rewriter, op.getLoc(), context, status);
    rewriter.replaceOp(
        op, LLVM::LoadOp::create(rewriter, op.getLoc(), resultType, output, 8));
    return success();
  }

private:
  std::string symbol;
};

} // namespace

void populateManagedStringToLLVMConversionPatterns(
    RewritePatternSet &patterns, TypeConverter &converter) {
  MLIRContext *context = patterns.getContext();
  patterns.add<StringLiteralConversion, StringFromPackedConversion,
               StringToPackedConversion, StringConcatConversion,
               StringLengthConversion, StringGetcConversion,
               StringCompareConversion, StringScanFieldConversion,
               PlusargTestConversion,
               PlusargValueConversion>(converter, context);
  patterns.add<StringFileOpenConversion<sim::SimFileOpenStringMCDOp>,
               StringFileOpenConversion<sim::SimFileOpenStringOp>>(
      converter, context);
  patterns.add<StringFileQueryConversion<sim::SimFileGetlineStringOp>>(
      converter, context, "obelisk_rt_v1_file_getline_string");
  patterns.add<StringFileQueryConversion<sim::SimFileErrorStringOp>>(
      converter, context, "obelisk_rt_v1_file_error_string");
  patterns.add<StringAllocatingCallConversion<sim::SimStringRepeatOp>>(
      converter, context, "obelisk_rt_v1_string_repeat");
  patterns.add<StringAllocatingCallConversion<sim::SimStringPutcOp>>(
      converter, context, "obelisk_rt_v1_string_putc");
  patterns.add<StringAllocatingCallConversion<sim::SimStringSubstrOp>>(
      converter, context, "obelisk_rt_v1_string_substr");
  patterns.add<StringAllocatingCallConversion<sim::SimStringCaseConvertOp>>(
      converter, context, "obelisk_rt_v1_string_case_convert");
  patterns.add<StringAllocatingCallConversion<sim::SimStringFormatIntegerOp>>(
      converter, context, "obelisk_rt_v1_string_format_integer");
  patterns.add<StringAllocatingCallConversion<sim::SimStringFormatRealOp>>(
      converter, context, "obelisk_rt_v1_string_format_real");
  patterns.add<StringParseConversion<sim::SimStringParseIntegerOp>>(
      converter, context, "obelisk_rt_v1_string_parse_integer");
  patterns.add<StringParseConversion<sim::SimStringParseRealOp>>(
      converter, context, "obelisk_rt_v1_string_parse_real");
}

} // namespace obelisk::detail
