//===- RuntimeToLLVM.cpp - Lower typed runtime operations to C ABI calls -===//

#include "obelisk/Conversion/RuntimeToLLVM.h"

#include "obelisk/Dialect/Runtime/RuntimeABI.h"
#include "obelisk/Dialect/Runtime/RuntimeOps.h"

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
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Error.h"
#include "llvm/TargetParser/Triple.h"

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_CONVERTOBELISKRUNTIMETOLLVMPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

struct ABIAlignments {
  unsigned pointer = 0;
  unsigned i8 = 0;
  unsigned i32 = 0;
  unsigned i64 = 0;
  unsigned span = 0;
  unsigned action = 0;
};

FailureOr<ABIAlignments> validateTargetABI(ModuleOp module,
                                           const llvm::DataLayout &layout) {
  if (!layout.isLittleEndian() || layout.getPointerSizeInBits() != 64) {
    module.emitError()
        << "runtime lowering currently requires a 64-bit little-endian target";
    return failure();
  }

  llvm::LLVMContext context;
  llvm::Type *pointer = llvm::PointerType::get(context, 0);
  llvm::Type *i8 = llvm::Type::getInt8Ty(context);
  llvm::Type *i16 = llvm::Type::getInt16Ty(context);
  llvm::Type *i32 = llvm::Type::getInt32Ty(context);
  llvm::Type *i64 = llvm::Type::getInt64Ty(context);
  auto *span = llvm::StructType::get(context, {pointer, i64});
  auto *handle = llvm::StructType::get(context, {i32, i32, i64});
  auto *action = llvm::StructType::get(context, {i32, i32, i32, i32, i64, i64});
  auto *bytecode = llvm::StructType::get(
      context, {pointer, i64, pointer, i32, i32, i64, pointer, pointer, i64,
                pointer, i32, i32, pointer, i64});
  auto checkType = [&](llvm::StringRef name, llvm::Type *type,
                       uint64_t expectedSize,
                       uint64_t expectedAlignment) -> LogicalResult {
    llvm::TypeSize size = layout.getTypeAllocSize(type);
    uint64_t alignment = layout.getABITypeAlign(type).value();
    if (size.isScalable() || size.getFixedValue() != expectedSize ||
        alignment != expectedAlignment) {
      module.emitError() << "LLVM data layout is incompatible with the "
                            "Obelisk runtime ABI for "
                         << name << " (expected size/alignment " << expectedSize
                         << "/" << expectedAlignment << ")";
      return failure();
    }
    return success();
  };
  auto checkStruct =
      [&](llvm::StringRef name, llvm::ArrayRef<llvm::Type *> elements,
          llvm::ArrayRef<uint64_t> offsets, uint64_t expectedSize,
          uint64_t expectedAlignment) -> LogicalResult {
    auto *type = llvm::StructType::get(context, elements);
    if (failed(checkType(name, type, expectedSize, expectedAlignment)))
      return failure();
    const llvm::StructLayout *structLayout = layout.getStructLayout(type);
    for (auto [index, offset] : llvm::enumerate(offsets))
      if (structLayout->getElementOffset(index) != offset) {
        module.emitError() << "LLVM data layout is incompatible with the "
                              "Obelisk runtime ABI for "
                           << name << " field " << index << " (expected offset "
                           << offset << ")";
        return failure();
      }
    return success();
  };

  if (failed(checkType("pointer", pointer, 8, 8)) ||
      failed(checkType("i8", i8, 1, 1)) ||
      failed(checkType("i16", i16, 2, 2)) ||
      failed(checkType("i32", i32, 4, 4)) ||
      failed(checkType("i64", i64, 8, 8)) ||
      failed(checkStruct("byte span", {pointer, i64}, {0, 8}, 16, 8)) ||
      failed(checkStruct("format argument", {i32, i32, i64, pointer, pointer},
                         {0, 4, 8, 16, 24}, 32, 8)) ||
      failed(checkStruct("stable handle", {i32, i32, i64}, {0, 4, 8}, 16, 8)) ||
      failed(checkStruct("fragment action", {i32, i32, i32, i32, i64, i64},
                         {0, 4, 8, 12, 16, 24}, 32, 8)) ||
      failed(checkStruct("format environment",
                         {pointer, i64, pointer, i64, i32, i32, pointer, i64},
                         {0, 8, 16, 24, 32, 36, 40, 48}, 56, 8)) ||
      failed(checkStruct("bytecode entry", {i32, i32}, {0, 4}, 8, 4)) ||
      failed(checkStruct("bytecode validation", {i32, i32}, {0, 4}, 8, 4)) ||
      failed(checkStruct("bytecode operand",
                         {i8, i8, i8, i8, i32, i64, i64, i64},
                         {0, 1, 2, 3, 4, 8, 16, 24}, 32, 8)) ||
      failed(checkStruct("bytecode service site", {i32, i32, i16, i16, i32},
                         {0, 4, 8, 10, 12}, 16, 4)) ||
      failed(checkStruct("bytecode program",
                         {pointer, i64, pointer, i32, i32, i64, pointer,
                          pointer, i64, pointer, i32, i32, pointer, i64},
                         {0, 8, 16, 24, 28, 32, 40, 48, 56, 64, 72, 76, 80, 88},
                         96, 8)) ||
      failed(checkStruct("fragment descriptor", {handle, i32, i32, bytecode},
                         {0, 16, 20, 24}, 120, 8)))
    return failure();

  return ABIAlignments{
      static_cast<unsigned>(layout.getABITypeAlign(pointer).value()),
      static_cast<unsigned>(layout.getABITypeAlign(i8).value()),
      static_cast<unsigned>(layout.getABITypeAlign(i32).value()),
      static_cast<unsigned>(layout.getABITypeAlign(i64).value()),
      static_cast<unsigned>(layout.getABITypeAlign(span).value()),
      static_cast<unsigned>(layout.getABITypeAlign(action).value())};
}

