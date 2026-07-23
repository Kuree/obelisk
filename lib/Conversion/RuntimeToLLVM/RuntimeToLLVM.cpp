//===- RuntimeToLLVM.cpp - Lower typed runtime operations to C ABI calls -===//

#include "obelisk/Conversion/RuntimeToLLVM.h"

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
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Error.h"
#include "llvm/TargetParser/Triple.h"

#include <limits>

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
  unsigned argument = 0;
  unsigned environment = 0;
  unsigned action = 0;
};

FailureOr<ABIAlignments> validateTargetABI(ModuleOp module,
                                           const llvm::DataLayout &layout) {
  if (!layout.isLittleEndian() || layout.getPointerSizeInBits() != 64) {
    module.emitError()
        << "runtime lowering currently requires a 64-bit little-endian target";
    return failure();
  }

  // DataLayout caches StructLayout objects by LLVM type identity. Keep those
  // entries local to the LLVMContext that owns the validation-only types.
  llvm::DataLayout validationLayout(layout.getStringRepresentation());
  llvm::LLVMContext context;
  llvm::Type *pointer = llvm::PointerType::get(context, 0);
  llvm::Type *i8 = llvm::Type::getInt8Ty(context);
  llvm::Type *i16 = llvm::Type::getInt16Ty(context);
  llvm::Type *i32 = llvm::Type::getInt32Ty(context);
  llvm::Type *i64 = llvm::Type::getInt64Ty(context);
  auto *span = llvm::StructType::get(context, {pointer, i64});
  auto *formatArgument =
      llvm::StructType::get(context, {i32, i32, i64, pointer, pointer});
  auto *formatEnvironment = llvm::StructType::get(
      context, {pointer, i64, pointer, i64, i32, i32, pointer, i64, i64});
  auto *handle = llvm::StructType::get(context, {i32, i32, i64});
  auto *action = llvm::StructType::get(context, {i32, i32, i32, i32, i64, i64});
  auto *bytecode = llvm::StructType::get(
      context, {pointer, i64, pointer, i32, i32, i64, pointer, pointer, i64,
                pointer, i32, i32, pointer, i64});
  auto checkType = [&](llvm::StringRef name, llvm::Type *type,
                       uint64_t expectedSize,
                       uint64_t expectedAlignment) -> LogicalResult {
    llvm::TypeSize size = validationLayout.getTypeAllocSize(type);
    uint64_t alignment = validationLayout.getABITypeAlign(type).value();
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
    const llvm::StructLayout *structLayout =
        validationLayout.getStructLayout(type);
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
      failed(checkStruct("format argument", formatArgument->elements(),
                         {0, 4, 8, 16, 24}, 32, 8)) ||
      failed(checkStruct("stable handle", {i32, i32, i64}, {0, 4, 8}, 16, 8)) ||
      failed(checkStruct("fragment action", {i32, i32, i32, i32, i64, i64},
                         {0, 4, 8, 12, 16, 24}, 32, 8)) ||
      failed(checkStruct("wait entry", {i64, i32, i32}, {0, 8, 12}, 16, 8)) ||
      failed(checkStruct("format environment", formatEnvironment->elements(),
                         {0, 8, 16, 24, 32, 36, 40, 48, 56}, 64, 8)) ||
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
                         {0, 16, 20, 24}, 120, 8)) ||
      failed(checkStruct("process frame field", {i32, i32, i64, i64, i32, i32},
                         {0, 4, 8, 16, 24, 28}, 32, 8)) ||
      failed(checkStruct("process frame layout",
                         {i32, i32, i64, i64, pointer, i32, i32, pointer, i64},
                         {0, 4, 8, 16, 24, 32, 36, 40, 48}, 56, 8)) ||
      failed(checkStruct("activation descriptor", {i64, pointer, i32, i32},
                         {0, 8, 16, 20}, 24, 8)) ||
      failed(checkStruct("execution descriptor",
                         {i32, i32, i32, i32, pointer, i64, pointer, i64, i64,
                          i64, pointer, i64, i32, i32, pointer, i64},
                         {0, 4, 8, 12, 16, 24, 32, 40, 48, 56, 64, 72, 80,
                          84, 88, 96},
                         104, 8)) ||
      failed(checkStruct("design bytecode entry", {pointer, i32, i32},
                         {0, 8, 12}, 16, 8)) ||
      failed(checkStruct("process descriptor",
                         {handle, i32, i32, i32, i32, pointer, pointer, pointer,
                          pointer, pointer, pointer, pointer},
                         {0, 16, 20, 24, 28, 32, 40, 48, 56, 64, 72, 80}, 88,
                         8)) ||
      failed(checkStruct("process instance",
                         {pointer, pointer, pointer, i64, i64, i64, pointer,
                          i32, i32, i32, i32, pointer, pointer},
                         {0, 8, 16, 24, 32, 40, 48, 56, 60, 64, 68, 72, 80}, 88,
                         8)))
    return failure();

  return ABIAlignments{
      static_cast<unsigned>(validationLayout.getABITypeAlign(pointer).value()),
      static_cast<unsigned>(validationLayout.getABITypeAlign(i8).value()),
      static_cast<unsigned>(validationLayout.getABITypeAlign(i32).value()),
      static_cast<unsigned>(validationLayout.getABITypeAlign(i64).value()),
      static_cast<unsigned>(validationLayout.getABITypeAlign(span).value()),
      static_cast<unsigned>(
          validationLayout.getABITypeAlign(formatArgument).value()),
      static_cast<unsigned>(
          validationLayout.getABITypeAlign(formatEnvironment).value()),
      static_cast<unsigned>(validationLayout.getABITypeAlign(action).value())};
}

