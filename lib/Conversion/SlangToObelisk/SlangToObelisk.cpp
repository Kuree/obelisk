//===- SlangToObelisk.cpp - Exhaustive semantic conversion --------------===//

#include "obelisk/Conversion/SlangToObelisk.h"

#include "obelisk/Dialect/Obelisk/ObeliskOps.h"
#include "obelisk/Dialect/Slang/SlangOps.h"

#include "mlir/IR/AttrTypeSubElements.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

#include <type_traits>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_CONVERTSLANGTOOBELISKPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

template <typename Source, typename Target>
constexpr bool haveSameEncoding(Source source, Target target) {
  return static_cast<std::underlying_type_t<Source>>(source) ==
         static_cast<std::underlying_type_t<Target>>(target);
}

// Enum attributes use builtin IntegerAttr storage. Keep the source and target
// encodings locked together so generic attribute conversion cannot reinterpret
// a value if either dialect is edited independently.
static_assert(haveSameEncoding(slangir::AssignmentKind::Blocking,
                               ir::SVAssignmentKind::Blocking) &&
              haveSameEncoding(slangir::AssignmentKind::Nonblocking,
                               ir::SVAssignmentKind::Nonblocking));
static_assert(haveSameEncoding(slangir::RangeSelectionKind::Simple,
                               ir::SVRangeSelectionKind::Simple) &&
              haveSameEncoding(slangir::RangeSelectionKind::IndexedUp,
                               ir::SVRangeSelectionKind::IndexedUp) &&
              haveSameEncoding(slangir::RangeSelectionKind::IndexedDown,
                               ir::SVRangeSelectionKind::IndexedDown));
static_assert(haveSameEncoding(slangir::ArgumentDirection::In,
                               ir::SVArgumentDirection::In) &&
              haveSameEncoding(slangir::ArgumentDirection::Out,
                               ir::SVArgumentDirection::Out) &&
              haveSameEncoding(slangir::ArgumentDirection::InOut,
                               ir::SVArgumentDirection::InOut) &&
              haveSameEncoding(slangir::ArgumentDirection::Ref,
                               ir::SVArgumentDirection::Ref));
static_assert(haveSameEncoding(slangir::DefinitionKind::Module,
                               ir::SVDefinitionKind::Module) &&
              haveSameEncoding(slangir::DefinitionKind::Interface,
                               ir::SVDefinitionKind::Interface) &&
              haveSameEncoding(slangir::DefinitionKind::Program,
                               ir::SVDefinitionKind::Program));
static_assert(haveSameEncoding(slangir::ProceduralBlockKind::Initial,
                               ir::SVProceduralBlockKind::Initial) &&
              haveSameEncoding(slangir::ProceduralBlockKind::Final,
                               ir::SVProceduralBlockKind::Final) &&
              haveSameEncoding(slangir::ProceduralBlockKind::Always,
                               ir::SVProceduralBlockKind::Always) &&
              haveSameEncoding(slangir::ProceduralBlockKind::AlwaysComb,
                               ir::SVProceduralBlockKind::AlwaysComb) &&
              haveSameEncoding(slangir::ProceduralBlockKind::AlwaysLatch,
                               ir::SVProceduralBlockKind::AlwaysLatch) &&
              haveSameEncoding(slangir::ProceduralBlockKind::AlwaysFF,
                               ir::SVProceduralBlockKind::AlwaysFF));
static_assert(haveSameEncoding(slangir::StatementBlockKind::Sequential,
                               ir::SVStatementBlockKind::Sequential) &&
              haveSameEncoding(slangir::StatementBlockKind::JoinAll,
                               ir::SVStatementBlockKind::JoinAll) &&
              haveSameEncoding(slangir::StatementBlockKind::JoinAny,
                               ir::SVStatementBlockKind::JoinAny) &&
              haveSameEncoding(slangir::StatementBlockKind::JoinNone,
                               ir::SVStatementBlockKind::JoinNone));
