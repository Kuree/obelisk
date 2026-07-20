//===- MooreToObelisk.cpp - Moore to Obelisk conversion ------------------===//

#include "obelisk/Conversion/MooreToObelisk.h"

#include "obelisk/Dialect/Sim/ObeliskOps.h"

#include "circt/Dialect/HW/HWTypes.h"
#include "circt/Dialect/Moore/MooreAttributes.h"
#include "circt/Dialect/Moore/MooreDialect.h"
#include "circt/Dialect/Moore/MooreOps.h"
#include "circt/Dialect/Moore/MooreTypes.h"
#include "circt/Dialect/Sim/SimDialect.h"
#include "circt/Dialect/Sim/SimOps.h"
#include "circt/Dialect/Sim/SimTypes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/TypeSwitch.h"

#include <type_traits>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_CONVERTMOORETOOBELISKPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

enum class LValueKind { Variable, Net, Mixed };

/// Recover the storage category erased by Moore's common RefType. Net-derived
/// selections remain nets so reads and continuous assignments retain resolved,
/// multi-driver behavior.
static LValueKind classifyLValue(Value value) {
  if (value.getDefiningOp<circt::moore::NetOp>())
    return LValueKind::Net;
  if (auto extract = value.getDefiningOp<circt::moore::ExtractRefOp>())
    return classifyLValue(extract.getInput());
  if (auto extract = value.getDefiningOp<circt::moore::DynExtractRefOp>())
    return classifyLValue(extract.getInput());
  if (auto extract = value.getDefiningOp<circt::moore::StructExtractRefOp>())
    return classifyLValue(extract.getInput());
  if (auto extract = value.getDefiningOp<circt::moore::UnionExtractRefOp>())
    return classifyLValue(extract.getInput());
  if (auto concat = value.getDefiningOp<circt::moore::ConcatRefOp>()) {
    std::optional<LValueKind> kind;
    for (Value input : concat.getValues()) {
      LValueKind inputKind = classifyLValue(input);
      if (!kind)
        kind = inputKind;
      else if (*kind != inputKind)
        return LValueKind::Mixed;
    }
    return kind.value_or(LValueKind::Variable);
  }
  return LValueKind::Variable;
}

/// Recursive type conversion used by every operation pattern.  In particular,
/// four-state Moore integers become `!obelisk.logic`, never builtin integers.
class MooreTypeConverter : public TypeConverter {
public:
  MooreTypeConverter() {
    addConversion(
        [this](Type type) -> std::optional<Type> { return lowerType(type); });
    addConversion([this](Value value) -> std::optional<Type> {
      if (auto reference = dyn_cast<circt::moore::RefType>(value.getType());
          reference && classifyLValue(value) == LValueKind::Net) {
        return ir::NetType::get(value.getContext(),
                                lowerType(reference.getNestedType()));
      }
      return std::nullopt;
    });
  }

  Type lowerType(Type type) const {
    MLIRContext *context = type.getContext();

    if (auto intType = dyn_cast<circt::moore::IntType>(type)) {
      if (intType.getDomain() == circt::moore::Domain::FourValued)
        return ir::LogicType::get(context, intType.getWidth());
      return IntegerType::get(context, intType.getWidth());
    }
    if (isa<circt::moore::VoidType>(type))
      return ir::VoidType::get(context);
    if (isa<circt::moore::StringType>(type))
      return circt::sim::DynamicStringType::get(context);
    if (isa<circt::moore::ChandleType>(type))
      return ir::ChandleType::get(context);
    if (isa<circt::moore::EventType>(type))
      return ir::EventType::get(context);
    if (isa<circt::moore::NullType>(type))
      return ir::NullType::get(context);
    if (auto classType = dyn_cast<circt::moore::ClassHandleType>(type))
      return ir::ClassHandleType::get(context, classType.getClassSym());
    if (isa<circt::moore::TimeType>(type))
      return ir::TimeType::get(context);
    if (auto realType = dyn_cast<circt::moore::RealType>(type)) {
      if (realType.getWidth() == circt::moore::RealWidth::f32)
        return Float32Type::get(context);
      return Float64Type::get(context);
    }
    if (isa<circt::moore::FormatStringType>(type))
      return circt::sim::FormatStringType::get(context);
    if (isa<circt::moore::ScanStringType>(type))
      return ir::ScanStateType::get(context);

    if (auto array = dyn_cast<circt::moore::ArrayType>(type))
      return ir::PackedArrayType::get(
          context, lowerType(array.getElementType()), array.getSize());
    if (auto array = dyn_cast<circt::moore::UnpackedArrayType>(type))
      return ir::UnpackedArrayType::get(
          context, lowerType(array.getElementType()), array.getSize());
    if (auto array = dyn_cast<circt::moore::OpenArrayType>(type))
      return ir::OpenArrayType::get(context, lowerType(array.getElementType()),
                                    true);
    if (auto array = dyn_cast<circt::moore::OpenUnpackedArrayType>(type))
      return ir::OpenArrayType::get(context, lowerType(array.getElementType()),
                                    false);
    if (auto array = dyn_cast<circt::moore::AssocArrayType>(type))
      return ir::AssocArrayType::get(context, lowerType(array.getIndexType()),
                                     lowerType(array.getElementType()));
    if (auto queue = dyn_cast<circt::moore::QueueType>(type))
      return circt::sim::QueueType::get(
          context, lowerType(queue.getElementType()), queue.getBound());
    if (auto ref = dyn_cast<circt::moore::RefType>(type))
      return ir::RefType::get(context, lowerType(ref.getNestedType()));

    auto lowerStruct = [&](auto structType) -> Type {
      SmallVector<circt::hw::StructType::FieldInfo> fields;
      for (auto member : structType.getMembers())
        fields.push_back({member.name, lowerType(member.type)});
      return circt::hw::StructType::get(context, fields);
    };
    if (auto structType = dyn_cast<circt::moore::StructType>(type))
      return ir::PackedStructType::get(context, lowerStruct(structType));
    if (auto structType = dyn_cast<circt::moore::UnpackedStructType>(type))
      return ir::UnpackedStructType::get(context, lowerStruct(structType));

    auto lowerUnion = [&](auto unionType) -> Type {
      SmallVector<circt::hw::UnionType::FieldInfo> fields;
      for (auto member : unionType.getMembers())
        fields.push_back({member.name, lowerType(member.type), 0});
      return circt::hw::UnionType::get(context, fields);
    };
    if (auto unionType = dyn_cast<circt::moore::UnionType>(type))
      return ir::PackedUnionType::get(context, lowerUnion(unionType));
    if (auto unionType = dyn_cast<circt::moore::UnpackedUnionType>(type))
      return ir::UnpackedUnionType::get(context, lowerUnion(unionType));

    if (auto functionType = dyn_cast<FunctionType>(type)) {
      SmallVector<Type> inputs, results;
      llvm::transform(functionType.getInputs(), std::back_inserter(inputs),
                      [&](Type input) { return lowerType(input); });
      llvm::transform(functionType.getResults(), std::back_inserter(results),
                      [&](Type result) { return lowerType(result); });
      return FunctionType::get(context, inputs, results);
    }
    if (auto moduleType = dyn_cast<circt::hw::ModuleType>(type)) {
      SmallVector<circt::hw::ModulePort> ports;
      for (auto port : moduleType.getPorts())
        ports.push_back({port.name, lowerType(port.type), port.dir});
      return circt::hw::ModuleType::get(context, ports);
    }
    if (auto array = dyn_cast<circt::hw::ArrayType>(type))
      return circt::hw::ArrayType::get(lowerType(array.getElementType()),
                                       array.getNumElements());
    if (auto structType = dyn_cast<circt::hw::StructType>(type)) {
      SmallVector<circt::hw::StructType::FieldInfo> fields;
      for (auto field : structType.getElements())
        fields.push_back({field.name, lowerType(field.type)});
      return circt::hw::StructType::get(context, fields);
    }
    if (auto unionType = dyn_cast<circt::hw::UnionType>(type)) {
      SmallVector<circt::hw::UnionType::FieldInfo> fields;
      for (auto field : unionType.getElements())
        fields.push_back({field.name, lowerType(field.type), field.offset});
      return circt::hw::UnionType::get(context, fields);
    }
    if (auto queue = dyn_cast<circt::sim::QueueType>(type))
      return circt::sim::QueueType::get(
          context, lowerType(queue.getElementType()), queue.getBound());
    if (auto array = dyn_cast<ir::PackedArrayType>(type))
      return ir::PackedArrayType::get(
          context, lowerType(array.getElementType()), array.getSize());
    if (auto array = dyn_cast<ir::UnpackedArrayType>(type))
      return ir::UnpackedArrayType::get(
          context, lowerType(array.getElementType()), array.getSize());
    if (auto structType = dyn_cast<ir::PackedStructType>(type))
      return ir::PackedStructType::get(context,
                                       lowerType(structType.getFields()));
    if (auto structType = dyn_cast<ir::UnpackedStructType>(type))
      return ir::UnpackedStructType::get(context,
                                         lowerType(structType.getFields()));
    if (auto unionType = dyn_cast<ir::PackedUnionType>(type))
      return ir::PackedUnionType::get(context,
                                      lowerType(unionType.getFields()));
    if (auto unionType = dyn_cast<ir::UnpackedUnionType>(type))
      return ir::UnpackedUnionType::get(context,
                                        lowerType(unionType.getFields()));
    if (auto ref = dyn_cast<ir::RefType>(type))
      return ir::RefType::get(context, lowerType(ref.getElementType()));
    if (auto net = dyn_cast<ir::NetType>(type))
      return ir::NetType::get(context, lowerType(net.getElementType()));
    if (auto open = dyn_cast<ir::OpenArrayType>(type))
      return ir::OpenArrayType::get(context, lowerType(open.getElementType()),
                                    open.getIsPacked());

    return type;
  }