struct ABITypes {
  explicit ABITypes(MLIRContext *context, ABIAlignments alignments)
      : pointer(LLVM::LLVMPointerType::get(context)),
        voidType(LLVM::LLVMVoidType::get(context)),
        i1(IntegerType::get(context, 1)), i8(IntegerType::get(context, 8)),
        i32(IntegerType::get(context, 32)), i64(IntegerType::get(context, 64)),
        span(LLVM::LLVMStructType::getLiteral(context, {pointer, i64})),
        argument(LLVM::LLVMStructType::getLiteral(
            context, {i32, i32, i64, pointer, pointer})),
        handle(LLVM::LLVMStructType::getLiteral(context, {i32, i32, i64})),
        action(LLVM::LLVMStructType::getLiteral(
            context, {i32, i32, i32, i32, i64, i64})),
        bytecodeEntry(LLVM::LLVMStructType::getLiteral(context, {i32, i32})),
        bytecodeValidation(
            LLVM::LLVMStructType::getLiteral(context, {i32, i32})),
        bytecodeOperand(LLVM::LLVMStructType::getLiteral(
            context, {i8, i8, i8, i8, i32, i64, i64, i64})),
        bytecodeServiceSite(LLVM::LLVMStructType::getLiteral(
            context, {i32, i32, IntegerType::get(context, 16),
                      IntegerType::get(context, 16), i32})),
        alignments(alignments) {}

  Type pointer;
  Type voidType;
  Type i1;
  Type i8;
  Type i32;
  Type i64;
  Type span;
  Type argument;
  Type handle;
  Type action;
  Type bytecodeEntry;
  Type bytecodeValidation;
  Type bytecodeOperand;
  Type bytecodeServiceSite;
  ABIAlignments alignments;
};

bool containsRuntimeType(Type type) {
  bool found = false;
  type.walk([&](Type nested) {
    if (nested.getDialect().getNamespace() == "obelisk_rt")
      found = true;
  });
  return found;
}