static_assert(haveSameEncoding(slangir::SubroutineKind::Function,
                               ir::SVSubroutineKind::Function) &&
              haveSameEncoding(slangir::SubroutineKind::Task,
                               ir::SVSubroutineKind::Task));
static_assert(haveSameEncoding(slangir::UnaryOperator::Plus,
                               ir::SVUnaryOperator::Plus) &&
              haveSameEncoding(slangir::UnaryOperator::Postdecrement,
                               ir::SVUnaryOperator::Postdecrement));
static_assert(haveSameEncoding(slangir::BinaryOperator::Add,
                               ir::SVBinaryOperator::Add) &&
              haveSameEncoding(slangir::BinaryOperator::Power,
                               ir::SVBinaryOperator::Power));
static_assert(haveSameEncoding(slangir::UniquePriorityCheck::None,
                               ir::SVUniquePriorityCheck::None) &&
              haveSameEncoding(slangir::UniquePriorityCheck::Priority,
                               ir::SVUniquePriorityCheck::Priority));
static_assert(haveSameEncoding(slangir::CaseCondition::Normal,
                               ir::SVCaseCondition::Normal) &&
              haveSameEncoding(slangir::CaseCondition::Inside,
                               ir::SVCaseCondition::Inside));
static_assert(haveSameEncoding(slangir::AssertionKind::Assert,
                               ir::SVAssertionKind::Assert) &&
              haveSameEncoding(slangir::AssertionKind::Expect,
                               ir::SVAssertionKind::Expect));
static_assert(haveSameEncoding(slangir::CoverageBinKind::Bins,
                               ir::SVCoverageBinKind::Bins) &&
              haveSameEncoding(slangir::CoverageBinKind::IgnoreBins,
                               ir::SVCoverageBinKind::IgnoreBins));
static_assert(
    haveSameEncoding(slangir::EdgeKind::None, ir::EdgeKind::Change) &&
    haveSameEncoding(slangir::EdgeKind::PosEdge, ir::EdgeKind::Posedge) &&
    haveSameEncoding(slangir::EdgeKind::NegEdge, ir::EdgeKind::Negedge) &&
    haveSameEncoding(slangir::EdgeKind::BothEdges, ir::EdgeKind::Both));
static_assert(haveSameEncoding(slangir::VariableLifetime::Automatic,
                               ir::SVVariableLifetime::Automatic) &&
              haveSameEncoding(slangir::VariableLifetime::Static,
                               ir::SVVariableLifetime::Static));
static_assert(haveSameEncoding(slangir::Visibility::Public,
                               ir::SVVisibility::Public) &&
              haveSameEncoding(slangir::Visibility::Local,
                               ir::SVVisibility::Local));
static_assert(haveSameEncoding(slangir::RandMode::None, ir::SVRandMode::None) &&
              haveSameEncoding(slangir::RandMode::RandC,
                               ir::SVRandMode::RandC));
static_assert(haveSameEncoding(slangir::IntegralFlavor::Generic,
                               ir::SVIntegralFlavor::Generic) &&
              haveSameEncoding(slangir::IntegralFlavor::Integer,
                               ir::SVIntegralFlavor::Integer));
static_assert(haveSameEncoding(slangir::NetKind::Unknown,
                               ir::SVNetKind::Unknown) &&
              haveSameEncoding(slangir::NetKind::UserDefined,
                               ir::SVNetKind::UserDefined));
static_assert(haveSameEncoding(slangir::AssertionUnaryOperator::Not,
                               ir::SVAssertionUnaryOperator::Not) &&
              haveSameEncoding(slangir::AssertionUnaryOperator::SEventually,
                               ir::SVAssertionUnaryOperator::SEventually));