  Attribute lowerAttribute(Attribute attr) const {
    if (!attr)
      return attr;
    if (auto fvInteger = dyn_cast<circt::moore::FVIntegerAttr>(attr)) {
      auto value = fvInteger.getValue();
      auto integerType =
          IntegerType::get(attr.getContext(), value.getBitWidth());
      NamedAttrList planes;
      planes.append("value",
                    IntegerAttr::get(integerType, value.getRawValue()));
      planes.append("unknown",
                    IntegerAttr::get(integerType, value.getRawUnknown()));
      return DictionaryAttr::get(attr.getContext(), planes);
    }
    if (auto typeAttr = dyn_cast<TypeAttr>(attr))
      return TypeAttr::get(lowerType(typeAttr.getValue()));
    if (auto array = dyn_cast<ArrayAttr>(attr)) {
      SmallVector<Attribute> values;
      llvm::transform(array, std::back_inserter(values),
                      [&](Attribute value) { return lowerAttribute(value); });
      return ArrayAttr::get(attr.getContext(), values);
    }
    if (auto dict = dyn_cast<DictionaryAttr>(attr)) {
      NamedAttrList values;
      for (auto named : dict)
        values.append(named.getName(), lowerAttribute(named.getValue()));
      return DictionaryAttr::get(attr.getContext(), values);
    }
    // Leave future Moore attributes illegal instead of silently converting
    // their semantics into text.
    return attr;
  }

  bool isFullyLegal(Type type) const {
    if (lowerType(type) != type)
      return false;
    return !type.walk([&](Type nested) {
                  return nested.getDialect().getNamespace() == "moore"
                             ? WalkResult::interrupt()
                             : WalkResult::advance();
                })
                .wasInterrupted();
  }

  bool isFullyLegal(Attribute attr) const {
    if (lowerAttribute(attr) != attr)
      return false;
    return !attr.walk(
                    [&](Attribute nested) {
                      return nested.getDialect().getNamespace() == "moore"
                                 ? WalkResult::interrupt()
                                 : WalkResult::advance();
                    },
                    [&](Type nested) {
                      return nested.getDialect().getNamespace() == "moore"
                                 ? WalkResult::interrupt()
                                 : WalkResult::advance();
                    })
                .wasInterrupted();
  }
};

template <typename TargetOp>
static Operation *createOperation(ConversionPatternRewriter &rewriter,
                                  Operation *source, ValueRange operands,
                                  TypeRange resultTypes,
                                  ArrayRef<NamedAttribute> attributes = {}) {
  return TargetOp::create(rewriter, source->getLoc(), resultTypes, operands,
                          attributes)
      .getOperation();
}

static ir::NetKind convertNetKind(circt::moore::NetKind kind);

/// Convert exact four-state constants to the two-plane Obelisk representation.
struct ConstantConversion
    : public OpConversionPattern<circt::moore::ConstantOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(circt::moore::ConstantOp op, OpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type resultType = getTypeConverter()->convertType(op.getResult().getType());
    auto value = op.getValue();
    auto integerType = rewriter.getIntegerType(value.getBitWidth());
    if (isa<ir::LogicType>(resultType)) {
      NamedAttribute attrs[] = {
          rewriter.getNamedAttr("value", rewriter.getIntegerAttr(
                                             integerType, value.getRawValue())),
          rewriter.getNamedAttr(
              "unknown",
              rewriter.getIntegerAttr(integerType, value.getRawUnknown()))};
      Operation *result = createOperation<ir::LogicConstantOp>(
          rewriter, op, {}, resultType, attrs);
      rewriter.replaceOp(op, result->getResults());
      return success();
    }

    NamedAttribute attr = rewriter.getNamedAttr(
        "value", rewriter.getIntegerAttr(cast<IntegerType>(resultType),
                                         value.toAPInt(false)));
    Operation *result = createOperation<arith::ConstantOp>(
        rewriter, op, {}, resultType, ArrayRef<NamedAttribute>(attr));
    rewriter.replaceOp(op, result->getResults());
    return success();
  }
};

struct TimeConstantConversion
    : public OpConversionPattern<circt::moore::ConstantTimeOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(circt::moore::ConstantTimeOp op, OpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto value = APInt(64, op.getValue());
    NamedAttribute attr = rewriter.getNamedAttr(
        "value", rewriter.getIntegerAttr(rewriter.getIntegerType(64), value));
    Operation *result = createOperation<ir::TimeConstantOp>(
        rewriter, op, {}, ir::TimeType::get(op.getContext()),
        ArrayRef<NamedAttribute>(attr));
    rewriter.replaceOp(op, result->getResults());
    return success();
  }
};

struct RealConstantConversion
    : public OpConversionPattern<circt::moore::ConstantRealOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(circt::moore::ConstantRealOp op, OpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type resultType = getTypeConverter()->convertType(op.getResult().getType());
    auto value = FloatAttr::get(cast<FloatType>(resultType), op.getValue());
    NamedAttribute attr = rewriter.getNamedAttr("value", value);
    Operation *result = createOperation<arith::ConstantOp>(
        rewriter, op, {}, resultType, ArrayRef<NamedAttribute>(attr));
    rewriter.replaceOp(op, result->getResults());
    return success();
  }
};

struct ConstantStringConversion
    : public OpConversionPattern<circt::moore::ConstantStringOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(circt::moore::ConstantStringOp op, OpAdaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type resultType = getTypeConverter()->convertType(op.getResult().getType());
    StringRef string = op.getValue();
    unsigned width;
    if (auto integer = dyn_cast<IntegerType>(resultType))
      width = integer.getWidth();
    else
      width = cast<ir::LogicType>(resultType).getWidth();
    APInt value(width, 0);
    size_t maxChars = std::min(string.size(), static_cast<size_t>(width / 8));
    for (size_t i = 0; i < maxChars; ++i) {
      size_t position = string.size() - 1 - i;
      value |= APInt(width, static_cast<uint8_t>(string[position])) << (8 * i);
    }
    auto integerType = rewriter.getIntegerType(width);
    if (isa<ir::LogicType>(resultType)) {
      NamedAttribute attrs[] = {
          rewriter.getNamedAttr("value",
                                rewriter.getIntegerAttr(integerType, value)),
          rewriter.getNamedAttr("unknown", rewriter.getIntegerAttr(
                                               integerType, APInt(width, 0)))};
      Operation *result = createOperation<ir::LogicConstantOp>(
          rewriter, op, {}, resultType, attrs);
      rewriter.replaceOp(op, result->getResults());
      return success();
    }
    NamedAttribute attr = rewriter.getNamedAttr(
        "value", rewriter.getIntegerAttr(integerType, value));
    Operation *result = createOperation<arith::ConstantOp>(
        rewriter, op, {}, resultType, ArrayRef<NamedAttribute>(attr));
    rewriter.replaceOp(op, result->getResults());
    return success();
  }
};

template <typename SourceOp, typename TargetOp>
struct OneToOneConversion : public OpConversionPattern<SourceOp> {
  using Base = OpConversionPattern<SourceOp>;
  using OpAdaptor = typename Base::OpAdaptor;
  OneToOneConversion(TypeConverter &converter, MLIRContext *context)
      : Base(converter, context, PatternBenefit(3)) {}

  LogicalResult
  matchAndRewrite(SourceOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if constexpr (std::is_same_v<SourceOp, circt::moore::ConcatRefOp>)
      if (classifyLValue(op.getResult()) == LValueKind::Mixed)
        return op.emitError(
            "cannot combine nets and variables in one lvalue concatenation");

    SmallVector<Type> resultTypes;
    for (Value result : op->getResults())
      if (failed(this->getTypeConverter()->convertType(result, resultTypes)))
        return failure();
    Operation *result = createOperation<TargetOp>(
        rewriter, op, adaptor.getOperands(), resultTypes);
    rewriter.replaceOp(op, result->getResults());
    return success();
  }
};

template <typename SourceOp, arith::CmpIPredicate Predicate>
struct IntegerCompareConversion : public OpConversionPattern<SourceOp> {
  using Base = OpConversionPattern<SourceOp>;
  using OpAdaptor = typename Base::OpAdaptor;
  IntegerCompareConversion(TypeConverter &converter, MLIRContext *context)
      : Base(converter, context, PatternBenefit(3)) {}