struct ABITypes {
  explicit ABITypes(MLIRContext *context, ABIAlignments alignments,
                    const llvm::DataLayout &layout)
      : pointer(LLVM::LLVMPointerType::get(context)),
        voidType(LLVM::LLVMVoidType::get(context)),
        i1(IntegerType::get(context, 1)), i8(IntegerType::get(context, 8)),
        i32(IntegerType::get(context, 32)), i64(IntegerType::get(context, 64)),
        span(LLVM::LLVMStructType::getLiteral(context, {pointer, i64})),
        argument(LLVM::LLVMStructType::getLiteral(
            context, {i32, i32, i64, pointer, pointer})),
        formatEnvironment(LLVM::LLVMStructType::getLiteral(
            context,
            {pointer, i64, pointer, i64, i32, i32, pointer, i64, i64})),
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
        alignments(alignments), layout(layout) {}

  Type pointer;
  Type voidType;
  Type i1;
  Type i8;
  Type i32;
  Type i64;
  Type span;
  Type argument;
  Type formatEnvironment;
  Type handle;
  Type action;
  Type bytecodeEntry;
  Type bytecodeValidation;
  Type bytecodeOperand;
  Type bytecodeServiceSite;
  ABIAlignments alignments;
  const llvm::DataLayout &layout;
};

ABIAlignments getABIAlignments(const llvm::DataLayout &layout) {
  llvm::DataLayout alignmentLayout(layout.getStringRepresentation());
  llvm::LLVMContext context;
  llvm::Type *pointer = llvm::PointerType::get(context, 0);
  llvm::Type *i8 = llvm::Type::getInt8Ty(context);
  llvm::Type *i32 = llvm::Type::getInt32Ty(context);
  llvm::Type *i64 = llvm::Type::getInt64Ty(context);
  auto *span = llvm::StructType::get(context, {pointer, i64});
  auto *argument =
      llvm::StructType::get(context, {i32, i32, i64, pointer, pointer});
  auto *environment = llvm::StructType::get(
      context, {pointer, i64, pointer, i64, i32, i32, pointer, i64, i64});
  auto *action = llvm::StructType::get(context, {i32, i32, i32, i32, i64, i64});
  return ABIAlignments{
      static_cast<unsigned>(alignmentLayout.getABITypeAlign(pointer).value()),
      static_cast<unsigned>(alignmentLayout.getABITypeAlign(i8).value()),
      static_cast<unsigned>(alignmentLayout.getABITypeAlign(i32).value()),
      static_cast<unsigned>(alignmentLayout.getABITypeAlign(i64).value()),
      static_cast<unsigned>(alignmentLayout.getABITypeAlign(span).value()),
      static_cast<unsigned>(alignmentLayout.getABITypeAlign(argument).value()),
      static_cast<unsigned>(
          alignmentLayout.getABITypeAlign(environment).value()),
      static_cast<unsigned>(alignmentLayout.getABITypeAlign(action).value())};
}

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
          FragmentDescriptorType, ProcessDescriptorType, ProcessInstanceType,
          BytecodeProgramType>(type))
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
    arguments = {abi.pointer, abi.i32, abi.i64, abi.pointer};
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
  case runtime::RuntimeSignature::ProcessCreate:
    arguments = {abi.pointer, abi.pointer};
    break;
  case runtime::RuntimeSignature::ProcessFrame:
    arguments = {abi.pointer, abi.pointer, abi.pointer};
    break;
  case runtime::RuntimeSignature::ProcessExecute:
    arguments = {abi.pointer, abi.pointer, abi.i32, abi.pointer};
    break;
  case runtime::RuntimeSignature::ProcessDestroy:
    arguments = {abi.pointer};
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