std::optional<Type> convertRuntimeType(Type type, const ABITypes &abi) {
  using namespace obelisk::runtime;
  if (isa<ContextType, CStringType, FormatEnvironmentType,
          FragmentDescriptorType, BytecodeProgramType>(type))
    return abi.pointer;
  if (isa<StatusType, FileDescriptorType>(type))
    return abi.i32;
  if (isa<ByteSpanType, MutableByteSpanType, BufferType, ArgumentArrayType>(
          type))
    return abi.span;
  if (isa<ArgumentType>(type))
    return abi.argument;
  if (isa<HandleType>(type))
    return abi.handle;
  if (isa<FragmentActionType>(type))
    return abi.action;
  if (isa<BytecodeEntryType>(type))
    return abi.bytecodeEntry;
  if (isa<BytecodeValidationType>(type))
    return abi.bytecodeValidation;
  if (isa<BytecodeOperandType>(type))
    return abi.bytecodeOperand;
  if (isa<BytecodeServiceSiteType>(type))
    return abi.bytecodeServiceSite;
  if (isa<BytecodeOpcodeType, BytecodeValueType, BytecodeOperandKindType,
          BytecodeOperandDirectionType, BytecodeServiceValueType>(type))
    return abi.i8;
  if (isa<BytecodeServiceType>(type))
    return abi.i32;
  if (auto function = dyn_cast<FunctionType>(type)) {
    SmallVector<Type> inputs;
    SmallVector<Type> results;
    for (Type input : function.getInputs()) {
      std::optional<Type> converted = convertRuntimeType(input, abi);
      if (!converted)
        return std::nullopt;
      inputs.push_back(*converted);
    }
    for (Type result : function.getResults()) {
      std::optional<Type> converted = convertRuntimeType(result, abi);
      if (!converted)
        return std::nullopt;
      results.push_back(*converted);
    }
    return FunctionType::get(type.getContext(), inputs, results);
  }
  if (auto tuple = dyn_cast<TupleType>(type)) {
    SmallVector<Type> elements;
    for (Type element : tuple.getTypes()) {
      std::optional<Type> converted = convertRuntimeType(element, abi);
      if (!converted)
        return std::nullopt;
      elements.push_back(*converted);
    }
    return TupleType::get(type.getContext(), elements);
  }
  // Do not silently legalize an unsupported container that still embeds a
  // runtime type. Function and tuple containers above are the only recursive
  // builtin boundaries currently supported.
  if (containsRuntimeType(type))
    return std::nullopt;
  return type;
}

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

LLVM::LLVMFunctionType getFunctionType(runtime::RuntimeCall call,
                                       const ABITypes &abi) {
  SmallVector<Type> arguments;
  Type result = abi.i32;
  switch (runtime::getRuntimeSignature(call)) {
  case runtime::RuntimeSignature::ContextCreate:
    arguments = {abi.pointer};
    break;
  case runtime::RuntimeSignature::ContextDestroy:
    result = abi.voidType;
    arguments = {abi.pointer};
    break;
  case runtime::RuntimeSignature::StatusString:
    result = abi.pointer;
    arguments = {abi.i32};
    break;
  case runtime::RuntimeSignature::BufferRelease:
    result = abi.voidType;
    arguments = {abi.pointer};
    break;
  case runtime::RuntimeSignature::LastError:
    arguments = {abi.pointer, abi.pointer};
    break;
  case runtime::RuntimeSignature::Format:
    arguments = {abi.pointer, abi.pointer, abi.i64,    abi.pointer,
                 abi.i64,     abi.pointer, abi.pointer};
    break;
  case runtime::RuntimeSignature::Display:
    arguments = {abi.pointer, abi.i32, abi.i32,    abi.i32,
                 abi.pointer, abi.i64, abi.pointer};
    break;
  case runtime::RuntimeSignature::FileOpenMCD:
    arguments = {abi.pointer, abi.pointer, abi.i64, abi.pointer};
    break;
  case runtime::RuntimeSignature::FileOpen:
    arguments = {abi.pointer, abi.pointer, abi.i64,
                 abi.pointer, abi.i64,     abi.pointer};
    break;
  case runtime::RuntimeSignature::FileDescriptorStatus:
    arguments = {abi.pointer, abi.i32};
    break;
  case runtime::RuntimeSignature::FileBytesCount:
    arguments = {abi.pointer, abi.i32, abi.pointer, abi.i64, abi.pointer};
    break;
  case runtime::RuntimeSignature::FileByteOut:
    arguments = {abi.pointer, abi.i32, abi.pointer};
    break;
  case runtime::RuntimeSignature::FileUngetc:
    arguments = {abi.pointer, abi.i32, abi.i8};
    break;
  case runtime::RuntimeSignature::FileBufferOut:
    arguments = {abi.pointer, abi.i32, abi.pointer};
    break;
  case runtime::RuntimeSignature::FileU32Out:
    arguments = {abi.pointer, abi.i32, abi.pointer};
    break;
  case runtime::RuntimeSignature::FileError:
    arguments = {abi.pointer, abi.i32, abi.pointer, abi.pointer};
    break;
  case runtime::RuntimeSignature::FileSeek:
    arguments = {abi.pointer, abi.i32, abi.i64, abi.i32};
    break;
  case runtime::RuntimeSignature::FileI64Out:
    arguments = {abi.pointer, abi.i32, abi.pointer};
    break;
  case runtime::RuntimeSignature::FragmentExecute:
    arguments = {abi.pointer, abi.pointer, abi.pointer,
                 abi.i64,     abi.i32,     abi.pointer};
    break;
  case runtime::RuntimeSignature::BytecodeBounded:
    arguments = {abi.pointer, abi.pointer, abi.pointer, abi.i64,
                 abi.i32,     abi.i64,     abi.pointer};
    break;
  }
  return LLVM::LLVMFunctionType::get(result, arguments, false);
}