  LogicalResult
  matchAndRewrite(SourceOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ValueRange operands = adaptor.getOperands();
    Type resultType =
        this->getTypeConverter()->convertType(op->getResult(0).getType());
    if (operands.size() != 2 || !resultType.isSignlessInteger(1) ||
        !llvm::all_of(operands, [](Value value) {
          return isa<IntegerType>(value.getType());
        }))
      return failure();
    NamedAttribute predicate = rewriter.getNamedAttr(
        "predicate",
        arith::CmpIPredicateAttr::get(op->getContext(), Predicate));
    Operation *result =
        createOperation<arith::CmpIOp>(rewriter, op, operands, resultType,
                                       ArrayRef<NamedAttribute>(predicate));
    rewriter.replaceOp(op, result->getResults());
    return success();
  }
};

template <typename SourceOp, arith::CmpFPredicate Predicate>
struct FloatCompareConversion : public OpConversionPattern<SourceOp> {
  using Base = OpConversionPattern<SourceOp>;
  using OpAdaptor = typename Base::OpAdaptor;
  FloatCompareConversion(TypeConverter &converter, MLIRContext *context)
      : Base(converter, context, PatternBenefit(3)) {}

  LogicalResult
  matchAndRewrite(SourceOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type resultType =
        this->getTypeConverter()->convertType(op->getResult(0).getType());
    NamedAttribute predicate = rewriter.getNamedAttr(
        "predicate",
        arith::CmpFPredicateAttr::get(op->getContext(), Predicate));
    Operation *result = createOperation<arith::CmpFOp>(
        rewriter, op, adaptor.getOperands(), resultType,
        ArrayRef<NamedAttribute>(predicate));
    rewriter.replaceOp(op, result->getResults());
    return success();
  }
};

template <typename SourceOp, ir::LogicUnaryKind Kind>
struct LogicUnaryConversion : public OpConversionPattern<SourceOp> {
  using Base = OpConversionPattern<SourceOp>;
  using OpAdaptor = typename Base::OpAdaptor;

  LogicUnaryConversion(TypeConverter &converter, MLIRContext *context)
      : Base(converter, context, PatternBenefit(3)) {}

  LogicalResult
  matchAndRewrite(SourceOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ValueRange operands = adaptor.getOperands();
    Type resultType =
        this->getTypeConverter()->convertType(op->getResult(0).getType());
    if (operands.size() != 1 || !isa<ir::LogicType>(resultType) ||
        !isa<ir::LogicType>(operands[0].getType()))
      return failure();
    NamedAttribute attr = rewriter.getNamedAttr(
        "kind", ir::LogicUnaryKindAttr::get(op->getContext(), Kind));
    Operation *result = createOperation<ir::LogicUnaryOp>(
        rewriter, op, operands, resultType, ArrayRef<NamedAttribute>(attr));
    rewriter.replaceOp(op, result->getResults());
    return success();
  }
};

template <typename SourceOp, ir::LogicBinaryKind Kind,
          typename IntegerTargetOp = void>
struct LogicBinaryConversion : public OpConversionPattern<SourceOp> {
  using Base = OpConversionPattern<SourceOp>;
  using OpAdaptor = typename Base::OpAdaptor;

  LogicBinaryConversion(TypeConverter &converter, MLIRContext *context)
      : Base(converter, context, PatternBenefit(3)) {}

  LogicalResult
  matchAndRewrite(SourceOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ValueRange operands = adaptor.getOperands();
    if (operands.size() != 2 || op->getNumResults() != 1)
      return failure();
    Type resultType =
        this->getTypeConverter()->convertType(op->getResult(0).getType());
    if (isa<ir::LogicType>(resultType) &&
        llvm::all_of(
            operands,
            [](Value value) { return isa<ir::LogicType>(value.getType()); }) &&
        operands[0].getType() == operands[1].getType() &&
        operands[0].getType() == resultType) {
      NamedAttribute attr = rewriter.getNamedAttr(
          "kind", ir::LogicBinaryKindAttr::get(op->getContext(), Kind));
      Operation *result = createOperation<ir::LogicBinaryOp>(
          rewriter, op, operands, resultType, ArrayRef<NamedAttribute>(attr));
      rewriter.replaceOp(op, result->getResults());
      return success();
    }
    if constexpr (!std::is_void_v<IntegerTargetOp>) {
      if (isa<IntegerType>(resultType) &&
          llvm::all_of(operands, [](Value value) {
            return isa<IntegerType>(value.getType());
          })) {
        Operation *result = createOperation<IntegerTargetOp>(
            rewriter, op, operands, resultType);
        rewriter.replaceOp(op, result->getResults());
        return success();
      }
    }
    return failure();
  }
};

template <typename SourceOp, ir::LogicCompareKind Kind>
struct LogicCompareConversion : public OpConversionPattern<SourceOp> {
  using Base = OpConversionPattern<SourceOp>;
  using OpAdaptor = typename Base::OpAdaptor;

  LogicCompareConversion(TypeConverter &converter, MLIRContext *context)
      : Base(converter, context, PatternBenefit(3)) {}

  LogicalResult
  matchAndRewrite(SourceOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ValueRange operands = adaptor.getOperands();
    if (operands.size() != 2 || op->getNumResults() != 1)
      return failure();
    Type resultType =
        this->getTypeConverter()->convertType(op->getResult(0).getType());
    if ((!isa<ir::LogicType>(resultType) && !resultType.isSignlessInteger(1)) ||
        !llvm::all_of(operands, [](Value value) {
          return isa<ir::LogicType>(value.getType());
        }))
      return failure();
    NamedAttribute attr = rewriter.getNamedAttr(
        "kind", ir::LogicCompareKindAttr::get(op->getContext(), Kind));
    Operation *result = createOperation<ir::LogicCompareOp>(
        rewriter, op, operands, resultType, ArrayRef<NamedAttribute>(attr));
    rewriter.replaceOp(op, result->getResults());
    return success();
  }
};

template <typename SourceOp, ir::LogicReduceKind Kind>
struct LogicReduceConversion : public OpConversionPattern<SourceOp> {
  using Base = OpConversionPattern<SourceOp>;
  using OpAdaptor = typename Base::OpAdaptor;

  LogicReduceConversion(TypeConverter &converter, MLIRContext *context)
      : Base(converter, context, PatternBenefit(3)) {}

  LogicalResult
  matchAndRewrite(SourceOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ValueRange operands = adaptor.getOperands();
    if (operands.size() != 1 || op->getNumResults() != 1 ||
        !isa<ir::LogicType>(operands[0].getType()))
      return failure();
    Type resultType =
        this->getTypeConverter()->convertType(op->getResult(0).getType());
    if (!isa<ir::LogicType>(resultType))
      return failure();
    NamedAttribute attr = rewriter.getNamedAttr(
        "kind", ir::LogicReduceKindAttr::get(op->getContext(), Kind));
    Operation *result = createOperation<ir::LogicReduceOp>(
        rewriter, op, operands, resultType, ArrayRef<NamedAttribute>(attr));
    rewriter.replaceOp(op, result->getResults());
    return success();
  }
};

struct LogicConcatConversion
    : public OpConversionPattern<circt::moore::ConcatOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(circt::moore::ConcatOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type resultType = getTypeConverter()->convertType(op.getResult().getType());
    if (!isa<ir::LogicType>(resultType) ||
        !llvm::all_of(adaptor.getOperands(), [](Value value) {
          return isa<ir::LogicType>(value.getType());
        }))
      return failure();
    Operation *result = createOperation<ir::LogicConcatOp>(
        rewriter, op, adaptor.getOperands(), resultType);
    rewriter.replaceOp(op, result->getResults());
    return success();
  }
};

struct LogicExtractConversion
    : public OpConversionPattern<circt::moore::ExtractOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(circt::moore::ExtractOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type resultType = getTypeConverter()->convertType(op.getResult().getType());
    if (!isa<ir::LogicType>(resultType) ||
        !isa<ir::LogicType>(adaptor.getInput().getType()))
      return failure();
    NamedAttribute lowBit = rewriter.getNamedAttr(
        "lowBit", rewriter.getI64IntegerAttr(op.getLowBit()));
    Operation *result = createOperation<ir::LogicExtractOp>(
        rewriter, op, adaptor.getOperands(), resultType,
        ArrayRef<NamedAttribute>(lowBit));
    rewriter.replaceOp(op, result->getResults());
    return success();
  }
};

enum class DomainCastKind { LogicToBits, BitsToLogic, NoOp };

template <typename SourceOp, DomainCastKind Kind>
struct DomainCastConversion : public OpConversionPattern<SourceOp> {
  using Base = OpConversionPattern<SourceOp>;
  using OpAdaptor = typename Base::OpAdaptor;

  DomainCastConversion(TypeConverter &converter, MLIRContext *context)
      : Base(converter, context, PatternBenefit(3)) {}

