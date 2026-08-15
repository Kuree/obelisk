//===- RuntimeToLLVMPatterns.cpp - Typed runtime rewrite patterns --------===//

#include "RuntimeToLLVMPatterns.h"

#include "obelisk/Dialect/Runtime/RuntimeABI.h"
#include "obelisk/Dialect/Runtime/RuntimeOps.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/LoopLikeInterface.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DataLayout.h"

#include <limits>

using namespace mlir;

namespace obelisk::runtimelowering {

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
  case runtime::RuntimeSignature::Finish:
    arguments = {abi.pointer, abi.i32};
    break;
  case runtime::RuntimeSignature::Error:
    arguments = {abi.pointer};
    break;
  case runtime::RuntimeSignature::TerminationRequested:
    arguments = {abi.pointer};
    break;
  case runtime::RuntimeSignature::SchedulerTime:
    result = abi.i64;
    arguments = {abi.pointer};
    break;
  case runtime::RuntimeSignature::Format:
    arguments = {abi.pointer, abi.pointer, abi.i64,    abi.pointer,
                 abi.i64,     abi.pointer, abi.pointer};
    break;
  case runtime::RuntimeSignature::StringOutputFormat:
    arguments = {abi.pointer, abi.i32, abi.pointer,
                 abi.i64,     abi.pointer, abi.pointer};
    break;
  case runtime::RuntimeSignature::Display:
    arguments = {abi.pointer, abi.i32, abi.i32,    abi.i32,
                 abi.pointer, abi.i64, abi.pointer};
    break;
  case runtime::RuntimeSignature::TimeFormat:
    arguments = {abi.pointer, abi.i32, abi.i32, abi.pointer, abi.i64, abi.i32};
    break;
  case runtime::RuntimeSignature::DumpOpen:
    arguments = {abi.pointer, abi.pointer, abi.i64};
    break;
  case runtime::RuntimeSignature::DumpVars:
    arguments = {abi.pointer, abi.i64, abi.pointer, abi.i64};
    break;
  case runtime::RuntimeSignature::DumpContext:
    arguments = {abi.pointer};
    break;
  case runtime::RuntimeSignature::DumpU32:
    arguments = {abi.pointer, abi.i32};
    break;
  case runtime::RuntimeSignature::DumpU64:
    arguments = {abi.pointer, abi.i64};
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
  case runtime::RuntimeSignature::FileReadMemToken:
    arguments = {abi.pointer, abi.i32,     abi.i32, abi.i64,     abi.pointer,
                 abi.i64,     abi.pointer, abi.i64, abi.pointer, abi.pointer};
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
  ArgumentReal,
  ArgumentBytes,
  ArgumentManagedString,
  ArgumentManagedContainer,
  ArgumentManagedObject,
  ArgumentVirtualInterface,
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
    case RuntimeMaterializer::ArgumentReal: {
      FailureOr<Value> data =
          allocateAtFunctionEntry(operation, rewriter, abi,
                                  operands[0].getType(), 1, abi.alignments.i64);
      if (failed(data))
        return failure();
      LLVM::StoreOp::create(rewriter, location, operands[0], *data,
                            abi.alignments.i64);
      Value argument = LLVM::ZeroOp::create(rewriter, location, abi.argument);
      argument = insertStructValue(
          rewriter, location, argument,
          llvmIntegerConstant(rewriter, location, abi.i32, 3), 0);
      argument = insertStructValue(rewriter, location, argument, *data, 3);
      rewriter.replaceOp(operation, argument);
      return success();
    }
    case RuntimeMaterializer::ArgumentBytes: {
      auto op = cast<runtime::RTArgumentBytesOp>(operation);
      Value argument = LLVM::ZeroOp::create(rewriter, location, abi.argument);
      argument = insertStructValue(
          rewriter, location, argument,
          llvmIntegerConstant(rewriter, location, abi.i32, 2), 0);
      argument = insertStructValue(
          rewriter, location, argument,
          llvmIntegerConstant(rewriter, location, abi.i32,
                              (op.getIsFormatString() ? 2 : 0) |
                                  (op.getDesignatedFormat() ? 4 : 0)),
          1);
      argument = insertStructValue(rewriter, location, argument,
                                   extract(operands[0], 1), 2);
      argument = insertStructValue(rewriter, location, argument,
                                   extract(operands[0], 0), 3);
      rewriter.replaceOp(operation, argument);
      return success();
    }
    case RuntimeMaterializer::ArgumentManagedString: {
      auto op = cast<runtime::RTArgumentManagedStringOp>(operation);
      FailureOr<Value> data =
          allocateAtFunctionEntry(operation, rewriter, abi,
                                  operands[0].getType(), 1, abi.alignments.i64);
      if (failed(data))
        return failure();
      LLVM::StoreOp::create(rewriter, location, operands[0], *data,
                            abi.alignments.i64);
      Value argument = LLVM::ZeroOp::create(rewriter, location, abi.argument);
      argument = insertStructValue(
          rewriter, location, argument,
          llvmIntegerConstant(rewriter, location, abi.i32,
                              OBELISK_RT_ARG_MANAGED_STRING),
          0);
      argument = insertStructValue(
          rewriter, location, argument,
          llvmIntegerConstant(rewriter, location, abi.i32,
                              (op.getIsFormatString() ? 2 : 0) |
                                  (op.getDesignatedFormat() ? 4 : 0)),
          1);
      argument = insertStructValue(rewriter, location, argument, *data, 3);
      rewriter.replaceOp(operation, argument);
      return success();
    }
    case RuntimeMaterializer::ArgumentManagedContainer: {
      FailureOr<Value> data =
          allocateAtFunctionEntry(operation, rewriter, abi,
                                  operands[0].getType(), 1, abi.alignments.i64);
      if (failed(data))
        return failure();
      LLVM::StoreOp::create(rewriter, location, operands[0], *data,
                            abi.alignments.i64);
      Value argument = LLVM::ZeroOp::create(rewriter, location, abi.argument);
      argument = insertStructValue(
          rewriter, location, argument,
          llvmIntegerConstant(rewriter, location, abi.i32,
                              OBELISK_RT_ARG_MANAGED_CONTAINER),
          0);
      argument = insertStructValue(rewriter, location, argument, *data, 3);
      rewriter.replaceOp(operation, argument);
      return success();
    }
    case RuntimeMaterializer::ArgumentManagedObject: {
      FailureOr<Value> data =
          allocateAtFunctionEntry(operation, rewriter, abi,
                                  operands[0].getType(), 1, abi.alignments.i64);
      if (failed(data))
        return failure();
      LLVM::StoreOp::create(rewriter, location, operands[0], *data,
                            abi.alignments.i64);
      Value argument = LLVM::ZeroOp::create(rewriter, location, abi.argument);
      argument =
          insertStructValue(rewriter, location, argument,
                            llvmIntegerConstant(rewriter, location, abi.i32,
                                                OBELISK_RT_ARG_MANAGED_OBJECT),
                            0);
      argument = insertStructValue(rewriter, location, argument, *data, 3);
      rewriter.replaceOp(operation, argument);
      return success();
    }
    case RuntimeMaterializer::ArgumentVirtualInterface: {
      FailureOr<Value> data =
          allocateAtFunctionEntry(operation, rewriter, abi,
                                  operands[0].getType(), 1, abi.alignments.i64);
      if (failed(data))
        return failure();
      LLVM::StoreOp::create(rewriter, location, operands[0], *data,
                            abi.alignments.i64);
      Value argument = LLVM::ZeroOp::create(rewriter, location, abi.argument);
      argument = insertStructValue(
          rewriter, location, argument,
          llvmIntegerConstant(rewriter, location, abi.i32,
                              OBELISK_RT_ARG_VIRTUAL_INTERFACE),
          0);
      argument = insertStructValue(rewriter, location, argument, *data, 3);
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
      environment = insertStructValue(
          rewriter, location, environment,
          llvmIntegerConstant(rewriter, location, abi.i32,
                              op.getTimePrecision()),
          5);
      environment =
          insertStructValue(rewriter, location, environment, suffix->first, 6);
      environment =
          insertStructValue(rewriter, location, environment, suffix->second, 7);
      environment =
          insertStructValue(rewriter, location, environment,
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
    case runtime::RuntimeCall::Finish:
    case runtime::RuntimeCall::Stop:
    case runtime::RuntimeCall::Fatal:
    case runtime::RuntimeCall::Error:
      return replaceStatus(operands);
    case runtime::RuntimeCall::TerminationRequested: {
      Value requested = emitCall(operands).getResult();
      rewriter.replaceOpWithNewOp<LLVM::TruncOp>(
          operation, rewriter.getI1Type(), requested);
      return success();
    }
    case runtime::RuntimeCall::SchedulerTime:
      rewriter.replaceOp(operation, emitCall(operands).getResult());
      return success();
    case runtime::RuntimeCall::Format: {
      auto [formatData, formatSize] = span(operands[1]);
      auto [argumentData, argumentCount] = span(operands[2]);
      Value output = allocate(abi.span, abi.alignments.span);
      return replaceStatusAndLoad({operands[0], formatData, formatSize,
                                   argumentData, argumentCount, operands[3],
                                   output},
                                  output, abi.span, abi.alignments.span);
    }
    case runtime::RuntimeCall::StringOutputFormat: {
      auto format = cast<runtime::RTStringOutputFormatOp>(operation);
      Value radix = LLVM::ConstantOp::create(
          rewriter, location, abi.i32,
          static_cast<int64_t>(format.getDefaultRadix()));
      auto [itemData, itemCount] = span(operands[1]);
      Value output = allocate(abi.i64, abi.alignments.i64);
      return replaceStatusAndLoad(
          {operands[0], radix, itemData, itemCount, operands[2], output},
          output, abi.i64, abi.alignments.i64);
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
    case runtime::RuntimeCall::TimeFormat: {
      auto [suffixData, suffixSize] = span(operands[3]);
      return replaceStatus({operands[0], operands[1], operands[2], suffixData,
                            suffixSize, operands[4]});
    }
    case runtime::RuntimeCall::DumpOpen: {
      auto [pathData, pathSize] = span(operands[1]);
      return replaceStatus({operands[0], pathData, pathSize});
    }
    case runtime::RuntimeCall::DumpVars: {
      auto [scopeData, scopeSize] = span(operands[2]);
      return replaceStatus({operands[0], operands[1], scopeData, scopeSize});
    }
    case runtime::RuntimeCall::DumpTimescale:
    case runtime::RuntimeCall::DumpAll:
    case runtime::RuntimeCall::DumpControl:
    case runtime::RuntimeCall::DumpLimit:
    case runtime::RuntimeCall::DumpFlush:
      return replaceStatus(operands);
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
    case runtime::RuntimeCall::FileReadMemToken: {
      auto [value, valueSize] = span(operands[4]);
      auto [unknown, unknownSize] = span(operands[5]);
      Value kind = allocate(abi.i32, abi.alignments.i32);
      Value address = allocate(abi.i64, abi.alignments.i64);
      Value status =
          callStatus({operands[0], operands[1], operands[2], operands[3], value,
                      valueSize, unknown, unknownSize, kind, address});
      rewriter.replaceOp(
          operation, ValueRange{status, load(abi.i32, kind, abi.alignments.i32),
                                load(abi.i64, address, abi.alignments.i64)});
      return success();
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
  OBELISK_RUNTIME_MATERIALIZER(RTArgumentRealOp, ArgumentReal);
  OBELISK_RUNTIME_MATERIALIZER(RTArgumentBytesOp, ArgumentBytes);
  OBELISK_RUNTIME_MATERIALIZER(RTArgumentManagedStringOp,
                               ArgumentManagedString);
  OBELISK_RUNTIME_MATERIALIZER(RTArgumentManagedContainerOp,
                               ArgumentManagedContainer);
  OBELISK_RUNTIME_MATERIALIZER(RTArgumentManagedObjectOp,
                               ArgumentManagedObject);
  OBELISK_RUNTIME_MATERIALIZER(RTArgumentVirtualInterfaceOp,
                               ArgumentVirtualInterface);
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

} // namespace obelisk::runtimelowering