static Value llvmIntegerConstant(OpBuilder &builder, Location location,
                                 Type type, uint64_t value) {
  return LLVM::ConstantOp::create(builder, location, type,
                                  builder.getIntegerAttr(type, value));
}

static Value insertStructValue(OpBuilder &builder, Location location,
                               Value aggregate, Value value, int64_t index) {
  return LLVM::InsertValueOp::create(builder, location, aggregate, value,
                                     ArrayRef<int64_t>{index});
}

static Value makeSpan(OpBuilder &builder, Location location,
                      const ABITypes &abi, Value data, Value size) {
  Value span = LLVM::ZeroOp::create(builder, location, abi.span);
  span = insertStructValue(builder, location, span, data, 0);
  return insertStructValue(builder, location, span, size, 1);
}

static Block *getFunctionEntry(Operation *anchor, Operation *&function) {
  if (auto funcFunction = anchor->getParentOfType<func::FuncOp>()) {
    function = funcFunction;
    return &funcFunction.getBody().front();
  }
  if (auto llvmFunction = anchor->getParentOfType<LLVM::LLVMFuncOp>()) {
    function = llvmFunction;
    return &llvmFunction.getBody().front();
  }
  function = nullptr;
  return nullptr;
}

static bool isNestedInConcurrentRegion(Operation *operation,
                                       Operation *function) {
  for (Operation *ancestor = operation->getParentOp();
       ancestor && ancestor != function; ancestor = ancestor->getParentOp()) {
    StringRef dialect = ancestor->getName().getDialectNamespace();
    if (ancestor->hasTrait<OpTrait::HasParallelRegion>() ||
        dialect == "async" || dialect == "gpu" || dialect == "omp" ||
        dialect == "acc")
      return true;
  }
  return false;
}

static FailureOr<Value>
allocateAtFunctionEntry(Operation *anchor, ConversionPatternRewriter &rewriter,
                        const ABITypes &abi, Type elementType, uint64_t count,
                        unsigned alignment, bool zeroInitialize = false) {
  Operation *function = nullptr;
  Block *entry = getFunctionEntry(anchor, function);
  if (!entry)
    return anchor->emitOpError()
           << "runtime materializer must be nested in a function";
  if (isNestedInConcurrentRegion(anchor, function))
    return anchor->emitOpError()
           << "cannot lower a stack-backed runtime materializer nested in a "
              "concurrent region with function-entry storage";
  Value address;
  {
    OpBuilder::InsertionGuard guard(rewriter);
    rewriter.setInsertionPointToStart(entry);
    Value elementCount =
        llvmIntegerConstant(rewriter, anchor->getLoc(), abi.i64, count);
    address = LLVM::AllocaOp::create(rewriter, anchor->getLoc(), abi.pointer,
                                     elementType, elementCount, alignment);
  }
  if (zeroInitialize) {
    Value zero = LLVM::ZeroOp::create(rewriter, anchor->getLoc(), elementType);
    LLVM::StoreOp::create(rewriter, anchor->getLoc(), zero, address, alignment);
  }
  return address;
}