  LogicalResult
  matchAndRewrite(SourceOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ValueRange operands = adaptor.getOperands();
    if (operands.size() != 1 || op->getNumResults() != 1)
      return failure();
    Type resultType =
        this->getTypeConverter()->convertType(op->getResult(0).getType());
    if constexpr (Kind == DomainCastKind::NoOp) {
      if (operands[0].getType() != resultType)
        return failure();
      rewriter.replaceOp(op, operands[0]);
      return success();
    }
    if constexpr (Kind == DomainCastKind::LogicToBits) {
      if (isa<ir::LogicType>(operands[0].getType()) &&
          isa<IntegerType>(resultType)) {
        Operation *result = createOperation<ir::LogicToBitsOp>(
            rewriter, op, operands, resultType);
        rewriter.replaceOp(op, result->getResults());
        return success();
      }
    }
    if constexpr (Kind == DomainCastKind::BitsToLogic) {
      if (isa<IntegerType>(operands[0].getType()) &&
          isa<ir::LogicType>(resultType)) {
        Operation *result = createOperation<ir::LogicFromBitsOp>(
            rewriter, op, operands, resultType);
        rewriter.replaceOp(op, result->getResults());
        return success();
      }
    }
    return failure();
  }
};

template <typename SourceOp, bool IsSigned, typename IntegerTargetOp>
struct ResizeConversion : public OpConversionPattern<SourceOp> {
  using Base = OpConversionPattern<SourceOp>;
  using OpAdaptor = typename Base::OpAdaptor;

  ResizeConversion(TypeConverter &converter, MLIRContext *context)
      : Base(converter, context, PatternBenefit(3)) {}

  LogicalResult
  matchAndRewrite(SourceOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ValueRange operands = adaptor.getOperands();
    if (operands.size() != 1 || op->getNumResults() != 1)
      return failure();
    Type resultType =
        this->getTypeConverter()->convertType(op->getResult(0).getType());
    if (isa<ir::LogicType>(operands[0].getType()) &&
        isa<ir::LogicType>(resultType)) {
      NamedAttribute attr =
          rewriter.getNamedAttr("isSigned", rewriter.getBoolAttr(IsSigned));
      Operation *result = createOperation<ir::LogicResizeOp>(
          rewriter, op, operands, resultType, ArrayRef<NamedAttribute>(attr));
      rewriter.replaceOp(op, result->getResults());
      return success();
    }
    if (isa<IntegerType>(operands[0].getType()) &&
        isa<IntegerType>(resultType)) {
      Operation *result =
          createOperation<IntegerTargetOp>(rewriter, op, operands, resultType);
      rewriter.replaceOp(op, result->getResults());
      return success();
    }
    return failure();
  }
};

struct LogicDynExtractConversion
    : public OpConversionPattern<circt::moore::DynExtractOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(circt::moore::DynExtractOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type resultType = getTypeConverter()->convertType(op.getResult().getType());
    if (!isa<ir::LogicType>(adaptor.getInput().getType()) ||
        !isa<ir::LogicType>(resultType))
      return failure();
    Operation *result = createOperation<ir::LogicDynExtractOp>(
        rewriter, op, adaptor.getOperands(), resultType);
    rewriter.replaceOp(op, result->getResults());
    return success();
  }
};

struct LogicReplicateConversion
    : public OpConversionPattern<circt::moore::ReplicateOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(circt::moore::ReplicateOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type resultType = getTypeConverter()->convertType(op.getResult().getType());
    if (!isa<ir::LogicType>(adaptor.getValue().getType()) ||
        !isa<ir::LogicType>(resultType))
      return failure();
    Operation *result = createOperation<ir::LogicReplicateOp>(
        rewriter, op, adaptor.getOperands(), resultType);
    rewriter.replaceOp(op, result->getResults());
    return success();
  }
};

template <typename SourceOp, ir::LogicBinaryKind Kind, typename IntegerTargetOp>
struct LogicShiftConversion : public OpConversionPattern<SourceOp> {
  using Base = OpConversionPattern<SourceOp>;
  using OpAdaptor = typename Base::OpAdaptor;

  LogicShiftConversion(TypeConverter &converter, MLIRContext *context)
      : Base(converter, context, PatternBenefit(3)) {}

  LogicalResult
  matchAndRewrite(SourceOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ValueRange operands = adaptor.getOperands();
    if (operands.size() != 2 || op->getNumResults() != 1)
      return failure();
    Type resultType =
        this->getTypeConverter()->convertType(op->getResult(0).getType());
    if (isa<ir::LogicType>(operands[0].getType()) &&
        isa<ir::LogicType>(resultType)) {
      NamedAttribute attr = rewriter.getNamedAttr(
          "kind", ir::LogicBinaryKindAttr::get(op->getContext(), Kind));
      Operation *result = createOperation<ir::LogicShiftOp>(
          rewriter, op, operands, resultType, ArrayRef<NamedAttribute>(attr));
      rewriter.replaceOp(op, result->getResults());
      return success();
    }
    if (isa<IntegerType>(operands[0].getType()) &&
        isa<IntegerType>(operands[1].getType()) &&
        isa<IntegerType>(resultType)) {
      Operation *result =
          createOperation<IntegerTargetOp>(rewriter, op, operands, resultType);
      rewriter.replaceOp(op, result->getResults());
      return success();
    }
    return failure();
  }
};

struct LogicBoolCastConversion
    : public OpConversionPattern<circt::moore::BoolCastOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(circt::moore::BoolCastOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type resultType = getTypeConverter()->convertType(op.getResult().getType());
    if (!isa<ir::LogicType>(adaptor.getInput().getType()) ||
        !isa<ir::LogicType>(resultType))
      return failure();
    Operation *result = createOperation<ir::LogicBoolCastOp>(
        rewriter, op, adaptor.getOperands(), resultType);
    rewriter.replaceOp(op, result->getResults());
    return success();
  }
};

struct VariableConversion
    : public OpConversionPattern<circt::moore::VariableOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(circt::moore::VariableOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type resultType = getTypeConverter()->convertType(op.getResult().getType());
    NamedAttrList attrs;
    if (auto name = op.getNameAttr())
      attrs.append("name", name);
    Operation *result = createOperation<ir::VarAllocOp>(
        rewriter, op, adaptor.getOperands(), resultType, attrs);
    rewriter.replaceOp(op, result->getResults());
    return success();
  }
};

struct NetConversion : public OpConversionPattern<circt::moore::NetOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(circt::moore::NetOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto reference = cast<circt::moore::RefType>(op.getResult().getType());
    Type elementType =
        static_cast<const MooreTypeConverter *>(getTypeConverter())
            ->lowerType(reference.getNestedType());
    auto resultType = ir::NetType::get(op.getContext(), elementType);
    NamedAttrList attrs;
    attrs.append("kind", ir::NetKindAttr::get(op.getContext(),
                                              convertNetKind(op.getKind())));
    if (auto name = op.getNameAttr())
      attrs.append("name", name);
    Operation *net =
        createOperation<ir::NetAllocOp>(rewriter, op, {}, resultType, attrs);

    if (Value assignment = adaptor.getAssignment()) {
      NamedAttribute driver =
          rewriter.getNamedAttr("driver", rewriter.getI64IntegerAttr(0));
      createOperation<ir::NetDriveOp>(rewriter, op,
                                      ValueRange{net->getResult(0), assignment},
                                      {}, ArrayRef<NamedAttribute>(driver));
    }
    rewriter.replaceOp(op, net->getResults());
    return success();
  }
};

struct ReadConversion : public OpConversionPattern<circt::moore::ReadOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(circt::moore::ReadOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type resultType = getTypeConverter()->convertType(op.getResult().getType());
    Operation *result;
    if (isa<ir::NetType>(adaptor.getInput().getType()))
      result = createOperation<ir::NetReadOp>(
          rewriter, op, adaptor.getOperands(), resultType);
    else
      result = createOperation<ir::LoadOp>(rewriter, op, adaptor.getOperands(),
                                           resultType);
    rewriter.replaceOp(op, result->getResults());
    return success();
  }
};

struct AssignedVariableConversion
    : public OpConversionPattern<circt::moore::AssignedVariableOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(circt::moore::AssignedVariableOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getInput().getType() !=
        getTypeConverter()->convertType(op.getResult().getType()))
      return failure();
    rewriter.replaceOp(op, adaptor.getInput());
    return success();
  }
};

struct BlockingAssignConversion
    : public OpConversionPattern<circt::moore::BlockingAssignOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(circt::moore::BlockingAssignOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Value operands[] = {adaptor.getSrc(), adaptor.getDst()};
    Operation *result =
        createOperation<ir::StoreOp>(rewriter, op, operands, {});
    rewriter.eraseOp(op);
    (void)result;
    return success();
  }
};