FailureOr<LLVM::LLVMFuncOp>
getOrCreateDeclaration(Operation *anchor, runtime::RuntimeCall call,
                       ConversionPatternRewriter &rewriter,
                       const ABITypes &abi) {
  ModuleOp module = anchor->getParentOfType<ModuleOp>();
  StringRef name = runtime::getRuntimeSymbol(call);
  LLVM::LLVMFunctionType expected = getFunctionType(call, abi);
  if (Operation *existing = SymbolTable::lookupSymbolIn(module, name)) {
    auto function = dyn_cast<LLVM::LLVMFuncOp>(existing);
    if (!function || function.getFunctionType() != expected) {
      anchor->emitOpError() << "runtime symbol @" << name
                            << " is predeclared with an incompatible type";
      return failure();
    }
    std::optional<StringRef> visibility = function.getSymVisibility();
    if (!function.isExternal() ||
        function.getLinkage() != LLVM::Linkage::External ||
        function.getCConv() != LLVM::cconv::CConv::C ||
        (visibility && *visibility != "public")) {
      anchor->emitOpError()
          << "runtime symbol @" << name
          << " must be an external public declaration using the C calling "
             "convention";
      return failure();
    }
    return function;
  }
  OpBuilder::InsertionGuard guard(rewriter);
  rewriter.setInsertionPointToStart(module.getBody());
  return LLVM::LLVMFuncOp::create(rewriter, anchor->getLoc(), name, expected);
}

class RuntimeCallLowering : public ConversionPattern {
public:
  RuntimeCallLowering(const TypeConverter &converter, StringRef operationName,
                      runtime::RuntimeCall call, MLIRContext *context,
                      const ABITypes &abi)
      : ConversionPattern(converter, operationName, 1, context), call(call),
        abi(abi) {}