static Value getSpanField(OpBuilder &builder, Location location, Value span,
                          int64_t index) {
  return LLVM::ExtractValueOp::create(builder, location, span, {index});
}

static FailureOr<std::pair<Value, Value>>
materializeGlobalBytes(Operation *anchor, StringRef bytes,
                       ConversionPatternRewriter &rewriter,
                       const ABITypes &abi) {
  Location location = anchor->getLoc();
  Value size = llvmIntegerConstant(rewriter, location, abi.i64, bytes.size());
  if (bytes.empty())
    return std::pair<Value, Value>{
        LLVM::ZeroOp::create(rewriter, location, abi.pointer), size};

  ModuleOp module = anchor->getParentOfType<ModuleOp>();
  if (!module)
    return anchor->emitOpError() << "requires a containing module";
  unsigned counter = 0;
  SmallString<32> name = SymbolTable::generateSymbolName<32>(
      "__obelisk_rt_bytes",
      [&](StringRef candidate) {
        return SymbolTable::lookupSymbolIn(module, candidate) != nullptr;
      },
      counter);
  auto arrayType = LLVM::LLVMArrayType::get(abi.i8, bytes.size());
  LLVM::GlobalOp global;
  {
    OpBuilder::InsertionGuard guard(rewriter);
    rewriter.setInsertionPointToStart(module.getBody());
    global = LLVM::GlobalOp::create(
        rewriter, location, arrayType, true, name, LLVM::Linkage::Internal,
        false, false, false, rewriter.getStringAttr(bytes),
        rewriter.getI64IntegerAttr(abi.alignments.i8), 0, {}, {}, {}, {},
        LLVM::Visibility::Default, {});
  }
  Value address = LLVM::AddressOfOp::create(rewriter, location, abi.pointer,
                                            global.getSymName());
  return std::pair<Value, Value>{address, size};
}

enum class RuntimeMaterializer {
  BytesConstant,
  Scratch,
  BytesSize,
  PackedFromBytes,
  ArgumentEmpty,
  ArgumentPacked,
  ArgumentBytes,
  ArgumentArray,
  FormatEnvironment,
  DescriptorFromBits,
  DescriptorToBits,
  StatusIs,
  StatusCast,
};

class RuntimeMaterializerLowering final : public ConversionPattern {
public:
  RuntimeMaterializerLowering(const TypeConverter &converter, StringRef name,
                              RuntimeMaterializer materializer,
                              MLIRContext *context, const ABITypes &abi)
      : ConversionPattern(converter, name, 2, context),
        materializer(materializer), abi(abi) {}