static_assert(
    haveSameEncoding(slangir::AssertionBinaryOperator::And,
                     ir::SVAssertionBinaryOperator::And) &&
    haveSameEncoding(slangir::AssertionBinaryOperator::NonOverlappedFollowedBy,
                     ir::SVAssertionBinaryOperator::NonOverlappedFollowedBy));
static_assert(haveSameEncoding(slangir::SequenceRepetitionKind::Consecutive,
                               ir::SVSequenceRepetitionKind::Consecutive) &&
              haveSameEncoding(slangir::SequenceRepetitionKind::GoTo,
                               ir::SVSequenceRepetitionKind::GoTo));
static_assert(haveSameEncoding(slangir::AssertionStrength::Strong,
                               ir::SVAssertionStrength::Strong) &&
              haveSameEncoding(slangir::AssertionStrength::Weak,
                               ir::SVAssertionStrength::Weak));
static_assert(haveSameEncoding(slangir::AssertionAbortAction::Accept,
                               ir::SVAssertionAbortAction::Accept) &&
              haveSameEncoding(slangir::AssertionAbortAction::Reject,
                               ir::SVAssertionAbortAction::Reject));

class SlangTypeConverter : public TypeConverter {
public:
  SlangTypeConverter() {
    addConversion([this](Type type) -> std::optional<Type> {
      Type converted = convertRecursively(type);
      if (!converted)
        return std::nullopt;
      return converted;
    });
  }

  Type convertRecursively(Type type) const {
    bool failedConversion = false;
    AttrTypeReplacer replacer;
    replacer.addReplacement([&](Type nested) -> std::optional<Type> {
      if (nested.getDialect().getNamespace() !=
          slangir::SlangDialect::getDialectNamespace())
        return std::nullopt;
      Type converted = convertSlangType(nested);
      if (!converted)
        failedConversion = true;
      return converted;
    });

    Type converted = replacer.replace(type);
    if (failedConversion || !converted)
      return {};
    bool containsSlang = false;
    converted.walk([&](Type nested) {
      if (nested.getDialect().getNamespace() ==
          slangir::SlangDialect::getDialectNamespace())
        containsSlang = true;
    });
    return containsSlang ? Type{} : converted;
  }

