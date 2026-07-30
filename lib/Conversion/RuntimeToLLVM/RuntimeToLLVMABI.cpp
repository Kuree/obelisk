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
      failed(checkStruct("observer capture ABI", {i32, i32}, {0, 4}, 8, 4)) ||
      failed(checkStruct("observer descriptor",
                         {i64, pointer, i32, i32, i32, i32, pointer, i64},
                         {0, 8, 16, 20, 24, 28, 32, 40}, 48, 8)) ||
      failed(
          checkStruct("execution descriptor",
                      {i32, i32, i32, i32, pointer, i64, pointer, i64, i64, i64,
                       pointer, i64, i32, i32, pointer, i64, pointer, i64},
                      {0, 4, 8, 12, 16, 24, 32, 40, 48, 56, 64, 72, 80, 84, 88,
                       96, 104, 112},
                      120, 8)) ||
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

ABITypes::ABITypes(MLIRContext *context, ABIAlignments alignments,
                   const llvm::DataLayout &layout)
    : pointer(LLVM::LLVMPointerType::get(context)),
      voidType(LLVM::LLVMVoidType::get(context)),
      i1(IntegerType::get(context, 1)), i8(IntegerType::get(context, 8)),
      i32(IntegerType::get(context, 32)), i64(IntegerType::get(context, 64)),
      span(LLVM::LLVMStructType::getLiteral(context, {pointer, i64})),
      argument(LLVM::LLVMStructType::getLiteral(
          context, {i32, i32, i64, pointer, pointer})),
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