template <typename SourceOp>
struct NonBlockingAssignConversion : public OpConversionPattern<SourceOp> {
  using OpConversionPattern<SourceOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(SourceOp op, typename SourceOp::Adaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    SmallVector<Value> operands = {adaptor.getSrc(), adaptor.getDst()};
    if constexpr (std::is_same_v<SourceOp,
                                 circt::moore::DelayedNonBlockingAssignOp>)
      operands.push_back(adaptor.getDelay());
    Operation *result =
        createOperation<ir::NBAEnqueueOp>(rewriter, op, operands, {});
    rewriter.eraseOp(op);
    (void)result;
    return success();
  }
};

struct ExtractRefConversion
    : public OpConversionPattern<circt::moore::ExtractRefOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(circt::moore::ExtractRefOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto sourceResult = cast<circt::moore::RefType>(op.getResult().getType());
    Type elementType =
        static_cast<const MooreTypeConverter *>(getTypeConverter())
            ->lowerType(sourceResult.getNestedType());
    bool isNet = isa<ir::NetType>(adaptor.getInput().getType());
    Type resultType =
        isNet ? Type(ir::NetType::get(op.getContext(), elementType))
              : Type(ir::RefType::get(op.getContext(), elementType));
    NamedAttribute lowBit = rewriter.getNamedAttr("lowBit", op.getLowBitAttr());
    Operation *result = isNet
                            ? createOperation<ir::NetExtractOp>(
                                  rewriter, op, adaptor.getOperands(),
                                  resultType, ArrayRef<NamedAttribute>(lowBit))
                            : createOperation<ir::RefExtractOp>(
                                  rewriter, op, adaptor.getOperands(),
                                  resultType, ArrayRef<NamedAttribute>(lowBit));
    rewriter.replaceOp(op, result->getResults());
    return success();
  }
};

struct DynExtractRefConversion
    : public OpConversionPattern<circt::moore::DynExtractRefOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(circt::moore::DynExtractRefOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto sourceResult = cast<circt::moore::RefType>(op.getResult().getType());
    Type elementType =
        static_cast<const MooreTypeConverter *>(getTypeConverter())
            ->lowerType(sourceResult.getNestedType());
    bool isNet = isa<ir::NetType>(adaptor.getInput().getType());
    Type resultType =
        isNet ? Type(ir::NetType::get(op.getContext(), elementType))
              : Type(ir::RefType::get(op.getContext(), elementType));
    Operation *result =
        isNet ? createOperation<ir::NetDynExtractOp>(
                    rewriter, op, adaptor.getOperands(), resultType)
              : createOperation<ir::RefDynExtractOp>(
                    rewriter, op, adaptor.getOperands(), resultType);
    rewriter.replaceOp(op, result->getResults());
    return success();
  }
};

struct ConcatRefConversion
    : public OpConversionPattern<circt::moore::ConcatRefOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(circt::moore::ConcatRefOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    bool hasNet = llvm::any_of(adaptor.getOperands(), [](Value input) {
      return isa<ir::NetType>(input.getType());
    });
    bool hasRef = llvm::any_of(adaptor.getOperands(), [](Value input) {
      return isa<ir::RefType>(input.getType());
    });
    if (hasNet == hasRef)
      return rewriter.notifyMatchFailure(
          op,
          "lvalue concatenation must contain exclusively nets or variables");
    auto sourceResult = cast<circt::moore::RefType>(op.getResult().getType());
    Type elementType =
        static_cast<const MooreTypeConverter *>(getTypeConverter())
            ->lowerType(sourceResult.getNestedType());
    Type resultType =
        hasNet ? Type(ir::NetType::get(op.getContext(), elementType))
               : Type(ir::RefType::get(op.getContext(), elementType));
    Operation *result =
        hasNet ? createOperation<ir::NetConcatOp>(
                     rewriter, op, adaptor.getOperands(), resultType)
               : createOperation<ir::RefConcatOp>(
                     rewriter, op, adaptor.getOperands(), resultType);
    rewriter.replaceOp(op, result->getResults());
    return success();
  }
};

static ir::ProcessKind convertProcedureKind(circt::moore::ProcedureKind kind) {
  using Source = circt::moore::ProcedureKind;
  switch (kind) {
  case Source::Initial:
    return ir::ProcessKind::Initial;
  case Source::Final:
    return ir::ProcessKind::Final;
  case Source::Always:
    return ir::ProcessKind::Always;
  case Source::AlwaysComb:
    return ir::ProcessKind::AlwaysComb;
  case Source::AlwaysLatch:
    return ir::ProcessKind::AlwaysLatch;
  case Source::AlwaysFF:
    return ir::ProcessKind::AlwaysFF;
  }
  llvm_unreachable("unknown Moore procedure kind");
}

static ir::NetKind convertNetKind(circt::moore::NetKind kind) {
  using Source = circt::moore::NetKind;
  switch (kind) {
  case Source::Supply0:
    return ir::NetKind::Supply0;
  case Source::Supply1:
    return ir::NetKind::Supply1;
  case Source::Tri:
    return ir::NetKind::Tri;
  case Source::TriAnd:
    return ir::NetKind::TriAnd;
  case Source::TriOr:
    return ir::NetKind::TriOr;
  case Source::TriReg:
    return ir::NetKind::TriReg;
  case Source::Tri0:
    return ir::NetKind::Tri0;
  case Source::Tri1:
    return ir::NetKind::Tri1;
  case Source::UWire:
    return ir::NetKind::UWire;
  case Source::Wire:
    return ir::NetKind::Wire;
  case Source::WAnd:
    return ir::NetKind::Wand;
  case Source::WOr:
    return ir::NetKind::Wor;
  case Source::Interconnect:
    return ir::NetKind::Interconnect;
  case Source::UserDefined:
    return ir::NetKind::UserDefined;
  case Source::Unknown:
    return ir::NetKind::Unknown;
  }
  llvm_unreachable("unknown Moore net kind");
}

static ir::JoinKind convertJoinKind(circt::moore::JoinKind kind) {
  using Source = circt::moore::JoinKind;
  switch (kind) {
  case Source::Join:
    return ir::JoinKind::All;
  case Source::JoinAny:
    return ir::JoinKind::Any;
  case Source::JoinNone:
    return ir::JoinKind::None;
  }
  llvm_unreachable("unknown Moore join kind");
}

static ir::EdgeKind convertEdgeKind(circt::moore::Edge edge) {
  using Source = circt::moore::Edge;
  switch (edge) {
  case Source::AnyChange:
    return ir::EdgeKind::Change;
  case Source::PosEdge:
    return ir::EdgeKind::Posedge;
  case Source::NegEdge:
    return ir::EdgeKind::Negedge;
  case Source::BothEdges:
    return ir::EdgeKind::Both;
  }
  llvm_unreachable("unknown Moore edge kind");
}

static ir::DeferAssert convertDeferAssert(circt::moore::DeferAssert defer) {
  using Source = circt::moore::DeferAssert;
  switch (defer) {
  case Source::Immediate:
    return ir::DeferAssert::Immediate;
  case Source::Observed:
    return ir::DeferAssert::Observed;
  case Source::Final:
    return ir::DeferAssert::Final;
  }
  llvm_unreachable("unknown Moore assertion deferral");
}

static ir::Severity convertSeverity(circt::moore::Severity severity) {
  using Source = circt::moore::Severity;
  switch (severity) {
  case Source::Info:
    return ir::Severity::Info;
  case Source::Warning:
    return ir::Severity::Warning;
  case Source::Error:
    return ir::Severity::Error;
  case Source::Fatal:
    return ir::Severity::Fatal;
  }
  llvm_unreachable("unknown Moore severity");
}

static ir::FileOpenMode convertFileOpenMode(circt::moore::FOpenMode mode) {
  using Source = circt::moore::FOpenMode;
  switch (mode) {
  case Source::Read:
    return ir::FileOpenMode::Read;
  case Source::Write:
    return ir::FileOpenMode::Write;
  case Source::Append:
    return ir::FileOpenMode::Append;
  case Source::ReadUpdate:
    return ir::FileOpenMode::ReadUpdate;
  case Source::WriteUpdate:
    return ir::FileOpenMode::WriteUpdate;
  case Source::AppendUpdate:
    return ir::FileOpenMode::AppendUpdate;
  }
  llvm_unreachable("unknown Moore file-open mode");
}

static ir::IntegerFormat convertIntegerFormat(circt::moore::IntFormat format) {
  using Source = circt::moore::IntFormat;
  switch (format) {
  case Source::Decimal:
    return ir::IntegerFormat::Decimal;
  case Source::Binary:
    return ir::IntegerFormat::Binary;
  case Source::Octal:
    return ir::IntegerFormat::Octal;
  case Source::HexLower:
    return ir::IntegerFormat::HexLower;
  case Source::HexUpper:
    return ir::IntegerFormat::HexUpper;
  }
  llvm_unreachable("unknown Moore integer format");
}

static ir::IntegerAlignment
convertIntegerAlignment(circt::moore::IntAlign alignment) {
  using Source = circt::moore::IntAlign;
  switch (alignment) {
  case Source::Right:
    return ir::IntegerAlignment::Right;
  case Source::Left:
    return ir::IntegerAlignment::Left;
  }
  llvm_unreachable("unknown Moore integer alignment");
}