  LogicalResult
  matchAndRewrite(Operation *operation, ArrayRef<Value> operands,
                  ConversionPatternRewriter &rewriter) const override {
    func::FuncOp function = operation->getParentOfType<func::FuncOp>();
    if (!function)
      return operation->emitOpError()
             << "must be nested in a function for LLVM lowering";
    for (Operation *ancestor = operation->getParentOp();
         ancestor && ancestor != function; ancestor = ancestor->getParentOp()) {
      StringRef dialect = ancestor->getName().getDialectNamespace();
      if (ancestor->hasTrait<OpTrait::HasParallelRegion>() ||
          dialect == "async" || dialect == "gpu" || dialect == "omp" ||
          dialect == "acc")
        return operation->emitOpError()
               << "cannot lower a runtime call nested in a concurrent region "
                  "with function-entry ABI scratch storage";
    }
    FailureOr<LLVM::LLVMFuncOp> declaration =
        getOrCreateDeclaration(operation, call, rewriter, abi);
    if (failed(declaration))
      return failure();
    Location location = operation->getLoc();

    auto allocate = [&](Type elementType, unsigned alignment) -> Value {
      Value address;
      {
        OpBuilder::InsertionGuard guard(rewriter);
        rewriter.setInsertionPointToStart(&function.getBody().front());
        Value one = LLVM::ConstantOp::create(rewriter, location, abi.i64, 1);
        address = LLVM::AllocaOp::create(rewriter, location, abi.pointer,
                                         elementType, one, alignment);
      }
      Value zero = LLVM::ZeroOp::create(rewriter, location, elementType);
      LLVM::StoreOp::create(rewriter, location, zero, address, alignment);
      return address;
    };
    auto load = [&](Type type, Value address, unsigned alignment) -> Value {
      return LLVM::LoadOp::create(rewriter, location, type, address, alignment);
    };
    auto store = [&](Value value, Value address, unsigned alignment) {
      LLVM::StoreOp::create(rewriter, location, value, address, alignment);
    };
    auto extract = [&](Value value, int64_t index) -> Value {
      return LLVM::ExtractValueOp::create(rewriter, location, value, {index});
    };
    auto span = [&](Value value) {
      return std::pair<Value, Value>{extract(value, 0), extract(value, 1)};
    };
    auto emitCall = [&](ValueRange arguments) -> LLVM::CallOp {
      Type result = declaration->getFunctionType().getReturnType();
      SmallVector<Type> results;
      if (!isa<LLVM::LLVMVoidType>(result))
        results.push_back(result);
      return LLVM::CallOp::create(rewriter, location, results,
                                  declaration->getSymNameAttr(), arguments);
    };
    auto callStatus = [&](ValueRange arguments) -> Value {
      return emitCall(arguments).getResult();
    };
    auto replaceStatus = [&](ValueRange arguments) {
      rewriter.replaceOp(operation, callStatus(arguments));
      return success();
    };
    auto replaceStatusAndLoad = [&](ValueRange arguments, Value out, Type type,
                                    unsigned alignment) {
      Value status = callStatus(arguments);
      Value value = load(type, out, alignment);
      rewriter.replaceOp(operation, ValueRange{status, value});
      return success();
    };

    switch (call) {
    case runtime::RuntimeCall::ContextCreate: {
      Value output = allocate(abi.pointer, abi.alignments.pointer);
      return replaceStatusAndLoad({output}, output, abi.pointer,
                                  abi.alignments.pointer);
    }
    case runtime::RuntimeCall::ContextDestroy:
      emitCall(operands);
      rewriter.eraseOp(operation);
      return success();
    case runtime::RuntimeCall::StatusString:
      rewriter.replaceOp(operation, emitCall(operands).getResult());
      return success();
    case runtime::RuntimeCall::BufferRelease: {
      Value buffer = allocate(abi.span, abi.alignments.span);
      store(operands[0], buffer, abi.alignments.span);
      emitCall(buffer);
      rewriter.eraseOp(operation);
      return success();
    }
    case runtime::RuntimeCall::LastError: {
      Value output = allocate(abi.span, abi.alignments.span);
      return replaceStatusAndLoad({operands[0], output}, output, abi.span,
                                  abi.alignments.span);
    }
    case runtime::RuntimeCall::Format: {
      auto [formatData, formatSize] = span(operands[1]);
      auto [argumentData, argumentCount] = span(operands[2]);
      Value output = allocate(abi.span, abi.alignments.span);
      return replaceStatusAndLoad({operands[0], formatData, formatSize,
                                   argumentData, argumentCount, operands[3],
                                   output},
                                  output, abi.span, abi.alignments.span);
    }
    case runtime::RuntimeCall::Display: {
      auto display = cast<runtime::RTDisplayOp>(operation);
      Value newline =
          LLVM::ZExtOp::create(rewriter, location, abi.i32, operands[2]);
      Value radix = LLVM::ConstantOp::create(
          rewriter, location, abi.i32,
          static_cast<int64_t>(display.getDefaultRadix()));
      auto [itemData, itemCount] = span(operands[3]);
      return replaceStatus({operands[0], operands[1], newline, radix, itemData,
                            itemCount, operands[4]});
    }
    case runtime::RuntimeCall::FileOpenMCD: {
      auto [pathData, pathSize] = span(operands[1]);
      Value output = allocate(abi.i32, abi.alignments.i32);
      return replaceStatusAndLoad({operands[0], pathData, pathSize, output},
                                  output, abi.i32, abi.alignments.i32);
    }
    case runtime::RuntimeCall::FileOpen: {
      auto [pathData, pathSize] = span(operands[1]);
      auto [modeData, modeSize] = span(operands[2]);
      Value output = allocate(abi.i32, abi.alignments.i32);
      return replaceStatusAndLoad(
          {operands[0], pathData, pathSize, modeData, modeSize, output}, output,
          abi.i32, abi.alignments.i32);
    }
    case runtime::RuntimeCall::FileClose:
    case runtime::RuntimeCall::FileFlush:
    case runtime::RuntimeCall::FileRewind:
      return replaceStatus(operands);
    case runtime::RuntimeCall::FileWrite:
    case runtime::RuntimeCall::FileRead: {
      auto [data, size] = span(operands[2]);
      Value output = allocate(abi.i64, abi.alignments.i64);
      return replaceStatusAndLoad(
          {operands[0], operands[1], data, size, output}, output, abi.i64,
          abi.alignments.i64);
    }
    case runtime::RuntimeCall::FileGetc: {
      Value output = allocate(abi.i8, abi.alignments.i8);
      return replaceStatusAndLoad({operands[0], operands[1], output}, output,
                                  abi.i8, abi.alignments.i8);
    }
    case runtime::RuntimeCall::FileUngetc:
      return replaceStatus(operands);
    case runtime::RuntimeCall::FileGetline: {
      Value output = allocate(abi.span, abi.alignments.span);
      return replaceStatusAndLoad({operands[0], operands[1], output}, output,
                                  abi.span, abi.alignments.span);
    }
    case runtime::RuntimeCall::FileEof: {
      Value output = allocate(abi.i32, abi.alignments.i32);
      return replaceStatusAndLoad({operands[0], operands[1], output}, output,
                                  abi.i32, abi.alignments.i32);
    }
    case runtime::RuntimeCall::FileError: {
      Value code = allocate(abi.i32, abi.alignments.i32);
      Value message = allocate(abi.span, abi.alignments.span);
      Value status = callStatus({operands[0], operands[1], code, message});
      rewriter.replaceOp(
          operation, ValueRange{status, load(abi.i32, code, abi.alignments.i32),
                                load(abi.span, message, abi.alignments.span)});
      return success();
    }
    case runtime::RuntimeCall::FileSeek: {
      auto seek = cast<runtime::RTFileSeekOp>(operation);
      Value origin = LLVM::ConstantOp::create(
          rewriter, location, abi.i32, static_cast<int64_t>(seek.getOrigin()));
      return replaceStatus({operands[0], operands[1], operands[2], origin});
    }
    case runtime::RuntimeCall::FileTell: {
      Value output = allocate(abi.i64, abi.alignments.i64);
      return replaceStatusAndLoad({operands[0], operands[1], output}, output,
                                  abi.i64, abi.alignments.i64);
    }
    case runtime::RuntimeCall::FragmentExecute:
    case runtime::RuntimeCall::BytecodeExecuteBounded: {
      auto [frame, frameSize] = span(operands[2]);
      Value output = allocate(abi.action, abi.alignments.action);
      SmallVector<Value> arguments = {operands[0], operands[1], frame,
                                      frameSize, operands[3]};
      if (call == runtime::RuntimeCall::BytecodeExecuteBounded)
        arguments.push_back(operands[4]);
      arguments.push_back(output);
      return replaceStatusAndLoad(arguments, output, abi.action,
                                  abi.alignments.action);
    }
    }
    llvm_unreachable("all runtime calls are handled");
  }

private:
  runtime::RuntimeCall call;
  const ABITypes &abi;
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
      if (!resultValue.hasOneUse() ||
          !isa<runtime::RTBufferReleaseOp>(*resultValue.getUsers().begin()) ||
          (*resultValue.getUsers().begin())->getBlock() !=
              operation->getBlock()) {
        operation->emitOpError()
            << "owned runtime buffer must have one same-block "
               "obelisk_rt.buffer.release consumer";
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
    FailureOr<ABIAlignments> alignments = validateTargetABI(module, *parsed);
    if (failed(alignments))
      return signalPassFailure();
    if (auto tripleAttr =
            module->getAttrOfType<StringAttr>("llvm.target_triple")) {
      llvm::Triple triple(tripleAttr.getValue());
      if (!triple.isArch64Bit() || !triple.isLittleEndian()) {
        module.emitError()
            << "llvm.target_triple is inconsistent with the supported "
               "64-bit little-endian runtime ABI";
        return signalPassFailure();
      }
    }
    if (failed(verifyRuntimeBufferOwnership(module)))
      return signalPassFailure();

    ABITypes abi(&getContext(), *alignments);
    TypeConverter converter;
    converter.addConversion([&](Type type) -> std::optional<Type> {
      return convertRuntimeType(type, abi);
    });

    RewritePatternSet patterns(&getContext());
#define OBELISK_RUNTIME_CALL(Name, Op, Symbol, Signature)                      \
  patterns.add<RuntimeCallLowering>(                                           \
      converter, runtime::Op::getOperationName(), runtime::RuntimeCall::Name,  \
      &getContext(), abi);
#include "obelisk/Dialect/Runtime/RuntimeABI.def"
#undef OBELISK_RUNTIME_CALL
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

} // namespace obelisk