  Type convertSlangType(Type type) const {
    MLIRContext *context = type.getContext();
    if (auto value = dyn_cast<slangir::IntegralType>(type))
      return ir::IntegralType::get(
          context, value.getWidth(), value.getIsSigned(),
          value.getIsFourState(), value.getLeft(), value.getRight(),
          static_cast<ir::SVIntegralFlavor>(value.getFlavor()));
    if (isa<slangir::StringType>(type))
      return ir::StringType::get(context);
    if (isa<slangir::RealType>(type))
      return ir::RealType::get(context);
    if (isa<slangir::RealtimeType>(type))
      return ir::RealtimeType::get(context);
    if (isa<slangir::ShortRealType>(type))
      return ir::ShortRealType::get(context);
    if (isa<slangir::VoidType>(type))
      return ir::VoidType::get(context);
    if (isa<slangir::TimeType>(type))
      return ir::TimeType::get(context);
    if (isa<slangir::NullType>(type))
      return ir::NullType::get(context);
    if (isa<slangir::ChandleType>(type))
      return ir::ChandleType::get(context);
    if (isa<slangir::EventType>(type))
      return ir::EventType::get(context);
    if (isa<slangir::UnboundedType>(type))
      return ir::UnboundedType::get(context);
    if (isa<slangir::UntypedType>(type))
      return ir::UntypedType::get(context);
    if (isa<slangir::TypeReferenceType>(type))
      return ir::TypeReferenceType::get(context);
    if (isa<slangir::SequenceType>(type))
      return ir::SequenceType::get(context);
    if (isa<slangir::PropertyType>(type))
      return ir::PropertyType::get(context);
    if (isa<slangir::ErrorType>(type))
      return {};
    if (auto value = dyn_cast<slangir::EnumType>(type)) {
      if (Type baseType = convertType(value.getBaseType()))
        return ir::EnumType::get(context, value.getName(), baseType);
      return {};
    }
    if (auto value = dyn_cast<slangir::PackedArrayType>(type)) {
      if (Type elementType = convertType(value.getElementType()))
        return ir::RangedPackedArrayType::get(
            context, elementType, value.getLeft(), value.getRight());
      return {};
    }
    if (auto value = dyn_cast<slangir::UnpackedArrayType>(type)) {
      if (Type elementType = convertType(value.getElementType()))
        return ir::RangedUnpackedArrayType::get(
            context, elementType, value.getLeft(), value.getRight());
      return {};
    }
    if (auto value = dyn_cast<slangir::DynamicArrayType>(type)) {
      if (Type elementType = convertType(value.getElementType()))
        return ir::DynArrayType::get(context, elementType);
      return {};
    }
    if (auto value = dyn_cast<slangir::OpenArrayType>(type)) {
      if (Type elementType = convertType(value.getElementType()))
        return ir::OpenArrayType::get(context, elementType,
                                      value.getIsPacked());
      return {};
    }
    if (auto value = dyn_cast<slangir::AssociativeArrayType>(type)) {
      Type indexType = convertType(value.getIndexType());
      Type elementType = convertType(value.getElementType());
      if (indexType && elementType)
        return ir::AssocArrayType::get(context, indexType, elementType,
                                       value.getWildcardIndex());
      return {};
    }
    if (auto value = dyn_cast<slangir::QueueType>(type)) {
      if (Type elementType = convertType(value.getElementType()))
        return ir::QueueType::get(context, elementType, value.getBound());
      return {};
    }
    if (auto value = dyn_cast<slangir::AggregateType>(type))
      return ir::SourceAggregateType::get(
          context, value.getName(), value.getIsPacked(), value.getIsUnion(),
          value.getIsTagged(), value.getIsSigned(), value.getIsFourState(),
          value.getIsSoft(), value.getBitWidth(), value.getSelectableWidth(),
          value.getBitstreamWidth(), value.getTagBits());
    if (auto value = dyn_cast<slangir::ClassHandleType>(type))
      return ir::ClassHandleType::get(context, value.getClassName());
    if (auto value = dyn_cast<slangir::CovergroupHandleType>(type))
      return ir::CovergroupHandleType::get(context, value.getCovergroupName());
    if (auto value = dyn_cast<slangir::VirtualInterfaceType>(type))
      return ir::VirtualInterfaceType::get(context, value.getInterfaceName(),
                                           value.getModport());
    if (auto value = dyn_cast<slangir::SubroutineType>(type)) {
      if (Type signature = convertType(value.getSignature()))
        return ir::SubroutineType::get(context, signature, value.getIsTask());
      return {};
    }
    if (auto value = dyn_cast<slangir::SourceRangeType>(type))
      return ir::SourceRangeType::get(
          context, value.getStartFile(), value.getStartLine(),
          value.getStartColumn(), value.getEndFile(), value.getEndLine(),
          value.getEndColumn(), value.getMacroName());
    return {};
  }

  FailureOr<Attribute> convertAttribute(Attribute attr) const {
    Attribute converted =
        attr.replace([this](Type type) { return convertType(type); });
    if (!converted)
      return failure();
    return converted;
  }

  /// Dialect conversion checks value types, but semantic types also occur in
  /// TypeAttrs nested in dictionaries and arrays. Keep an operation legal only
  /// when both locations are free of source-dialect types.
  bool isRecursivelyLegal(Operation *op) const {
    if (!isLegal(op))
      return false;
    for (Region &region : op->getRegions()) {
      if (!isLegal(&region))
        return false;
    }
    for (NamedAttribute named : op->getAttrs()) {
      bool legal = true;
      named.getValue().walk([&](Type type) {
        if (!isLegal(type))
          legal = false;
      });
      if (!legal)
        return false;
    }
    return true;
  }
};

