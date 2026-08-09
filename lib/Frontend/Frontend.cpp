//===- Frontend.cpp - slang semantic AST to Slang dialect importer -------===//

#include "obelisk/Frontend/Frontend.h"

#include "obelisk/Dialect/ForeachLoopMetadata.h"
#include "obelisk/Dialect/Slang/SlangOps.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Verifier.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/expressions/Operator.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/driver/Driver.h"
#include "slang/numeric/Time.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/util/OS.h"
#include "slang/util/VersionInfo.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <concepts>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <variant>
#include <vector>

using namespace mlir;

// ASTVisitor dispatches these nested production classes by their unqualified
// names inside RandSeqProductionSymbol. Local aliases let the common inventory
// name both the exact C++ overload and its concrete ODS operation.
namespace slang::ast {
using ProdItem = RandSeqProductionSymbol::ProdItem;
using CodeBlockProd = RandSeqProductionSymbol::CodeBlockProd;
using IfElseProd = RandSeqProductionSymbol::IfElseProd;
using RepeatProd = RandSeqProductionSymbol::RepeatProd;
using CaseProd = RandSeqProductionSymbol::CaseProd;
} // namespace slang::ast

namespace obelisk::frontend {
namespace {

std::string formatReal(double value) {
  std::array<char, 64> buffer;
  auto [end, error] = std::to_chars(buffer.begin(), buffer.end(), value,
                                    std::chars_format::general,
                                    std::numeric_limits<double>::max_digits10);
  if (error != std::errc())
    llvm_unreachable("buffer is too small to format a double");
  return std::string(buffer.begin(), end);
}

template <typename Value> std::string formatConstant(const Value &value) {
  return value.toString(slang::SVInt::MAX_BITS, /*exactUnknowns=*/true);
}

uint64_t getFemtoseconds(slang::TimeScaleValue value) {
  uint64_t unit = 1;
  switch (value.unit) {
  case slang::TimeUnit::Seconds:
    unit = 1'000'000'000'000'000ULL;
    break;
  case slang::TimeUnit::Milliseconds:
    unit = 1'000'000'000'000ULL;
    break;
  case slang::TimeUnit::Microseconds:
    unit = 1'000'000'000ULL;
    break;
  case slang::TimeUnit::Nanoseconds:
    unit = 1'000'000ULL;
    break;
  case slang::TimeUnit::Picoseconds:
    unit = 1'000ULL;
    break;
  case slang::TimeUnit::Femtoseconds:
    unit = 1;
    break;
  }
  return unit * static_cast<uint64_t>(value.magnitude);
}

slangir::ArgumentDirection
convertEnum(slang::ast::ArgumentDirection direction) {
  switch (direction) {
  case slang::ast::ArgumentDirection::In:
    return slangir::ArgumentDirection::In;
  case slang::ast::ArgumentDirection::Out:
    return slangir::ArgumentDirection::Out;
  case slang::ast::ArgumentDirection::InOut:
    return slangir::ArgumentDirection::InOut;
  case slang::ast::ArgumentDirection::Ref:
    return slangir::ArgumentDirection::Ref;
  }
  llvm_unreachable("unknown slang argument direction");
}

slangir::DefinitionKind convertEnum(slang::ast::DefinitionKind kind) {
  switch (kind) {
  case slang::ast::DefinitionKind::Module:
    return slangir::DefinitionKind::Module;
  case slang::ast::DefinitionKind::Interface:
    return slangir::DefinitionKind::Interface;
  case slang::ast::DefinitionKind::Program:
    return slangir::DefinitionKind::Program;
  }
  llvm_unreachable("unknown slang definition kind");
}

slangir::ProceduralBlockKind convertEnum(slang::ast::ProceduralBlockKind kind) {
  switch (kind) {
  case slang::ast::ProceduralBlockKind::Initial:
    return slangir::ProceduralBlockKind::Initial;
  case slang::ast::ProceduralBlockKind::Final:
    return slangir::ProceduralBlockKind::Final;
  case slang::ast::ProceduralBlockKind::Always:
    return slangir::ProceduralBlockKind::Always;
  case slang::ast::ProceduralBlockKind::AlwaysComb:
    return slangir::ProceduralBlockKind::AlwaysComb;
  case slang::ast::ProceduralBlockKind::AlwaysLatch:
    return slangir::ProceduralBlockKind::AlwaysLatch;
  case slang::ast::ProceduralBlockKind::AlwaysFF:
    return slangir::ProceduralBlockKind::AlwaysFF;
  }
  llvm_unreachable("unknown slang procedural block kind");
}

slangir::StatementBlockKind convertEnum(slang::ast::StatementBlockKind kind) {
  switch (kind) {
  case slang::ast::StatementBlockKind::Sequential:
    return slangir::StatementBlockKind::Sequential;
  case slang::ast::StatementBlockKind::JoinAll:
    return slangir::StatementBlockKind::JoinAll;
  case slang::ast::StatementBlockKind::JoinAny:
    return slangir::StatementBlockKind::JoinAny;
  case slang::ast::StatementBlockKind::JoinNone:
    return slangir::StatementBlockKind::JoinNone;
  }
  llvm_unreachable("unknown slang statement block kind");
}

slangir::SubroutineKind convertEnum(slang::ast::SubroutineKind kind) {
  switch (kind) {
  case slang::ast::SubroutineKind::Function:
    return slangir::SubroutineKind::Function;
  case slang::ast::SubroutineKind::Task:
    return slangir::SubroutineKind::Task;
  }
  llvm_unreachable("unknown slang subroutine kind");
}

slangir::UnaryOperator convertEnum(slang::ast::UnaryOperator op) {
  static_assert(static_cast<int>(slang::ast::UnaryOperator::Plus) == 0 &&
                static_cast<int>(slang::ast::UnaryOperator::Postdecrement) ==
                    13);
#define MAP_UNARY(Name)                                                        \
  case slang::ast::UnaryOperator::Name:                                        \
    return slangir::UnaryOperator::Name
  switch (op) {
    MAP_UNARY(Plus);
    MAP_UNARY(Minus);
    MAP_UNARY(BitwiseNot);
    MAP_UNARY(BitwiseAnd);
    MAP_UNARY(BitwiseOr);
    MAP_UNARY(BitwiseXor);
    MAP_UNARY(BitwiseNand);
    MAP_UNARY(BitwiseNor);
    MAP_UNARY(BitwiseXnor);
    MAP_UNARY(LogicalNot);
    MAP_UNARY(Preincrement);
    MAP_UNARY(Predecrement);
    MAP_UNARY(Postincrement);
    MAP_UNARY(Postdecrement);
  }
#undef MAP_UNARY
  llvm_unreachable("unknown slang unary operator");
}

slangir::BinaryOperator convertEnum(slang::ast::BinaryOperator op) {
  static_assert(static_cast<int>(slang::ast::BinaryOperator::Add) == 0 &&
                static_cast<int>(slang::ast::BinaryOperator::Power) == 27);
#define MAP_BINARY(Name)                                                       \
  case slang::ast::BinaryOperator::Name:                                       \
    return slangir::BinaryOperator::Name
  switch (op) {
    MAP_BINARY(Add);
    MAP_BINARY(Subtract);
    MAP_BINARY(Multiply);
    MAP_BINARY(Divide);
    MAP_BINARY(Mod);
    MAP_BINARY(BinaryAnd);
    MAP_BINARY(BinaryOr);
    MAP_BINARY(BinaryXor);
    MAP_BINARY(BinaryXnor);
    MAP_BINARY(Equality);
    MAP_BINARY(Inequality);
    MAP_BINARY(CaseEquality);
    MAP_BINARY(CaseInequality);
    MAP_BINARY(GreaterThanEqual);
    MAP_BINARY(GreaterThan);
    MAP_BINARY(LessThanEqual);
    MAP_BINARY(LessThan);
    MAP_BINARY(WildcardEquality);
    MAP_BINARY(WildcardInequality);
    MAP_BINARY(LogicalAnd);
    MAP_BINARY(LogicalOr);
    MAP_BINARY(LogicalImplication);
    MAP_BINARY(LogicalEquivalence);
    MAP_BINARY(LogicalShiftLeft);
    MAP_BINARY(LogicalShiftRight);
    MAP_BINARY(ArithmeticShiftLeft);
    MAP_BINARY(ArithmeticShiftRight);
    MAP_BINARY(Power);
  }
#undef MAP_BINARY
  llvm_unreachable("unknown slang binary operator");
}

slangir::UniquePriorityCheck
convertEnum(slang::ast::UniquePriorityCheck check) {
  static_assert(static_cast<int>(slang::ast::UniquePriorityCheck::None) == 0 &&
                static_cast<int>(slang::ast::UniquePriorityCheck::Priority) ==
                    3);
  return static_cast<slangir::UniquePriorityCheck>(static_cast<int>(check));
}

slangir::CaseCondition
convertEnum(slang::ast::CaseStatementCondition condition) {
  static_assert(
      static_cast<int>(slang::ast::CaseStatementCondition::Normal) == 0 &&
      static_cast<int>(slang::ast::CaseStatementCondition::Inside) == 3);
  return static_cast<slangir::CaseCondition>(static_cast<int>(condition));
}

slangir::AssertionKind convertEnum(slang::ast::AssertionKind kind) {
  static_assert(static_cast<int>(slang::ast::AssertionKind::Assert) == 0 &&
                static_cast<int>(slang::ast::AssertionKind::Expect) == 5);
  return static_cast<slangir::AssertionKind>(static_cast<int>(kind));
}

slangir::CoverageBinKind
convertEnum(slang::ast::CoverageBinSymbol::BinKind kind) {
  static_assert(static_cast<int>(slang::ast::CoverageBinSymbol::Bins) == 0 &&
                static_cast<int>(slang::ast::CoverageBinSymbol::IgnoreBins) ==
                    2);
  return static_cast<slangir::CoverageBinKind>(static_cast<int>(kind));
}

slangir::EdgeKind convertEnum(slang::ast::EdgeKind edge) {
  switch (edge) {
  case slang::ast::EdgeKind::None:
    return slangir::EdgeKind::None;
  case slang::ast::EdgeKind::PosEdge:
    return slangir::EdgeKind::PosEdge;
  case slang::ast::EdgeKind::NegEdge:
    return slangir::EdgeKind::NegEdge;
  case slang::ast::EdgeKind::BothEdges:
    return slangir::EdgeKind::BothEdges;
  }
  llvm_unreachable("unknown slang edge kind");
}

slangir::RangeSelectionKind convertEnum(slang::ast::RangeSelectionKind kind) {
  static_assert(static_cast<int>(slang::ast::RangeSelectionKind::Simple) == 0 &&
                static_cast<int>(slang::ast::RangeSelectionKind::IndexedDown) ==
                    2);
  return static_cast<slangir::RangeSelectionKind>(static_cast<int>(kind));
}

slangir::VariableLifetime convertEnum(slang::ast::VariableLifetime lifetime) {
  static_assert(static_cast<int>(slang::ast::VariableLifetime::Automatic) ==
                    0 &&
                static_cast<int>(slang::ast::VariableLifetime::Static) == 1);
  return static_cast<slangir::VariableLifetime>(static_cast<int>(lifetime));
}

slangir::Visibility convertEnum(slang::ast::Visibility visibility) {
  static_assert(static_cast<int>(slang::ast::Visibility::Public) == 0 &&
                static_cast<int>(slang::ast::Visibility::Local) == 2);
  return static_cast<slangir::Visibility>(static_cast<int>(visibility));
}

slangir::RandMode convertEnum(slang::ast::RandMode mode) {
  static_assert(static_cast<int>(slang::ast::RandMode::None) == 0 &&
                static_cast<int>(slang::ast::RandMode::RandC) == 2);
  return static_cast<slangir::RandMode>(static_cast<int>(mode));
}

slangir::IntegralFlavor convertEnum(slang::ast::ScalarType::Kind kind) {
  switch (kind) {
  case slang::ast::ScalarType::Bit:
    return slangir::IntegralFlavor::Bit;
  case slang::ast::ScalarType::Logic:
    return slangir::IntegralFlavor::Logic;
  case slang::ast::ScalarType::Reg:
    return slangir::IntegralFlavor::Reg;
  }
  llvm_unreachable("unknown slang scalar type");
}

slangir::IntegralFlavor
convertEnum(slang::ast::PredefinedIntegerType::Kind kind) {
  switch (kind) {
  case slang::ast::PredefinedIntegerType::ShortInt:
    return slangir::IntegralFlavor::ShortInt;
  case slang::ast::PredefinedIntegerType::Int:
    return slangir::IntegralFlavor::Int;
  case slang::ast::PredefinedIntegerType::LongInt:
    return slangir::IntegralFlavor::LongInt;
  case slang::ast::PredefinedIntegerType::Byte:
    return slangir::IntegralFlavor::Byte;
  case slang::ast::PredefinedIntegerType::Integer:
    return slangir::IntegralFlavor::Integer;
  case slang::ast::PredefinedIntegerType::Time:
    llvm_unreachable("time has a dedicated semantic type");
  }
  llvm_unreachable("unknown slang predefined integer type");
}

slangir::NetKind convertEnum(slang::ast::NetType::NetKind kind) {
  static_assert(static_cast<int>(slang::ast::NetType::Unknown) == 0 &&
                static_cast<int>(slang::ast::NetType::UserDefined) == 14);
  return static_cast<slangir::NetKind>(static_cast<int>(kind));
}

slangir::AssertionUnaryOperator
convertEnum(slang::ast::UnaryAssertionOperator op) {
  static_assert(
      static_cast<int>(slang::ast::UnaryAssertionOperator::Not) == 0 &&
      static_cast<int>(slang::ast::UnaryAssertionOperator::SEventually) == 6);
  return static_cast<slangir::AssertionUnaryOperator>(static_cast<int>(op));
}

slangir::AssertionBinaryOperator
convertEnum(slang::ast::BinaryAssertionOperator op) {
  static_assert(
      static_cast<int>(slang::ast::BinaryAssertionOperator::And) == 0 &&
      static_cast<int>(
          slang::ast::BinaryAssertionOperator::NonOverlappedFollowedBy) == 14);
  return static_cast<slangir::AssertionBinaryOperator>(static_cast<int>(op));
}

slangir::SequenceRepetitionKind
convertEnum(slang::ast::SequenceRepetition::Kind kind) {
  static_assert(static_cast<int>(slang::ast::SequenceRepetition::Consecutive) ==
                    0 &&
                static_cast<int>(slang::ast::SequenceRepetition::GoTo) == 2);
  return static_cast<slangir::SequenceRepetitionKind>(static_cast<int>(kind));
}

template <typename Node>
inline constexpr bool isInvalidSemanticNode =
    std::same_as<Node, slang::ast::InvalidTimingControl> ||
    std::same_as<Node, slang::ast::InvalidConstraint> ||
    std::same_as<Node, slang::ast::InvalidAssertionExpr> ||
    std::same_as<Node, slang::ast::InvalidBinsSelectExpr> ||
    std::same_as<Node, slang::ast::InvalidPattern> ||
    std::same_as<Node, slang::ast::ErrorType>;

/// Converts every slang semantic type that can occur on an elaborated AST node
/// into a concrete Slang dialect type. Aliases are represented by their own AST
/// operations while their semantic type points at the canonical target.
class SlangTypeConverter {
public:
  using SymbolReferenceBuilder =
      std::function<SymbolRefAttr(const slang::ast::Symbol &)>;