static ir::IntegerPadding
convertIntegerPadding(circt::moore::IntPadding padding) {
  using Source = circt::moore::IntPadding;
  switch (padding) {
  case Source::Space:
    return ir::IntegerPadding::Space;
  case Source::Zero:
    return ir::IntegerPadding::Zero;
  }
  llvm_unreachable("unknown Moore integer padding");
}

static ir::RealFormat convertRealFormat(circt::moore::RealFormat format) {
  using Source = circt::moore::RealFormat;
  switch (format) {
  case Source::Float:
    return ir::RealFormat::Float;
  case Source::Exponential:
    return ir::RealFormat::Exponential;
  case Source::General:
    return ir::RealFormat::General;
  }
  llvm_unreachable("unknown Moore real format");
}

static ir::ArrayCmpPredicate
convertArrayPredicate(circt::moore::UArrayCmpPredicate predicate) {
  using Source = circt::moore::UArrayCmpPredicate;
  switch (predicate) {
  case Source::eq:
    return ir::ArrayCmpPredicate::Eq;
  case Source::ne:
    return ir::ArrayCmpPredicate::Ne;
  }
  llvm_unreachable("unknown Moore array comparison predicate");
}

static ir::StringCmpPredicate
convertStringPredicate(circt::moore::StringCmpPredicate predicate) {
  using Source = circt::moore::StringCmpPredicate;
  switch (predicate) {
  case Source::eq:
    return ir::StringCmpPredicate::Eq;
  case Source::ne:
    return ir::StringCmpPredicate::Ne;
  case Source::lt:
    return ir::StringCmpPredicate::Lt;
  case Source::le:
    return ir::StringCmpPredicate::Le;
  case Source::gt:
    return ir::StringCmpPredicate::Gt;
  case Source::ge:
    return ir::StringCmpPredicate::Ge;
  }
  llvm_unreachable("unknown Moore string comparison predicate");
}

static ir::DPIArgDirection
convertDPIArgDirection(circt::moore::DPIArgDirection direction) {
  using Source = circt::moore::DPIArgDirection;
  switch (direction) {
  case Source::In:
    return ir::DPIArgDirection::In;
  case Source::Out:
    return ir::DPIArgDirection::Out;
  case Source::InOut:
    return ir::DPIArgDirection::InOut;
  case Source::Return:
    return ir::DPIArgDirection::Return;
  }
  llvm_unreachable("unknown Moore DPI argument direction");
}

static void normalizeEnumAttributes(Operation *op, NamedAttrList &attributes) {
  MLIRContext *context = op->getContext();
  if (auto procedure = dyn_cast<circt::moore::ProcedureOp>(op)) {
    attributes.set("kind",
                   ir::ProcessKindAttr::get(
                       context, convertProcedureKind(procedure.getKind())));
  } else if (auto net = dyn_cast<circt::moore::NetOp>(op)) {
    attributes.set(
        "kind", ir::NetKindAttr::get(context, convertNetKind(net.getKind())));
  } else if (auto fork = dyn_cast<circt::moore::ForkJoinOp>(op)) {
    attributes.set("kind", ir::JoinKindAttr::get(
                               context, convertJoinKind(fork.getKind())));
  } else if (auto detect = dyn_cast<circt::moore::DetectEventOp>(op)) {
    attributes.set("edge", ir::EdgeKindAttr::get(
                               context, convertEdgeKind(detect.getEdge())));
  }

  auto normalizeDefer = [&](auto assertOp) {
    attributes.set("defer",
                   ir::DeferAssertAttr::get(
                       context, convertDeferAssert(assertOp.getDefer())));
  };
  if (auto assertOp = dyn_cast<circt::moore::AssertOp>(op))
    normalizeDefer(assertOp);
  else if (auto assumeOp = dyn_cast<circt::moore::AssumeOp>(op))
    normalizeDefer(assumeOp);
  else if (auto coverOp = dyn_cast<circt::moore::CoverOp>(op))
    normalizeDefer(coverOp);

  if (auto severity = dyn_cast<circt::moore::SeverityBIOp>(op))
    attributes.set("severity",
                   ir::SeverityAttr::get(
                       context, convertSeverity(severity.getSeverity())));
  if (auto open = dyn_cast<circt::moore::FOpenBIOp>(op))
    if (auto mode = open.getMode())
      attributes.set("mode", ir::FileOpenModeAttr::get(
                                 context, convertFileOpenMode(*mode)));

  if (auto format = dyn_cast<circt::moore::FormatIntOp>(op)) {
    attributes.set("format",
                   ir::IntegerFormatAttr::get(
                       context, convertIntegerFormat(format.getFormat())));
    attributes.set("alignment", ir::IntegerAlignmentAttr::get(
                                    context, convertIntegerAlignment(
                                                 format.getAlignment())));
    attributes.set("padding",
                   ir::IntegerPaddingAttr::get(
                       context, convertIntegerPadding(format.getPadding())));
  } else if (auto format = dyn_cast<circt::moore::FormatRealOp>(op)) {
    attributes.set("format",
                   ir::RealFormatAttr::get(
                       context, convertRealFormat(format.getFormat())));
    attributes.set("alignment", ir::IntegerAlignmentAttr::get(
                                    context, convertIntegerAlignment(
                                                 format.getAlignment())));
  }

  auto normalizeArrayPredicate = [&](auto compare) {
    attributes.set("predicate",
                   ir::ArrayCmpPredicateAttr::get(
                       context, convertArrayPredicate(compare.getPredicate())));
  };
  if (auto compare = dyn_cast<circt::moore::UArrayCmpOp>(op))
    normalizeArrayPredicate(compare);
  else if (auto compare = dyn_cast<circt::moore::QueueCmpOp>(op))
    normalizeArrayPredicate(compare);
  if (auto compare = dyn_cast<circt::moore::StringCmpOp>(op))
    attributes.set("predicate", ir::StringCmpPredicateAttr::get(
                                    context, convertStringPredicate(
                                                 compare.getPredicate())));

  if (auto dpi = dyn_cast<circt::moore::DPIFuncOp>(op)) {
    SmallVector<Attribute> directions;
    for (Attribute direction : dpi.getDpiArgDirs()) {
      auto sourceDirection =
          cast<circt::moore::DPIArgDirectionAttr>(direction).getValue();
      directions.push_back(ir::DPIArgDirectionAttr::get(
          context, convertDPIArgDirection(sourceDirection)));
    }
    attributes.set("dpi_arg_dirs", ArrayAttr::get(context, directions));
  }
}

/// Strongly typed, exhaustive semantic conversion. Specialized conversions
/// have a higher benefit and produce lower-level target operations. Every
/// remaining Moore operation has one explicit instantiation of this template
/// and one generated Obelisk enum value.
template <typename SourceOp, ir::SemanticKind Kind>
struct SemanticConversion : public OpConversionPattern<SourceOp> {
  using Base = OpConversionPattern<SourceOp>;
  using OpAdaptor = typename Base::OpAdaptor;

  SemanticConversion(TypeConverter &converter, MLIRContext *context)
      : Base(converter, context, PatternBenefit(0)) {
    static_assert(SourceOp::template hasTrait<OpTrait::ZeroSuccessors>(),
                  "successor-bearing Moore operations need a dedicated "
                  "control-flow conversion");
  }

