//===- RuntimeToLLVMABI.cpp - Runtime LLVM ABI model ---------------------===//

#include "RuntimeToLLVMABI.h"

#include "obelisk/Dialect/Runtime/RuntimeOps.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"

#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"

using namespace mlir;

namespace obelisk::runtimelowering {

FailureOr<ABIAlignments> validateTargetABI(ModuleOp module,
                                           const llvm::DataLayout &layout) {
  unsigned pointerBits = layout.getPointerSizeInBits();
  if (!layout.isLittleEndian() ||
      (pointerBits != 32 && pointerBits != 64)) {
    module.emitError() << "runtime lowering requires a little-endian target "
                          "with 32-bit or 64-bit pointers";
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
  auto *action = llvm::StructType::get(context, {i32, i32, i32, i32, i64, i64});
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
  // The runtime-facing records are ordinary C structs made from these scalar
  // types. Their padding and offsets are deliberately obtained from this
  // DataLayout wherever lowering needs them; duplicating complete 32-bit and
  // 64-bit offset tables here made the target layout a second, fallible source
  // of truth. Validate only the scalar C ABI contract that those derived
  // layouts rely on.
  uint64_t pointerBytes = pointerBits / 8;
  if (failed(checkType("pointer", pointer, pointerBytes, pointerBytes)) ||
      failed(checkType("i8", i8, 1, 1)) ||
      failed(checkType("i16", i16, 2, 2)) ||
      failed(checkType("i32", i32, 4, 4)) ||
      failed(checkType("i64", i64, 8, 8)))
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

ABITypes::ABITypes(MLIRContext *context, ABIAlignments alignments,
                   const llvm::DataLayout &layout)
    : pointer(LLVM::LLVMPointerType::get(context)),
      voidType(LLVM::LLVMVoidType::get(context)),
      i1(IntegerType::get(context, 1)), i8(IntegerType::get(context, 8)),
      i32(IntegerType::get(context, 32)), i64(IntegerType::get(context, 64)),
      span(LLVM::LLVMStructType::getLiteral(context, {pointer, i64})),
      argument(LLVM::LLVMStructType::getLiteral(
          context, {i32, i32, i64, pointer, pointer})),
      enumArgument(LLVM::LLVMStructType::getLiteral(
          context, {i64, i32, i32, pointer, pointer, i64})),
      rawAggregateArgument(
          LLVM::LLVMStructType::getLiteral(context, {i64, i64, i64})),
      formatEnvironment(LLVM::LLVMStructType::getLiteral(
          context, {pointer, i64, pointer, i64, i32, i32, pointer, i64, i64})),
      handle(LLVM::LLVMStructType::getLiteral(context, {i32, i32, i64})),
      action(LLVM::LLVMStructType::getLiteral(context,
                                              {i32, i32, i32, i32, i64, i64})),
      bytecodeEntry(LLVM::LLVMStructType::getLiteral(context, {i32, i32})),
      bytecodeValidation(LLVM::LLVMStructType::getLiteral(context, {i32, i32})),
      bytecodeOperand(LLVM::LLVMStructType::getLiteral(
          context, {i8, i8, i8, i8, i32, i64, i64, i64})),
      bytecodeServiceSite(LLVM::LLVMStructType::getLiteral(
          context, {i32, i32, IntegerType::get(context, 16),
                    IntegerType::get(context, 16), i32})),
      alignments(alignments), layout(layout) {}

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
  if (containsRuntimeType(type))
    return std::nullopt;
  return type;
}

} // namespace obelisk::runtimelowering