  LogicalResult
  matchAndRewrite(Operation *operation, ArrayRef<Value> operands,
                  ConversionPatternRewriter &rewriter) const override {
    Location location = operation->getLoc();
    auto extract = [&](Value span, int64_t index) {
      return getSpanField(rewriter, location, span, index);
    };
    switch (materializer) {
    case RuntimeMaterializer::BytesConstant: {
      auto op = cast<runtime::RTBytesConstantOp>(operation);
      FailureOr<std::pair<Value, Value>> bytes =
          materializeGlobalBytes(operation, op.getValue(), rewriter, abi);
      if (failed(bytes))
        return failure();
      rewriter.replaceOp(operation, makeSpan(rewriter, location, abi,
                                             bytes->first, bytes->second));
      return success();
    }
    case RuntimeMaterializer::Scratch: {
      auto op = cast<runtime::RTScratchOp>(operation);
      uint64_t size = op.getSize();
      auto arrayType = LLVM::LLVMArrayType::get(abi.i8, size);
      FailureOr<Value> address = allocateAtFunctionEntry(
          operation, rewriter, abi, arrayType, 1, abi.alignments.i8, true);
      if (failed(address))
        return failure();
      Value count = llvmIntegerConstant(rewriter, location, abi.i64, size);
      rewriter.replaceOp(operation,
                         makeSpan(rewriter, location, abi, *address, count));
      return success();
    }
    case RuntimeMaterializer::BytesSize:
      rewriter.replaceOp(operation, extract(operands[0], 1));
      return success();
    case RuntimeMaterializer::PackedFromBytes: {
      auto op = cast<runtime::RTPackedFromBytesOp>(operation);
      auto resultType = cast<IntegerType>(operation->getResult(0).getType());
      unsigned resultWidth = resultType.getWidth();
      uint64_t byteCount = (static_cast<uint64_t>(resultWidth) + 7) / 8;
      if (byteCount == 0 || byteCount > std::numeric_limits<unsigned>::max())
        return operation->emitOpError("packed byte width is unsupported");
      unsigned storageWidth = static_cast<unsigned>(byteCount * 8);
      auto storageType = IntegerType::get(rewriter.getContext(), storageWidth);
      auto arrayType = LLVM::LLVMArrayType::get(abi.i8, byteCount);
      FailureOr<Value> scratch = allocateAtFunctionEntry(
          operation, rewriter, abi, arrayType, 1, abi.alignments.i8, true);
      if (failed(scratch))
        return failure();
      Value source = extract(operands[0], 0);
      Value sourceSize = extract(operands[0], 1);
      Value maxCount =
          llvmIntegerConstant(rewriter, location, abi.i64, byteCount);
      Value withinDestination = LLVM::ICmpOp::create(
          rewriter, location, LLVM::ICmpPredicate::ule, operands[1], maxCount);
      Value destinationCount = LLVM::SelectOp::create(
          rewriter, location, withinDestination, operands[1], maxCount);
      Value withinSource =
          LLVM::ICmpOp::create(rewriter, location, LLVM::ICmpPredicate::ule,
                               destinationCount, sourceSize);
      Value safeCount = LLVM::SelectOp::create(rewriter, location, withinSource,
                                               destinationCount, sourceSize);
      LLVM::MemcpyOp::create(rewriter, location, *scratch, source, safeCount,
                             false);

      Value assembled = LLVM::ZeroOp::create(rewriter, location, storageType);
      Value one = llvmIntegerConstant(rewriter, location, abi.i64, 1);
      Value eight = llvmIntegerConstant(rewriter, location, abi.i64, 8);
      for (uint64_t index = 0; index != byteCount; ++index) {
        Value address = LLVM::GEPOp::create(
            rewriter, location, abi.pointer, abi.i8, *scratch,
            ArrayRef<LLVM::GEPArg>{static_cast<int32_t>(index)});
        Value byte = LLVM::LoadOp::create(rewriter, location, abi.i8, address,
                                          abi.alignments.i8);
        Value indexValue =
            llvmIntegerConstant(rewriter, location, abi.i64, index);
        Value active =
            LLVM::ICmpOp::create(rewriter, location, LLVM::ICmpPredicate::ult,
                                 indexValue, safeCount);
        byte = LLVM::SelectOp::create(
            rewriter, location, active, byte,
            llvmIntegerConstant(rewriter, location, abi.i8, 0));
        Value wide =
            LLVM::ZExtOp::create(rewriter, location, storageType, byte);
        Value shift;
        if (op.getHighAlignment()) {
          shift = llvmIntegerConstant(rewriter, location, storageType,
                                      (byteCount - 1 - index) * 8);
        } else {
          Value last =
              LLVM::SubOp::create(rewriter, location, abi.i64, safeCount, one);
          Value distance = LLVM::SubOp::create(rewriter, location, abi.i64,
                                               last, indexValue);
          Value amount =
              LLVM::MulOp::create(rewriter, location, abi.i64, distance, eight);
          Value safeAmount = LLVM::SelectOp::create(
              rewriter, location, active, amount,
              llvmIntegerConstant(rewriter, location, abi.i64, 0));
          if (storageWidth < 64)
            shift = LLVM::TruncOp::create(rewriter, location, storageType,
                                          safeAmount);
          else if (storageWidth > 64)
            shift = LLVM::ZExtOp::create(rewriter, location, storageType,
                                         safeAmount);
          else
            shift = safeAmount;
        }
        Value placed =
            LLVM::ShlOp::create(rewriter, location, storageType, wide, shift);
        assembled = LLVM::OrOp::create(rewriter, location, storageType,
                                       assembled, placed);
      }
      Value result = assembled;
      if (resultWidth != storageWidth)
        result =
            LLVM::TruncOp::create(rewriter, location, resultType, assembled);
      rewriter.replaceOp(operation, result);
      return success();
    }
    case RuntimeMaterializer::ArgumentEmpty:
      rewriter.replaceOp(
          operation, LLVM::ZeroOp::create(rewriter, location, abi.argument));
      return success();
    case RuntimeMaterializer::ArgumentPacked: {
      auto op = cast<runtime::RTArgumentPackedOp>(operation);
      auto valueType = cast<IntegerType>(operands[0].getType());
      unsigned width = valueType.getWidth();
      uint64_t wordCount = (static_cast<uint64_t>(width) + 63) / 64;
      uint64_t paddedWidth64 = wordCount * 64;
      if (paddedWidth64 > std::numeric_limits<unsigned>::max())
        return operation->emitOpError("packed argument width is unsupported");
      auto paddedType = IntegerType::get(rewriter.getContext(),
                                         static_cast<unsigned>(paddedWidth64));
      unsigned alignment =
          abi.layout.getABIIntegerTypeAlignment(paddedType.getWidth()).value();
      auto storePlane = [&](Value plane) -> FailureOr<Value> {
        FailureOr<Value> address = allocateAtFunctionEntry(
            operation, rewriter, abi, paddedType, 1, alignment);
        if (failed(address))
          return failure();
        Value padded = plane;
        if (width != paddedType.getWidth())
          padded = LLVM::ZExtOp::create(rewriter, location, paddedType, plane);
        LLVM::StoreOp::create(rewriter, location, padded, *address, alignment);
        return *address;
      };
      FailureOr<Value> data = storePlane(operands[0]);
      if (failed(data))
        return failure();
      Value unknown = LLVM::ZeroOp::create(rewriter, location, abi.pointer);
      if (operands.size() == 2) {
        FailureOr<Value> stored = storePlane(operands[1]);
        if (failed(stored))
          return failure();
        unknown = *stored;
      }
      Value argument = LLVM::ZeroOp::create(rewriter, location, abi.argument);
      argument = insertStructValue(
          rewriter, location, argument,
          llvmIntegerConstant(rewriter, location, abi.i32, 1), 0);
      argument =
          insertStructValue(rewriter, location, argument,
                            llvmIntegerConstant(rewriter, location, abi.i32,
                                                op.getIsSigned() ? 1 : 0),
                            1);
      argument = insertStructValue(
          rewriter, location, argument,
          llvmIntegerConstant(rewriter, location, abi.i64, width), 2);
      argument = insertStructValue(rewriter, location, argument, *data, 3);
      argument = insertStructValue(rewriter, location, argument, unknown, 4);
      rewriter.replaceOp(operation, argument);
      return success();
    }
    case RuntimeMaterializer::ArgumentBytes: {
      auto op = cast<runtime::RTArgumentBytesOp>(operation);
      Value argument = LLVM::ZeroOp::create(rewriter, location, abi.argument);
      argument = insertStructValue(
          rewriter, location, argument,
          llvmIntegerConstant(rewriter, location, abi.i32, 2), 0);
      argument =
          insertStructValue(rewriter, location, argument,
                            llvmIntegerConstant(rewriter, location, abi.i32,
                                                op.getIsFormatString() ? 2 : 0),
                            1);
      argument = insertStructValue(rewriter, location, argument,
                                   extract(operands[0], 1), 2);
      argument = insertStructValue(rewriter, location, argument,
                                   extract(operands[0], 0), 3);
      rewriter.replaceOp(operation, argument);
      return success();
    }
    case RuntimeMaterializer::ArgumentArray: {
      Value count =
          llvmIntegerConstant(rewriter, location, abi.i64, operands.size());
      if (operands.empty()) {
        Value null = LLVM::ZeroOp::create(rewriter, location, abi.pointer);
        rewriter.replaceOp(operation,
                           makeSpan(rewriter, location, abi, null, count));
        return success();
      }
      FailureOr<Value> array =
          allocateAtFunctionEntry(operation, rewriter, abi, abi.argument,
                                  operands.size(), abi.alignments.argument);
      if (failed(array))
        return failure();
      for (auto [index, argument] : llvm::enumerate(operands)) {
        Value address = LLVM::GEPOp::create(
            rewriter, location, abi.pointer, abi.argument, *array,
            ArrayRef<LLVM::GEPArg>{static_cast<int32_t>(index)});
        LLVM::StoreOp::create(rewriter, location, argument, address,
                              abi.alignments.argument);
      }
      rewriter.replaceOp(operation,
                         makeSpan(rewriter, location, abi, *array, count));
      return success();
    }
    case RuntimeMaterializer::FormatEnvironment: {
      auto op = cast<runtime::RTFormatEnvironmentOp>(operation);
      FailureOr<std::pair<Value, Value>> scope =
          materializeGlobalBytes(operation, op.getScope(), rewriter, abi);
      FailureOr<std::pair<Value, Value>> libraryCell =
          materializeGlobalBytes(operation, op.getLibraryCell(), rewriter, abi);
      FailureOr<std::pair<Value, Value>> suffix =
          materializeGlobalBytes(operation, op.getTimeSuffix(), rewriter, abi);
      if (failed(scope) || failed(libraryCell) || failed(suffix))
        return failure();
      Value environment =
          LLVM::ZeroOp::create(rewriter, location, abi.formatEnvironment);
      environment =
          insertStructValue(rewriter, location, environment, scope->first, 0);
      environment =
          insertStructValue(rewriter, location, environment, scope->second, 1);
      environment = insertStructValue(rewriter, location, environment,
                                      libraryCell->first, 2);
      environment = insertStructValue(rewriter, location, environment,
                                      libraryCell->second, 3);
      environment = insertStructValue(
          rewriter, location, environment,
          llvmIntegerConstant(rewriter, location, abi.i32, op.getTimeWidth()),
          4);
      environment =
          insertStructValue(rewriter, location, environment, suffix->first, 6);
      environment =
          insertStructValue(rewriter, location, environment, suffix->second, 7);
      environment = insertStructValue(
          rewriter, location, environment,
          llvmIntegerConstant(rewriter, location, abi.i64,
                              op.getTimeMultiplier()),
          8);
      FailureOr<Value> address = allocateAtFunctionEntry(
          operation, rewriter, abi, abi.formatEnvironment, 1,
          abi.alignments.environment);
      if (failed(address))
        return failure();
      LLVM::StoreOp::create(rewriter, location, environment, *address,
                            abi.alignments.environment);
      rewriter.replaceOp(operation, *address);
      return success();
    }
    case RuntimeMaterializer::DescriptorFromBits:
    case RuntimeMaterializer::DescriptorToBits:
    case RuntimeMaterializer::StatusCast:
      rewriter.replaceOp(operation, operands[0]);
      return success();
    case RuntimeMaterializer::StatusIs: {
      auto op = cast<runtime::RTStatusIsOp>(operation);
      Value expected =
          llvmIntegerConstant(rewriter, location, abi.i32, op.getValue());
      rewriter.replaceOp(operation,
                         LLVM::ICmpOp::create(rewriter, location,
                                              LLVM::ICmpPredicate::eq,
                                              operands[0], expected));
      return success();
    }
    }
    llvm_unreachable("all runtime materializers are handled");
  }

private:
  RuntimeMaterializer materializer;
  ABITypes abi;
};

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
    Operation *function = nullptr;
    Block *entry = getFunctionEntry(operation, function);
    if (!entry)
      return operation->emitOpError()
             << "must be nested in a function for LLVM lowering";
    if (isNestedInConcurrentRegion(operation, function))
      return operation->emitOpError()
             << "cannot lower a runtime call nested in a concurrent region "
                "with function-entry ABI scratch storage";
    FailureOr<LLVM::LLVMFuncOp> declaration =
        getOrCreateDeclaration(operation, call, rewriter, abi);
    if (failed(declaration))
      return failure();
    Location location = operation->getLoc();

    auto allocate = [&](Type elementType, unsigned alignment) -> Value {
      Value address;
      {
        OpBuilder::InsertionGuard guard(rewriter);
        rewriter.setInsertionPointToStart(entry);
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
      return replaceStatusAndLoad(
          {operands[0], operands[1], operands[2], output}, output, abi.span,
          abi.alignments.span);
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
      return replaceStatus(operands);
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
    case runtime::RuntimeCall::ProcessInstanceCreate: {
      Value output = allocate(abi.pointer, abi.alignments.pointer);
      return replaceStatusAndLoad({operands[0], output}, output, abi.pointer,
                                  abi.alignments.pointer);
    }
    case runtime::RuntimeCall::ProcessInstanceFrame: {
      Value data = allocate(abi.pointer, abi.alignments.pointer);
      Value size = allocate(abi.i64, abi.alignments.i64);
      Value status = callStatus({operands[0], data, size});
      Value result = LLVM::ZeroOp::create(rewriter, location, abi.span);
      result = LLVM::InsertValueOp::create(
          rewriter, location, result,
          load(abi.pointer, data, abi.alignments.pointer),
          ArrayRef<int64_t>{0});
      result = LLVM::InsertValueOp::create(
          rewriter, location, result, load(abi.i64, size, abi.alignments.i64),
          ArrayRef<int64_t>{1});
      rewriter.replaceOp(operation, ValueRange{status, result});
      return success();
    }
    case runtime::RuntimeCall::ProcessInstanceExecute: {
      Value output = allocate(abi.action, abi.alignments.action);
      return replaceStatusAndLoad(
          {operands[0], operands[1], operands[2], output}, output, abi.action,
          abi.alignments.action);
    }
    case runtime::RuntimeCall::ProcessInstanceDestroy:
      return replaceStatus(operands);
    }
    llvm_unreachable("all runtime calls are handled");
  }