  LogicalResult
  matchAndRewrite(SourceOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    SmallVector<Type> resultTypes;
    for (Value result : op->getResults())
      if (failed(this->getTypeConverter()->convertType(result, resultTypes)))
        return failure();

    auto *converter =
        static_cast<const MooreTypeConverter *>(this->getTypeConverter());
    NamedAttrList sourceAttrs;
    for (auto attr : op->getAttrs())
      sourceAttrs.append(attr.getName(),
                         converter->lowerAttribute(attr.getValue()));
    normalizeEnumAttributes(op, sourceAttrs);
    if constexpr (SourceOp::template hasTrait<SymbolOpInterface::Trait>()) {
      sourceAttrs.erase(SymbolTable::getSymbolAttrName());
      sourceAttrs.erase(SymbolTable::getVisibilityAttrName());
    }

    auto kind = ir::SemanticKindAttr::get(op->getContext(), Kind);
    DictionaryAttr sourceAttrDict;
    if (!sourceAttrs.empty())
      sourceAttrDict = rewriter.getDictionaryAttr(sourceAttrs);
    Operation *replacement;
    if constexpr (std::is_same_v<SourceOp, circt::moore::SVModuleOp>) {
      replacement = ir::SemanticGraphSymbolOp::create(
                        rewriter, op.getLoc(), resultTypes,
                        SymbolTable::getSymbolName(op), kind, sourceAttrDict,
                        adaptor.getOperands())
                        .getOperation();
    } else if constexpr (SourceOp::template hasTrait<OpTrait::SymbolTable>()) {
      replacement = ir::SemanticSymbolTableOp::create(
                        rewriter, op.getLoc(), resultTypes,
                        SymbolTable::getSymbolName(op), kind, sourceAttrDict,
                        adaptor.getOperands())
                        .getOperation();
    } else if constexpr (std::is_same_v<SourceOp, circt::moore::ForkJoinOp>) {
      replacement =
          ir::SemanticGraphOp::create(rewriter, op.getLoc(), resultTypes, kind,
                                      sourceAttrDict, adaptor.getOperands(),
                                      op->getNumRegions())
              .getOperation();
    } else if constexpr (SourceOp::template hasTrait<OpTrait::IsTerminator>()) {
      replacement = ir::SemanticTerminatorOp::create(rewriter, op.getLoc(),
                                                     kind, sourceAttrDict,
                                                     adaptor.getOperands())
                        .getOperation();
    } else if constexpr (SourceOp::template hasTrait<
                             SymbolOpInterface::Trait>() &&
                         SourceOp::template hasTrait<
                             OpTrait::IsIsolatedFromAbove>()) {
      replacement = ir::SemanticIsolatedSymbolOp::create(
                        rewriter, op.getLoc(), resultTypes,
                        SymbolTable::getSymbolName(op), kind, sourceAttrDict,
                        adaptor.getOperands())
                        .getOperation();
    } else if constexpr (SourceOp::template hasTrait<
                             SymbolOpInterface::Trait>()) {
      replacement =
          ir::SemanticSymbolOp::create(rewriter, op.getLoc(), resultTypes,
                                       SymbolTable::getSymbolName(op), kind,
                                       sourceAttrDict, adaptor.getOperands())
              .getOperation();
    } else if constexpr (SourceOp::template hasTrait<
                             OpTrait::IsIsolatedFromAbove>() &&
                         !SourceOp::template hasTrait<OpTrait::ZeroRegions>()) {
      replacement = ir::SemanticIsolatedRegionOp::create(
                        rewriter, op.getLoc(), resultTypes, kind,
                        sourceAttrDict, adaptor.getOperands())
                        .getOperation();
    } else if constexpr (!SourceOp::template hasTrait<OpTrait::ZeroRegions>()) {
      replacement =
          ir::SemanticRegionOp::create(rewriter, op.getLoc(), resultTypes, kind,
                                       sourceAttrDict, adaptor.getOperands(),
                                       op->getNumRegions())
              .getOperation();
    } else if constexpr (SourceOp::template hasTrait<
                             OpTrait::AlwaysSpeculatableImplTrait>()) {
      if (!isPure(op))
        return rewriter.notifyMatchFailure(
            op, "source operation is speculatable but has memory effects");
      replacement =
          ir::SemanticValueOp::create(rewriter, op.getLoc(), resultTypes, kind,
                                      sourceAttrDict, adaptor.getOperands())
              .getOperation();
    } else {
      replacement =
          ir::SemanticEffectOp::create(rewriter, op.getLoc(), resultTypes, kind,
                                       sourceAttrDict, adaptor.getOperands())
              .getOperation();
    }
    if constexpr (SourceOp::template hasTrait<SymbolOpInterface::Trait>())
      if (Attribute visibility =
              op->getAttr(SymbolTable::getVisibilityAttrName()))
        replacement->setAttr(SymbolTable::getVisibilityAttrName(), visibility);
    for (auto [sourceRegion, targetRegion] :
         llvm::zip(op->getRegions(), replacement->getRegions())) {
      if (failed(rewriter.convertRegionTypes(&sourceRegion,
                                             *this->getTypeConverter())))
        return failure();
      rewriter.inlineRegionBefore(sourceRegion, targetRegion,
                                  targetRegion.end());
    }
    rewriter.replaceOp(op, replacement->getResults());
    return success();
  }
};

/// Non-Moore operations may contain Moore types in signatures or attributes.
/// Update those operations in place, preserving their registered semantics.
struct LegalDialectTypeConversion : public ConversionPattern {
  LegalDialectTypeConversion(TypeConverter &converter, MLIRContext *context)
      : ConversionPattern(converter, MatchAnyOpTypeTag(), PatternBenefit(1),
                          context) {}

  LogicalResult
  matchAndRewrite(Operation *op, ArrayRef<Value> operands,
                  ConversionPatternRewriter &rewriter) const override {
    if (op->getDialect() && op->getDialect()->getNamespace() == "moore")
      return failure();
    auto *converter =
        static_cast<const MooreTypeConverter *>(getTypeConverter());

    bool needsUpdate = !llvm::equal(op->getOperands(), operands);
    SmallVector<Type> resultTypes;
    for (Value result : op->getResults()) {
      Type converted = converter->lowerType(result.getType());
      resultTypes.push_back(converted);
      needsUpdate |= converted != result.getType();
    }
    for (auto attr : op->getAttrs())
      needsUpdate |=
          converter->lowerAttribute(attr.getValue()) != attr.getValue();
    for (Region &region : op->getRegions())
      for (Block &block : region)
        for (BlockArgument argument : block.getArguments())
          needsUpdate |=
              converter->lowerType(argument.getType()) != argument.getType();
    if (!needsUpdate)
      return failure();

    rewriter.modifyOpInPlace(op, [&] {
      op->setOperands(operands);
      for (auto [result, type] : llvm::zip(op->getResults(), resultTypes))
        result.setType(type);
      for (auto attr : llvm::to_vector(op->getAttrs()))
        op->setAttr(attr.getName(), converter->lowerAttribute(attr.getValue()));
      for (Region &region : op->getRegions())
        for (Block &block : region)
          for (BlockArgument argument : block.getArguments())
            argument.setType(converter->lowerType(argument.getType()));
    });
    return success();
  }
};

static LogicalResult verifyMooreInventory(ModuleOp module) {
  module.getContext()->getOrLoadDialect<circt::moore::MooreDialect>();
  llvm::StringSet<> expectedOperations;
  llvm::SmallDenseSet<uint32_t, 256> expectedKinds;
#define OBELISK_MOORE_SEMANTIC_OP(SourceOp, Kind)                              \
  expectedOperations.insert(circt::moore::SourceOp::getOperationName());       \
  expectedKinds.insert(static_cast<uint32_t>(ir::SemanticKind::Kind));
#include "obelisk/Conversion/MooreToObeliskOps.def"
#undef OBELISK_MOORE_SEMANTIC_OP

  auto registered = module.getContext()->getRegisteredOperationsByDialect(
      circt::moore::MooreDialect::getDialectNamespace());
  if (registered.size() != expectedOperations.size())
    return module.emitError()
           << "Moore inventory has " << expectedOperations.size()
           << " entries but the registered dialect has " << registered.size();
  if (expectedKinds.size() != expectedOperations.size())
    return module.emitError("Moore inventory maps multiple operations to the "
                            "same SemanticKind");
  for (RegisteredOperationName operation : registered)
    if (!expectedOperations.contains(operation.getStringRef()))
      return module.emitError()
             << "Moore operation " << operation.getStringRef()
             << " is missing from MooreToObeliskOps.def";
  return success();
}