template <typename Op>
inline constexpr bool isInvalidSemanticOp =
    std::is_same_v<Op, slangir::InvalidTimingControlOp> ||
    std::is_same_v<Op, slangir::InvalidConstraintOp> ||
    std::is_same_v<Op, slangir::InvalidAssertionExprOp> ||
    std::is_same_v<Op, slangir::InvalidBinsSelectExprOp> ||
    std::is_same_v<Op, slangir::InvalidPatternOp> ||
    std::is_same_v<Op, slangir::ErrorTypeOp>;

template <typename SourceOp, typename TargetOp>
class ConcreteASTNodeConversion : public OpConversionPattern<SourceOp> {
public:
  using OpConversionPattern<SourceOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(SourceOp op, typename SourceOp::Adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if constexpr (isInvalidSemanticOp<SourceOp>)
      return rewriter.notifyMatchFailure(
          op, "invalid semantic sentinels have no Obelisk representation");

    const auto &converter =
        *static_cast<const SlangTypeConverter *>(this->getTypeConverter());
    NamedAttrList attrs;
    for (NamedAttribute attr : op->getAttrs()) {
      FailureOr<Attribute> converted =
          converter.convertAttribute(attr.getValue());
      if (failed(converted))
        return rewriter.notifyMatchFailure(op,
                                           "attribute type conversion failed");
      attrs.set(attr.getName(), *converted);
    }

    SmallVector<NamedAttribute> discardableAttrs;
    for (NamedAttribute attr : op->getDiscardableAttrs()) {
      FailureOr<Attribute> converted =
          converter.convertAttribute(attr.getValue());
      if (failed(converted))
        return rewriter.notifyMatchFailure(
            op, "discardable attribute type conversion failed");
      discardableAttrs.emplace_back(attr.getName(), *converted);
    }

    TargetOp target = TargetOp::create(rewriter, op.getLoc(), TypeRange{},
                                       ValueRange{}, attrs.getAttrs());
    for (NamedAttribute attr : discardableAttrs)
      target->setDiscardableAttr(attr.getName(), attr.getValue());
    rewriter.inlineRegionBefore(op.getBody(), target.getBody(),
                                target.getBody().end());
    rewriter.replaceOp(op, target->getResults());
    return success();
  }
};

class ConvertSlangToObeliskPass
    : public impl::ConvertSlangToObeliskPassBase<ConvertSlangToObeliskPass> {
public:
  void runOnOperation() override {
    MLIRContext &context = getContext();
    SlangTypeConverter converter;
    ConversionTarget target(context);
    target.addIllegalDialect<slangir::SlangDialect>();
    target.addDynamicallyLegalDialect<ir::ObeliskDialect>(
        [&](Operation *op) { return converter.isRecursivelyLegal(op); });
    target.addDynamicallyLegalOp<ModuleOp>(
        [&](Operation *op) { return converter.isRecursivelyLegal(op); });
    target.markUnknownOpDynamicallyLegal(
        [&](Operation *op) { return converter.isRecursivelyLegal(op); });

    RewritePatternSet patterns(&context);
#define SLANG_AST_NODE(Category, Kind, CppType)                                \
  patterns.add<                                                                \
      ConcreteASTNodeConversion<slangir::CppType##Op, ir::SV##CppType##Op>>(   \
      converter, &context);
#include "obelisk/Dialect/Slang/SlangASTNodes.def"
#undef SLANG_AST_NODE

    // Patterns fail only before mutating IR, so rollback is never needed.
    // Disabling it drops the driver's undo bookkeeping.
    ConversionConfig config;
    config.allowPatternRollback = false;
    if (failed(applyFullConversion(getOperation(), target, std::move(patterns),
                                   config)))
      signalPassFailure();
  }
};

} // namespace
} // namespace obelisk