private:
  runtime::RuntimeCall call;
  ABITypes abi;
};

void populateRuntimePatterns(const TypeConverter &converter,
                             RewritePatternSet &patterns, const ABITypes &abi) {
  MLIRContext *context = patterns.getContext();
#define OBELISK_RUNTIME_MATERIALIZER(Op, Kind)                                 \
  patterns.add<RuntimeMaterializerLowering>(                                   \
      converter, runtime::Op::getOperationName(), RuntimeMaterializer::Kind,   \
      context, abi)
  OBELISK_RUNTIME_MATERIALIZER(RTBytesConstantOp, BytesConstant);
  OBELISK_RUNTIME_MATERIALIZER(RTScratchOp, Scratch);
  OBELISK_RUNTIME_MATERIALIZER(RTBytesSizeOp, BytesSize);
  OBELISK_RUNTIME_MATERIALIZER(RTPackedFromBytesOp, PackedFromBytes);
  OBELISK_RUNTIME_MATERIALIZER(RTArgumentEmptyOp, ArgumentEmpty);
  OBELISK_RUNTIME_MATERIALIZER(RTArgumentPackedOp, ArgumentPacked);
  OBELISK_RUNTIME_MATERIALIZER(RTArgumentBytesOp, ArgumentBytes);
  OBELISK_RUNTIME_MATERIALIZER(RTArgumentArrayOp, ArgumentArray);
  OBELISK_RUNTIME_MATERIALIZER(RTFormatEnvironmentOp, FormatEnvironment);
  OBELISK_RUNTIME_MATERIALIZER(RTFileDescriptorFromBitsOp, DescriptorFromBits);
  OBELISK_RUNTIME_MATERIALIZER(RTFileDescriptorToBitsOp, DescriptorToBits);
  OBELISK_RUNTIME_MATERIALIZER(RTStatusIsOp, StatusIs);
  OBELISK_RUNTIME_MATERIALIZER(RTStatusFromBitsOp, StatusCast);
  OBELISK_RUNTIME_MATERIALIZER(RTStatusToBitsOp, StatusCast);
#undef OBELISK_RUNTIME_MATERIALIZER
#define OBELISK_RUNTIME_CALL(Name, Op, Symbol, Signature)                      \
  patterns.add<RuntimeCallLowering>(converter,                                 \
                                    runtime::Op::getOperationName(),           \
                                    runtime::RuntimeCall::Name, context, abi);
#include "obelisk/Dialect/Runtime/RuntimeABI.def"
#undef OBELISK_RUNTIME_CALL
}

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