class ConvertMooreToObeliskPass
    : public impl::ConvertMooreToObeliskPassBase<ConvertMooreToObeliskPass> {
public:
  void runOnOperation() override {
    if (failed(verifyMooreInventory(getOperation()))) {
      signalPassFailure();
      return;
    }
    MLIRContext &context = getContext();
    MooreTypeConverter converter;
    ConversionTarget target(context);
    target.addIllegalDialect<circt::moore::MooreDialect>();
    auto hasLegalTypesAndAttributes = [&](Operation *op) {
      if (op->getDialect() && op->getDialect()->getNamespace() == "moore")
        return false;
      if (!llvm::all_of(op->getOperandTypes(), [&](Type type) {
            return converter.isFullyLegal(type);
          }))
        return false;
      if (!llvm::all_of(op->getResultTypes(), [&](Type type) {
            return converter.isFullyLegal(type);
          }))
        return false;
      for (auto attr : op->getAttrs())
        if (!converter.isFullyLegal(attr.getValue()))
          return false;
      for (Region &region : op->getRegions())
        for (Block &block : region)
          for (BlockArgument argument : block.getArguments())
            if (!converter.isFullyLegal(argument.getType()))
              return false;
      return true;
    };
    target.addDynamicallyLegalDialect<ir::ObeliskDialect>(
        hasLegalTypesAndAttributes);
    target.markUnknownOpDynamicallyLegal(hasLegalTypesAndAttributes);

    RewritePatternSet patterns(&context);
    patterns.add<
        ConstantConversion, TimeConstantConversion, RealConstantConversion,
        ConstantStringConversion, LogicConcatConversion, LogicExtractConversion,
        LogicDynExtractConversion, LogicReplicateConversion,
        LogicBoolCastConversion, VariableConversion, NetConversion,
        ReadConversion, AssignedVariableConversion, BlockingAssignConversion,
        NonBlockingAssignConversion<circt::moore::NonBlockingAssignOp>,
        NonBlockingAssignConversion<circt::moore::DelayedNonBlockingAssignOp>,
        ExtractRefConversion, DynExtractRefConversion, ConcatRefConversion,
        OneToOneConversion<circt::moore::NegRealOp, arith::NegFOp>,
        OneToOneConversion<circt::moore::AddRealOp, arith::AddFOp>,
        OneToOneConversion<circt::moore::SubRealOp, arith::SubFOp>,
        OneToOneConversion<circt::moore::MulRealOp, arith::MulFOp>,
        OneToOneConversion<circt::moore::DivRealOp, arith::DivFOp>,
        OneToOneConversion<circt::moore::SIntToRealOp, arith::SIToFPOp>,
        OneToOneConversion<circt::moore::UIntToRealOp, arith::UIToFPOp>,
        OneToOneConversion<circt::moore::IntToStringOp,
                           circt::sim::IntToStringOp>,
        OneToOneConversion<circt::moore::FormatStringToStringOp,
                           circt::sim::FormatToStringOp>,
        FloatCompareConversion<circt::moore::EqRealOp,
                               arith::CmpFPredicate::OEQ>,
        FloatCompareConversion<circt::moore::NeRealOp,
                               arith::CmpFPredicate::UNE>,
        FloatCompareConversion<circt::moore::FltOp, arith::CmpFPredicate::OLT>,
        FloatCompareConversion<circt::moore::FleOp, arith::CmpFPredicate::OLE>,
        FloatCompareConversion<circt::moore::FgtOp, arith::CmpFPredicate::OGT>,
        FloatCompareConversion<circt::moore::FgeOp, arith::CmpFPredicate::OGE>,
        IntegerCompareConversion<circt::moore::EqOp, arith::CmpIPredicate::eq>,
        IntegerCompareConversion<circt::moore::NeOp, arith::CmpIPredicate::ne>,
        IntegerCompareConversion<circt::moore::CaseEqOp,
                                 arith::CmpIPredicate::eq>,
        IntegerCompareConversion<circt::moore::CaseNeOp,
                                 arith::CmpIPredicate::ne>,
        IntegerCompareConversion<circt::moore::CaseZEqOp,
                                 arith::CmpIPredicate::eq>,
        IntegerCompareConversion<circt::moore::CaseXZEqOp,
                                 arith::CmpIPredicate::eq>,
        IntegerCompareConversion<circt::moore::WildcardEqOp,
                                 arith::CmpIPredicate::eq>,
        IntegerCompareConversion<circt::moore::WildcardNeOp,
                                 arith::CmpIPredicate::ne>,
        IntegerCompareConversion<circt::moore::UltOp,
                                 arith::CmpIPredicate::ult>,
        IntegerCompareConversion<circt::moore::UleOp,
                                 arith::CmpIPredicate::ule>,
        IntegerCompareConversion<circt::moore::UgtOp,
                                 arith::CmpIPredicate::ugt>,
        IntegerCompareConversion<circt::moore::UgeOp,
                                 arith::CmpIPredicate::uge>,
        IntegerCompareConversion<circt::moore::SltOp,
                                 arith::CmpIPredicate::slt>,
        IntegerCompareConversion<circt::moore::SleOp,
                                 arith::CmpIPredicate::sle>,
        IntegerCompareConversion<circt::moore::SgtOp,
                                 arith::CmpIPredicate::sgt>,
        IntegerCompareConversion<circt::moore::SgeOp,
                                 arith::CmpIPredicate::sge>>(converter,
                                                             &context);

    patterns.add<
        DomainCastConversion<circt::moore::LogicToIntOp,
                             DomainCastKind::LogicToBits>,
        DomainCastConversion<circt::moore::IntToLogicOp,
                             DomainCastKind::BitsToLogic>,
        DomainCastConversion<circt::moore::ToBuiltinIntOp,
                             DomainCastKind::NoOp>,
        DomainCastConversion<circt::moore::FromBuiltinIntOp,
                             DomainCastKind::NoOp>,
        ResizeConversion<circt::moore::TruncOp, false, arith::TruncIOp>,
        ResizeConversion<circt::moore::ZExtOp, false, arith::ExtUIOp>,
        ResizeConversion<circt::moore::SExtOp, true, arith::ExtSIOp>,
        LogicUnaryConversion<circt::moore::NegOp, ir::LogicUnaryKind::Negate>,
        LogicUnaryConversion<circt::moore::NotOp, ir::LogicUnaryKind::BitNot>,
        LogicBinaryConversion<circt::moore::AddOp, ir::LogicBinaryKind::Add,
                              arith::AddIOp>,
        LogicBinaryConversion<circt::moore::SubOp, ir::LogicBinaryKind::Sub,
                              arith::SubIOp>,
        LogicBinaryConversion<circt::moore::MulOp, ir::LogicBinaryKind::Mul,
                              arith::MulIOp>,
        LogicBinaryConversion<circt::moore::DivUOp, ir::LogicBinaryKind::UDiv,
                              arith::DivUIOp>,
        LogicBinaryConversion<circt::moore::DivSOp, ir::LogicBinaryKind::SDiv,
                              arith::DivSIOp>,
        LogicBinaryConversion<circt::moore::ModUOp, ir::LogicBinaryKind::UMod,
                              arith::RemUIOp>,
        LogicBinaryConversion<circt::moore::ModSOp, ir::LogicBinaryKind::SMod,
                              arith::RemSIOp>,
        LogicBinaryConversion<circt::moore::PowUOp, ir::LogicBinaryKind::UPow>,
        LogicBinaryConversion<circt::moore::PowSOp, ir::LogicBinaryKind::SPow>,
        LogicBinaryConversion<circt::moore::AndOp, ir::LogicBinaryKind::BitAnd,
                              arith::AndIOp>,
        LogicBinaryConversion<circt::moore::OrOp, ir::LogicBinaryKind::BitOr,
                              arith::OrIOp>,
        LogicBinaryConversion<circt::moore::XorOp, ir::LogicBinaryKind::BitXor,
                              arith::XOrIOp>,
        LogicShiftConversion<circt::moore::ShlOp,
                             ir::LogicBinaryKind::ShiftLeft, arith::ShLIOp>,
        LogicShiftConversion<circt::moore::ShrOp,
                             ir::LogicBinaryKind::ShiftRight, arith::ShRUIOp>,
        LogicShiftConversion<circt::moore::AShrOp,
                             ir::LogicBinaryKind::AShiftRight, arith::ShRSIOp>,
        LogicCompareConversion<circt::moore::EqOp,
                               ir::LogicCompareKind::LogicalEq>,
        LogicCompareConversion<circt::moore::NeOp,
                               ir::LogicCompareKind::LogicalNe>,
        LogicCompareConversion<circt::moore::CaseEqOp,
                               ir::LogicCompareKind::CaseEq>,
        LogicCompareConversion<circt::moore::CaseNeOp,
                               ir::LogicCompareKind::CaseNe>,
        LogicCompareConversion<circt::moore::CaseZEqOp,
                               ir::LogicCompareKind::CaseZEq>,
        LogicCompareConversion<circt::moore::CaseXZEqOp,
                               ir::LogicCompareKind::CaseXZEq>,
        LogicCompareConversion<circt::moore::WildcardEqOp,
                               ir::LogicCompareKind::WildEq>,
        LogicCompareConversion<circt::moore::WildcardNeOp,
                               ir::LogicCompareKind::WildNe>,
        LogicCompareConversion<circt::moore::UltOp, ir::LogicCompareKind::ULT>,
        LogicCompareConversion<circt::moore::UleOp, ir::LogicCompareKind::ULE>,
        LogicCompareConversion<circt::moore::UgtOp, ir::LogicCompareKind::UGT>,
        LogicCompareConversion<circt::moore::UgeOp, ir::LogicCompareKind::UGE>,
        LogicCompareConversion<circt::moore::SltOp, ir::LogicCompareKind::SLT>,
        LogicCompareConversion<circt::moore::SleOp, ir::LogicCompareKind::SLE>,
        LogicCompareConversion<circt::moore::SgtOp, ir::LogicCompareKind::SGT>,
        LogicCompareConversion<circt::moore::SgeOp, ir::LogicCompareKind::SGE>,
        LogicReduceConversion<circt::moore::ReduceAndOp,
                              ir::LogicReduceKind::And>,
        LogicReduceConversion<circt::moore::ReduceOrOp,
                              ir::LogicReduceKind::Or>,
        LogicReduceConversion<circt::moore::ReduceXorOp,
                              ir::LogicReduceKind::Xor>>(converter, &context);

    // Register one typed fallback for every operation in the supported Moore
    // dialect. Specialized patterns above have higher benefit.
#define OBELISK_MOORE_SEMANTIC_OP(SourceOp, Kind)                              \
  patterns.add<                                                                \
      SemanticConversion<circt::moore::SourceOp, ir::SemanticKind::Kind>>(     \
      converter, &context);
#include "obelisk/Conversion/MooreToObeliskOps.def"
#undef OBELISK_MOORE_SEMANTIC_OP

    patterns.add<LegalDialectTypeConversion>(converter, &context);

    if (failed(
            applyFullConversion(getOperation(), target, std::move(patterns))))
      signalPassFailure();
  }
};

} // namespace

} // namespace obelisk