  SlangTypeConverter(MLIRContext *context,
                     SymbolReferenceBuilder buildSymbolReference)
      : context(context),
        buildSymbolReference(std::move(buildSymbolReference)) {}

  Type convert(const slang::ast::Type &sourceType) {
    const slang::ast::Type &type = sourceType.getCanonicalType();
    if (auto found = cache.find(&type); found != cache.end())
      return found->second;

    // Aggregate and class declarations are represented by symbolic identity,
    // so recursive source type graphs do not recurse through their fields.
    Type result;
    using SK = slang::ast::SymbolKind;
    switch (type.kind) {
    case SK::PredefinedIntegerType: {
      const auto &integer = type.as<slang::ast::PredefinedIntegerType>();
      if (integer.integerKind == slang::ast::PredefinedIntegerType::Time) {
        result = slangir::TimeType::get(context);
        break;
      }
      auto range = type.getFixedRange();
      result = slangir::IntegralType::get(
          context, type.getBitWidth(), type.isSigned(), type.isFourState(),
          range.left, range.right, convertEnum(integer.integerKind));
      break;
    }
    case SK::ScalarType: {
      auto range = type.getFixedRange();
      result = slangir::IntegralType::get(
          context, type.getBitWidth(), type.isSigned(), type.isFourState(),
          range.left, range.right,
          convertEnum(type.as<slang::ast::ScalarType>().scalarKind));
      break;
    }
    case SK::FloatingType: {
      const auto &floating = type.as<slang::ast::FloatingType>();
      switch (floating.floatKind) {
      case slang::ast::FloatingType::Real:
        result = slangir::RealType::get(context);
        break;
      case slang::ast::FloatingType::ShortReal:
        result = slangir::ShortRealType::get(context);
        break;
      case slang::ast::FloatingType::RealTime:
        result = slangir::RealtimeType::get(context);
        break;
      }
      break;
    }
    case SK::EnumType: {
      const auto &enumeration = type.as<slang::ast::EnumType>();
      std::string name = type.getHierarchicalPath();
      if (name.empty())
        name = ("$anon.enum." + std::to_string(enumeration.systemId));
      result = slangir::EnumType::get(context, StringAttr::get(context, name),
                                      convert(enumeration.baseType));
      break;
    }
    case SK::PackedArrayType: {
      const auto &array = type.as<slang::ast::PackedArrayType>();
      result =
          slangir::PackedArrayType::get(context, convert(array.elementType),
                                        array.range.left, array.range.right);
      break;
    }
    case SK::FixedSizeUnpackedArrayType: {
      const auto &array = type.as<slang::ast::FixedSizeUnpackedArrayType>();
      result =
          slangir::UnpackedArrayType::get(context, convert(array.elementType),
                                          array.range.left, array.range.right);
      break;
    }
    case SK::DynamicArrayType: {
      const auto &array = type.as<slang::ast::DynamicArrayType>();
      result =
          slangir::DynamicArrayType::get(context, convert(array.elementType));
      break;
    }
    case SK::DPIOpenArrayType: {
      const auto &array = type.as<slang::ast::DPIOpenArrayType>();
      result = slangir::OpenArrayType::get(context, convert(array.elementType),
                                           array.isPacked);
      break;
    }
    case SK::AssociativeArrayType: {
      const auto &array = type.as<slang::ast::AssociativeArrayType>();
      Type indexType = array.indexType
                           ? convert(*array.indexType)
                           : Type(slangir::UntypedType::get(context));
      result = slangir::AssociativeArrayType::get(
          context, convert(array.elementType), indexType,
          array.hasWildcardIndexType());
      break;
    }
    case SK::QueueType: {
      const auto &queue = type.as<slang::ast::QueueType>();
      result = slangir::QueueType::get(context, convert(queue.elementType),
                                       queue.maxBound);
      break;
    }
    case SK::PackedStructType:
    case SK::UnpackedStructType:
    case SK::PackedUnionType:
    case SK::UnpackedUnionType: {
      bool isPacked =
          type.kind == SK::PackedStructType || type.kind == SK::PackedUnionType;
      bool isUnion = type.kind == SK::PackedUnionType ||
                     type.kind == SK::UnpackedUnionType;
      bool isTagged = false;
      bool isSigned = false;
      bool isFourState = false;
      bool isSoft = false;
      uint64_t bitWidth = 0;
      uint64_t selectableWidth = 0;
      uint64_t bitstreamWidth = 0;
      uint32_t tagBits = 0;
      if (type.kind == SK::PackedUnionType) {
        const auto &value = type.as<slang::ast::PackedUnionType>();
        isTagged = value.isTagged;
        isSoft = value.isSoft;
        tagBits = value.tagBits;
      } else if (type.kind == SK::UnpackedUnionType) {
        const auto &value = type.as<slang::ast::UnpackedUnionType>();
        isTagged = value.isTagged;
        selectableWidth = value.selectableWidth;
        bitstreamWidth = value.bitstreamWidth;
      } else if (type.kind == SK::UnpackedStructType) {
        const auto &value = type.as<slang::ast::UnpackedStructType>();
        selectableWidth = value.selectableWidth;
        bitstreamWidth = value.bitstreamWidth;
      }
      if (isPacked) {
        bitWidth = type.getBitWidth();
        isSigned = type.isSigned();
        isFourState = type.isFourState();
        selectableWidth = bitWidth;
        bitstreamWidth = bitWidth;
      }
      std::string name = type.getHierarchicalPath();
      if (name.empty())
        name = type.toString();

      const slang::ast::Scope *aggregateScope = nullptr;
      if (type.kind == SK::PackedStructType)
        aggregateScope = &type.as<slang::ast::PackedStructType>();
      else if (type.kind == SK::UnpackedStructType)
        aggregateScope = &type.as<slang::ast::UnpackedStructType>();
      else if (type.kind == SK::PackedUnionType)
        aggregateScope = &type.as<slang::ast::PackedUnionType>();
      else
        aggregateScope = &type.as<slang::ast::UnpackedUnionType>();
      SmallVector<Attribute> fields;
      for (const slang::ast::FieldSymbol &field :
           aggregateScope->membersOfType<slang::ast::FieldSymbol>()) {
        fields.push_back(DictionaryAttr::get(
            context,
            {
                NamedAttribute(StringAttr::get(context, "name"),
                               StringAttr::get(context, field.name)),
                NamedAttribute(StringAttr::get(context, "type"),
                               TypeAttr::get(convert(field.getType()))),
                NamedAttribute(StringAttr::get(context, "ordinal"),
                               IntegerAttr::get(IntegerType::get(context, 32),
                                                field.fieldIndex)),
                NamedAttribute(
                    StringAttr::get(context, "packed_offset"),
                    IntegerAttr::get(IntegerType::get(context, 64),
                                     isPacked ? field.bitOffset : 0)),
            }));
      }
      result = slangir::AggregateType::get(
          context, StringAttr::get(context, name), isPacked, isUnion, isTagged,
          isSigned, isFourState, isSoft, bitWidth, selectableWidth,
          bitstreamWidth, tagBits, ArrayAttr::get(context, fields));
      break;
    }
    case SK::ClassType: {
      result = slangir::ClassHandleType::get(
          context, buildSymbolReference(type.as<slang::ast::ClassType>()));
      break;
    }
    case SK::CovergroupType: {
      result = slangir::CovergroupHandleType::get(
          context, buildSymbolReference(type.as<slang::ast::CovergroupType>()));
      break;
    }
    case SK::VoidType:
      result = slangir::VoidType::get(context);
      break;
    case SK::NullType:
      result = slangir::NullType::get(context);
      break;
    case SK::CHandleType:
      result = slangir::ChandleType::get(context);
      break;
    case SK::StringType:
      result = slangir::StringType::get(context);
      break;
    case SK::EventType:
      result = slangir::EventType::get(context);
      break;
    case SK::UnboundedType:
      result = slangir::UnboundedType::get(context);
      break;
    case SK::TypeRefType:
      result = slangir::TypeReferenceType::get(context);
      break;
    case SK::UntypedType:
      result = slangir::UntypedType::get(context);
      break;
    case SK::SequenceType:
      result = slangir::SequenceType::get(context);
      break;
    case SK::PropertyType:
      result = slangir::PropertyType::get(context);
      break;
    case SK::VirtualInterfaceType: {
      const auto &interface = type.as<slang::ast::VirtualInterfaceType>();
      std::string_view modport =
          interface.modport ? interface.modport->name : std::string_view();
      result = slangir::VirtualInterfaceType::get(
          context, buildSymbolReference(interface.iface),
          StringAttr::get(context, modport));
      break;
    }
    case SK::ErrorType:
      llvm_unreachable("error recovery type cannot be converted to Slang IR");
    default:
      llvm_unreachable("unhandled canonical slang semantic type");
    }

    cache.try_emplace(&type, result);
    return result;
  }

private:
  MLIRContext *context;
  SymbolReferenceBuilder buildSymbolReference;
  llvm::DenseMap<const slang::ast::Type *, Type> cache;
};

/// Exhaustive concrete visitor for the selected semantic AST. The macro expands
/// to 220 ordinary overload declarations; ASTVisitor has no generic handler to
/// fall back to in this class.
class SlangASTImporter
    : public slang::ast::ASTVisitor<SlangASTImporter,
                                    slang::ast::VisitFlags::AllGood |
                                        slang::ast::VisitFlags::Bad> {
public:
  SlangASTImporter(ModuleOp module, const slang::SourceManager &sourceManager)
      : builder(module.getContext()), sourceManager(sourceManager),
        typeConverter(module.getContext(),
                      [this](const slang::ast::Symbol &symbol) {
                        return getSemanticSymbolReference(symbol);
                      }) {
    builder.setInsertionPointToStart(module.getBody());
  }

  [[nodiscard]] bool succeeded() const { return !sawInvalidNode; }

  void markDPIExport(const slang::ast::SubroutineSymbol &subroutine,
                     StringRef cIdentifier) {
    auto found = emittedSymbolOperations.find(&subroutine);
    if (found == emittedSymbolOperations.end()) {
      emitError(sourceLocation(subroutine.location))
          << "resolved DPI export subroutine was not imported";
      sawInvalidNode = true;
      return;
    }
    found->second->setAttr("dpi_export_c_identifier",
                           builder.getStringAttr(cIdentifier));
  }

  LogicalResult finalizeReferences() {
    // ASTVisitor follows ownership edges, while elaborated expressions can
    // reference semantic dependencies outside those roots (for example an
    // anonymous enum's members or declarations in the built-in std package).
    // Materialize that transitive closure beneath the real semantic parent so
    // every SymbolRefAttr remains both resolvable and hierarchically accurate.
    llvm::SmallPtrSet<const slang::ast::Symbol *, 32> scheduled;
    SmallVector<const slang::ast::Symbol *, 32> worklist;
    size_t nextPending = 0;
    size_t nextDependency = 0;
    auto schedule = [&](const slang::ast::Symbol *symbol) {
      if (!emittedSymbolPaths.contains(symbol) &&
          scheduled.insert(symbol).second)
        worklist.push_back(symbol);
    };

    // Imports append more references and type dependencies. Consume those
    // append-only vectors incrementally instead of rescanning their complete
    // contents for every newly discovered dependency layer.
    while (nextPending != pendingReferences.size() ||
           nextDependency != semanticDependencies.size() || !worklist.empty()) {
      while (nextPending != pendingReferences.size())
        schedule(pendingReferences[nextPending++].target);
      while (nextDependency != semanticDependencies.size())
        schedule(semanticDependencies[nextDependency++]);
      if (worklist.empty())
        continue;

      const slang::ast::Symbol *symbol = worklist.pop_back_val();
      if (emittedSymbolPaths.contains(symbol))
        continue;
      importReferencedSymbol(*symbol);
      if (!emittedSymbolPaths.contains(symbol)) {
        emitError(sourceLocation(symbol->location))
            << "referenced slang symbol was not imported: "
            << getSymbolPath(*symbol);
        sawInvalidNode = true;
      }
    }

    for (const PendingReference &pending : pendingReferences) {
      auto target = emittedSymbolPaths.find(pending.target);
      if (target == emittedSymbolPaths.end()) {
        pending.operation->emitError()
            << "referenced slang symbol was not imported: "
            << getSymbolPath(*pending.target);
        sawInvalidNode = true;
        continue;
      }

      ArrayRef<std::string> targetPath = target->second;
      assert(!targetPath.empty() && "imported symbol has no symbol path");

      SmallVector<FlatSymbolRefAttr> nested;
      for (StringRef component : targetPath.drop_front())
        nested.push_back(
            FlatSymbolRefAttr::get(builder.getContext(), component));
      pending.operation->setAttr(
          pending.attributeName,
          SymbolRefAttr::get(builder.getContext(), targetPath.front(), nested));
    }
    return success(!sawInvalidNode);
  }

#define SLANG_AST_NODE(Category, Kind, CppType)                                \
  void handle(const slang::ast::CppType &node) {                               \
    if constexpr (isInvalidSemanticNode<slang::ast::CppType>)                  \
      sawInvalidNode = true;                                                   \
    else                                                                       \
      importNode<slangir::CppType##Op>(node);                                  \
  }
#include "obelisk/Dialect/Slang/SlangASTNodes.def"
#undef SLANG_AST_NODE

  void handle(const slang::ast::InvalidStatement &) { sawInvalidNode = true; }
  void handle(const slang::ast::InvalidExpression &) { sawInvalidNode = true; }
  void handle(const slang::ast::InvalidSymbol &) { sawInvalidNode = true; }

private:
  void importReferencedSymbol(const slang::ast::Symbol &symbol) {
    if (emittedSymbolPaths.contains(&symbol))
      return;

    const slang::ast::Symbol *parentSymbol = nullptr;
    if (const auto *parent = symbol.getHierarchicalParent()) {
      const auto &candidate = parent->asSymbol();
      if (&candidate != &symbol)
        parentSymbol = &candidate;
    }
    if (parentSymbol && !emittedSymbolPaths.contains(parentSymbol))
      importReferencedSymbol(*parentSymbol);
    if (emittedSymbolPaths.contains(&symbol))
      return;

    OpBuilder::InsertionGuard guard(builder);
    SmallVector<std::string, 8> savedPath = std::move(currentSymbolPath);
    currentSymbolPath.clear();
    if (parentSymbol) {
      auto parentOperation = emittedSymbolOperations.find(parentSymbol);
      auto parentPath = emittedSymbolPaths.find(parentSymbol);
      if (parentOperation != emittedSymbolOperations.end() &&
          parentPath != emittedSymbolPaths.end()) {
        Operation *operation = parentOperation->second;
        assert(operation->getNumRegions() == 1 &&
               !operation->getRegion(0).empty());
        builder.setInsertionPointToEnd(&operation->getRegion(0).front());
        currentSymbolPath.assign(parentPath->second.begin(),
                                 parentPath->second.end());
      }
    }
    symbol.visit(*this);
    currentSymbolPath = std::move(savedPath);
  }

  Location fileLocation(slang::SourceLocation location) const {
    if (!location.valid())
      return UnknownLoc::get(builder.getContext());
    slang::SourceLocation fileLoc = sourceManager.getFullyExpandedLoc(location);
    if (!sourceManager.isFileLoc(fileLoc))
      fileLoc = sourceManager.getFullyOriginalLoc(location);
    if (!fileLoc.valid() || !sourceManager.isFileLoc(fileLoc))
      return UnknownLoc::get(builder.getContext());
    return FileLineColLoc::get(builder.getContext(),
                               sourceManager.getFileName(fileLoc),
                               sourceManager.getLineNumber(fileLoc),
                               sourceManager.getColumnNumber(fileLoc));
  }

  Location sourceLocation(slang::SourceLocation location) const {
    Location expanded = fileLocation(location);
    if (!location.valid() || !sourceManager.isMacroLoc(location))
      return expanded;
    Location original =
        fileLocation(sourceManager.getFullyOriginalLoc(location));
    return CallSiteLoc::get(original, expanded);
  }

  std::optional<TypeAttr> sourceRangeAttr(slang::SourceRange range,
                                          bool useOriginalLocations = false) {
    slang::SourceLocation start = range.start();
    slang::SourceLocation end = range.end();
    if (useOriginalLocations) {
      start = sourceManager.getFullyOriginalLoc(start);
      end = sourceManager.getFullyOriginalLoc(end);
    } else {
      start = sourceManager.getFullyExpandedLoc(start);
      end = sourceManager.getFullyExpandedLoc(end);
    }
    if (!start.valid() || !end.valid() || !sourceManager.isFileLoc(start) ||
        !sourceManager.isFileLoc(end))
      return std::nullopt;

    auto type = slangir::SourceRangeType::get(
        builder.getContext(),
        builder.getStringAttr(sourceManager.getFileName(start)),
        static_cast<uint32_t>(sourceManager.getLineNumber(start)),
        static_cast<uint32_t>(sourceManager.getColumnNumber(start)),
        builder.getStringAttr(sourceManager.getFileName(end)),
        static_cast<uint32_t>(sourceManager.getLineNumber(end)),
        static_cast<uint32_t>(sourceManager.getColumnNumber(end)),
        builder.getStringAttr(sourceManager.isMacroLoc(range.start())
                                  ? sourceManager.getMacroName(range.start())
                                  : std::string_view{}));
    return TypeAttr::get(type);
  }

  ArrayAttr macroExpansionStack(slang::SourceLocation location) {
    SmallVector<Attribute> frames;
    while (location.valid() && sourceManager.isMacroLoc(location)) {
      NamedAttrList frame;
      std::string_view name = sourceManager.getMacroName(location);
      if (!name.empty())
        frame.set("name", builder.getStringAttr(name));

      slang::SourceLocation original = sourceManager.getOriginalLoc(location);
      if (std::optional<TypeAttr> definition =
              sourceRangeAttr(slang::SourceRange(original, original),
                              /*useOriginalLocations=*/true))
        frame.set("definition", *definition);

      slang::SourceRange expansion = sourceManager.getExpansionRange(location);
      if (std::optional<TypeAttr> invocation = sourceRangeAttr(expansion))
        frame.set("invocation", *invocation);

      frames.push_back(DictionaryAttr::get(builder.getContext(), frame));
      location = sourceManager.getExpansionLoc(location);
    }
    return builder.getArrayAttr(frames);
  }

  template <typename Node>
  slang::SourceRange getSourceRange(const Node &node) const {
    if constexpr (requires { node.sourceRange; }) {
      return node.sourceRange;
    } else if constexpr (std::derived_from<Node, slang::ast::Symbol>) {
      if (const auto *syntax = node.getSyntax())
        return syntax->sourceRange();
      return {node.location, node.location};
    } else if constexpr (requires { node.syntax; }) {
      if (node.syntax)
        return node.syntax->sourceRange();
      return {};
    } else {
      return {};
    }
  }

  template <typename Node>
  std::optional<Type> getSemanticType(const Node &node) {
    if constexpr (std::derived_from<Node, slang::ast::Type>) {
      return typeConverter.convert(node);
    } else if constexpr (std::derived_from<Node, slang::ast::Expression>) {
      return typeConverter.convert(*node.type);
    } else if constexpr (requires { node.getType(); }) {
      if constexpr (std::same_as<std::remove_cvref_t<decltype(node.getType())>,
                                 slang::ast::Type>)
        return typeConverter.convert(node.getType());
    } else if constexpr (std::derived_from<Node, slang::ast::Symbol>) {
      if (const auto *declaredType = node.getDeclaredType())
        return typeConverter.convert(declaredType->getType());
    }
    return std::nullopt;
  }

  std::string getSymbolPath(const slang::ast::Symbol &symbol) {
    if (std::string path = symbol.getHierarchicalPath(); !path.empty())
      return path;

    if (auto found = anonymousSymbolPaths.find(&symbol);
        found != anonymousSymbolPaths.end())
      return found->second;

    std::string path;
    if (const auto *parent = symbol.getHierarchicalParent()) {
      const auto &parentSymbol = parent->asSymbol();
      if (&parentSymbol != &symbol)
        path = getSymbolPath(parentSymbol);
    }
    if (!path.empty())
      path += '.';
    path += "$anon." + std::to_string(nextAnonymousSymbolId++);
    anonymousSymbolPaths.try_emplace(&symbol, path);
    return path;
  }

  StringAttr getInternalSymbolName(const slang::ast::Symbol &symbol) {
    if (auto found = internalSymbolNames.find(&symbol);
        found != internalSymbolNames.end())
      return found->second;

    std::string name = "s" + std::to_string(nextInternalSymbolId++);
    if (!symbol.name.empty()) {
      name += '.';
      name += symbol.name;
    }
    StringAttr attr = builder.getStringAttr(name);
    internalSymbolNames.try_emplace(&symbol, attr);
    return attr;
  }

  SymbolRefAttr getInternalSymbolReference(const slang::ast::Symbol &symbol) {
    SmallVector<StringAttr, 8> components;
    const slang::ast::Symbol *current = &symbol;
    while (current) {
      components.push_back(getInternalSymbolName(*current));
      const auto *parent = current->getHierarchicalParent();
      if (!parent)
        break;
      const auto &parentSymbol = parent->asSymbol();
      current = &parentSymbol == current ? nullptr : &parentSymbol;
    }
    std::ranges::reverse(components);
    assert(!components.empty());
    SmallVector<FlatSymbolRefAttr, 8> nested;
    for (StringAttr component : ArrayRef(components).drop_front())
      nested.push_back(FlatSymbolRefAttr::get(component));
    return SymbolRefAttr::get(components.front(), nested);
  }

  SymbolRefAttr getSemanticSymbolReference(const slang::ast::Symbol &symbol) {
    semanticDependencies.push_back(&symbol);
    return getInternalSymbolReference(symbol);
  }

  template <typename Op>
  void setReferencedSymbol(NamedAttrList &attrs,
                           const slang::ast::Symbol &symbol) {
    OperationName operationName(Op::getOperationName(), builder.getContext());
    setSymbolReference(attrs, symbol,
                       Op::getReferencedSymbolAttrName(operationName),
                       Op::getReferencedPathAttrName(operationName));
  }

  void setSymbolReference(NamedAttrList &attrs,
                          const slang::ast::Symbol &symbol,
                          StringAttr referenceName, StringAttr pathName) {
    attrs.set(pathName, builder.getStringAttr(getSymbolPath(symbol)));
    currentPendingReferences.push_back({&symbol, referenceName});
  }

  template <typename Op>
  void addSequenceRange(NamedAttrList &attrs,
                        const slang::ast::SequenceRange &range) {
    OperationName operationName(Op::getOperationName(), builder.getContext());
    attrs.set(Op::getRangeMinAttrName(operationName),
              builder.getI64IntegerAttr(range.min));
    attrs.set(Op::getRangeIsUnboundedAttrName(operationName),
              builder.getBoolAttr(!range.max));
    if (range.max)
      attrs.set(Op::getRangeMaxAttrName(operationName),
                builder.getI64IntegerAttr(*range.max));
  }

  template <typename Op>
  void addRepetition(
      NamedAttrList &attrs,
      const std::optional<slang::ast::SequenceRepetition> &repetition) {
    OperationName operationName(Op::getOperationName(), builder.getContext());
    attrs.set(Op::getHasRepetitionAttrName(operationName),
              builder.getBoolAttr(repetition.has_value()));
    attrs.set(Op::getRepetitionIsUnboundedAttrName(operationName),
              builder.getBoolAttr(repetition && !repetition->range.max));
    if (!repetition)
      return;
    attrs.set(Op::getRepetitionKindAttrName(operationName),
              slangir::SequenceRepetitionKindAttr::get(
                  builder.getContext(), convertEnum(repetition->kind)));
    attrs.set(Op::getRepetitionMinAttrName(operationName),
              builder.getI64IntegerAttr(repetition->range.min));
    if (repetition->range.max)
      attrs.set(Op::getRepetitionMaxAttrName(operationName),
                builder.getI64IntegerAttr(*repetition->range.max));
  }

  template <typename Op, typename Node>
  void addSpecificAttributes(const Node &node, NamedAttrList &attrs) {
    using T = std::remove_cvref_t<Node>;
    OperationName operationName(Op::getOperationName(), builder.getContext());
#define SET_OP_ATTR(Name, Value)                                               \
  attrs.set(Op::get##Name##AttrName(operationName), (Value))

    if constexpr (std::same_as<T, slang::ast::ProceduralBlockSymbol> ||
                  std::same_as<T, slang::ast::ContinuousAssignSymbol> ||
                  std::same_as<T, slang::ast::PrimitiveInstanceSymbol> ||
                  std::same_as<T, slang::ast::SubroutineSymbol>) {
      slang::TimeScale scale;
      if (const slang::ast::Scope *scope = node.getParentScope())
        scale = scope->getTimeScale().value_or(slang::TimeScale{});
      attrs.set("time_unit_fs",
                builder.getI64IntegerAttr(getFemtoseconds(scale.base)));
      attrs.set("time_precision_fs",
                builder.getI64IntegerAttr(getFemtoseconds(scale.precision)));
    }

    if constexpr (std::same_as<T, slang::ast::InstanceBodySymbol>) {
      slang::TimeScale scale = node.getTimeScale().value_or(slang::TimeScale{});
      attrs.set("time_unit_fs",
                builder.getI64IntegerAttr(getFemtoseconds(scale.base)));
      attrs.set("time_precision_fs",
                builder.getI64IntegerAttr(getFemtoseconds(scale.precision)));
    }

    if constexpr (std::same_as<T, slang::ast::PrimitiveInstanceSymbol>) {
      attrs.set("primitive_name",
                builder.getStringAttr(node.primitiveType.name));
      auto [strength0, strength1] = node.getDriveStrength();
      if (strength0 || strength1) {
        std::string spelling;
        if (strength0)
          spelling += slang::ast::toString(*strength0);
        spelling += ',';
        if (strength1)
          spelling += slang::ast::toString(*strength1);
        SET_OP_ATTR(UnsupportedStrength, builder.getStringAttr(spelling));
      }
      if (const slang::ast::TimingControl *delay = node.getDelay()) {
        slang::SourceRange range = getSourceRange(*delay);
        if (range.start().valid() && range.end().valid() &&
            range.start().buffer() == range.end().buffer()) {
          std::string_view buffer =
              sourceManager.getSourceText(range.start().buffer());
          size_t begin = range.start().offset();
          size_t end = range.end().offset();
          if (begin <= end && end < buffer.size())
            SET_OP_ATTR(UnsupportedDelay, builder.getStringAttr(buffer.substr(
                                              begin, end - begin + 1)));
        }
      }
    }

    if constexpr (std::derived_from<T, slang::ast::VariableSymbol>) {
      SET_OP_ATTR(Lifetime,
                  slangir::VariableLifetimeAttr::get(
                      builder.getContext(), convertEnum(node.lifetime)));
      SET_OP_ATTR(RandMode,
                  slangir::RandModeAttr::get(builder.getContext(),
                                             convertEnum(node.getRandMode())));
      using VF = slang::ast::VariableFlags;
      if (node.flags.has(VF::Const))
        SET_OP_ATTR(IsConst, builder.getUnitAttr());
      if constexpr (std::same_as<T, slang::ast::PatternVarSymbol>)
        SET_OP_ATTR(IsConst, builder.getUnitAttr());
      if (node.flags.has(VF::CompilerGenerated))
        SET_OP_ATTR(IsCompilerGenerated, builder.getUnitAttr());
      if (node.flags.has(VF::ImmutableCoverageOption))
        SET_OP_ATTR(IsImmutableCoverageOption, builder.getUnitAttr());
      if (node.flags.has(VF::CoverageSampleFormal))
        SET_OP_ATTR(IsCoverageSampleFormal, builder.getUnitAttr());
      if (node.flags.has(VF::CheckerFreeVariable))
        SET_OP_ATTR(IsCheckerFreeVariable, builder.getUnitAttr());
      if (node.flags.has(VF::RefStatic))
        SET_OP_ATTR(IsRefStatic, builder.getUnitAttr());

      // Slang keeps unpacked-structure member defaults on the FieldSymbols,
      // rather than synthesizing an initializer on every variable of that
      // type. Record which field expressions are imported below so later
      // stages can initialize the corresponding aggregate subelements.
      if constexpr (std::same_as<T, slang::ast::VariableSymbol>) {
        if (!node.getInitializer()) {
          const slang::ast::Type &type = node.getType().getCanonicalType();
          if (type.kind == slang::ast::SymbolKind::UnpackedStructType) {
            SmallVector<int64_t> ordinals;
            for (const slang::ast::FieldSymbol *field :
                 type.as<slang::ast::UnpackedStructType>().fields)
              if (field->getInitializer())
                ordinals.push_back(field->fieldIndex);
            if (!ordinals.empty())
              attrs.set("obelisk.aggregate_member_initializer_ordinals",
                        builder.getDenseI64ArrayAttr(ordinals));
          }
        }
      }
    }

    if constexpr (std::same_as<T, slang::ast::ClassPropertySymbol>) {
      SET_OP_ATTR(MemberVisibility,
                  slangir::VisibilityAttr::get(builder.getContext(),
                                               convertEnum(node.visibility)));
    } else if constexpr (std::same_as<T, slang::ast::FieldSymbol>) {
      SET_OP_ATTR(BitOffset, builder.getI64IntegerAttr(node.bitOffset));
      SET_OP_ATTR(FieldIndex, builder.getI64IntegerAttr(node.fieldIndex));
    }

    if constexpr (std::same_as<T, slang::ast::IntegerLiteral> ||
                  std::same_as<T, slang::ast::UnbasedUnsizedIntegerLiteral> ||
                  std::same_as<T, slang::ast::ParameterSymbol> ||
                  std::same_as<T, slang::ast::EnumValueSymbol> ||
                  std::same_as<T, slang::ast::SpecparamSymbol>) {
      SET_OP_ATTR(ConstantValue,
                  builder.getStringAttr(formatConstant(node.getValue())));
    } else if constexpr (std::same_as<T, slang::ast::RealLiteral>) {
      SET_OP_ATTR(ConstantValue,
                  builder.getStringAttr(formatReal(node.getValue())));
    } else if constexpr (std::same_as<T, slang::ast::TimeLiteral>) {
      SET_OP_ATTR(ConstantValue,
                  builder.getStringAttr(formatReal(node.getValue())));
      SET_OP_ATTR(TimeScale, builder.getStringAttr(node.getScale().toString()));
    } else if constexpr (std::same_as<T, slang::ast::StringLiteral>) {
      SET_OP_ATTR(ConstantValue, builder.getStringAttr(node.getValue()));
    }

    if constexpr (std::same_as<
                      T, slang::ast::StructuredAssignmentPatternExpression>) {
      SET_OP_ATTR(MemberSetterCount,
                  builder.getI64IntegerAttr(node.memberSetters.size()));
      SET_OP_ATTR(TypeSetterCount,
                  builder.getI64IntegerAttr(node.typeSetters.size()));
      SET_OP_ATTR(IndexSetterCount,
                  builder.getI64IntegerAttr(node.indexSetters.size()));
      SET_OP_ATTR(HasDefaultSetter,
                  builder.getBoolAttr(node.defaultSetter != nullptr));
    }

    if constexpr (std::same_as<T, slang::ast::NamedValueExpression> ||
                  std::same_as<T, slang::ast::HierarchicalValueExpression>) {
      setReferencedSymbol<Op>(attrs, node.symbol);
    } else if constexpr (std::same_as<T,
                                      slang::ast::ArbitrarySymbolExpression>) {
      setReferencedSymbol<Op>(attrs, *node.symbol);
    } else if constexpr (std::same_as<T, slang::ast::MemberAccessExpression> ||
                         std::same_as<T, slang::ast::TaggedPattern>) {
      setReferencedSymbol<Op>(attrs, node.member);
      const auto &field = node.member.template as<slang::ast::FieldSymbol>();
      attrs.set("field_ordinal", builder.getI64IntegerAttr(field.fieldIndex));
      attrs.set("packed_offset", builder.getI64IntegerAttr(field.bitOffset));
    } else if constexpr (std::same_as<T, slang::ast::TaggedUnionExpression>) {
      const auto &field = node.member.template as<slang::ast::FieldSymbol>();
      attrs.set("field_ordinal", builder.getI64IntegerAttr(field.fieldIndex));
      attrs.set("packed_offset", builder.getI64IntegerAttr(field.bitOffset));
    } else if constexpr (std::same_as<T, slang::ast::VariablePattern>) {
      setReferencedSymbol<Op>(attrs, node.variable);
    } else if constexpr (std::same_as<T, slang::ast::VariableDeclStatement>) {
      setReferencedSymbol<Op>(attrs, node.symbol);
    } else if constexpr (std::same_as<T, slang::ast::CallExpression>) {
      SET_OP_ATTR(CalleeName, builder.getStringAttr(node.getSubroutineName()));
      SET_OP_ATTR(IsSystemCall, builder.getBoolAttr(node.isSystemCall()));
      SET_OP_ATTR(SubroutineKind, slangir::SubroutineKindAttr::get(
                                      builder.getContext(),
                                      convertEnum(node.getSubroutineKind())));
      SET_OP_ATTR(ArgumentCount,
                  builder.getI64IntegerAttr(node.arguments().size()));
      SET_OP_ATTR(HasThisClass,
                  builder.getBoolAttr(node.thisClass() != nullptr));
      bool isSuperClass = false;
      slang::SourceRange callRange = getSourceRange(node);
      if (callRange.start().valid() && callRange.end().valid() &&
          callRange.start().buffer() == callRange.end().buffer()) {
        std::string_view source =
            sourceManager.getSourceText(callRange.start().buffer());
        size_t begin = callRange.start().offset();
        size_t end = callRange.end().offset();
        if (begin <= end && end < source.size()) {
          std::string_view spelling = source.substr(begin, end - begin + 1);
          while (!spelling.empty() &&
                 std::isspace(static_cast<unsigned char>(spelling.front())))
            spelling.remove_prefix(1);
          isSuperClass =
              spelling.size() >= 6 && spelling.substr(0, 6) == "super.";
        }
      }
      SET_OP_ATTR(IsSuperClass, builder.getBoolAttr(isSuperClass));
      SET_OP_ATTR(HasOutputArguments,
                  builder.getBoolAttr(node.hasOutputArgs()));
      SET_OP_ATTR(HasIteratorExpression, builder.getBoolAttr(false));
      SET_OP_ATTR(HasInlineConstraints, builder.getBoolAttr(false));
      SET_OP_ATTR(ConstraintRestrictions, builder.getArrayAttr({}));
      SmallVector<int64_t> defaultedArguments;
      defaultedArguments.reserve(node.arguments().size());
      const slang::ast::SubroutineSymbol *calledSubroutine = nullptr;
      if (const auto *subroutine =
              std::get_if<const slang::ast::SubroutineSymbol *>(
                  &node.subroutine))
        calledSubroutine = *subroutine;
      auto formalArguments =
          calledSubroutine ? calledSubroutine->getArguments()
                           : std::span<
                                 const slang::ast::FormalArgumentSymbol *const>();
      for (auto [index, argument] : llvm::enumerate(node.arguments())) {
        bool isDefaulted =
            index < formalArguments.size() &&
            formalArguments[index]->getDefaultValue() == argument;
        defaultedArguments.push_back(isDefaulted);
      }
      SET_OP_ATTR(DefaultedArguments,
                  builder.getDenseI64ArrayAttr(defaultedArguments));
      if (const auto *subroutine =
              std::get_if<const slang::ast::SubroutineSymbol *>(
                  &node.subroutine);
          subroutine && *subroutine)
        setReferencedSymbol<Op>(attrs, **subroutine);
      if (const auto *system =
              std::get_if<slang::ast::CallExpression::SystemCallInfo>(
                  &node.subroutine)) {
        const slang::ast::Symbol &scope = system->scope->asSymbol();
        setSymbolReference(attrs, scope,
                           Op::getSystemScopeSymbolAttrName(operationName),
                           Op::getSystemScopePathAttrName(operationName));
        std::string libraryCell;
        if (const auto *library = scope.getSourceLibrary()) {
          libraryCell += library->name;
          libraryCell.push_back('.');
        }
        if (const auto *definition = scope.getDeclaringDefinition())
          libraryCell += definition->name;
        else
          libraryCell += "$unit";
        attrs.set(Op::getSystemLibraryCellAttrName(operationName),
                  builder.getStringAttr(libraryCell));
        if (const auto *iterator =
                std::get_if<slang::ast::CallExpression::IteratorCallInfo>(
                    &system->extraInfo)) {
          SET_OP_ATTR(HasIteratorExpression,
                      builder.getBoolAttr(iterator->iterExpr != nullptr));
          if (iterator->iterVar)
            setSymbolReference(
                attrs, *iterator->iterVar,
                Op::getIteratorVariableSymbolAttrName(operationName),
                Op::getIteratorVariablePathAttrName(operationName));
        } else if (const auto *randomize = std::get_if<
                       slang::ast::CallExpression::RandomizeCallInfo>(
                       &system->extraInfo)) {
          SET_OP_ATTR(
              HasInlineConstraints,
              builder.getBoolAttr(randomize->inlineConstraints != nullptr));
          SmallVector<Attribute> restrictions;
          for (StringRef restriction : randomize->constraintRestrictions)
            restrictions.push_back(builder.getStringAttr(restriction));
          SET_OP_ATTR(ConstraintRestrictions,
                      builder.getArrayAttr(restrictions));
        }
      }
    } else if constexpr (std::same_as<T, slang::ast::GenerateBlockSymbol>) {
      attrs.set("is_uninstantiated",
                builder.getBoolAttr(node.isUninstantiated));
    } else if constexpr (std::same_as<T, slang::ast::InstanceSymbol>) {
      setReferencedSymbol<Op>(attrs, node.getDefinition());
      SET_OP_ATTR(IsUninstantiated,
                  builder.getBoolAttr(node.body.flags.has(
                      slang::ast::InstanceFlags::Uninstantiated)));
    } else if constexpr (std::same_as<T, slang::ast::ContinuousAssignSymbol>) {
      auto [strength0, strength1] = node.getDriveStrength();
      if (strength0 || strength1) {
        std::string spelling;
        if (strength0)
          spelling += slang::ast::toString(*strength0);
        spelling += ',';
        if (strength1)
          spelling += slang::ast::toString(*strength1);
        SET_OP_ATTR(UnsupportedStrength, builder.getStringAttr(spelling));
      }
      if (const slang::ast::TimingControl *delay = node.getDelay()) {
        slang::SourceRange range = getSourceRange(*delay);
        if (range.start().valid() && range.end().valid() &&
            range.start().buffer() == range.end().buffer()) {
          std::string_view buffer =
              sourceManager.getSourceText(range.start().buffer());
          size_t begin = range.start().offset();
          size_t end = range.end().offset();
          if (begin <= end && end < buffer.size())
            SET_OP_ATTR(UnsupportedDelay, builder.getStringAttr(buffer.substr(
                                              begin, end - begin + 1)));
        }
      }
    } else if constexpr (std::same_as<T, slang::ast::NetSymbol>) {
      SET_OP_ATTR(NetKind,
                  slangir::NetKindAttr::get(builder.getContext(),
                                            convertEnum(node.netType.netKind)));
      SET_OP_ATTR(IsImplicit, builder.getBoolAttr(node.isImplicit));
      auto [strength0, strength1] = node.getDriveStrength();
      if (std::optional<slang::ast::ChargeStrength> charge =
              node.getChargeStrength()) {
        SET_OP_ATTR(UnsupportedStrength,
                    builder.getStringAttr(slang::ast::toString(*charge)));
      } else if (strength0 || strength1) {
        std::string spelling;
        if (strength0)
          spelling += slang::ast::toString(*strength0);
        spelling += ',';
        if (strength1)
          spelling += slang::ast::toString(*strength1);
        SET_OP_ATTR(UnsupportedStrength, builder.getStringAttr(spelling));
      }
      if (const slang::ast::TimingControl *delay = node.getDelay()) {
        slang::SourceRange range = getSourceRange(*delay);
        if (range.start().valid() && range.end().valid() &&
            range.start().buffer() == range.end().buffer()) {
          std::string_view buffer =
              sourceManager.getSourceText(range.start().buffer());
          size_t begin = range.start().offset();
          size_t end = range.end().offset();
          if (begin <= end && end < buffer.size())
            SET_OP_ATTR(UnsupportedDelay, builder.getStringAttr(buffer.substr(
                                              begin, end - begin + 1)));
        }
      }
    }

    if constexpr (std::same_as<T, slang::ast::UnaryExpression>) {
      SET_OP_ATTR(OperatorKind,
                  slangir::UnaryOperatorAttr::get(builder.getContext(),
                                                  convertEnum(node.op)));
    } else if constexpr (std::same_as<T, slang::ast::BinaryExpression>) {
      SET_OP_ATTR(OperatorKind,
                  slangir::BinaryOperatorAttr::get(builder.getContext(),
                                                   convertEnum(node.op)));
    } else if constexpr (std::same_as<T, slang::ast::AssignmentExpression>) {
      if (node.op)
        SET_OP_ATTR(OperatorKind,
                    slangir::BinaryOperatorAttr::get(builder.getContext(),
                                                     convertEnum(*node.op)));
      SET_OP_ATTR(AssignmentKind, slangir::AssignmentKindAttr::get(
                                      builder.getContext(),
                                      node.isNonBlocking()
                                          ? slangir::AssignmentKind::Nonblocking
                                          : slangir::AssignmentKind::Blocking));
      SET_OP_ATTR(HasTimingControl,
                  builder.getBoolAttr(node.timingControl != nullptr));
    } else if constexpr (std::same_as<T, slang::ast::BlockStatement>) {
      SET_OP_ATTR(BlockKind,
                  slangir::StatementBlockKindAttr::get(
                      builder.getContext(), convertEnum(node.blockKind)));
      if (node.blockSymbol && !node.blockSymbol->name.empty())
        setSymbolReference(attrs, *node.blockSymbol,
                           Op::getBlockSymbolAttrName(operationName),
                           Op::getBlockPathAttrName(operationName));
    } else if constexpr (std::same_as<T, slang::ast::DisableStatement>) {
      if (const slang::ast::Symbol *target = node.target.getSymbolReference())
        setSymbolReference(attrs, *target,
                           Op::getTargetSymbolAttrName(operationName),
                           Op::getTargetPathAttrName(operationName));
      const auto &target =
          node.target.template as<slang::ast::ArbitrarySymbolExpression>();
      SET_OP_ATTR(IsHierarchical,
                  builder.getBoolAttr(target.hierRef.target != nullptr));
    } else if constexpr (std::same_as<T, slang::ast::WaitOrderStatement>) {
      SET_OP_ATTR(EventCount, builder.getI64IntegerAttr(node.events.size()));
      SET_OP_ATTR(HasSuccessAction,
                  builder.getBoolAttr(node.ifTrue != nullptr));
      SET_OP_ATTR(HasFailureAction,
                  builder.getBoolAttr(node.ifFalse != nullptr));
    } else if constexpr (std::same_as<T, slang::ast::EventTriggerStatement>) {
      SET_OP_ATTR(IsNonblocking, builder.getBoolAttr(node.isNonBlocking));
      SET_OP_ATTR(HasTimingControl,
                  builder.getBoolAttr(node.timing != nullptr));
    } else if constexpr (std::same_as<T, slang::ast::RangeSelectExpression>) {
      SET_OP_ATTR(SelectionKind, slangir::RangeSelectionKindAttr::get(
                                     builder.getContext(),
                                     convertEnum(node.getSelectionKind())));
    } else if constexpr (std::same_as<T, slang::ast::NewClassExpression>) {
      SET_OP_ATTR(IsSuperClass, builder.getBoolAttr(node.isSuperClass));
    }

    if constexpr (std::same_as<T, slang::ast::PortSymbol> ||
                  std::same_as<T, slang::ast::MultiPortSymbol> ||
                  std::same_as<T, slang::ast::FormalArgumentSymbol>) {
      SET_OP_ATTR(Direction,
                  slangir::ArgumentDirectionAttr::get(
                      builder.getContext(), convertEnum(node.direction)));
    } else if constexpr (std::same_as<T, slang::ast::DefinitionSymbol>) {
      SET_OP_ATTR(DefinitionKind,
                  slangir::DefinitionKindAttr::get(
                      builder.getContext(), convertEnum(node.definitionKind)));
    } else if constexpr (std::same_as<T, slang::ast::ProceduralBlockSymbol>) {
      SET_OP_ATTR(ProcedureKind,
                  slangir::ProceduralBlockKindAttr::get(
                      builder.getContext(), convertEnum(node.procedureKind)));
    } else if constexpr (std::same_as<T, slang::ast::StatementBlockSymbol>) {
      SET_OP_ATTR(BlockKind,
                  slangir::StatementBlockKindAttr::get(
                      builder.getContext(), convertEnum(node.blockKind)));
    } else if constexpr (std::same_as<T, slang::ast::SubroutineSymbol> ||
                         std::same_as<T, slang::ast::MethodPrototypeSymbol>) {
      SET_OP_ATTR(SubroutineKind,
                  slangir::SubroutineKindAttr::get(
                      builder.getContext(), convertEnum(node.subroutineKind)));
    }

    if constexpr (std::same_as<T, slang::ast::SubroutineSymbol> ||
                  std::same_as<T, slang::ast::MethodPrototypeSymbol>) {
      using MF = slang::ast::MethodFlags;
      SET_OP_ATTR(MemberVisibility,
                  slangir::VisibilityAttr::get(builder.getContext(),
                                               convertEnum(node.visibility)));
      if (node.isVirtual())
        SET_OP_ATTR(IsVirtual, builder.getUnitAttr());
      if (node.flags.has(MF::Virtual))
        SET_OP_ATTR(IsDeclaredVirtual, builder.getUnitAttr());
      if (node.flags.has(MF::Pure))
        SET_OP_ATTR(IsPure, builder.getUnitAttr());
      if (node.flags.has(MF::Static))
        SET_OP_ATTR(IsStatic, builder.getUnitAttr());
      if (node.flags.has(MF::Constructor))
        SET_OP_ATTR(IsConstructor, builder.getUnitAttr());
      if (node.flags.has(MF::InterfaceExtern))
        SET_OP_ATTR(IsInterfaceExtern, builder.getUnitAttr());
      if (node.flags.has(MF::ModportImport))
        SET_OP_ATTR(IsModportImport, builder.getUnitAttr());
      if (node.flags.has(MF::ModportExport))
        SET_OP_ATTR(IsModportExport, builder.getUnitAttr());
      if (node.flags.has(MF::DPIImport))
        SET_OP_ATTR(IsDpiImport, builder.getUnitAttr());
      if (node.flags.has(MF::DPIImport)) {
        StringRef cIdentifier = node.name;
        if (const auto *syntax = node.getSyntax()) {
          const auto &dpi =
              syntax->template as<slang::syntax::DPIImportSyntax>();
          if (!dpi.c_identifier.valueText().empty())
            cIdentifier = dpi.c_identifier.valueText();
        }
        SET_OP_ATTR(DpiCIdentifier, builder.getStringAttr(cIdentifier));
      }
      if (node.flags.has(MF::DPIContext))
        SET_OP_ATTR(IsDpiContext, builder.getUnitAttr());
      if (node.flags.has(MF::BuiltIn))
        SET_OP_ATTR(IsBuiltin, builder.getUnitAttr());
      if (node.flags.has(MF::ForkJoin))
        SET_OP_ATTR(IsForkJoin, builder.getUnitAttr());
      if (node.flags.has(MF::DefaultedSuperArg))
        SET_OP_ATTR(HasDefaultedSuperArg, builder.getUnitAttr());
      if (node.flags.has(MF::Initial))
        SET_OP_ATTR(IsInitial, builder.getUnitAttr());
      if (node.flags.has(MF::Extends))
        SET_OP_ATTR(IsExtends, builder.getUnitAttr());
      if (node.flags.has(MF::Final))
        SET_OP_ATTR(IsFinal, builder.getUnitAttr());
    }

    if constexpr (std::same_as<T, slang::ast::SubroutineSymbol>) {
      using MF = slang::ast::MethodFlags;
      SET_OP_ATTR(DefaultLifetime,
                  slangir::VariableLifetimeAttr::get(
                      builder.getContext(), convertEnum(node.defaultLifetime)));
      if (node.flags.has(MF::Randomize))
        SET_OP_ATTR(IsRandomize, builder.getUnitAttr());
      if (node.flags.has(MF::PrePostRandomize))
        SET_OP_ATTR(IsPrePostRandomize, builder.getUnitAttr());
      SET_OP_ATTR(OutOfBlockIndex,
                  builder.getI64IntegerAttr(
                      static_cast<uint32_t>(node.outOfBlockIndex)));
      if (const auto *overridden = node.getOverride())
        setSymbolReference(attrs, *overridden,
                           Op::getOverrideSymbolAttrName(operationName),
                           Op::getOverridePathAttrName(operationName));
      if (const auto *prototype = node.getPrototype())
        setSymbolReference(attrs, *prototype,
                           Op::getPrototypeSymbolAttrName(operationName),
                           Op::getPrototypePathAttrName(operationName));
      if (node.returnValVar)
        setSymbolReference(attrs, *node.returnValVar,
                           Op::getReturnVariableSymbolAttrName(operationName),
                           Op::getReturnVariablePathAttrName(operationName));
      if (node.thisVar)
        setSymbolReference(attrs, *node.thisVar,
                           Op::getThisVariableSymbolAttrName(operationName),
                           Op::getThisVariablePathAttrName(operationName));
    } else if constexpr (std::same_as<T, slang::ast::MethodPrototypeSymbol>) {
      if (const auto *subroutine = node.getSubroutine())
        setSymbolReference(attrs, *subroutine,
                           Op::getSubroutineSymbolAttrName(operationName),
                           Op::getSubroutinePathAttrName(operationName));
      if (const auto *overridden = node.getOverride())
        setSymbolReference(attrs, *overridden,
                           Op::getOverrideSymbolAttrName(operationName),
                           Op::getOverridePathAttrName(operationName));
      SmallVector<Attribute> implementationSymbols;
      SmallVector<Attribute> implementationPaths;
      for (const auto *implementation = node.getFirstExternImpl();
           implementation; implementation = implementation->getNextImpl()) {
        const slang::ast::SubroutineSymbol &symbol = *implementation->impl;
        implementationSymbols.push_back(getSemanticSymbolReference(symbol));
        implementationPaths.push_back(
            builder.getStringAttr(getSymbolPath(symbol)));
      }
      SET_OP_ATTR(ExternImplementationCount,
                  builder.getI64IntegerAttr(implementationSymbols.size()));
      SET_OP_ATTR(ExternImplementationSymbols,
                  builder.getArrayAttr(implementationSymbols));
      SET_OP_ATTR(ExternImplementationPaths,
                  builder.getArrayAttr(implementationPaths));
    }

    if constexpr (std::same_as<T, slang::ast::ConstraintBlockSymbol>) {
      using CF = slang::ast::ConstraintBlockFlags;
      if (node.flags.has(CF::Pure))
        SET_OP_ATTR(IsPure, builder.getUnitAttr());
      if (node.flags.has(CF::Static))
        SET_OP_ATTR(IsStatic, builder.getUnitAttr());
      if (node.flags.has(CF::Extern))
        SET_OP_ATTR(IsExtern, builder.getUnitAttr());
      if (node.flags.has(CF::ExplicitExtern))
        SET_OP_ATTR(IsExplicitExtern, builder.getUnitAttr());
      if (node.flags.has(CF::Initial))
        SET_OP_ATTR(IsInitial, builder.getUnitAttr());
      if (node.flags.has(CF::Extends))
        SET_OP_ATTR(IsExtends, builder.getUnitAttr());
      if (node.flags.has(CF::Final))
        SET_OP_ATTR(IsFinal, builder.getUnitAttr());
      SET_OP_ATTR(OutOfBlockIndex,
                  builder.getI64IntegerAttr(
                      static_cast<uint32_t>(node.getOutOfBlockIndex())));
      if (node.thisVar)
        setSymbolReference(attrs, *node.thisVar,
                           Op::getThisVariableSymbolAttrName(operationName),
                           Op::getThisVariablePathAttrName(operationName));
    }

    if constexpr (std::same_as<T, slang::ast::FormalArgumentSymbol>) {
      if (const auto *mergedVariable = node.getMergedVariable())
        setSymbolReference(attrs, *mergedVariable,
                           Op::getMergedVariableSymbolAttrName(operationName),
                           Op::getMergedVariablePathAttrName(operationName));
    } else if constexpr (std::same_as<T, slang::ast::IteratorSymbol>) {
      SET_OP_ATTR(ArrayType,
                  TypeAttr::get(typeConverter.convert(node.arrayType)));
      SET_OP_ATTR(IndexMethodName, builder.getStringAttr(node.indexMethodName));
    } else if constexpr (std::same_as<T, slang::ast::ClockVarSymbol>) {
      SET_OP_ATTR(Direction,
                  slangir::ArgumentDirectionAttr::get(
                      builder.getContext(), convertEnum(node.direction)));
      SET_OP_ATTR(InputEdge,
                  slangir::EdgeKindAttr::get(builder.getContext(),
                                             convertEnum(node.inputSkew.edge)));
      SET_OP_ATTR(OutputEdge,
                  slangir::EdgeKindAttr::get(
                      builder.getContext(), convertEnum(node.outputSkew.edge)));
      SET_OP_ATTR(HasInputDelay,
                  builder.getBoolAttr(node.inputSkew.delay != nullptr));
      SET_OP_ATTR(HasOutputDelay,
                  builder.getBoolAttr(node.outputSkew.delay != nullptr));
    } else if constexpr (std::same_as<T, slang::ast::LocalAssertionVarSymbol>) {
      if (node.formalPort)
        setSymbolReference(attrs, *node.formalPort,
                           Op::getFormalPortSymbolAttrName(operationName),
                           Op::getFormalPortPathAttrName(operationName));
    } else if constexpr (std::same_as<T, slang::ast::GenericClassDefSymbol>) {
      SET_OP_ATTR(IsInterface, builder.getBoolAttr(node.isInterface));
      SET_OP_ATTR(SpecializationCount,
                  builder.getI64IntegerAttr(node.numSpecializations()));
      if (const auto *forwardDeclaration = node.getFirstForwardDecl())
        setSymbolReference(
            attrs, *forwardDeclaration,
            Op::getFirstForwardDeclarationSymbolAttrName(operationName),
            Op::getFirstForwardDeclarationPathAttrName(operationName));
    }

    if constexpr (std::same_as<T, slang::ast::SubroutineSymbol> ||
                  std::same_as<T, slang::ast::MethodPrototypeSymbol>) {
      SmallVector<Type> inputs;
      for (const auto *argument : node.getArguments())
        inputs.push_back(typeConverter.convert(argument->getType()));
      SmallVector<Type> results;
      bool isTask = node.subroutineKind == slang::ast::SubroutineKind::Task;
      if (!isTask)
        results.push_back(typeConverter.convert(node.getReturnType()));
      auto signature = FunctionType::get(builder.getContext(), inputs, results);
      SET_OP_ATTR(SemanticType, TypeAttr::get(slangir::SubroutineType::get(
                                    builder.getContext(), signature, isTask)));
    }

    if constexpr (std::same_as<T, slang::ast::ClassType>) {
      SET_OP_ATTR(IsAbstract, builder.getBoolAttr(node.isAbstract));
      SET_OP_ATTR(IsInterface, builder.getBoolAttr(node.isInterface));
      SET_OP_ATTR(IsFinal, builder.getBoolAttr(node.isFinal));
      SET_OP_ATTR(IsUninstantiated, builder.getBoolAttr(node.isUninstantiated));
      if (const slang::ast::Type *base = node.getBaseClass())
        SET_OP_ATTR(BaseClass, TypeAttr::get(typeConverter.convert(*base)));

      SmallVector<Attribute> interfaces;
      for (const slang::ast::Type *interface : node.getImplementedInterfaces())
        interfaces.push_back(TypeAttr::get(typeConverter.convert(*interface)));
      SET_OP_ATTR(ImplementedInterfaces, builder.getArrayAttr(interfaces));

      SmallVector<Attribute> declaredInterfaces;
      for (const slang::ast::Type *interface : node.getDeclaredInterfaces())
        declaredInterfaces.push_back(
            TypeAttr::get(typeConverter.convert(*interface)));
      SET_OP_ATTR(DeclaredInterfaces, builder.getArrayAttr(declaredInterfaces));

      if (node.genericClass)
        setSymbolReference(attrs, *node.genericClass,
                           Op::getGenericClassSymbolAttrName(operationName),
                           Op::getGenericClassPathAttrName(operationName));
      SmallVector<Attribute> parameterSymbols;
      SmallVector<Attribute> parameterPaths;
      for (const slang::ast::Symbol *parameter : node.genericParameters) {
        parameterSymbols.push_back(getSemanticSymbolReference(*parameter));
        parameterPaths.push_back(
            builder.getStringAttr(getSymbolPath(*parameter)));
      }
      SET_OP_ATTR(GenericParameterSymbols,
                  builder.getArrayAttr(parameterSymbols));
      SET_OP_ATTR(GenericParameterPaths, builder.getArrayAttr(parameterPaths));

      if (node.thisVar)
        setSymbolReference(attrs, *node.thisVar,
                           Op::getThisVariableSymbolAttrName(operationName),
                           Op::getThisVariablePathAttrName(operationName));
      if (const auto *constructor = node.getConstructor())
        setSymbolReference(attrs, *constructor,
                           Op::getConstructorSymbolAttrName(operationName),
                           Op::getConstructorPathAttrName(operationName));
      SET_OP_ATTR(
          HasBaseConstructorCall,
          builder.getBoolAttr(node.getBaseConstructorCall() != nullptr));
      SET_OP_ATTR(BitstreamWidth,
                  builder.getI64IntegerAttr(node.getBitstreamWidth()));
      SET_OP_ATTR(HasCycles, builder.getBoolAttr(node.hasCycles()));
    } else if constexpr (std::same_as<T, slang::ast::NetType>) {
      SET_OP_ATTR(NetKind,
                  slangir::NetKindAttr::get(builder.getContext(),
                                            convertEnum(node.netKind)));
      SET_OP_ATTR(DataType,
                  TypeAttr::get(typeConverter.convert(node.getDataType())));
      SET_OP_ATTR(IsBuiltin, builder.getBoolAttr(node.isBuiltIn()));
      if (const auto *resolutionFunction = node.getResolutionFunction())
        setSymbolReference(
            attrs, *resolutionFunction,
            Op::getResolutionFunctionSymbolAttrName(operationName),
            Op::getResolutionFunctionPathAttrName(operationName));
    } else if constexpr (std::same_as<T, slang::ast::CovergroupType>) {
      if (const slang::ast::Type *base = node.getBaseGroup())
        SET_OP_ATTR(BaseGroup, TypeAttr::get(typeConverter.convert(*base)));
      SET_OP_ATTR(ConstructorArgumentCount,
                  builder.getI64IntegerAttr(node.getArguments().size()));
      uint64_t sampleFormals = 0;
      for (const auto &formal :
           node.template membersOfType<slang::ast::FormalArgumentSymbol>())
        sampleFormals += formal.flags.has(
            slang::ast::VariableFlags::CoverageSampleFormal);
      SET_OP_ATTR(SampleFormalCount,
                  builder.getI64IntegerAttr(sampleFormals));
      SET_OP_ATTR(HasCoverageEvent,
                  builder.getBoolAttr(node.getCoverageEvent() != nullptr));
    } else if constexpr (std::same_as<T,
                                      slang::ast::CovergroupBodySymbol>) {
      SET_OP_ATTR(OptionCount,
                  builder.getI64IntegerAttr(node.options.size()));
    } else if constexpr (std::same_as<T, slang::ast::CoverpointSymbol>) {
      SET_OP_ATTR(HasIff, builder.getBoolAttr(node.getIffExpr() != nullptr));
      SET_OP_ATTR(OptionCount,
                  builder.getI64IntegerAttr(node.options.size()));
    } else if constexpr (std::same_as<T, slang::ast::CoverageBinSymbol>) {
      SET_OP_ATTR(BinsKind,
                  slangir::CoverageBinKindAttr::get(
                      builder.getContext(), convertEnum(node.binsKind)));
      SET_OP_ATTR(IsArray, builder.getBoolAttr(node.isArray));
      SET_OP_ATTR(IsWildcard, builder.getBoolAttr(node.isWildcard));
      SET_OP_ATTR(IsDefault, builder.getBoolAttr(node.isDefault));
      SET_OP_ATTR(IsDefaultSequence,
                  builder.getBoolAttr(node.isDefaultSequence));
      SET_OP_ATTR(HasIff, builder.getBoolAttr(node.getIffExpr() != nullptr));
      SET_OP_ATTR(HasNumberOfBins,
                  builder.getBoolAttr(node.getNumberOfBinsExpr() != nullptr));
      SET_OP_ATTR(HasSetCoverage,
                  builder.getBoolAttr(node.getSetCoverageExpr() != nullptr));
      SET_OP_ATTR(HasWith,
                  builder.getBoolAttr(node.getWithExpr() != nullptr));
      SET_OP_ATTR(ValueCount,
                  builder.getI64IntegerAttr(node.getValues().size()));
      SET_OP_ATTR(TransitionSetCount,
                  builder.getI64IntegerAttr(node.getTransList().size()));
    } else if constexpr (std::same_as<T,
                                      slang::ast::NewCovergroupExpression>) {
      SET_OP_ATTR(ArgumentCount,
                  builder.getI64IntegerAttr(node.arguments.size()));
    } else if constexpr (std::same_as<T, slang::ast::ConditionalStatement>) {
      SET_OP_ATTR(CheckKind,
                  slangir::UniquePriorityCheckAttr::get(
                      builder.getContext(), convertEnum(node.check)));
      SET_OP_ATTR(ConditionCount,
                  builder.getI64IntegerAttr(node.conditions.size()));
      SmallVector<int64_t> patternFlags;
      patternFlags.reserve(node.conditions.size());
      for (const auto &condition : node.conditions)
        patternFlags.push_back(condition.pattern != nullptr);
      SET_OP_ATTR(ConditionPatternFlags,
                  builder.getDenseI64ArrayAttr(patternFlags));
      SET_OP_ATTR(HasElse, builder.getBoolAttr(node.ifFalse != nullptr));
    } else if constexpr (std::same_as<T,
                                      slang::ast::ConditionalExpression>) {
      SET_OP_ATTR(ConditionCount,
                  builder.getI64IntegerAttr(node.conditions.size()));
      SmallVector<int64_t> patternFlags;
      patternFlags.reserve(node.conditions.size());
      for (const auto &condition : node.conditions)
        patternFlags.push_back(condition.pattern != nullptr);
      SET_OP_ATTR(ConditionPatternFlags,
                  builder.getDenseI64ArrayAttr(patternFlags));
    } else if constexpr (std::same_as<T, slang::ast::ForLoopStatement>) {
      SET_OP_ATTR(InitializerCount,
                  builder.getI64IntegerAttr(node.initializers.size()));
      SET_OP_ATTR(HasCondition, builder.getBoolAttr(node.stopExpr != nullptr));
      SET_OP_ATTR(StepCount, builder.getI64IntegerAttr(node.steps.size()));
    } else if constexpr (std::same_as<T, slang::ast::CaseStatement>) {
      SET_OP_ATTR(ConditionKind,
                  slangir::CaseConditionAttr::get(builder.getContext(),
                                                  convertEnum(node.condition)));
      SET_OP_ATTR(CheckKind,
                  slangir::UniquePriorityCheckAttr::get(
                      builder.getContext(), convertEnum(node.check)));
      SET_OP_ATTR(ItemCount, builder.getI64IntegerAttr(node.items.size()));
      SmallVector<int64_t> itemLabelCounts;
      itemLabelCounts.reserve(node.items.size());
      for (const auto &item : node.items)
        itemLabelCounts.push_back(item.expressions.size());
      SET_OP_ATTR(ItemLabelCounts,
                  builder.getDenseI64ArrayAttr(itemLabelCounts));
      SET_OP_ATTR(HasDefault, builder.getBoolAttr(node.defaultCase != nullptr));
    } else if constexpr (std::same_as<T, slang::ast::PatternCaseStatement>) {
      SET_OP_ATTR(ConditionKind,
                  slangir::CaseConditionAttr::get(builder.getContext(),
                                                  convertEnum(node.condition)));
      SET_OP_ATTR(CheckKind,
                  slangir::UniquePriorityCheckAttr::get(
                      builder.getContext(), convertEnum(node.check)));
      SET_OP_ATTR(ItemCount, builder.getI64IntegerAttr(node.items.size()));
      SmallVector<int64_t> filterFlags;
      filterFlags.reserve(node.items.size());
      for (const auto &item : node.items)
        filterFlags.push_back(item.filter != nullptr);
      SET_OP_ATTR(ItemFilterFlags, builder.getDenseI64ArrayAttr(filterFlags));
      SET_OP_ATTR(HasDefault, builder.getBoolAttr(node.defaultCase != nullptr));
    } else if constexpr (std::same_as<T, slang::ast::InsideExpression>) {
      SET_OP_ATTR(ItemCount,
                  builder.getI64IntegerAttr(node.rangeList().size()));
    } else if constexpr (std::same_as<T, slang::ast::ValueRangeExpression>) {
      SET_OP_ATTR(RangeKind, slangir::ValueRangeKindAttr::get(
                                 builder.getContext(),
                                 static_cast<slangir::ValueRangeKind>(
                                     static_cast<int>(node.rangeKind))));
    } else if constexpr (std::same_as<T, slang::ast::StructurePattern>) {
      SmallVector<int64_t> ordinals;
      ordinals.reserve(node.patterns.size());
      for (const auto &pattern : node.patterns)
        ordinals.push_back(pattern.field->fieldIndex);
      SET_OP_ATTR(FieldOrdinals, builder.getDenseI64ArrayAttr(ordinals));
    } else if constexpr (std::same_as<
                             T, slang::ast::ImmediateAssertionStatement>) {
      SET_OP_ATTR(AssertionKind,
                  slangir::AssertionKindAttr::get(
                      builder.getContext(), convertEnum(node.assertionKind)));
      SET_OP_ATTR(IsDeferred, builder.getBoolAttr(node.isDeferred));
      SET_OP_ATTR(IsFinal, builder.getBoolAttr(node.isFinal));
      SET_OP_ATTR(HasPassAction, builder.getBoolAttr(node.ifTrue != nullptr));
      SET_OP_ATTR(HasFailAction, builder.getBoolAttr(node.ifFalse != nullptr));
    } else if constexpr (std::same_as<
                             T, slang::ast::ConcurrentAssertionStatement>) {
      SET_OP_ATTR(AssertionKind,
                  slangir::AssertionKindAttr::get(
                      builder.getContext(), convertEnum(node.assertionKind)));
      SET_OP_ATTR(HasPassAction, builder.getBoolAttr(node.ifTrue != nullptr));
      SET_OP_ATTR(HasFailAction, builder.getBoolAttr(node.ifFalse != nullptr));
    } else if constexpr (std::same_as<T,
                                      slang::ast::ProceduralAssignStatement>) {
      SET_OP_ATTR(IsForce, builder.getBoolAttr(node.isForce));
    } else if constexpr (std::same_as<
                             T, slang::ast::ProceduralDeassignStatement>) {
      SET_OP_ATTR(IsRelease, builder.getBoolAttr(node.isRelease));
    } else if constexpr (std::same_as<T, slang::ast::Delay3Control>) {
      int64_t delayCount = node.expr3 ? 3 : node.expr2 ? 2 : 1;
      SET_OP_ATTR(DelayCount, builder.getI64IntegerAttr(delayCount));
    } else if constexpr (std::same_as<T, slang::ast::SignalEventControl>) {
      SET_OP_ATTR(EdgeKind, slangir::EdgeKindAttr::get(builder.getContext(),
                                                       convertEnum(node.edge)));
      SET_OP_ATTR(HasIff, builder.getBoolAttr(node.iffCondition != nullptr));
    } else if constexpr (std::same_as<T, slang::ast::EventListControl>) {
      SET_OP_ATTR(EventCount, builder.getI64IntegerAttr(node.events.size()));
    } else if constexpr (std::same_as<T, slang::ast::BlockEventListControl>) {
      SmallVector<Attribute> isBegin;
      for (const auto &event : node.events)
        isBegin.push_back(builder.getBoolAttr(event.isBegin));
      SET_OP_ATTR(EventIsBegin, builder.getArrayAttr(isBegin));
    }

    if constexpr (std::same_as<T, slang::ast::ConstraintList>) {
      SET_OP_ATTR(ItemCount, builder.getI64IntegerAttr(node.list.size()));
    } else if constexpr (std::same_as<T, slang::ast::ExpressionConstraint>) {
      SET_OP_ATTR(IsSoft, builder.getBoolAttr(node.isSoft));
    } else if constexpr (std::same_as<T, slang::ast::ConditionalConstraint>) {
      SET_OP_ATTR(HasElse, builder.getBoolAttr(node.elseBody != nullptr));
    } else if constexpr (std::same_as<T, slang::ast::UniquenessConstraint>) {
      SET_OP_ATTR(ItemCount, builder.getI64IntegerAttr(node.items.size()));
    } else if constexpr (std::same_as<T, slang::ast::SolveBeforeConstraint>) {
      SET_OP_ATTR(SolveCount, builder.getI64IntegerAttr(node.solve.size()));
      SET_OP_ATTR(AfterCount, builder.getI64IntegerAttr(node.after.size()));
    } else if constexpr (std::same_as<T, slang::ast::ForeachConstraint> ||
                         std::same_as<T, slang::ast::ForeachLoopStatement>) {
      SmallVector<Attribute> dimensions;
      for (const auto &dimension : node.loopDims) {
        NamedAttrList attributes;
        attributes.set(foreach_metadata::hasStaticRange,
                       builder.getBoolAttr(dimension.range.has_value()));
        if (dimension.range) {
          attributes.set(foreach_metadata::left,
                         builder.getI64IntegerAttr(dimension.range->left));
          attributes.set(foreach_metadata::right,
                         builder.getI64IntegerAttr(dimension.range->right));
        }
        attributes.set(foreach_metadata::hasIterator,
                       builder.getBoolAttr(dimension.loopVar != nullptr));
        if (dimension.loopVar) {
          attributes.set(foreach_metadata::iteratorSymbol,
                         getSemanticSymbolReference(*dimension.loopVar));
          attributes.set(
              foreach_metadata::iteratorPath,
              builder.getStringAttr(getSymbolPath(*dimension.loopVar)));
          if (std::optional<Type> iteratorType =
                  getSemanticType(*dimension.loopVar))
            attributes.set(foreach_metadata::iteratorType,
                           TypeAttr::get(*iteratorType));
        }
        dimensions.push_back(
            DictionaryAttr::get(builder.getContext(), attributes));
      }
      SET_OP_ATTR(LoopDimensions, builder.getArrayAttr(dimensions));
    }

    if constexpr (std::same_as<T, slang::ast::SimpleAssertionExpr>) {
      SET_OP_ATTR(IsNull, builder.getBoolAttr(node.isNullExpr));
      addRepetition<Op>(attrs, node.repetition);
    } else if constexpr (std::same_as<T, slang::ast::SequenceConcatExpr>) {
      SmallVector<Attribute> delays;
      for (const auto &element : node.elements) {
        NamedAttrList delay;
        delay.set("min", builder.getI64IntegerAttr(element.delay.min));
        delay.set("is_unbounded", builder.getBoolAttr(!element.delay.max));
        if (element.delay.max)
          delay.set("max", builder.getI64IntegerAttr(*element.delay.max));
        if (std::optional<TypeAttr> range = sourceRangeAttr(element.delayRange))
          delay.set("source_range", *range);
        delays.push_back(DictionaryAttr::get(builder.getContext(), delay));
      }
      SET_OP_ATTR(Delays, builder.getArrayAttr(delays));
    } else if constexpr (std::same_as<T, slang::ast::SequenceWithMatchExpr>) {
      SET_OP_ATTR(MatchItemCount,
                  builder.getI64IntegerAttr(node.matchItems.size()));
      addRepetition<Op>(attrs, node.repetition);
    } else if constexpr (std::same_as<T, slang::ast::UnaryAssertionExpr>) {
      SET_OP_ATTR(OperatorKind,
                  slangir::AssertionUnaryOperatorAttr::get(
                      builder.getContext(), convertEnum(node.op)));
      SET_OP_ATTR(HasRange, builder.getBoolAttr(node.range.has_value()));
      SET_OP_ATTR(RangeIsUnbounded,
                  builder.getBoolAttr(node.range && !node.range->max));
      if (node.range)
        addSequenceRange<Op>(attrs, *node.range);
    } else if constexpr (std::same_as<T, slang::ast::BinaryAssertionExpr>) {
      SET_OP_ATTR(OperatorKind,
                  slangir::AssertionBinaryOperatorAttr::get(
                      builder.getContext(), convertEnum(node.op)));
      if (std::optional<TypeAttr> range = sourceRangeAttr(node.opRange))
        SET_OP_ATTR(OperatorRange, *range);
    } else if constexpr (std::same_as<T, slang::ast::FirstMatchAssertionExpr>) {
      SET_OP_ATTR(MatchItemCount,
                  builder.getI64IntegerAttr(node.matchItems.size()));
    } else if constexpr (std::same_as<T, slang::ast::StrongWeakAssertionExpr>) {
      auto strength =
          node.strength == slang::ast::StrongWeakAssertionExpr::Strong
              ? slangir::AssertionStrength::Strong
              : slangir::AssertionStrength::Weak;
      SET_OP_ATTR(Strength, slangir::AssertionStrengthAttr::get(
                                builder.getContext(), strength));
    } else if constexpr (std::same_as<T, slang::ast::AbortAssertionExpr>) {
      auto action = node.action == slang::ast::AbortAssertionExpr::Accept
                        ? slangir::AssertionAbortAction::Accept
                        : slangir::AssertionAbortAction::Reject;
      SET_OP_ATTR(Action, slangir::AssertionAbortActionAttr::get(
                              builder.getContext(), action));
      SET_OP_ATTR(IsSynchronous, builder.getBoolAttr(node.isSync));
    } else if constexpr (std::same_as<T,
                                      slang::ast::ConditionalAssertionExpr>) {
      SET_OP_ATTR(HasElse, builder.getBoolAttr(node.elseExpr != nullptr));
    } else if constexpr (std::same_as<T, slang::ast::CaseAssertionExpr>) {
      SmallVector<Attribute> groupSizes;
      for (const auto &item : node.items)
        groupSizes.push_back(
            builder.getI64IntegerAttr(item.expressions.size()));
      SET_OP_ATTR(ItemGroupSizes, builder.getArrayAttr(groupSizes));
      SET_OP_ATTR(HasDefault, builder.getBoolAttr(node.defaultCase != nullptr));
    }
#undef SET_OP_ATTR
  }

  template <typename Op, typename Node> void importNode(const Node &node) {
    if constexpr (std::derived_from<Node, slang::ast::Symbol>) {
      if (emittedSymbolPaths.contains(&node))
        return;
    }
    int64_t id = nextNodeId++;
    NamedAttrList attrs;
    OperationName operationName(Op::getOperationName(), builder.getContext());
#define SET_OP_ATTR(Name, Value)                                               \
  attrs.set(Op::get##Name##AttrName(operationName), (Value))
    SET_OP_ATTR(NodeId, builder.getI64IntegerAttr(id));

    slang::SourceRange range = getSourceRange(node);
    Location location = sourceLocation(range.start());
    if (range.start().valid()) {
      if (std::optional<TypeAttr> expandedRange = sourceRangeAttr(range))
        SET_OP_ATTR(SourceRange, *expandedRange);
      if (std::optional<TypeAttr> originalRange =
              sourceRangeAttr(range, /*useOriginalLocations=*/true))
        SET_OP_ATTR(OriginalSourceRange, *originalRange);
      if (ArrayAttr macroStack = macroExpansionStack(range.start());
          !macroStack.empty())
        SET_OP_ATTR(MacroExpansionStack, macroStack);

      auto expanded = sourceManager.getFullyExpandedLoc(range.start());
      if (expanded.valid() && sourceManager.isFileLoc(expanded)) {
        SET_OP_ATTR(SourceFile,
                    builder.getStringAttr(sourceManager.getFileName(expanded)));
      }
      auto end = sourceManager.getFullyExpandedLoc(range.end());
      if (end.valid() && sourceManager.isFileLoc(end)) {
        SET_OP_ATTR(SourceEndLine,
                    builder.getI64IntegerAttr(static_cast<int64_t>(
                        sourceManager.getLineNumber(end))));
        SET_OP_ATTR(SourceEndColumn,
                    builder.getI64IntegerAttr(static_cast<int64_t>(
                        sourceManager.getColumnNumber(end))));
      }
      if (sourceManager.isMacroLoc(range.start())) {
        std::string_view macroName = sourceManager.getMacroName(range.start());
        if (!macroName.empty())
          SET_OP_ATTR(MacroName, builder.getStringAttr(macroName));
      }
    }

    if constexpr (std::derived_from<Node, slang::ast::Symbol>) {
      StringAttr symbolName = getInternalSymbolName(node);
      SET_OP_ATTR(SymName, symbolName);
      if (!node.name.empty())
        SET_OP_ATTR(Name, builder.getStringAttr(node.name));
      SET_OP_ATTR(HierarchicalName, builder.getStringAttr(getSymbolPath(node)));
    }

    if (std::optional<Type> type = getSemanticType(node)) {
      if constexpr (requires { Op::getSemanticTypeAttrName(operationName); }) {
        SET_OP_ATTR(SemanticType, TypeAttr::get(*type));
      } else {
        llvm_unreachable(
            "semantic type produced for an operation without a type field");
      }
    }
    currentPendingReferences.clear();
    addSpecificAttributes<Op>(node, attrs);
#undef SET_OP_ATTR

    Op operation = Op::create(builder, location, TypeRange{}, ValueRange{},
                              attrs.getAttrs());
    Block &body = operation.getBody().emplaceBlock();

    if constexpr (std::derived_from<Node, slang::ast::Symbol>) {
      currentSymbolPath.push_back(getInternalSymbolName(node).getValue().str());
      emittedSymbolPaths.try_emplace(&node, currentSymbolPath);
      emittedSymbolOperations.try_emplace(&node, operation);
    }
    for (const PendingReferenceSeed &pending : currentPendingReferences)
      pendingReferences.push_back(
          {operation, pending.target, pending.attributeName});

    OpBuilder::InsertionGuard guard{builder};
    builder.setInsertionPointToStart(&body);
    using T = std::remove_cvref_t<Node>;
    if constexpr (std::same_as<T, slang::ast::ClassType>) {
      this->visitDefault(node);
      if (const auto *baseConstructorCall = node.getBaseConstructorCall())
        baseConstructorCall->visit(*this);
    } else if constexpr (std::same_as<T, slang::ast::InstanceSymbol>) {
      importPortConnections(node);
      node.body.visit(*this);
    } else if constexpr (std::same_as<T, slang::ast::VariableSymbol>) {
      this->visitDefault(node);
      if (!node.getInitializer()) {
        const slang::ast::Type &type = node.getType().getCanonicalType();
        if (type.kind == slang::ast::SymbolKind::UnpackedStructType)
          for (const slang::ast::FieldSymbol *field :
               type.as<slang::ast::UnpackedStructType>().fields)
            if (const slang::ast::Expression *initializer =
                    field->getInitializer())
              initializer->visit(*this);
      }
    } else if constexpr (std::same_as<T, slang::ast::ClockVarSymbol>) {
      this->visitDefault(node);
      if (node.inputSkew.delay)
        node.inputSkew.delay->visit(*this);
      if (node.outputSkew.delay &&
          node.outputSkew.delay != node.inputSkew.delay)
        node.outputSkew.delay->visit(*this);
    } else if constexpr (std::same_as<T, slang::ast::BlockEventListControl>) {
      for (const auto &event : node.events)
        if (event.target)
          event.target->visit(*this);
    } else if constexpr (std::same_as<T, slang::ast::ProdItem>) {
      node.visitExprs(*this);
    } else if constexpr (std::same_as<T, slang::ast::CodeBlockProd>) {
      node.block->visit(*this);
    } else if constexpr (std::same_as<T, slang::ast::IfElseProd>) {
      node.expr->visit(*this);
      node.ifItem.visit(*this);
      if (node.elseItem)
        node.elseItem->visit(*this);
    } else if constexpr (std::same_as<T, slang::ast::RepeatProd>) {
      node.expr->visit(*this);
      node.item.visit(*this);
    } else if constexpr (std::same_as<T, slang::ast::CaseProd>) {
      node.expr->visit(*this);
      for (const auto &item : node.items) {
        for (const auto *expression : item.expressions)
          expression->visit(*this);
        item.item.visit(*this);
      }
      if (node.defaultItem)
        node.defaultItem->visit(*this);
    } else {
      this->visitDefault(node);
    }
    if constexpr (std::derived_from<Node, slang::ast::Symbol>)
      currentSymbolPath.pop_back();
  }

  slangir::PortConnectionKind
  getConnectionProvenance(const slang::ast::InstanceSymbol &instance,
                          const slang::ast::PortConnection &connection,
                          const slang::ast::Symbol &externalPort,
                          size_t resolvedOrdinal) {
    using Kind = slangir::PortConnectionKind;
    if (connection.isWildcard)
      return Kind::Wildcard;
    if (connection.isImplicit)
      return Kind::Implicit;

    const slang::syntax::SyntaxNode *syntax = instance.getSyntax();
    if (!syntax ||
        syntax->kind != slang::syntax::SyntaxKind::HierarchicalInstance)
      return connection.getExpression() ? Kind::Ordered : Kind::Omitted;
    const auto &connections =
        syntax->as<slang::syntax::HierarchicalInstanceSyntax>().connections;
    bool named = false;
    for (const slang::syntax::PortConnectionSyntax *candidate : connections) {
      if (candidate->kind == slang::syntax::SyntaxKind::NamedPortConnection ||
          candidate->kind ==
              slang::syntax::SyntaxKind::WildcardPortConnection) {
        named = true;
        break;
      }
    }

    bool hasDefault = false;
    if (externalPort.kind == slang::ast::SymbolKind::Port) {
      const auto &port = externalPort.as<slang::ast::PortSymbol>();
      hasDefault = port.direction == slang::ast::ArgumentDirection::In &&
                   port.hasInitializer() &&
                   connection.getExpression() == port.getInitializer();
    }
    if (!named) {
      if (resolvedOrdinal >= connections.size())
        return hasDefault ? Kind::Default : Kind::Omitted;
      return connections[resolvedOrdinal]->kind ==
                     slang::syntax::SyntaxKind::EmptyPortConnection
                 ? Kind::ExplicitOpen
                 : Kind::Ordered;
    }

    for (const slang::syntax::PortConnectionSyntax *candidate : connections) {
      if (candidate->kind != slang::syntax::SyntaxKind::NamedPortConnection)
        continue;
      const auto &namedConnection =
          candidate->as<slang::syntax::NamedPortConnectionSyntax>();
      if (namedConnection.name.valueText() != externalPort.name)
        continue;
      if (!namedConnection.openParen)
        return Kind::Implicit;
      return namedConnection.expr ? Kind::Named : Kind::ExplicitOpen;
    }
    for (const slang::syntax::PortConnectionSyntax *candidate : connections)
      if (candidate->kind ==
              slang::syntax::SyntaxKind::WildcardPortConnection &&
          connection.getExpression())
        return Kind::Wildcard;
    return hasDefault ? Kind::Default : Kind::Omitted;
  }

  void importPortConnections(const slang::ast::InstanceSymbol &instance) {
    std::span<const slang::ast::PortConnection *const> connections =
        instance.getPortConnections();
    std::span<const slang::ast::Symbol *const> externalPorts =
        instance.body.getPortList();

    llvm::DenseMap<const slang::ast::PortSymbol *, const slang::ast::Symbol *>
        leafToExternal;
    llvm::DenseMap<const slang::ast::Symbol *, size_t> externalOrdinals;
    for (auto [externalOrdinal, port] : llvm::enumerate(externalPorts)) {
      externalOrdinals[port] = externalOrdinal;
      if (port->kind == slang::ast::SymbolKind::Port)
        leafToExternal[&port->as<slang::ast::PortSymbol>()] = port;
      else if (port->kind == slang::ast::SymbolKind::MultiPort)
        for (const slang::ast::PortSymbol *leaf :
             port->as<slang::ast::MultiPortSymbol>().ports)
          leafToExternal[leaf] = port;
    }

    for (auto [ordinal, connection] : llvm::enumerate(connections)) {
      const slang::ast::Symbol &formal = connection->port;
      const slang::ast::Symbol *external = &formal;
      if (formal.kind == slang::ast::SymbolKind::Port)
        if (auto found =
                leafToExternal.find(&formal.as<slang::ast::PortSymbol>());
            found != leafToExternal.end())
          external = found->second;

      slang::ast::ArgumentDirection direction =
          slang::ast::ArgumentDirection::InOut;
      Type formalType = slangir::UntypedType::get(builder.getContext());
      bool isNet = false;
      bool isAnsi = false;
      const slang::ast::Expression *internal = nullptr;
      if (formal.kind == slang::ast::SymbolKind::Port) {
        const auto &port = formal.as<slang::ast::PortSymbol>();
        direction = port.direction;
        formalType = typeConverter.convert(port.getType());
        isNet = port.isNetPort();
        isAnsi = port.isAnsiPort;
        internal = port.getInternalExpr();
      }

      NamedAttrList attrs;
      attrs.set("node_id", builder.getI64IntegerAttr(nextNodeId++));
      attrs.set("formal_path", builder.getStringAttr(getSymbolPath(formal)));
      attrs.set("formal_ordinal", builder.getI64IntegerAttr(ordinal));
      if (!formal.name.empty())
        attrs.set("formal_name", builder.getStringAttr(formal.name));
      attrs.set("direction", slangir::ArgumentDirectionAttr::get(
                                 builder.getContext(), convertEnum(direction)));
      attrs.set("formal_type", TypeAttr::get(formalType));
      attrs.set("is_net", builder.getBoolAttr(isNet));
      attrs.set("is_ansi", builder.getBoolAttr(isAnsi));
      bool actualIsConstant = false;
      if (const slang::ast::Expression *actual = connection->getExpression()) {
        // Ask Slang's evaluator instead of inferring constness from the
        // imported expression shape. In particular, a call can read design
        // state through its callee even when the actual has no named-value
        // node of its own.
        slang::ast::EvalContext evalContext(instance);
        actualIsConstant = static_cast<bool>(actual->eval(evalContext));
      }
      attrs.set("actual_is_constant", builder.getBoolAttr(actualIsConstant));
      attrs.set("provenance", slangir::PortConnectionKindAttr::get(
                                  builder.getContext(),
                                  getConnectionProvenance(
                                      instance, *connection, *external,
                                      externalOrdinals.lookup(external))));

      currentPendingReferences.clear();
      setSymbolReference(attrs, formal, builder.getStringAttr("formal_symbol"),
                         builder.getStringAttr("formal_path"));
      if (formal.kind == slang::ast::SymbolKind::Port) {
        const auto &port = formal.as<slang::ast::PortSymbol>();
        if (port.internalSymbol)
          setSymbolReference(attrs, *port.internalSymbol,
                             builder.getStringAttr("internal_symbol"),
                             builder.getStringAttr("internal_path"));
      }
      auto [interfaceInstance, modport] = connection->getIfaceConn();
      // For an interface array Slang's convenience connection points at a
      // synthetic element-shaped symbol. The resolved arbitrary-symbol
      // expression retains the actual array instance and its full hierarchy.
      if (const slang::ast::Expression *actual = connection->getExpression())
        if (const auto *arbitrary =
                actual->as_if<slang::ast::ArbitrarySymbolExpression>())
          interfaceInstance = arbitrary->symbol;
      if (interfaceInstance) {
        setSymbolReference(attrs, *interfaceInstance,
                           builder.getStringAttr("interface_instance_symbol"),
                           builder.getStringAttr("interface_instance_path"));
      }
      if (modport)
        attrs.set("selected_modport", builder.getStringAttr(modport->name));
      if (formal.kind == slang::ast::SymbolKind::InterfacePort) {
        const auto &port = formal.as<slang::ast::InterfacePortSymbol>();
        if (auto shape = port.getDeclaredRange()) {
          SmallVector<int64_t> bounds;
          for (const slang::ConstantRange &range : *shape) {
            bounds.push_back(range.left);
            bounds.push_back(range.right);
          }
          attrs.set("interface_shape", builder.getDenseI64ArrayAttr(bounds));
        }
      }

      Location location = sourceLocation(instance.location);
      auto record = slangir::PortConnectionOp::create(
          builder, location, TypeRange{}, ValueRange{}, attrs.getAttrs());
      record.getInternal().emplaceBlock();
      record.getActual().emplaceBlock();
      for (const PendingReferenceSeed &pending : currentPendingReferences)
        pendingReferences.push_back(
            {record, pending.target, pending.attributeName});

      if (internal) {
        OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToStart(&record.getInternal().front());
        internal->visit(*this);
      }
      if (const slang::ast::Expression *actual = connection->getExpression()) {
        OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToStart(&record.getActual().front());
        actual->visit(*this);
      }
    }
  }

  struct PendingReference {
    Operation *operation;
    const slang::ast::Symbol *target;
    StringAttr attributeName;
  };

  struct PendingReferenceSeed {
    const slang::ast::Symbol *target;
    StringAttr attributeName;
  };

  OpBuilder builder;
  const slang::SourceManager &sourceManager;
  SlangTypeConverter typeConverter;
  llvm::DenseMap<const slang::ast::Symbol *, std::string> anonymousSymbolPaths;
  llvm::DenseMap<const slang::ast::Symbol *, StringAttr> internalSymbolNames;
  llvm::DenseMap<const slang::ast::Symbol *, SmallVector<std::string, 8>>
      emittedSymbolPaths;
  llvm::DenseMap<const slang::ast::Symbol *, Operation *>
      emittedSymbolOperations;
  SmallVector<std::string, 8> currentSymbolPath;
  SmallVector<PendingReference, 0> pendingReferences;
  SmallVector<const slang::ast::Symbol *, 0> semanticDependencies;
  SmallVector<PendingReferenceSeed, 2> currentPendingReferences;
  int64_t nextNodeId = 0;
  uint64_t nextAnonymousSymbolId = 0;
  uint64_t nextInternalSymbolId = 0;
  bool sawInvalidNode = false;
};

static void appendFlag(std::vector<std::string> &arguments, StringRef flag,
                       bool enabled) {
  if (enabled)
    arguments.emplace_back(flag);
}

template <typename Range>
static void appendValues(std::vector<std::string> &arguments, StringRef flag,
                         const Range &values) {
  for (const auto &value : values) {
    arguments.emplace_back(flag);
    arguments.emplace_back(value);
  }
}

static std::vector<std::string>
buildSlangArguments(ArrayRef<std::string> inputs,
                    const FrontendOptions &options) {
  std::vector<std::string> result;
  result.emplace_back("obelisk");
  result.emplace_back("--std");
  result.emplace_back(options.languageVersion == LanguageVersion::IEEE1800_2017
                          ? "1800-2017"
                          : "1800-2023");

  appendValues(result, "-I", options.includeDirs);
  appendValues(result, "--isystem", options.includeSystemDirs);
  appendValues(result, "-D", options.defines);
  appendValues(result, "-U", options.undefines);
  appendValues(result, "-f", options.commandFiles);
  appendValues(result, "-y", options.libDirs);
  appendValues(result, "-Y", options.libExts);
  appendValues(result, "-v", options.libraryFiles);
  appendValues(result, "--top", options.topModules);
  appendValues(result, "-G", options.paramOverrides);
  appendValues(result, "-W", options.warningOptions);
  appendValues(result, "--suppress-warnings", options.suppressWarningsPaths);

  appendFlag(result, "--single-unit", options.singleUnit);
  appendFlag(result, "--libraries-inherit-macros",
             options.librariesInheritMacros);
  appendFlag(result, "--allow-use-before-declare",
             options.allowUseBeforeDeclare);
  appendFlag(result, "--ignore-unknown-modules", options.ignoreUnknownModules);

  if (options.maxIncludeDepth) {
    result.emplace_back("--max-include-depth");
    result.push_back(std::to_string(*options.maxIncludeDepth));
  }
  if (options.errorLimit) {
    result.emplace_back("--error-limit");
    result.push_back(std::to_string(*options.errorLimit));
  }
  if (options.timeScale) {
    result.emplace_back("--timescale");
    result.push_back(*options.timeScale);
  }
  result.insert(result.end(), options.slangArgs.begin(),
                options.slangArgs.end());

  // Make filenames beginning with '-' unambiguously positional.
  result.emplace_back("--");
  result.insert(result.end(), inputs.begin(), inputs.end());
  return result;
}

} // namespace

FailureOr<std::string>
preprocessSystemVerilog(ArrayRef<std::string> inputFilenames,
                        const FrontendOptions &options) {
  slang::driver::Driver driver;
  driver.addStandardArgs();

  std::vector<std::string> arguments =
      buildSlangArguments(inputFilenames, options);
  SmallVector<const char *> argv;
  argv.reserve(arguments.size());
  for (const std::string &argument : arguments)
    argv.push_back(argument.c_str());

  bool succeeded = false;
  std::string output;
  std::string diagnostics;
  {
    auto capture = slang::OS::captureOutput();
    slang::bitmask<slang::driver::PreprocessOutputFlags> flags;
    succeeded =
        driver.parseCommandLine(static_cast<int>(argv.size()), argv.data()) &&
        driver.processOptions() && driver.runPreprocessor(flags);
    output = slang::OS::capturedStdout;
    diagnostics = slang::OS::capturedStderr;
  }
  if (!diagnostics.empty())
    llvm::errs() << diagnostics;
  if (!succeeded)
    return failure();
  return output;
}

FailureOr<OwningOpRef<ModuleOp>>
importSystemVerilog(ArrayRef<std::string> inputFilenames, MLIRContext &context,
                    const FrontendOptions &options, bool verifyIR) {
  slang::driver::Driver driver;
  driver.addStandardArgs();

  std::vector<std::string> arguments =
      buildSlangArguments(inputFilenames, options);
  SmallVector<const char *> argv;
  argv.reserve(arguments.size());
  for (const std::string &argument : arguments)
    argv.push_back(argument.c_str());

  if (!driver.parseCommandLine(static_cast<int>(argv.size()), argv.data()) ||
      !driver.processOptions() || !driver.parseAllSources())
    return failure();

  std::unique_ptr<slang::ast::Compilation> compilation =
      driver.createCompilation();
  driver.reportCompilation(*compilation, /*quiet=*/true);
  if (!driver.reportDiagnostics(/*quiet=*/true))
    return failure();

  OwningOpRef<ModuleOp> module(ModuleOp::create(UnknownLoc::get(&context)));
  SlangASTImporter importer(*module, driver.sourceManager);
  // Definitions are kept in Compilation's deterministic definition map and
  // are not children of RootSymbol. Import them explicitly so modules,
  // interfaces, programs, and primitives remain represented even when they
  // have no elaborated instance.
  for (const slang::ast::Symbol *definition : compilation->getDefinitions())
    definition->visit(importer);
  compilation->getRoot().visit(importer);
  if (failed(importer.finalizeReferences()) || !importer.succeeded()) {
    emitError(UnknownLoc::get(&context))
        << "slang compilation contained an invalid semantic AST node";
    return failure();
  }
  for (const slang::ast::Compilation::DPIExport &entry :
       compilation->getDPIExports())
    importer.markDPIExport(*entry.subroutine, entry.cIdentifier);
  if (!importer.succeeded())
    return failure();

  if (verifyIR && failed(verify(*module))) {
    emitError(UnknownLoc::get(&context))
        << "imported Slang dialect IR failed verification";
    return failure();
  }
  return module;
}

std::string getSlangVersion() {
  return "slang version " + slang::VersionInfo::getVersionString();
}

} // namespace obelisk::frontend
