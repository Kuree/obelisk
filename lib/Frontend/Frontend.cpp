//===- Frontend.cpp - slang semantic AST to Slang dialect importer -------===//

#include "obelisk/Frontend/Frontend.h"
#include "obelisk/Frontend/ProtectedEnvelope.h"

#include "SDF.h"

#include "obelisk/Dialect/ForeachLoopMetadata.h"
#include "obelisk/Dialect/Slang/SlangOps.h"
#include "obelisk/Reflection/VPIObjectModel.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Verifier.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#include "slang/analysis/AnalysisManager.h"
#include "slang/analysis/ValueDriver.h"
#include "slang/ast/ASTContext.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/Lookup.h"
#include "slang/ast/expressions/Operator.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/types/TypePrinter.h"
#include "slang/driver/Driver.h"
#include "slang/numeric/Time.h"
#include "slang/parsing/ProtectEnvelope.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/util/OS.h"
#include "slang/util/VersionInfo.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <concepts>
#include <functional>
#include <limits>
#include <map>
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

ProtectedEncoding mapProtectEncoding(slang::parsing::ProtectEncoding value) {
  switch (value) {
  case slang::parsing::ProtectEncoding::UUEncode:
    return ProtectedEncoding::UUEncode;
  case slang::parsing::ProtectEncoding::Base64:
    return ProtectedEncoding::Base64;
  case slang::parsing::ProtectEncoding::QuotedPrintable:
    return ProtectedEncoding::QuotedPrintable;
  case slang::parsing::ProtectEncoding::Raw:
    return ProtectedEncoding::Raw;
  }
  llvm_unreachable("unknown Slang protected encoding");
}

ProtectedBlockKind mapProtectBlockKind(slang::parsing::ProtectBlockKind value) {
  switch (value) {
  case slang::parsing::ProtectBlockKind::Data:
    return ProtectedBlockKind::Data;
  case slang::parsing::ProtectBlockKind::Digest:
    return ProtectedBlockKind::Digest;
  case slang::parsing::ProtectBlockKind::Key:
    return ProtectedBlockKind::Key;
  case slang::parsing::ProtectBlockKind::DataPublicKey:
    return ProtectedBlockKind::DataPublicKey;
  case slang::parsing::ProtectBlockKind::DataDecryptKey:
    return ProtectedBlockKind::DataDecryptKey;
  case slang::parsing::ProtectBlockKind::DigestPublicKey:
    return ProtectedBlockKind::DigestPublicKey;
  case slang::parsing::ProtectBlockKind::DigestDecryptKey:
    return ProtectedBlockKind::DigestDecryptKey;
  case slang::parsing::ProtectBlockKind::KeyPublicKey:
    return ProtectedBlockKind::KeyPublicKey;
  }
  llvm_unreachable("unknown Slang protected block kind");
}

slang::parsing::ProtectEnvelopeStatus
mapProtectStatus(ProtectedEnvelopeStatus value) {
  switch (value) {
  case ProtectedEnvelopeStatus::Success:
    return slang::parsing::ProtectEnvelopeStatus::Success;
  case ProtectedEnvelopeStatus::ProviderUnavailable:
    return slang::parsing::ProtectEnvelopeStatus::ProviderUnavailable;
  case ProtectedEnvelopeStatus::Rejected:
    return slang::parsing::ProtectEnvelopeStatus::Rejected;
  case ProtectedEnvelopeStatus::InvalidData:
    return slang::parsing::ProtectEnvelopeStatus::InvalidData;
  case ProtectedEnvelopeStatus::ResourceLimit:
    return slang::parsing::ProtectEnvelopeStatus::ResourceLimit;
  }
  llvm_unreachable("unknown protected-envelope status");
}

class SlangProtectEnvelopeAdapter final
    : public slang::parsing::ProtectEnvelopeDecryptor {
public:
  explicit SlangProtectEnvelopeAdapter(
      std::shared_ptr<const ProtectedEnvelopeProvider> provider)
      : provider(std::move(provider)) {}

  slang::parsing::ProtectEnvelopeResult
  decrypt(const slang::parsing::ProtectEnvelope &input) const override {
    SmallVector<ProtectedEnvelopeRecord> records;
    records.reserve(input.records.size());
    for (const slang::parsing::ProtectEnvelopeRecord &inputRecord :
         input.records) {
      ProtectedEnvelopeRecord record;
      switch (inputRecord.recordKind) {
      case slang::parsing::ProtectRecordKind::Expression:
        record.recordKind = ProtectedRecordKind::Expression;
        record.name = inputRecord.name;
        record.value = inputRecord.value;
        break;
      case slang::parsing::ProtectRecordKind::EncodedBlock:
        record.recordKind = ProtectedRecordKind::EncodedBlock;
        record.name = inputRecord.name;
        record.blockKind = mapProtectBlockKind(inputRecord.blockKind);
        record.encoding = mapProtectEncoding(inputRecord.encoding);
        record.expectedBytes = inputRecord.expectedBytes;
        record.encodedText = inputRecord.encodedText;
        break;
      }
      records.push_back(std::move(record));
    }

    ProtectedEnvelopeResult result = provider->decrypt({records});
    slang::parsing::ProtectEnvelopeResult converted;
    converted.status = mapProtectStatus(result.status);
    // Allocate exactly once before copying Clause 34.3.2 plaintext so an
    // internal SmallVector growth cannot abandon a decrypted allocation.
    converted.source.reserve(result.source.size());
    converted.source.append(result.source.begin(), result.source.end());
    result.clear();
    return converted;
  }

private:
  std::shared_ptr<const ProtectedEnvelopeProvider> provider;
};

void installProtectedEnvelopeProvider(slang::driver::Driver &driver,
                                      const FrontendOptions &options) {
  driver.options.maxProtectEnvelopeDepth = options.maxProtectedEnvelopeDepth;
  driver.options.maxProtectEnvelopeBytes = options.maxProtectedEnvelopeBytes;
  driver.options.maxProtectEnvelopeCount = options.maxProtectedEnvelopeCount;
  if (options.protectedEnvelopeProvider)
    driver.options.protectEnvelopeDecryptor =
        std::make_shared<SlangProtectEnvelopeAdapter>(
            options.protectedEnvelopeProvider);
}

std::string formatReal(double value) {
  std::array<char, 64> buffer;
  auto [end, error] = std::to_chars(
      buffer.data(), buffer.data() + buffer.size(), value,
      std::chars_format::general, std::numeric_limits<double>::max_digits10);
  if (error != std::errc())
    llvm_unreachable("buffer is too small to format a double");
  return std::string(buffer.data(), end);
}

template <typename Value> std::string formatConstant(const Value &value) {
  return value.toString(slang::SVInt::MAX_BITS, /*exactUnknowns=*/true);
}

/// Value of an expression that elaboration folded to an integer constant. It
/// is redundant with the expression tree beneath it, so consumers may ignore
/// it; what it buys is not having to fold that tree a second time. Read back
/// under the same name by the simulation lowering.
constexpr llvm::StringLiteral foldedConstantAttrName = "folded_constant";
// IEEE 1800-2017 20.7 query order cannot be reconstructed from a canonical
// storage type after unpacked-array typedef boundaries have been erased.
constexpr llvm::StringLiteral arrayQueryDimensionsAttrName =
    "obelisk.array_query_dimensions";
// Identity a type reference shares with every other reference to a matching
// type (IEEE 1800-2017 6.22.1), so that the comparisons 6.23 allows can be
// settled without re-implementing the matching rules downstream.
constexpr llvm::StringLiteral typeReferenceIdentityAttrName =
    "type_reference_identity";
// Ordered, post-elaboration UDP declaration data.  Slang has already
// normalized table spellings and rejected conflicting rows by the time this
// snapshot is taken; keeping the snapshot in one uniqued dictionary lets all
// instances of a UDP share it without retaining frontend objects.
constexpr llvm::StringLiteral udpMetadataAttrName = "udp_metadata";

std::string getQualifiedLibraryCell(const slang::ast::Symbol &symbol,
                                    StringRef cellName) {
  std::string result;
  if (const slang::SourceLibrary *library = symbol.getSourceLibrary();
      library && !library->name.empty()) {
    result += library->name;
    result.push_back('.');
  }
  result += cellName;
  return result;
}

StringRef getConfigurationRuleKind(const slang::ast::ConfigRule &rule) {
  using slang::syntax::SyntaxKind;
  switch (rule.syntax->kind) {
  case SyntaxKind::CellConfigRule:
    return "cell";
  case SyntaxKind::InstanceConfigRule:
    return "instance";
  default:
    return "unknown";
  }
}

bool containsDirectBoundInstance(const slang::ast::Symbol &symbol) {
  using slang::ast::InstanceFlags;
  using slang::ast::SymbolKind;
  switch (symbol.kind) {
  case SymbolKind::Instance:
    return symbol.as<slang::ast::InstanceSymbol>().body.flags.has(
        InstanceFlags::FromBind);
  case SymbolKind::CheckerInstance:
    return symbol.as<slang::ast::CheckerInstanceSymbol>().body.flags.has(
        InstanceFlags::FromBind);
  case SymbolKind::InstanceArray:
    return llvm::any_of(symbol.as<slang::ast::InstanceArraySymbol>().elements,
                        [](const slang::ast::Symbol *element) {
                          return containsDirectBoundInstance(*element);
                        });
  default:
    return false;
  }
}

std::string formatConstant(const slang::ConstantValue &value) {
  if (value.isString())
    return value.str();
  return value.toString(slang::SVInt::MAX_BITS, /*exactUnknowns=*/true);
}

const slang::ast::Type &unwrapTypeAliases(const slang::ast::Type &type) {
  const slang::ast::Type *current = &type;
  while (current->kind == slang::ast::SymbolKind::TypeAlias)
    current = &current->as<slang::ast::TypeAliasType>().targetType.getType();
  return *current;
}

/// Slang exposes the resolver written directly on a nettype declaration but
/// currently returns null for the LRM alias form `nettype original alias;`.
/// Resolve that named base explicitly so imported net symbols retain the
/// original resolution function through arbitrarily long alias chains.
const slang::ast::SubroutineSymbol *
getEffectiveResolutionFunction(const slang::ast::NetType &netType) {
  if (const auto *function = netType.getResolutionFunction())
    return function;
  const auto *syntax = netType.getSyntax();
  const auto *scope = netType.getParentScope();
  if (!syntax || !scope ||
      syntax->kind != slang::syntax::SyntaxKind::NetTypeDeclaration)
    return nullptr;
  const auto &declaration =
      syntax->as<slang::syntax::NetTypeDeclarationSyntax>();
  if (declaration.withFunction ||
      declaration.type->kind != slang::syntax::SyntaxKind::NamedType)
    return nullptr;
  const auto &named = declaration.type->as<slang::syntax::NamedTypeSyntax>();
  slang::ast::ASTContext context(*scope,
                                 slang::ast::LookupLocation::after(netType),
                                 slang::ast::ASTFlags::AllowNetType);
  slang::ast::LookupResult result;
  slang::ast::Lookup::name(*named.name, context, slang::ast::LookupFlags::Type,
                           result);
  if (!result.found || result.found == &netType ||
      result.found->kind != slang::ast::SymbolKind::NetType)
    return nullptr;
  return getEffectiveResolutionFunction(
      result.found->as<slang::ast::NetType>());
}

// Slang's canonical PackedArrayType inherits signedness from its element type.
// That loses the distinction between an explicitly signed packed declaration
// and an unsigned packed dimension wrapped around a signed typedef. Preserve
// that distinction by following the non-canonical type graph, where typedef
// boundaries remain visible.
bool isWholeTypeSigned(const slang::ast::Type &sourceType) {
  const slang::ast::Type &type = unwrapTypeAliases(sourceType);
  if (type.kind != slang::ast::SymbolKind::PackedArrayType)
    return type.isSigned();

  const slang::ast::Type *element =
      &type.as<slang::ast::PackedArrayType>().elementType;
  while (element->kind == slang::ast::SymbolKind::PackedArrayType)
    element = &element->as<slang::ast::PackedArrayType>().elementType;
  if (element->kind == slang::ast::SymbolKind::TypeAlias)
    return false;
  return element->isSigned();
}

const slang::ast::Type *
getDeclaredSelectionType(const slang::ast::Expression &expression) {
  using namespace slang::ast;
  if (ValueExpressionBase::isKind(expression.kind))
    return &expression.as<ValueExpressionBase>().symbol.getType();
  if (expression.kind != ExpressionKind::ElementSelect)
    return nullptr;

  const auto &select = expression.as<ElementSelectExpression>();
  const slang::ast::Type *valueType = getDeclaredSelectionType(select.value());
  if (!valueType)
    return nullptr;
  const slang::ast::Type &selectedType = unwrapTypeAliases(*valueType);
  if (selectedType.kind == SymbolKind::PackedArrayType)
    return &selectedType.as<PackedArrayType>().elementType;
  if (selectedType.kind == SymbolKind::FixedSizeUnpackedArrayType)
    return &selectedType.as<FixedSizeUnpackedArrayType>().elementType;
  return nullptr;
}

bool isEffectivelySigned(const slang::ast::Expression &expression) {
  using namespace slang::ast;
  if (ValueExpressionBase::isKind(expression.kind))
    return isWholeTypeSigned(
        expression.as<ValueExpressionBase>().symbol.getType());
  if (expression.kind != ExpressionKind::ElementSelect)
    return expression.type->isSigned();

  const auto &select = expression.as<ElementSelectExpression>();
  const slang::ast::Type *valueType = getDeclaredSelectionType(select.value());
  const slang::ast::Type *resultType = getDeclaredSelectionType(expression);
  if (!valueType || !resultType)
    return expression.type->isSigned();

  const slang::ast::Type &selectedType = unwrapTypeAliases(*valueType);
  if (selectedType.kind == SymbolKind::PackedArrayType) {
    // An individual packed element is unsigned unless its element type is a
    // named type whose whole declaration is signed.
    return resultType->kind == SymbolKind::TypeAlias &&
           isWholeTypeSigned(*resultType);
  }
  return isWholeTypeSigned(*resultType);
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

SmallVector<std::optional<int64_t>, 12>
expandTimingDelays(ArrayRef<std::optional<int64_t>> values) {
  using OptionalDelay = std::optional<int64_t>;
  auto minimum = [](OptionalDelay lhs, OptionalDelay rhs) -> OptionalDelay {
    return lhs && rhs ? OptionalDelay(std::min(*lhs, *rhs)) : std::nullopt;
  };
  auto maximum = [](OptionalDelay lhs, OptionalDelay rhs) -> OptionalDelay {
    return lhs && rhs ? OptionalDelay(std::max(*lhs, *rhs)) : std::nullopt;
  };
  SmallVector<OptionalDelay, 12> result;
  switch (values.size()) {
  case 1:
    result.assign(12, values[0]);
    break;
  case 2: {
    OptionalDelay rise = values[0], fall = values[1];
    result = {rise,
              fall,
              rise,
              rise,
              fall,
              fall,
              rise,
              rise,
              fall,
              fall,
              maximum(rise, fall),
              minimum(rise, fall)};
    break;
  }
  case 3: {
    OptionalDelay rise = values[0], fall = values[1], turnOff = values[2];
    result = {rise,
              fall,
              turnOff,
              rise,
              turnOff,
              fall,
              minimum(rise, turnOff),
              rise,
              minimum(fall, turnOff),
              fall,
              turnOff,
              minimum(rise, fall)};
    break;
  }
  case 6: {
    OptionalDelay rise = values[0], fall = values[1], zeroToZ = values[2];
    OptionalDelay zToOne = values[3], oneToZ = values[4], zToZero = values[5];
    result = {rise,
              fall,
              zeroToZ,
              zToOne,
              oneToZ,
              zToZero,
              minimum(rise, zeroToZ),
              maximum(rise, zToOne),
              minimum(fall, oneToZ),
              maximum(fall, zToZero),
              maximum(oneToZ, zeroToZ),
              minimum(zToOne, zToZero)};
    break;
  }
  case 12:
    result.assign(values.begin(), values.end());
    break;
  }
  return result;
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

slangir::DriveStrength convertEnum(slang::ast::DriveStrength strength) {
  switch (strength) {
  case slang::ast::DriveStrength::Supply:
    return slangir::DriveStrength::Supply;
  case slang::ast::DriveStrength::Strong:
    return slangir::DriveStrength::Strong;
  case slang::ast::DriveStrength::Pull:
    return slangir::DriveStrength::Pull;
  case slang::ast::DriveStrength::Weak:
    return slangir::DriveStrength::Weak;
  case slang::ast::DriveStrength::HighZ:
    return slangir::DriveStrength::HighZ;
  }
  llvm_unreachable("unknown slang drive strength");
}

slangir::ChargeStrength convertEnum(slang::ast::ChargeStrength strength) {
  switch (strength) {
  case slang::ast::ChargeStrength::Small:
    return slangir::ChargeStrength::Small;
  case slang::ast::ChargeStrength::Medium:
    return slangir::ChargeStrength::Medium;
  case slang::ast::ChargeStrength::Large:
    return slangir::ChargeStrength::Large;
  }
  llvm_unreachable("unknown slang charge strength");
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
                NamedAttribute(
                    StringAttr::get(context, "rand_mode"),
                    IntegerAttr::get(IntegerType::get(context, 32),
                                     static_cast<uint32_t>(field.randMode))),
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
      SymbolRefAttr interfaceIdentity =
          getVirtualInterfaceIdentity(interface.iface.body, interface.iface);
      result = slangir::VirtualInterfaceType::get(
          context, interfaceIdentity, StringAttr::get(context, modport));
      break;
    }
    case SK::ErrorType:
      // Slang uses ErrorType in declarations that it type-checks but does not
      // elaborate, such as parameter-dependent members of an uninstantiated
      // definition. Preserve that unavailable type without treating it as an
      // error in the selected design. Invalid* nodes reached in elaborated
      // code are diagnosed separately by recordInvalidNode.
      result = slangir::ErrorType::get(context, /*invalid=*/false);
      break;
    default:
      llvm_unreachable("unhandled canonical slang semantic type");
    }

    cache.try_emplace(&type, result);
    return result;
  }

  SymbolRefAttr
  getVirtualInterfaceIdentity(const slang::ast::InstanceBodySymbol &body,
                              const slang::ast::InstanceSymbol &instance) {
    for (const auto &[candidate, identity] : virtualInterfaceIdentities)
      if (candidate->hasSameType(body))
        return identity;
    SymbolRefAttr identity = buildSymbolReference(instance);
    virtualInterfaceIdentities.emplace_back(&body, identity);
    return identity;
  }

private:
  MLIRContext *context;
  SymbolReferenceBuilder buildSymbolReference;
  llvm::DenseMap<const slang::ast::Type *, Type> cache;
  SmallVector<std::pair<const slang::ast::InstanceBodySymbol *, SymbolRefAttr>>
      virtualInterfaceIdentities;
};

/// Exhaustive concrete visitor for the selected semantic AST. The macro expands
/// to 220 ordinary overload declarations; ASTVisitor has no generic handler to
/// fall back to in this class.
class SlangASTImporter
    : public slang::ast::ASTVisitor<SlangASTImporter,
                                    slang::ast::VisitFlags::AllGood |
                                        slang::ast::VisitFlags::Bad> {
public:
  SlangASTImporter(ModuleOp module, const slang::SourceManager &sourceManager,
                   const slang::ast::Compilation &compilation,
                   const slang::analysis::AnalysisManager &analysisManager,
                   const SDFAnnotationDatabase &sdfAnnotations)
      : builder(module.getContext()), sourceManager(sourceManager),
        compilation(compilation), analysisManager(analysisManager),
        sdfAnnotations(sdfAnnotations),
        typeConverter(module.getContext(),
                      [this](const slang::ast::Symbol &symbol) {
                        return getSemanticSymbolReference(symbol);
                      }) {
    builder.setInsertionPointToStart(module.getBody());
  }

  [[nodiscard]] bool succeeded() const { return !sawInvalidNode; }

  void markDPIExport(const slang::ast::SubroutineSymbol &subroutine,
                     StringRef cIdentifier,
                     const slang::syntax::DPIExportSyntax *exportSyntax) {
    if (exportSyntax && exportSyntax->specString.valueText() == "DPI") {
      emitError(sourceLocation(exportSyntax->specString.location()))
          << "legacy SystemVerilog 3.1a `DPI` exports are unsupported; use "
             "`DPI-C`";
      sawInvalidNode = true;
      return;
    }
    const slang::syntax::SyntaxNode *syntax = subroutine.getSyntax();
    if (syntax && !dpiExportSyntaxIndexBuilt) {
      for (auto [symbol, operation] : emittedSymbolOperations) {
        if (symbol->kind != slang::ast::SymbolKind::Subroutine)
          continue;
        const auto &candidate = symbol->as<slang::ast::SubroutineSymbol>();
        if (const slang::syntax::SyntaxNode *candidateSyntax =
                candidate.getSyntax())
          dpiExportOperationsBySyntax[candidateSyntax].push_back(operation);
      }
      dpiExportSyntaxIndexBuilt = true;
    }
    // Compilation::getDPIExports() may identify one elaborated clone of a
    // module declaration. Every clone shares the declaration syntax and needs
    // its own scope-specific export bridge downstream.
    bool found = false;
    auto mark = [&](Operation *operation) {
      operation->setAttr("dpi_export_c_identifier",
                         builder.getStringAttr(cIdentifier));
      found = true;
    };
    if (syntax) {
      if (auto clones = dpiExportOperationsBySyntax.find(syntax);
          clones != dpiExportOperationsBySyntax.end())
        for (Operation *operation : clones->second)
          mark(operation);
    } else if (auto operation = emittedSymbolOperations.find(&subroutine);
               operation != emittedSymbolOperations.end()) {
      mark(operation->second);
    }
    if (!found) {
      emitError(sourceLocation(subroutine.location))
          << "resolved DPI export subroutine was not imported";
      sawInvalidNode = true;
    }
  }

  LogicalResult finalizeReferences() {
    // ASTVisitor follows ownership edges, while elaborated expressions can
    // reference semantic dependencies outside those roots (for example an
    // anonymous enum's members or declarations in the built-in std package).
    // Materialize that transitive closure beneath the real semantic parent so
    // every SymbolRefAttr remains both resolvable and hierarchically accurate.
    llvm::SmallPtrSet<const slang::ast::Symbol *, 32> scheduled;
    std::multimap<std::string, const slang::ast::Symbol *> worklist;
    size_t nextPending = 0;
    size_t nextDependency = 0;
    auto schedule = [&](const slang::ast::Symbol *symbol) {
      if (!emittedSymbolPaths.contains(symbol) &&
          scheduled.insert(symbol).second) {
        std::string key;
        llvm::raw_string_ostream stream(key);
        // Resolve the importer path before ordering. This gives distinct,
        // deterministic shadow suffixes to Slang's synthesized foreach and
        // block variables that otherwise share name, kind, and source loc.
        stream << getSymbolPath(*symbol) << '\0'
               << static_cast<uint32_t>(symbol->kind) << '\0' << symbol->name
               << '\0';
        slang::SourceLocation location =
            sourceManager.getFullyExpandedLoc(symbol->location);
        if (location.valid() && sourceManager.isFileLoc(location))
          stream << sourceManager.getFileName(location) << ':'
                 << sourceManager.getLineNumber(location) << ':'
                 << sourceManager.getColumnNumber(location);
        else
          stream << location.buffer().getId() << ':' << location.offset();
        // Multiple synthesized semantic symbols can intentionally share this
        // complete visible key (foreach iterators are the common case).
        // std::multimap keeps their already-deterministic discovery order
        // within the equal-key range instead of dropping dependencies.
        worklist.emplace(std::move(key), symbol);
      }
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

      auto next = worklist.begin();
      const slang::ast::Symbol *symbol = next->second;
      worklist.erase(next);
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
    for (const PendingReferenceArray &pending : pendingReferenceArrays) {
      SmallVector<Attribute> references;
      references.reserve(pending.targets.size());
      for (const slang::ast::Symbol *symbol : pending.targets) {
        auto target = emittedSymbolPaths.find(symbol);
        if (target == emittedSymbolPaths.end()) {
          pending.operation->emitError()
              << "referenced slang symbol was not imported: "
              << getSymbolPath(*symbol);
          sawInvalidNode = true;
          continue;
        }
        ArrayRef<std::string> targetPath = target->second;
        SmallVector<FlatSymbolRefAttr> nested;
        for (StringRef component : targetPath.drop_front())
          nested.push_back(
              FlatSymbolRefAttr::get(builder.getContext(), component));
        references.push_back(SymbolRefAttr::get(builder.getContext(),
                                                targetPath.front(), nested));
      }
      pending.operation->setAttr(pending.attributeName,
                                 builder.getArrayAttr(references));
    }
    return success(!sawInvalidNode);
  }

#define SLANG_AST_NODE(Category, Kind, CppType)                                \
  void handle(const slang::ast::CppType &node) {                               \
    if constexpr (isInvalidSemanticNode<slang::ast::CppType>)                  \
      recordInvalidNode(node, #CppType);                                       \
    else                                                                       \
      importNode<slangir::CppType##Op>(node);                                  \
  }
#include "obelisk/Dialect/Slang/SlangASTNodes.def"
#undef SLANG_AST_NODE

  void handle(const slang::ast::InvalidStatement &node) {
    recordInvalidNode(node, "InvalidStatement");
  }
  void handle(const slang::ast::InvalidExpression &node) {
    recordInvalidNode(node, "InvalidExpression");
  }
  void handle(const slang::ast::InvalidSymbol &node) {
    recordInvalidNode(node, "InvalidSymbol");
  }

private:
  /// The number this type shares with every type it matches under IEEE
  /// 1800-2017 6.22.1. Matching is not an equivalence Slang exposes as a key,
  /// so representatives are collected and each new type is matched against
  /// them. Cache exact AST type pointers so all members of the same enum and
  /// repeated uses of a typedef pay for matching only once.
  int64_t matchingTypeIdentity(const slang::ast::Type &type) {
    if (auto found = matchingTypeIdentities.find(&type);
        found != matchingTypeIdentities.end())
      return found->second;
    for (auto [index, representative] :
         llvm::enumerate(matchingTypeRepresentatives))
      if (type.isMatching(*representative)) {
        int64_t identity = static_cast<int64_t>(index);
        matchingTypeIdentities.try_emplace(&type, identity);
        return identity;
      }
    matchingTypeRepresentatives.push_back(&type);
    int64_t identity =
        static_cast<int64_t>(matchingTypeRepresentatives.size()) - 1;
    matchingTypeIdentities.try_emplace(&type, identity);
    return identity;
  }

  /// slang represents code it deliberately never elaborates - the unselected
  /// arm of a generate condition, the body of an uninstantiated module, an
  /// unspecialized generic class - with Invalid* placeholders, and suppresses
  /// the diagnostics that would describe them because that code is not part of
  /// the design. Such a placeholder says nothing about the design's validity;
  /// only one reached through elaborated code makes the AST unusable.
  template <typename Node>
  void recordInvalidNode(const Node &node, StringRef kind) {
    // A symbol records the scope it belongs to. An expression or a statement
    // does not, so for those the scope being visited is what stands in for it.
    const slang::ast::Scope *scope = nullptr;
    if constexpr (std::derived_from<Node, slang::ast::Symbol>)
      scope = node.getParentScope();
    if (!scope)
      scope = getCurrentScope();
    if (scope && scope->isUninstantiated())
      return;
    sawInvalidNode = true;
    slang::SourceLocation location;
    if constexpr (requires { node.sourceRange; })
      location = node.sourceRange.start();
    else if constexpr (std::derived_from<Node, slang::ast::Symbol>)
      location = node.location;
    // slang stays silent for most of these, so naming the node and its source
    // is the only thing that makes the failure diagnosable.
    emitError(location.valid() ? sourceLocation(location)
                               : UnknownLoc::get(builder.getContext()))
        << "invalid semantic AST node in elaborated code: " << kind;
  }

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

  struct ArrayQueryDimension {
    StringRef kind;
    bool unpacked;
    int64_t left = 0;
    int64_t right = 0;
    const slang::ast::Type *indexType = nullptr;
  };

  struct ArrayQueryDimensionInventory {
    SmallVector<ArrayQueryDimension, 4> unpacked;
    SmallVector<ArrayQueryDimension, 2> packed;
    bool sawAlias = false;
  };

  bool collectArrayQueryDimensions(
      const slang::ast::Type &type, ArrayQueryDimensionInventory &inventory,
      llvm::SmallPtrSetImpl<const slang::ast::Type *> &activeAliases) {
    using SK = slang::ast::SymbolKind;
    if (type.kind == SK::TypeAlias) {
      inventory.sawAlias = true;
      if (!activeAliases.insert(&type).second)
        return false;
      const auto &alias = type.as<slang::ast::TypeAliasType>();
      bool complete = collectArrayQueryDimensions(alias.targetType.getType(),
                                                  inventory, activeAliases);
      activeAliases.erase(&type);
      return complete;
    }

    // A run of dimensions written on one declaration keeps its outer-to-inner
    // order. The type named by that declaration is expanded first, however,
    // so recurse through a trailing alias before appending the whole run.
    SmallVector<ArrayQueryDimension, 4> localUnpacked;
    const slang::ast::Type *element = &type;
    while (true) {
      switch (element->kind) {
      case SK::FixedSizeUnpackedArrayType: {
        const auto &array =
            element->as<slang::ast::FixedSizeUnpackedArrayType>();
        localUnpacked.push_back(
            {"fixed", true, array.range.left, array.range.right});
        element = &array.elementType;
        continue;
      }
      case SK::DynamicArrayType: {
        const auto &array = element->as<slang::ast::DynamicArrayType>();
        localUnpacked.push_back({"dynamic", true});
        element = &array.elementType;
        continue;
      }
      case SK::AssociativeArrayType: {
        const auto &array = element->as<slang::ast::AssociativeArrayType>();
        if (!array.indexType)
          return false;
        localUnpacked.push_back({"associative", true, 0, 0, array.indexType});
        element = &array.elementType;
        continue;
      }
      case SK::QueueType: {
        const auto &array = element->as<slang::ast::QueueType>();
        localUnpacked.push_back({"queue", true});
        element = &array.elementType;
        continue;
      }
      case SK::DPIOpenArrayType: {
        const auto &array = element->as<slang::ast::DPIOpenArrayType>();
        if (array.isPacked)
          break;
        localUnpacked.push_back({"open", true});
        element = &array.elementType;
        continue;
      }
      default:
        break;
      }
      break;
    }
    if (!localUnpacked.empty()) {
      if (!collectArrayQueryDimensions(*element, inventory, activeAliases))
        return false;
      inventory.unpacked.append(localUnpacked);
      return true;
    }

    SmallVector<ArrayQueryDimension, 2> localPacked;
    element = &type;
    while (element->kind == SK::PackedArrayType ||
           (element->kind == SK::DPIOpenArrayType &&
            element->as<slang::ast::DPIOpenArrayType>().isPacked)) {
      if (element->kind == SK::PackedArrayType) {
        const auto &array = element->as<slang::ast::PackedArrayType>();
        localPacked.push_back(
            {"fixed", false, array.range.left, array.range.right});
        element = &array.elementType;
      } else {
        const auto &array = element->as<slang::ast::DPIOpenArrayType>();
        localPacked.push_back({"open", false});
        element = &array.elementType;
      }
    }
    if (!localPacked.empty()) {
      if (!collectArrayQueryDimensions(*element, inventory, activeAliases))
        return false;
      inventory.packed.append(localPacked);
      return true;
    }

    if (type.kind == SK::StringType) {
      inventory.packed.push_back({"string", false});
      return true;
    }
    if (type.kind == SK::EnumType) {
      uint64_t width = type.getBitWidth();
      if (width)
        inventory.packed.push_back(
            {"fixed", false, static_cast<int64_t>(width - 1), 0});
      return true;
    }
    if (type.kind == SK::PredefinedIntegerType) {
      slang::ConstantRange range = type.getFixedRange();
      inventory.packed.push_back({"fixed", false, range.left, range.right});
      return true;
    }
    if (type.kind == SK::PackedStructType || type.kind == SK::PackedUnionType) {
      uint64_t width = type.getBitWidth();
      if (width)
        inventory.packed.push_back(
            {"fixed", false, static_cast<int64_t>(width - 1), 0});
      return true;
    }
    // A scalar bit / logic / reg and every nonarray unpacked type contribute
    // no query dimension. Any alias layers above them have still been fully
    // inventoried.
    return !type.isArray();
  }

  ArrayAttr getArrayQueryDimensions(const slang::ast::Type &type) {
    if (auto found = arrayQueryDimensionCache.find(&type);
        found != arrayQueryDimensionCache.end())
      return found->second;

    ArrayQueryDimensionInventory inventory;
    llvm::SmallPtrSet<const slang::ast::Type *, 4> activeAliases;
    if (!collectArrayQueryDimensions(type, inventory, activeAliases) ||
        !inventory.sawAlias) {
      arrayQueryDimensionCache.try_emplace(&type, ArrayAttr{});
      return {};
    }

    SmallVector<Attribute> dimensions;
    dimensions.reserve(inventory.unpacked.size() + inventory.packed.size());
    auto append = [&](const ArrayQueryDimension &dimension) {
      NamedAttrList descriptor;
      descriptor.set("kind", builder.getStringAttr(dimension.kind));
      descriptor.set("unpacked", builder.getBoolAttr(dimension.unpacked));
      if (dimension.kind == "fixed") {
        descriptor.set("left", builder.getI64IntegerAttr(dimension.left));
        descriptor.set("right", builder.getI64IntegerAttr(dimension.right));
      }
      if (dimension.indexType)
        descriptor.set("index_type", TypeAttr::get(typeConverter.convert(
                                         *dimension.indexType)));
      dimensions.push_back(builder.getDictionaryAttr(descriptor));
    };
    llvm::for_each(inventory.unpacked, append);
    llvm::for_each(inventory.packed, append);
    ArrayAttr result = builder.getArrayAttr(dimensions);
    arrayQueryDimensionCache.try_emplace(&type, result);
    return result;
  }

  /// Freeze every typedef layer before SlangTypeConverter canonicalizes the
  /// type. Each descriptor names the VPI semantic-child path and the ordered
  /// alias chain written at that layer. Keeping paths separate from the type
  /// payload handles aliases nested below arrays and aggregate fields without
  /// introducing executable wrapper types.
  ArrayAttr getVPITypedefLayers(const slang::ast::Type &root) {
    if (auto found = vpiTypedefLayerCache.find(&root);
        found != vpiTypedefLayerCache.end())
      return found->second;

    SmallVector<Attribute> layers;
    SmallVector<int64_t, 8> path;
    llvm::SmallPtrSet<const slang::ast::Type *, 8> active;
    std::function<bool(const slang::ast::Type &)> collect =
        [&](const slang::ast::Type &type) -> bool {
      using SK = slang::ast::SymbolKind;
      if (!active.insert(&type).second)
        return false;
      SmallVector<Attribute, 2> aliases;
      const slang::ast::Type *current = &type;
      while (current->kind == SK::TypeAlias) {
        const auto &alias = current->as<slang::ast::TypeAliasType>();
        aliases.push_back(getSemanticSymbolReference(alias));
        current = &alias.targetType.getType();
        if (!active.insert(current).second) {
          active.erase(&type);
          return false;
        }
      }
      if (!aliases.empty()) {
        NamedAttrList descriptor;
        descriptor.set("path", builder.getDenseI64ArrayAttr(path));
        descriptor.set("aliases", builder.getArrayAttr(aliases));
        layers.push_back(builder.getDictionaryAttr(descriptor));
      }

      auto descend = [&](int64_t child, const slang::ast::Type &nested) {
        path.push_back(child);
        bool complete = collect(nested);
        path.pop_back();
        return complete;
      };
      bool complete = true;
      switch (current->kind) {
      case SK::EnumType:
        complete = descend(0, current->as<slang::ast::EnumType>().baseType);
        break;
      case SK::PackedArrayType:
        complete =
            descend(0, current->as<slang::ast::PackedArrayType>().elementType);
        break;
      case SK::FixedSizeUnpackedArrayType:
        complete = descend(
            0,
            current->as<slang::ast::FixedSizeUnpackedArrayType>().elementType);
        break;
      case SK::DynamicArrayType:
        complete =
            descend(0, current->as<slang::ast::DynamicArrayType>().elementType);
        break;
      case SK::DPIOpenArrayType:
        complete =
            descend(0, current->as<slang::ast::DPIOpenArrayType>().elementType);
        break;
      case SK::QueueType:
        complete = descend(0, current->as<slang::ast::QueueType>().elementType);
        break;
      case SK::AssociativeArrayType: {
        const auto &array = current->as<slang::ast::AssociativeArrayType>();
        if (array.indexType)
          complete = descend(0, *array.indexType);
        if (complete)
          complete = descend(1, array.elementType);
        break;
      }
      case SK::PackedStructType:
      case SK::UnpackedStructType:
      case SK::PackedUnionType:
      case SK::UnpackedUnionType: {
        const slang::ast::Scope *scope = nullptr;
        if (current->kind == SK::PackedStructType)
          scope = &current->as<slang::ast::PackedStructType>();
        else if (current->kind == SK::UnpackedStructType)
          scope = &current->as<slang::ast::UnpackedStructType>();
        else if (current->kind == SK::PackedUnionType)
          scope = &current->as<slang::ast::PackedUnionType>();
        else
          scope = &current->as<slang::ast::UnpackedUnionType>();
        for (const slang::ast::FieldSymbol &field :
             scope->membersOfType<slang::ast::FieldSymbol>()) {
          if (!descend(field.fieldIndex, field.getType())) {
            complete = false;
            break;
          }
        }
        break;
      }
      default:
        break;
      }
      // The alias walk inserts every node it crosses; remove the whole chain
      // before returning so a legal repeated type in a sibling field is not
      // mistaken for recursion.
      current = &type;
      active.erase(current);
      while (current->kind == SK::TypeAlias) {
        current =
            &current->as<slang::ast::TypeAliasType>().targetType.getType();
        active.erase(current);
      }
      return complete;
    };
    if (!collect(root) || layers.empty()) {
      vpiTypedefLayerCache.try_emplace(&root, ArrayAttr{});
      return {};
    }
    ArrayAttr result = builder.getArrayAttr(layers);
    vpiTypedefLayerCache.try_emplace(&root, result);
    return result;
  }

  template <typename Node>
  const slang::ast::Type *getUncanonicalizedSemanticType(const Node &node) {
    if constexpr (std::derived_from<Node, slang::ast::Type>) {
      return &node;
    } else if constexpr (std::derived_from<Node, slang::ast::Expression>) {
      return node.type;
    } else if constexpr (requires { node.getType(); }) {
      if constexpr (std::same_as<std::remove_cvref_t<decltype(node.getType())>,
                                 slang::ast::Type>)
        return &node.getType();
    } else if constexpr (std::derived_from<Node, slang::ast::Symbol>) {
      if (const auto *declaredType = node.getDeclaredType())
        return &declaredType->getType();
    }
    return nullptr;
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
    } else if constexpr (std::same_as<std::remove_cvref_t<Node>,
                                      slang::ast::CallExpression>) {
      // Slang currently exposes the two value-returning global sampled
      // functions as `bit`, although 16.9.4 defines each as the sampled value
      // of its expression. Freeze the LRM result type in semantic IR so X/Z
      // and widths survive independently of their use context. The eight
      // transition/stability predicates intentionally retain Slang's bit
      // result type.
      StringRef name = node.getSubroutineName();
      if (node.isSystemCall() &&
          (name == "$past_gclk" || name == "$future_gclk") &&
          node.arguments().size() == 1 && node.arguments().front())
        return typeConverter.convert(*node.arguments().front()->type);
      return typeConverter.convert(*node.type);
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

  void appendStableTypeIdentity(
      llvm::raw_ostream &stream, const slang::ast::Type &type,
      llvm::SmallPtrSetImpl<const slang::ast::Type *> &active) {
    const slang::ast::Type &canonical = type.getCanonicalType();
    std::string path = canonical.getHierarchicalPath();
    std::string spelling = canonical.toString();
    stream << static_cast<uint32_t>(canonical.kind) << ':' << path.size() << ':'
           << path << ':' << spelling.size() << ':' << spelling;
    if (!active.insert(&canonical).second)
      return;
    if (const slang::ast::Type *element = canonical.getArrayElementType()) {
      stream << "[element:";
      appendStableTypeIdentity(stream, *element, active);
      stream << ']';
    }
    if (const slang::ast::Type *index = canonical.getAssociativeIndexType()) {
      stream << "[index:";
      appendStableTypeIdentity(stream, *index, active);
      stream << ']';
    }
    if (canonical.kind == slang::ast::SymbolKind::ClassType) {
      const auto &classType = canonical.as<slang::ast::ClassType>();
      for (const slang::ast::Symbol *parameter : classType.genericParameters) {
        stream << "[parameter:" << static_cast<uint32_t>(parameter->kind)
               << ':';
        if (parameter->kind == slang::ast::SymbolKind::Parameter) {
          const auto &value = parameter->as<slang::ast::ParameterSymbol>();
          std::string valueString = value.getValue().toString(
              std::numeric_limits<slang::bitwidth_t>::max(), true, true);
          stream << valueString.size() << ':' << valueString;
        } else if (parameter->kind == slang::ast::SymbolKind::TypeParameter) {
          const auto &typeParameter =
              parameter->as<slang::ast::TypeParameterSymbol>();
          appendStableTypeIdentity(stream, typeParameter.targetType.getType(),
                                   active);
        }
        stream << ']';
      }
    }
    active.erase(&canonical);
  }

  std::string
  getStableSpecializationKey(const slang::ast::ClassType &specialization) {
    std::string key;
    llvm::raw_string_ostream stream(key);
    if (specialization.genericClass)
      stream << specialization.genericClass->getHierarchicalPath();
    llvm::SmallPtrSet<const slang::ast::Type *, 8> active;
    for (const slang::ast::Symbol *parameter :
         specialization.genericParameters) {
      stream << '\0' << static_cast<uint32_t>(parameter->kind) << ':';
      if (parameter->kind == slang::ast::SymbolKind::Parameter) {
        const auto &value = parameter->as<slang::ast::ParameterSymbol>();
        std::string valueString = value.getValue().toString(
            std::numeric_limits<slang::bitwidth_t>::max(), true, true);
        stream << valueString.size() << ':' << valueString;
      } else if (parameter->kind == slang::ast::SymbolKind::TypeParameter) {
        const auto &typeParameter =
            parameter->as<slang::ast::TypeParameterSymbol>();
        appendStableTypeIdentity(stream, typeParameter.targetType.getType(),
                                 active);
      }
    }
    return key;
  }

  std::string getSymbolPath(const slang::ast::Symbol &symbol) {
    if (auto found = resolvedSymbolPaths.find(&symbol);
        found != resolvedSymbolPaths.end())
      return found->second;

    if (std::string path = symbol.getHierarchicalPath(); !path.empty()) {
      // Slang's display-oriented hierarchical path intentionally omits
      // lexical block identity. That makes two legal shadowing declarations
      // look identical and used to collapse their frozen bindings downstream.
      // Preserve the familiar path for the first declaration and disambiguate
      // later variables with a stable importer-local suffix. References call
      // this helper with the declaration symbol and therefore receive exactly
      // the same identity.
      if (symbol.kind == slang::ast::SymbolKind::Variable) {
        auto [claimed, inserted] =
            claimedVariablePaths.try_emplace(path, &symbol);
        if (!inserted && claimed->second != &symbol)
          path += ".$shadow." + std::to_string(nextShadowedSymbolId++);
      }
      resolvedSymbolPaths.try_emplace(&symbol, path);
      return path;
    }

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
    resolvedSymbolPaths.try_emplace(&symbol, path);
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

  const slang::ast::Scope *getCurrentScope() const {
    return currentScopes.empty() ? nullptr : currentScopes.back();
  }

  const slang::ast::Expression *getCurrentDefaultDisable() const {
    if (const slang::ast::Scope *scope = getCurrentScope())
      return compilation.getDefaultDisable(*scope);
    return nullptr;
  }

  template <typename Op>
  void addDefaultClocking(NamedAttrList &attrs,
                          const slang::ast::Scope *scope) {
    if (!scope)
      return;
    if (const slang::ast::Symbol *clocking =
            compilation.getDefaultClocking(*scope)) {
      OperationName operationName(Op::getOperationName(), builder.getContext());
      setSymbolReference(attrs, *clocking,
                         Op::getDefaultClockingSymbolAttrName(operationName),
                         Op::getDefaultClockingPathAttrName(operationName));
    }
  }

  const slang::analysis::AnalyzedProcedure *
  getAnalyzedProcedure(const slang::ast::Symbol &symbol) {
    if (symbol.kind == slang::ast::SymbolKind::Subroutine)
      return analysisManager.getAnalyzedSubroutine(
          symbol.as<slang::ast::SubroutineSymbol>());

    const slang::ast::Scope *scope = symbol.getParentScope();
    if (!scope)
      return nullptr;
    if (indexedAnalysisScopes.insert(scope).second)
      if (const slang::analysis::AnalyzedScope *analyzed =
              analysisManager.getAnalyzedScope(*scope))
        for (const slang::analysis::AnalyzedProcedure &procedure :
             analyzed->procedures)
          analyzedProcedures.try_emplace(procedure.analyzedSymbol, &procedure);
    auto found = analyzedProcedures.find(&symbol);
    return found == analyzedProcedures.end() ? nullptr : found->second;
  }

  void cacheContextualAssertionClocks(const slang::ast::Symbol &symbol) {
    if (!analyzedAssertionProcedures.insert(&symbol).second)
      return;
    const slang::analysis::AnalyzedProcedure *procedure =
        getAnalyzedProcedure(symbol);
    if (!procedure)
      return;
    const slang::ast::TimingControl *clock = procedure->getInferredClock();
    const slang::analysis::SensitivityList &sensitivity =
        procedure->getSensitivityList();
    for (const slang::analysis::AnalyzedAssertion *assertion :
         analysisManager.getAnalyzedAssertions(symbol)) {
      const auto *statement =
          std::get_if<const slang::ast::ConcurrentAssertionStatement *>(
              &assertion->astNode);
      if (!statement)
        continue;
      proceduralAssertions.insert(*statement);
      const slang::ast::TimingControl *semanticClock =
          assertion->getSemanticLeadingClock();
      if (clock && semanticClock == clock)
        contextualAssertionClocks.try_emplace(*statement, clock);
      if (clock && semanticClock &&
          sensitivity.kind ==
              slang::analysis::SensitivityList::Kind::Explicit &&
          sensitivity.timingControl &&
          (sensitivity.timingControl == clock ||
           sensitivity.timingControl->isEquivalentTo(*clock)) &&
          semanticClock->isEquivalentTo(*clock))
        proceduralAssertionsStartingOnCurrentClock.insert(*statement);
    }
  }

  static bool hasNamedOutermostProcessScope(const slang::ast::Symbol &symbol) {
    const auto *procedure = symbol.as_if<slang::ast::ProceduralBlockSymbol>();
    if (!procedure)
      return false;
    const slang::ast::Statement *body = &procedure->getBody();
    if (const auto *timed = body->as_if<slang::ast::TimedStatement>())
      body = &timed->stmt;
    const auto *block = body->as_if<slang::ast::BlockStatement>();
    return block && block->blockSymbol && !block->blockSymbol->name.empty();
  }

  static bool isStaticTimeZeroEquivalent(
      const slang::ast::Symbol &symbol,
      const slang::ast::ConcurrentAssertionStatement &assertion) {
    const auto *procedure = symbol.as_if<slang::ast::ProceduralBlockSymbol>();
    if (!procedure ||
        procedure->procedureKind != slang::ast::ProceduralBlockKind::Initial ||
        assertion.assertionKind == slang::ast::AssertionKind::Expect ||
        hasNamedOutermostProcessScope(symbol))
      return false;

    // A sole assertion reached by an unnamed initial process at time zero is
    // equivalent to a static monitor unless queue-time values must be saved.
    // The lowering verifies that latter condition before taking this path.
    const slang::ast::Statement *statement = &procedure->getBody();
    while (true) {
      if (const auto *block = statement->as_if<slang::ast::BlockStatement>()) {
        if (block->blockKind != slang::ast::StatementBlockKind::Sequential)
          return false;
        statement = &block->body;
        continue;
      }
      if (const auto *list = statement->as_if<slang::ast::StatementList>()) {
        if (list->list.size() != 1)
          return false;
        statement = list->list.front();
        continue;
      }
      return statement == &assertion;
    }
  }

  const slang::ast::ClockingBlockSymbol *
  getGlobalClocking(const slang::ast::CallExpression &call) {
    StringRef name = call.getSubroutineName();
    bool usesGlobalClock = name == "$global_clock" || name == "$past_gclk" ||
                           name == "$rose_gclk" || name == "$fell_gclk" ||
                           name == "$stable_gclk" || name == "$changed_gclk" ||
                           name == "$future_gclk" || name == "$rising_gclk" ||
                           name == "$falling_gclk" || name == "$steady_gclk" ||
                           name == "$changing_gclk";
    if (!call.isSystemCall() || !usesGlobalClock)
      return nullptr;
    const auto *system =
        std::get_if<slang::ast::CallExpression::SystemCallInfo>(
            &call.subroutine);
    if (!system || !system->scope)
      return nullptr;
    const slang::ast::Scope *scope = system->scope;
    while (scope) {
      for (const auto &clocking :
           scope->membersOfType<slang::ast::ClockingBlockSymbol>())
        if (clocking.isGlobal)
          return &clocking;
      scope = scope->asSymbol().getHierarchicalParent();
    }
    return nullptr;
  }

  bool requiresClockingEventMonitor(
      const slang::ast::ClockingBlockSymbol &clocking) const {
    const slang::ast::TimingControl &control = clocking.getEvent();
    if (control.as_if<slang::ast::EventListControl>())
      return true;
    const auto *event = control.as_if<slang::ast::SignalEventControl>();
    if (!event)
      return false;
    bool direct = event->expr.as_if<slang::ast::NamedValueExpression>() ||
                  event->expr.as_if<slang::ast::HierarchicalValueExpression>();
    return !direct || event->iffCondition;
  }

  void addStaticClockingEventDescriptor(
      NamedAttrList &attrs, const slang::ast::ClockingBlockSymbol &clocking) {
    if (requiresClockingEventMonitor(clocking)) {
      if (clocking.getEvent().as_if<slang::ast::EventListControl>())
        attrs.set("clocking_event_list", builder.getUnitAttr());
      attrs.set("clocking_event_monitor", builder.getUnitAttr());
      attrs.set("clocking_event_edge",
                slangir::EdgeKindAttr::get(builder.getContext(),
                                           slangir::EdgeKind::None));
      setSymbolReference(attrs, clocking,
                         builder.getStringAttr("clocking_event_symbol"),
                         builder.getStringAttr("clocking_event_path"));
      if (const auto *event =
              clocking.getEvent().as_if<slang::ast::SignalEventControl>()) {
        const slang::ast::Symbol *clockSymbol = nullptr;
        if (auto *named = event->expr.as_if<slang::ast::NamedValueExpression>())
          clockSymbol = &named->symbol;
        else if (auto *hierarchical =
                     event->expr
                         .as_if<slang::ast::HierarchicalValueExpression>())
          clockSymbol = &hierarchical->symbol;
        if (clockSymbol) {
          attrs.set("clocking_event_raw_edge",
                    slangir::EdgeKindAttr::get(builder.getContext(),
                                               convertEnum(event->edge)));
          setSymbolReference(attrs, *clockSymbol,
                             builder.getStringAttr("clocking_event_raw_symbol"),
                             builder.getStringAttr("clocking_event_raw_path"));
        }
      }
      return;
    }
    const auto *event =
        clocking.getEvent().as_if<slang::ast::SignalEventControl>();
    if (!event)
      return;
    attrs.set("clocking_event_edge",
              slangir::EdgeKindAttr::get(builder.getContext(),
                                         convertEnum(event->edge)));
    if (event->iffCondition)
      attrs.set("clocking_event_has_iff", builder.getUnitAttr());
    const slang::ast::Symbol *clockSymbol = nullptr;
    if (auto *named = event->expr.as_if<slang::ast::NamedValueExpression>())
      clockSymbol = &named->symbol;
    else if (auto *hierarchical =
                 event->expr.as_if<slang::ast::HierarchicalValueExpression>())
      clockSymbol = &hierarchical->symbol;
    if (!clockSymbol)
      return;
    setSymbolReference(attrs, *clockSymbol,
                       builder.getStringAttr("clocking_event_symbol"),
                       builder.getStringAttr("clocking_event_path"));
  }

  void addVirtualClockingEventDescriptor(
      NamedAttrList &attrs, const slang::ast::ClockingBlockSymbol &clocking) {
    if (requiresClockingEventMonitor(clocking)) {
      if (clocking.getEvent().as_if<slang::ast::EventListControl>())
        attrs.set("virtual_interface_clock_event_list", builder.getUnitAttr());
      attrs.set("virtual_interface_clock_event_monitor", builder.getUnitAttr());
      attrs.set("virtual_interface_clock_event_edge",
                slangir::EdgeKindAttr::get(builder.getContext(),
                                           slangir::EdgeKind::None));
      attrs.set("virtual_interface_clock_member",
                builder.getStringAttr(clocking.name));
      if (const auto *event =
              clocking.getEvent().as_if<slang::ast::SignalEventControl>()) {
        const slang::ast::Symbol *clockSymbol = nullptr;
        if (auto *named = event->expr.as_if<slang::ast::NamedValueExpression>())
          clockSymbol = &named->symbol;
        else if (auto *hierarchical =
                     event->expr
                         .as_if<slang::ast::HierarchicalValueExpression>())
          clockSymbol = &hierarchical->symbol;
        if (clockSymbol) {
          attrs.set("virtual_interface_clock_raw_event_edge",
                    slangir::EdgeKindAttr::get(builder.getContext(),
                                               convertEnum(event->edge)));
          attrs.set("virtual_interface_clock_raw_member",
                    builder.getStringAttr(clockSymbol->name));
        }
      }
      return;
    }
    const auto *event =
        clocking.getEvent().as_if<slang::ast::SignalEventControl>();
    if (!event)
      return;
    attrs.set("virtual_interface_clock_event_edge",
              slangir::EdgeKindAttr::get(builder.getContext(),
                                         convertEnum(event->edge)));
    const slang::ast::Symbol *clockSymbol = nullptr;
    if (auto *named = event->expr.as_if<slang::ast::NamedValueExpression>())
      clockSymbol = &named->symbol;
    else if (auto *hierarchical =
                 event->expr.as_if<slang::ast::HierarchicalValueExpression>())
      clockSymbol = &hierarchical->symbol;
    if (clockSymbol)
      attrs.set("virtual_interface_clock_member",
                builder.getStringAttr(clockSymbol->name));
    if (event->iffCondition)
      attrs.set("virtual_interface_clock_event_has_iff", builder.getUnitAttr());
  }

  /// Freeze the directly addressable event selected by an ordinary clocking
  /// block. Clocking-block references have void expression type in Slang;
  /// retaining the event signal separately lets executable lowering watch the
  /// actual storage while preserving the clocking block's symbol identity.
  void addStaticClockingEvent(NamedAttrList &attrs,
                              const slang::ast::ClockingBlockSymbol &clocking) {
    attrs.set("clocking_block_event", builder.getUnitAttr());
    addStaticClockingEventDescriptor(attrs, clocking);
  }

  void addStaticClockingSkew(NamedAttrList &attrs, StringRef prefix,
                             const slang::ast::ClockingSkew &skew,
                             bool defaultOneStep,
                             const slang::ast::ClockVarSymbol &clockVar) {
    attrs.set((prefix + "_edge").str(),
              slangir::EdgeKindAttr::get(builder.getContext(),
                                         convertEnum(skew.edge)));
    if (!skew.delay) {
      if (!skew.hasValue() && defaultOneStep)
        attrs.set((prefix + "_one_step").str(), builder.getUnitAttr());
      else if (!skew.hasValue())
        attrs.set((prefix + "_delay").str(), builder.getStringAttr("0"));
      else
        attrs.set((prefix + "_edge_only").str(), builder.getUnitAttr());
      return;
    }
    if (skew.delay->kind == slang::ast::TimingControlKind::OneStepDelay) {
      attrs.set((prefix + "_one_step").str(), builder.getUnitAttr());
      return;
    }
    if (const auto *delay = skew.delay->as_if<slang::ast::DelayControl>()) {
      slang::ast::EvalContext evalContext(clockVar);
      slang::ConstantValue value = delay->expr.eval(evalContext);
      if (value) {
        attrs.set((prefix + "_delay").str(),
                  builder.getStringAttr(formatConstant(value)));
        attrs.set((prefix + "_delay_is_real").str(),
                  builder.getBoolAttr(value.isReal() || value.isShortReal()));
      }
    }
  }

  void addStaticClockingVariable(NamedAttrList &attrs,
                                 const slang::ast::ClockVarSymbol &clockVar) {
    attrs.set("clocking_variable", builder.getUnitAttr());
    attrs.set("clocking_access_direction",
              slangir::ArgumentDirectionAttr::get(
                  builder.getContext(), convertEnum(clockVar.direction)));
    const slang::ast::Expression *source = clockVar.getInitializer();
    const slang::ast::Symbol *sourceSymbol = nullptr;
    if (auto *named = source ? source->as_if<slang::ast::NamedValueExpression>()
                             : nullptr)
      sourceSymbol = &named->symbol;
    else if (auto *hierarchical =
                 source
                     ? source->as_if<slang::ast::HierarchicalValueExpression>()
                     : nullptr)
      sourceSymbol = &hierarchical->symbol;
    if (sourceSymbol)
      setSymbolReference(attrs, *sourceSymbol,
                         builder.getStringAttr("clocking_source_symbol"),
                         builder.getStringAttr("clocking_source_path"));
    else if (source && clockVar.direction != slang::ast::ArgumentDirection::Out)
      attrs.set("clocking_source_expression", builder.getUnitAttr());

    const auto &clocking = clockVar.getParentScope()
                               ->asSymbol()
                               .template as<slang::ast::ClockingBlockSymbol>();
    addStaticClockingEventDescriptor(attrs, clocking);
    slang::TimeScale scale =
        clockVar.getParentScope()->getTimeScale().value_or(slang::TimeScale{});
    attrs.set("clocking_time_unit_fs",
              builder.getI64IntegerAttr(getFemtoseconds(scale.base)));
    attrs.set("clocking_time_precision_fs",
              builder.getI64IntegerAttr(getFemtoseconds(scale.precision)));
    slang::ast::ClockingSkew inputSkew = clockVar.inputSkew.hasValue()
                                             ? clockVar.inputSkew
                                             : clocking.getDefaultInputSkew();
    slang::ast::ClockingSkew outputSkew = clockVar.outputSkew.hasValue()
                                              ? clockVar.outputSkew
                                              : clocking.getDefaultOutputSkew();
    addStaticClockingSkew(attrs, "clocking_input_skew", inputSkew,
                          /*defaultOneStep=*/true, clockVar);
    addStaticClockingSkew(attrs, "clocking_output_skew", outputSkew,
                          /*defaultOneStep=*/false, clockVar);
  }

  template <typename Node>
  static bool canMakeDefaultAssertionInstance(const Node &node) {
    return std::ranges::all_of(node.ports, [](const auto *port) {
      return port->defaultValueSyntax != nullptr;
    });
  }

  /// A constraint prototype declares no body of its own: a `pure` constraint
  /// never has one (IEEE 1800-2017 18.5.2) and an extern prototype's body is
  /// defined out of block. slang represents that absence with the
  /// InvalidConstraint placeholder and separately diagnoses the prototypes
  /// whose required body is genuinely missing, so the placeholder alone does
  /// not mean the design is ill-formed.
  static bool
  isBodylessConstraintPrototype(const slang::ast::ConstraintBlockSymbol &node) {
    const slang::syntax::SyntaxNode *syntax = node.getSyntax();
    return syntax &&
           syntax->kind == slang::syntax::SyntaxKind::ConstraintPrototype &&
           node.getConstraints().kind == slang::ast::ConstraintKind::Invalid;
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

  bool addStaticPropagationDelay(NamedAttrList &attrs,
                                 const slang::ast::TimingControl *control,
                                 const slang::ast::Symbol &contextSymbol) {
    if (!control)
      return true;
    SmallVector<const slang::ast::Expression *, 3> expressions;
    if (const auto *delay = control->as_if<slang::ast::DelayControl>()) {
      expressions.push_back(&delay->expr);
    } else if (const auto *delay =
                   control->as_if<slang::ast::Delay3Control>()) {
      expressions.push_back(&delay->expr1);
      if (delay->expr2)
        expressions.push_back(delay->expr2);
      if (delay->expr3)
        expressions.push_back(delay->expr3);
    } else {
      return false;
    }

    slang::TimeScale scale;
    if (const slang::ast::Scope *scope = contextSymbol.getParentScope())
      scale = scope->getTimeScale().value_or(slang::TimeScale{});
    uint64_t unitFs = getFemtoseconds(scale.base);
    uint64_t precisionFs = getFemtoseconds(scale.precision);
    if (unitFs == 0 || precisionFs == 0 || unitFs < precisionFs ||
        unitFs % precisionFs != 0)
      return false;

    // IEEE 1800-2017 6.20.5 permits specparams in timing and delay
    // expressions. Slang deliberately excludes specparams from an ordinary
    // constant-evaluation context, so opt into them for the static
    // propagation-delay snapshot taken at elaboration.
    slang::ast::EvalContext evalContext(
        contextSymbol, slang::ast::EvalFlags::SpecparamsAllowed);
    SmallVector<int64_t, 3> delays;
    for (const slang::ast::Expression *expression : expressions) {
      slang::ConstantValue value = expression->eval(evalContext);
      if (!value)
        return false;
      long double amount = 0;
      if (value.isInteger()) {
        const slang::SVInt &integer = value.integer();
        if (!integer.hasUnknown() &&
            !(integer.isSigned() && integer.isNegative())) {
          std::optional<uint64_t> converted = integer.as<uint64_t>();
          if (!converted)
            return false;
          amount = static_cast<long double>(*converted);
        }
      } else if (value.isReal()) {
        amount = static_cast<long double>(value.real());
      } else if (value.isShortReal()) {
        amount = static_cast<long double>(value.shortReal());
      } else {
        return false;
      }
      if (!std::isfinite(amount))
        return false;
      if (amount < 0)
        amount = 0;
      long double steps =
          amount * static_cast<long double>(unitFs / precisionFs);
      long double femtoseconds = std::round(steps) * precisionFs;
      if (!std::isfinite(femtoseconds) || femtoseconds < 0 ||
          femtoseconds >
              static_cast<long double>(std::numeric_limits<int64_t>::max()))
        return false;
      delays.push_back(static_cast<int64_t>(femtoseconds));
    }
    attrs.set("delay_fs", builder.getDenseI64ArrayAttr(delays));
    return true;
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

    if constexpr (std::same_as<T, slang::ast::TimingPathSymbol>) {
      using TimingPath = slang::ast::TimingPathSymbol;
      attrs.set("timing_connection_full",
                builder.getBoolAttr(node.connectionKind ==
                                    TimingPath::ConnectionKind::Full));
      attrs.set("timing_polarity",
                builder.getI32IntegerAttr(static_cast<int32_t>(node.polarity)));
      attrs.set(
          "timing_edge_polarity",
          builder.getI32IntegerAttr(static_cast<int32_t>(node.edgePolarity)));
      attrs.set(
          "timing_edge_identifier",
          builder.getI32IntegerAttr(static_cast<int32_t>(node.edgeIdentifier)));
      const slang::ast::Expression *edgeSource = node.getEdgeSourceExpr();
      if (edgeSource)
        attrs.set("timing_edge_sensitive", builder.getUnitAttr());
      attrs.set("timing_state_dependent",
                builder.getBoolAttr(node.isStateDependent));
      if (node.getConditionExpr())
        attrs.set("timing_condition", builder.getUnitAttr());
      else if (node.isStateDependent)
        attrs.set("timing_ifnone", builder.getUnitAttr());

      struct DirectTimingTerminal {
        std::string path;
        uint64_t rootWidth;
        uint64_t low;
        uint64_t width;
        uint64_t lsb;
      };
      slang::ast::EvalContext terminalEvalContext(node);
      auto directStaticTerminal = [&](const slang::ast::Expression *expression)
          -> std::optional<DirectTimingTerminal> {
        if (!expression || !expression->type)
          return std::nullopt;
        const slang::ast::Symbol *symbol = expression->getSymbolReference();
        if (!symbol)
          return std::nullopt;
        const auto *value = symbol->template as_if<slang::ast::ValueSymbol>();
        if (!value)
          return std::nullopt;
        uint64_t width = expression->type->getBitWidth();
        uint64_t rootWidth = value->getType().getBitWidth();
        if (width == 0 || rootWidth == 0 || width > rootWidth ||
            !value->getType().hasFixedRange())
          return std::nullopt;
        uint64_t low = 0;
        uint64_t lsb = 0;
        if (expression->kind == slang::ast::ExpressionKind::ElementSelect ||
            expression->kind == slang::ast::ExpressionKind::RangeSelect) {
          // Slang normalizes a packed selector into physical offsets from the
          // right-hand end of the declared root range. Preserve the normalized
          // right bound separately: Clause 30.4.3 samples that semantic LSB,
          // which is not necessarily the low storage offset for an ascending
          // or reversed selected descriptor.
          std::optional<slang::ConstantRange> selected =
              expression->evalSelector(terminalEvalContext,
                                       /*enforceBounds=*/false);
          if (!selected || selected->left < 0 || selected->right < 0 ||
              selected->fullWidth() != width)
            return std::nullopt;
          low = static_cast<uint64_t>(selected->lower());
          lsb = static_cast<uint64_t>(selected->right);
          if (low > rootWidth || width > rootWidth - low)
            return std::nullopt;
        } else if (width != rootWidth) {
          return std::nullopt;
        }
        return DirectTimingTerminal{getSymbolPath(*symbol), rootWidth, low,
                                    width, lsb};
      };

      auto inputs = node.getInputs();
      auto outputs = node.getOutputs();
      attrs.set("timing_input_count", builder.getI64IntegerAttr(inputs.size()));
      attrs.set("timing_output_count",
                builder.getI64IntegerAttr(outputs.size()));
      SmallVector<Attribute> inputPaths;
      SmallVector<int64_t> inputWidths;
      SmallVector<Attribute> inputTerminals;
      for (const slang::ast::Expression *input : inputs) {
        std::optional<DirectTimingTerminal> terminal =
            directStaticTerminal(input);
        if (!terminal) {
          inputPaths.clear();
          inputWidths.clear();
          inputTerminals.clear();
          break;
        }
        inputPaths.push_back(builder.getStringAttr(terminal->path));
        inputWidths.push_back(terminal->width);
        inputTerminals.push_back(builder.getDictionaryAttr({
            builder.getNamedAttr("path", builder.getStringAttr(terminal->path)),
            builder.getNamedAttr(
                "root_width", builder.getI64IntegerAttr(terminal->rootWidth)),
            builder.getNamedAttr("low",
                                 builder.getI64IntegerAttr(terminal->low)),
            builder.getNamedAttr("width",
                                 builder.getI64IntegerAttr(terminal->width)),
            builder.getNamedAttr("lsb",
                                 builder.getI64IntegerAttr(terminal->lsb)),
        }));
      }
      if (inputPaths.size() == inputs.size()) {
        attrs.set("timing_input_paths", builder.getArrayAttr(inputPaths));
        attrs.set("timing_input_widths",
                  builder.getDenseI64ArrayAttr(inputWidths));
        attrs.set("timing_input_terminals",
                  builder.getArrayAttr(inputTerminals));
        if (inputs.size() == 1)
          attrs.set("timing_input_path", cast<StringAttr>(inputPaths.front()));
      }
      std::optional<DirectTimingTerminal> output;
      if (outputs.size() == 1)
        output = directStaticTerminal(outputs.front());
      if (output) {
        attrs.set("timing_output_path", builder.getStringAttr(output->path));
        attrs.set("timing_output_width",
                  builder.getI64IntegerAttr(output->width));
        attrs.set(
            "timing_output_terminal",
            builder.getDictionaryAttr({
                builder.getNamedAttr("path",
                                     builder.getStringAttr(output->path)),
                builder.getNamedAttr(
                    "root_width", builder.getI64IntegerAttr(output->rootWidth)),
                builder.getNamedAttr("low",
                                     builder.getI64IntegerAttr(output->low)),
                builder.getNamedAttr("width",
                                     builder.getI64IntegerAttr(output->width)),
                builder.getNamedAttr("lsb",
                                     builder.getI64IntegerAttr(output->lsb)),
            }));
      }

      slang::TimeScale scale;
      if (const slang::ast::Scope *scope = node.getParentScope())
        scale = scope->getTimeScale().value_or(slang::TimeScale{});
      uint64_t unitFs = getFemtoseconds(scale.base);
      uint64_t precisionFs = getFemtoseconds(scale.precision);
      SmallVector<int64_t, 12> delays;
      bool sdfAnnotated = false;
      bool staticDelays = unitFs != 0 && precisionFs != 0 &&
                          unitFs >= precisionFs && unitFs % precisionFs == 0;
      slang::ast::EvalContext evalContext(node);
      for (const slang::ast::Expression *expression : node.getDelays()) {
        slang::ConstantValue value = expression->eval(evalContext);
        long double amount = 0;
        if (value.isInteger()) {
          const slang::SVInt &integer = value.integer();
          std::optional<uint64_t> converted;
          if (!integer.hasUnknown() &&
              !(integer.isSigned() && integer.isNegative()))
            converted = integer.as<uint64_t>();
          if (!converted) {
            staticDelays = false;
            break;
          }
          amount = static_cast<long double>(*converted);
        } else if (value.isReal()) {
          amount = static_cast<long double>(value.real());
        } else if (value.isShortReal()) {
          amount = static_cast<long double>(value.shortReal());
        } else {
          staticDelays = false;
          break;
        }
        if (!std::isfinite(amount)) {
          staticDelays = false;
          break;
        }
        if (amount < 0)
          amount = 0;
        long double steps =
            amount * static_cast<long double>(unitFs / precisionFs);
        long double femtoseconds = std::round(steps) * precisionFs;
        if (!std::isfinite(femtoseconds) || femtoseconds < 0 ||
            femtoseconds >
                static_cast<long double>(std::numeric_limits<int64_t>::max())) {
          staticDelays = false;
          break;
        }
        delays.push_back(static_cast<int64_t>(femtoseconds));
      }
      if (const auto *annotations =
              sdfAnnotations.getTimingPathAnnotations(node)) {
        using DelayAnnotation = SDFAnnotationDatabase::DelayAnnotation;
        sdfAnnotated = true;
        bool singleDenseAbsolute =
            annotations->size() == 1 &&
            annotations->front().kind == DelayAnnotation::Kind::Absolute &&
            llvm::all_of(annotations->front().delays,
                         [](const std::optional<int64_t> &value) {
                           return value.has_value();
                         });
        if (singleDenseAbsolute) {
          // IEEE 1800-2017 32.5: keep the common dense ABSOLUTE update in its
          // compact source arity; no transient SDF state reaches Simulation IR.
          delays.clear();
          llvm::transform(
              annotations->front().delays, std::back_inserter(delays),
              [](const std::optional<int64_t> &value) { return *value; });
          staticDelays = true;
        } else {
          SmallVector<std::optional<int64_t>, 12> current;
          if (staticDelays) {
            llvm::transform(delays, std::back_inserter(current),
                            [](int64_t value) { return value; });
            current = expandTimingDelays(current);
          } else {
            current.assign(12, std::nullopt);
          }

          bool annotationValid = current.size() == 12;
          for (const DelayAnnotation &annotation : *annotations) {
            SmallVector<std::optional<int64_t>, 12> update =
                expandTimingDelays(annotation.delays);
            if (update.size() != 12) {
              annotationValid = false;
              break;
            }
            for (size_t index = 0; index != 12; ++index) {
              if (!update[index])
                continue;
              if (annotation.kind == DelayAnnotation::Kind::Absolute) {
                current[index] = *update[index];
                continue;
              }
              if (!current[index])
                continue;
              int64_t value = *current[index];
              int64_t increment = *update[index];
              // IEEE 1800-2017 32.6 applies INCREMENT to the value effective
              // after preceding annotations. Compute the negative magnitude
              // in unsigned space so INT64_MIN never incurs signed overflow.
              uint64_t decrement =
                  increment < 0 ? uint64_t(0) - uint64_t(increment) : 0;
              bool overflow =
                  (increment > 0 &&
                   value > std::numeric_limits<int64_t>::max() - increment) ||
                  (increment < 0 && static_cast<uint64_t>(value) < decrement);
              if (overflow) {
                llvm::errs() << *annotation.filename << ':' << annotation.line
                             << ':' << annotation.column
                             << ": error: SDF INCREMENT produces a negative "
                                "or overflowing path delay\n";
                annotationValid = false;
                break;
              }
              current[index] = value + increment;
            }
            if (!annotationValid)
              break;
          }

          bool hasUnknown =
              llvm::any_of(current, [](const std::optional<int64_t> &value) {
                return !value;
              });
          if (annotationValid && !hasUnknown) {
            delays.clear();
            llvm::transform(
                current, std::back_inserter(delays),
                [](const std::optional<int64_t> &value) { return *value; });
            staticDelays = true;
          } else {
            staticDelays = false;
            if (annotationValid)
              emitError(sourceLocation(node.location))
                  << "static SDF sparse or incremental delay fields require "
                     "constant current path delays";
            sawInvalidNode = true;
          }
        }
      }
      attrs.set("timing_delay_count",
                builder.getI64IntegerAttr(
                    sdfAnnotated ? delays.size() : node.getDelays().size()));
      if (staticDelays)
        attrs.set("timing_delay_fs", builder.getDenseI64ArrayAttr(delays));

      bool shapeSupported = false;
      if (inputTerminals.size() == inputs.size() && output) {
        if (node.connectionKind == TimingPath::ConnectionKind::Parallel)
          shapeSupported =
              inputs.size() == 1 &&
              static_cast<uint64_t>(inputWidths.front()) == output->width;
        else
          shapeSupported = !inputs.empty();
      }
      // IEEE 1800-2017 30.4.7 makes path polarity a declaration of the
      // expected relationship between source and destination transitions; it
      // does not invert data or alter which destination transition-delay bank
      // is selected. All three legal simple-path polarities can therefore use
      // the same frozen driver-delay representation.
      bool supportedCandidate =
          shapeSupported && outputs.size() == 1 && staticDelays &&
          (!edgeSource || !node.isStateDependent || node.getConditionExpr()) &&
          (edgeSource || (node.edgePolarity == TimingPath::Polarity::Unknown &&
                          node.edgeIdentifier == slang::ast::EdgeKind::None)) &&
          (sdfAnnotated || delays.size() == node.getDelays().size()) &&
          (delays.size() == 1 || delays.size() == 2 || delays.size() == 3 ||
           delays.size() == 6 || delays.size() == 12);
      if (supportedCandidate)
        attrs.set("obelisk.simple_timing_path", builder.getUnitAttr());
    }

    if constexpr (std::same_as<T, slang::ast::SystemTimingCheckSymbol>) {
      auto arguments = node.getArguments();
      attrs.set("timing_check_kind",
                builder.getI32IntegerAttr(
                    static_cast<int32_t>(node.timingCheckKind)));
      attrs.set("timing_check_arg_count",
                builder.getI64IntegerAttr(arguments.size()));
      SmallVector<int64_t> hasExpression;
      SmallVector<int64_t> hasCondition;
      SmallVector<int64_t> expressionChildren;
      SmallVector<int64_t> conditionChildren;
      SmallVector<Attribute> edges;
      SmallVector<Attribute> descriptors;
      SmallVector<int32_t> effectiveEdges;
      SmallVector<int32_t> conditionPredicates;
      SmallVector<int64_t> isTime;
      SmallVector<int64_t> timeFs;
      hasExpression.reserve(arguments.size());
      hasCondition.reserve(arguments.size());
      expressionChildren.reserve(arguments.size());
      conditionChildren.reserve(arguments.size());
      edges.reserve(arguments.size());
      descriptors.reserve(arguments.size());
      effectiveEdges.reserve(arguments.size());
      conditionPredicates.reserve(arguments.size());
      isTime.reserve(arguments.size());
      timeFs.reserve(arguments.size());
      slang::TimeScale scale;
      if (const slang::ast::Scope *scope = node.getParentScope())
        scale = scope->getTimeScale().value_or(slang::TimeScale{});
      uint64_t unitFs = getFemtoseconds(scale.base);
      uint64_t precisionFs = getFemtoseconds(scale.precision);
      slang::ast::EvalContext evalContext(node);
      int64_t nextChild = 0;
      bool staticTimes = unitFs != 0 && precisionFs != 0 &&
                         unitFs >= precisionFs && unitFs % precisionFs == 0;
      auto isTimeSlot = [&](size_t index) {
        using Kind = slang::ast::SystemTimingCheckKind;
        switch (node.timingCheckKind) {
        case Kind::Setup:
        case Kind::Hold:
        case Kind::Recovery:
        case Kind::Removal:
        case Kind::Skew:
        case Kind::TimeSkew:
          return index == 2;
        case Kind::SetupHold:
        case Kind::RecRem:
        case Kind::FullSkew:
          return index == 2 || index == 3;
        case Kind::Period:
          return index == 1;
        case Kind::Width:
          return index == 1 || index == 2;
        case Kind::NoChange:
          return index == 2 || index == 3;
        default:
          return false;
        }
      };
      auto freezeTime = [&](const slang::ast::Expression *expression)
          -> std::optional<int64_t> {
        if (!expression || !staticTimes)
          return std::nullopt;
        slang::ConstantValue value = expression->eval(evalContext);
        long double amount = 0;
        if (value.isInteger()) {
          const slang::SVInt &integer = value.integer();
          if (integer.hasUnknown())
            return std::nullopt;
          std::optional<int64_t> converted = integer.as<int64_t>();
          if (!converted)
            return std::nullopt;
          amount = static_cast<long double>(*converted);
        } else if (value.isReal()) {
          amount = static_cast<long double>(value.real());
        } else if (value.isShortReal()) {
          amount = static_cast<long double>(value.shortReal());
        } else {
          return std::nullopt;
        }
        // IEEE 1800-2017 3.14.1 rounds delay/time values to the declaring
        // design element's time precision before simulation; freeze this
        // Clause 31 limit in that precision here.
        long double steps =
            amount * static_cast<long double>(unitFs / precisionFs);
        long double femtoseconds = std::round(steps) * precisionFs;
        if (!std::isfinite(femtoseconds) ||
            femtoseconds <
                static_cast<long double>(std::numeric_limits<int64_t>::min()) ||
            femtoseconds >
                static_cast<long double>(std::numeric_limits<int64_t>::max()))
          return std::nullopt;
        return static_cast<int64_t>(femtoseconds);
      };
      auto unwrapImplicitConversions = [](const slang::ast::Expression *expr) {
        while (const auto *conversion =
                   expr ? expr->as_if<slang::ast::ConversionExpression>()
                        : nullptr) {
          if (!conversion->isImplicit())
            break;
          expr = &conversion->operand();
        }
        return expr;
      };
      auto classifyCondition = [&](const slang::ast::Expression *condition) {
        if (!condition)
          return int32_t{-1};
        const slang::ast::Expression *expression =
            unwrapImplicitConversions(condition);
        if (const auto *unary =
                expression->as_if<slang::ast::UnaryExpression>()) {
          // IEEE 1800-2017 31.7 applies this outer operator after reducing
          // the conditioning expression to its LSB. Freeze the operator even
          // when its operand needs a compiled observer later.
          if (unary->op == slang::ast::UnaryOperator::BitwiseNot)
            return int32_t{1}; // known zero
          return int32_t{0};
        }
        const auto *binary = expression->as_if<slang::ast::BinaryExpression>();
        if (!binary)
          return int32_t{0};
        slang::ConstantValue constant = binary->right().eval(evalContext);
        if (!constant.isInteger() || constant.integer().hasUnknown())
          return int32_t{0};
        std::optional<uint64_t> value = constant.integer().as<uint64_t>();
        if (!value || *value > 1)
          return int32_t{0};
        int32_t base = -1;
        switch (binary->op) {
        case slang::ast::BinaryOperator::Equality:
          base = 2;
          break;
        case slang::ast::BinaryOperator::Inequality:
          base = 4;
          break;
        case slang::ast::BinaryOperator::CaseEquality:
          base = 6;
          break;
        case slang::ast::BinaryOperator::CaseInequality:
          base = 8;
          break;
        default:
          return int32_t{0};
        }
        return base + static_cast<int32_t>(*value);
      };
      for (auto [index, argument] : llvm::enumerate(arguments)) {
        hasExpression.push_back(argument.expr != nullptr);
        hasCondition.push_back(argument.condition != nullptr);
        expressionChildren.push_back(argument.expr ? nextChild++ : -1);
        conditionChildren.push_back(argument.condition ? nextChild++ : -1);
        edges.push_back(slangir::EdgeKindAttr::get(builder.getContext(),
                                                   convertEnum(argument.edge)));
        SmallVector<Attribute> encoded;
        encoded.reserve(argument.edgeDescriptors.size());
        uint32_t descriptorMask = 0;
        for (const auto &descriptor : argument.edgeDescriptors) {
          encoded.push_back(builder.getStringAttr(
              StringRef(descriptor.data(), descriptor.size())));
          auto lowerDescriptor = [](char value) {
            return value == 'X' || value == 'Z' ? value + ('a' - 'A') : value;
          };
          char from = lowerDescriptor(descriptor[0]);
          char to = lowerDescriptor(descriptor[1]);
          if (from == '0' && to == '1')
            descriptorMask |= 1u << 0;
          else if (from == '0' && (to == 'x' || to == 'z'))
            descriptorMask |= 1u << 1;
          else if (from == '1' && to == '0')
            descriptorMask |= 1u << 2;
          else if (from == '1' && (to == 'x' || to == 'z'))
            descriptorMask |= 1u << 3;
          else if ((from == 'x' || from == 'z') && to == '0')
            descriptorMask |= 1u << 4;
          else if ((from == 'x' || from == 'z') && to == '1')
            descriptorMask |= 1u << 5;
        }
        descriptors.push_back(builder.getArrayAttr(encoded));
        int32_t effectiveEdge =
            static_cast<int32_t>(convertEnum(argument.edge));
        if (!argument.edgeDescriptors.empty()) {
          // IEEE 1800-2017 31.5 treats Z as X in transition descriptors.
          // Canonical sets keep their standard-edge spelling; proper subsets
          // freeze their six classes into the existing clock-entry edge word.
          effectiveEdge =
              descriptorMask == 0x23
                  ? static_cast<int32_t>(slangir::EdgeKind::PosEdge)
              : descriptorMask == 0x1c
                  ? static_cast<int32_t>(slangir::EdgeKind::NegEdge)
              : descriptorMask == 0x3f
                  ? static_cast<int32_t>(slangir::EdgeKind::BothEdges)
                  : static_cast<int32_t>(0x100 | descriptorMask);
        }
        effectiveEdges.push_back(effectiveEdge);
        conditionPredicates.push_back(classifyCondition(argument.condition));
        bool time = isTimeSlot(index);
        isTime.push_back(time);
        std::optional<int64_t> frozen =
            time ? freezeTime(argument.expr) : std::optional<int64_t>{0};
        if (!frozen)
          staticTimes = false;
        timeFs.push_back(frozen.value_or(0));
      }
      // IEEE 1800-2017 31.2 gives every check an ordered formal argument
      // ABI, including explicit optional holes, event-local conditions, and
      // transition descriptors. Preserve dense aligned metadata so later
      // lowering never reparses source text or builds a runtime check table.
      attrs.set("timing_check_arg_has_expression",
                builder.getDenseI64ArrayAttr(hasExpression));
      attrs.set("timing_check_arg_has_condition",
                builder.getDenseI64ArrayAttr(hasCondition));
      attrs.set("timing_check_arg_expression_children",
                builder.getDenseI64ArrayAttr(expressionChildren));
      attrs.set("timing_check_arg_condition_children",
                builder.getDenseI64ArrayAttr(conditionChildren));
      attrs.set("timing_check_arg_edges", builder.getArrayAttr(edges));
      attrs.set("timing_check_arg_edge_descriptors",
                builder.getArrayAttr(descriptors));
      attrs.set("timing_check_arg_effective_edges",
                builder.getDenseI32ArrayAttr(effectiveEdges));
      // IEEE 1800-2017 31.7 samples the conditioned expression at the timing
      // event. Freeze only its small deterministic/nondeterministic predicate;
      // the existing direct handle remains the publication-time operand.
      if (llvm::any_of(conditionPredicates,
                       [](int32_t predicate) { return predicate > 0; }))
        attrs.set("timing_check_arg_condition_predicates",
                  builder.getDenseI32ArrayAttr(conditionPredicates));
      attrs.set("timing_check_arg_is_time",
                builder.getDenseI64ArrayAttr(isTime));
      if (staticTimes)
        attrs.set("timing_check_arg_time_fs",
                  builder.getDenseI64ArrayAttr(timeFs));
      attrs.set("time_unit_fs", builder.getI64IntegerAttr(unitFs));
      attrs.set("time_precision_fs", builder.getI64IntegerAttr(precisionFs));

      using Kind = slang::ast::SystemTimingCheckKind;
      bool singleLimit = node.timingCheckKind == Kind::Setup ||
                         node.timingCheckKind == Kind::Hold ||
                         node.timingCheckKind == Kind::Recovery ||
                         node.timingCheckKind == Kind::Removal;
      bool combined = node.timingCheckKind == Kind::SetupHold ||
                      node.timingCheckKind == Kind::RecRem;
      bool skew = node.timingCheckKind == Kind::Skew;
      bool timeSkew = node.timingCheckKind == Kind::TimeSkew;
      bool fullSkew = node.timingCheckKind == Kind::FullSkew;
      bool period = node.timingCheckKind == Kind::Period;
      bool width = node.timingCheckKind == Kind::Width;
      bool noChange = node.timingCheckKind == Kind::NoChange;
      size_t requiredArguments = combined || fullSkew || noChange ? 4
                                 : period || width                ? 2
                                                                  : 3;
      bool unsupportedCombinedOption = false;
      if (combined) {
        // IEEE 1800-2017 31.3.3/.6 assign slots 5--8 to negative-check
        // conditions and delayed signals. Keep the positive, unconditioned
        // actor pay-for-play: retain those forms semantically for the later
        // Clause 31.9 tranche instead of silently ignoring them here.
        for (size_t index = 5; index < arguments.size(); ++index)
          unsupportedCombinedOption |= arguments[index].expr != nullptr ||
                                       arguments[index].condition != nullptr;
      }
      bool hasRequiredArguments = arguments.size() >= requiredArguments;
      bool representableConditions = hasRequiredArguments;
      bool representableEvents = hasRequiredArguments &&
                                 effectiveEdges[0] >= 0 &&
                                 (period || width || effectiveEdges[1] >= 0);
      bool customControlledEdge = hasRequiredArguments &&
                                  (effectiveEdges[0] & ~0x3f) == 0x100 &&
                                  (effectiveEdges[0] & 0x3f) != 0;
      bool controlledEdge =
          hasRequiredArguments &&
          (effectiveEdges[0] ==
               static_cast<int32_t>(slangir::EdgeKind::PosEdge) ||
           effectiveEdges[0] ==
               static_cast<int32_t>(slangir::EdgeKind::NegEdge) ||
           (period && (effectiveEdges[0] ==
                           static_cast<int32_t>(slangir::EdgeKind::BothEdges) ||
                       customControlledEdge)));
      bool nonnegativeTimes =
          llvm::all_of(llvm::zip_equal(timeFs, isTime), [](auto valueAndTime) {
            auto [value, time] = valueAndTime;
            return !time || value >= 0;
          });
      bool negativeCombined =
          combined && timeFs.size() > 3 && (timeFs[2] < 0 || timeFs[3] < 0);
      __int128 combinedWindow =
          negativeCombined ? static_cast<__int128>(timeFs[2]) + timeFs[3] : 0;
      bool validNegativeWindow =
          negativeCombined &&
          combinedWindow > static_cast<__int128>(precisionFs);
      if (negativeCombined)
        attrs.set("obelisk.negative_timing_check", builder.getUnitAttr());
      if (negativeCombined && !validNegativeWindow)
        attrs.set("obelisk.invalid_negative_timing_window",
                  builder.getUnitAttr());
      auto freezeFlag = [&](size_t index) -> std::optional<bool> {
        if (index >= arguments.size() || !arguments[index].expr)
          return false;
        slang::ConstantValue value = arguments[index].expr->eval(evalContext);
        if (!value)
          return std::nullopt;
        if (value.isInteger()) {
          if (value.integer().hasUnknown())
            return std::nullopt;
        } else if (!value.isReal() && !value.isShortReal()) {
          return std::nullopt;
        }
        return value.isTrue();
      };
      std::optional<bool> eventBased;
      std::optional<bool> remainActive;
      if (timeSkew || fullSkew) {
        eventBased = freezeFlag(fullSkew ? 5 : 4);
        remainActive = freezeFlag(fullSkew ? 6 : 5);
      }
      // IEEE 1800-2017 31.4.1, 31.4.4, and 31.4.5 make these
      // clock/control checks static event-distance comparisons. Canonical
      // $skew events reuse every existing standard-edge subscription;
      // $period requires a controlled edge, while $width additionally needs
      // a unique posedge/negedge inverse for its implicit timecheck event.
      bool noChangeReference =
          !noChange || (arguments[0].edgeDescriptors.empty() && controlledEdge);
      // IEEE 1800-2017 31.4.6 uniquely permits signed start/end offsets and
      // requires the reference to name only posedge or negedge, without an
      // edge-control descriptor. Its data event retains the ordinary direct
      // canonical edge/change forms and Clause 31.7 condition sampling.
      if ((singleLimit || combined || skew || timeSkew || fullSkew || period ||
           width || noChange) &&
          staticTimes &&
          (noChange || nonnegativeTimes || validNegativeWindow) &&
          hasRequiredArguments && arguments[0].expr &&
          ((period || width) || arguments[1].expr) && representableConditions &&
          representableEvents && noChangeReference &&
          (!(period || width) || controlledEdge) &&
          (!width || effectiveEdges[0] !=
                         static_cast<int32_t>(slangir::EdgeKind::BothEdges)) &&
          (!(timeSkew || fullSkew) || (eventBased && remainActive)) &&
          !unsupportedCombinedOption) {
        attrs.set("obelisk.basic_timing_check", builder.getUnitAttr());
        if (timeSkew || fullSkew) {
          // IEEE 1800-2017 31.4.2/.3 default to timer mode when the optional
          // event_based_flag is absent or zero. Freeze both mode flags here:
          // lowering must never select timer/event behavior at run time.
          attrs.set("timing_check_event_based",
                    builder.getBoolAttr(*eventBased));
          attrs.set("timing_check_remain_active",
                    builder.getBoolAttr(*remainActive));
        }
      }
    }

    if constexpr (std::same_as<T, slang::ast::PulseStyleSymbol>) {
      attrs.set(
          "pulse_style_kind",
          builder.getI32IntegerAttr(static_cast<int32_t>(node.pulseStyleKind)));
      SmallVector<Attribute> terminals;
      slang::ast::EvalContext evalContext(node);
      for (const slang::ast::Expression *expression : node.getTerminals()) {
        if (!expression || !expression->type)
          continue;
        const slang::ast::Symbol *symbol = expression->getSymbolReference();
        const auto *value =
            symbol ? symbol->template as_if<slang::ast::ValueSymbol>()
                   : nullptr;
        if (!value || !value->getType().hasFixedRange())
          continue;
        uint64_t rootWidth = value->getType().getBitWidth();
        uint64_t width = expression->type->getBitWidth();
        uint64_t low = 0;
        if (rootWidth == 0 || width == 0 || width > rootWidth)
          continue;
        if (expression->kind == slang::ast::ExpressionKind::ElementSelect ||
            expression->kind == slang::ast::ExpressionKind::RangeSelect) {
          std::optional<slang::ConstantRange> selected =
              expression->evalSelector(evalContext, /*enforceBounds=*/false);
          if (!selected || selected->left < 0 || selected->right < 0 ||
              selected->fullWidth() != width)
            continue;
          low = static_cast<uint64_t>(selected->lower());
          if (low > rootWidth || width > rootWidth - low)
            continue;
        } else if (width != rootWidth) {
          continue;
        }
        terminals.push_back(builder.getDictionaryAttr({
            builder.getNamedAttr("path",
                                 builder.getStringAttr(getSymbolPath(*symbol))),
            builder.getNamedAttr("root_width",
                                 builder.getI64IntegerAttr(rootWidth)),
            builder.getNamedAttr("low", builder.getI64IntegerAttr(low)),
            builder.getNamedAttr("width", builder.getI64IntegerAttr(width)),
        }));
      }
      attrs.set("pulse_style_terminal_count",
                builder.getI64IntegerAttr(node.getTerminals().size()));
      if (terminals.size() == node.getTerminals().size())
        attrs.set("pulse_style_terminals", builder.getArrayAttr(terminals));
    }

    if constexpr (std::same_as<T, slang::ast::SpecparamSymbol>) {
      // IEEE 1800-2017 30.7.1 defines PATHPULSE$ limits as constant timing
      // data, and 22.7 requires delay values to be rounded to the current time
      // precision. Freeze the rounded femtoseconds beside the semantic
      // declaration so path preparation never reparses source text and the
      // runtime never performs a name or timing-scale lookup.
      if (node.name.starts_with("PATHPULSE$")) {
        slang::TimeScale scale;
        if (const slang::ast::Scope *scope = node.getParentScope())
          scale = scope->getTimeScale().value_or(slang::TimeScale{});
        uint64_t unitFs = getFemtoseconds(scale.base);
        uint64_t precisionFs = getFemtoseconds(scale.precision);
        auto freezeLimit =
            [&](const slang::ConstantValue &value) -> std::optional<int64_t> {
          if (unitFs == 0 || precisionFs == 0 || unitFs < precisionFs ||
              unitFs % precisionFs != 0 || !value)
            return std::nullopt;
          long double amount = 0;
          if (value.isInteger()) {
            const slang::SVInt &integer = value.integer();
            if (integer.hasUnknown())
              return std::nullopt;
            if (integer.isSigned() && integer.isNegative())
              amount = 0;
            else {
              std::optional<uint64_t> converted = integer.as<uint64_t>();
              if (!converted)
                return std::nullopt;
              amount = static_cast<long double>(*converted);
            }
          } else if (value.isReal()) {
            amount = static_cast<long double>(value.real());
          } else if (value.isShortReal()) {
            amount = static_cast<long double>(value.shortReal());
          } else {
            return std::nullopt;
          }
          if (!std::isfinite(amount))
            return std::nullopt;
          if (amount < 0)
            amount = 0;
          long double steps =
              amount * static_cast<long double>(unitFs / precisionFs);
          long double femtoseconds = std::round(steps) * precisionFs;
          if (!std::isfinite(femtoseconds) || femtoseconds < 0 ||
              femtoseconds >
                  static_cast<long double>(std::numeric_limits<int64_t>::max()))
            return std::nullopt;
          return static_cast<int64_t>(femtoseconds);
        };
        std::optional<int64_t> reject = freezeLimit(node.getValue());
        std::optional<int64_t> error = reject;
        if (node.isPathPulse) {
          reject = freezeLimit(node.getPulseRejectLimit());
          if (std::optional<int64_t> second =
                  freezeLimit(node.getPulseErrorLimit()))
            error = second;
        }
        attrs.set("path_pulse_name", builder.getStringAttr(node.name));
        if (reject && error) {
          attrs.set("path_pulse_reject_fs", builder.getI64IntegerAttr(*reject));
          attrs.set("path_pulse_error_fs", builder.getI64IntegerAttr(*error));
        }
      }
    }

    if constexpr (std::same_as<T, slang::ast::InstanceBodySymbol>) {
      slang::TimeScale scale = node.getTimeScale().value_or(slang::TimeScale{});
      attrs.set("time_unit_fs",
                builder.getI64IntegerAttr(getFemtoseconds(scale.base)));
      attrs.set("time_precision_fs",
                builder.getI64IntegerAttr(getFemtoseconds(scale.precision)));
      if (node.parentInstance && node.parentInstance->isInterface())
        attrs.set("virtual_interface_identity",
                  typeConverter.getVirtualInterfaceIdentity(
                      node, *node.parentInstance));
      if (node.parentInstance) {
        const slang::ast::DefinitionSymbol &definition = node.getDefinition();
        attrs.set("obelisk_sim.vpi_definition_name",
                  builder.getStringAttr(definition.name));
        attrs.set("obelisk_sim.vpi_top",
                  builder.getBoolAttr(node.parentInstance->isTopLevel()));
        attrs.set("obelisk_sim.vpi_automatic",
                  builder.getBoolAttr(definition.defaultLifetime ==
                                      slang::ast::VariableLifetime::Automatic));
        attrs.set("obelisk_sim.vpi_cell_instance",
                  builder.getBoolAttr(definition.cellDefine));
        using VPIKind = reflection::VPIObjectKind;
        VPIKind scopeKind = VPIKind::Module;
        switch (node.parentInstance->getDefinition().definitionKind) {
        case slang::ast::DefinitionKind::Module:
          scopeKind = VPIKind::Module;
          break;
        case slang::ast::DefinitionKind::Interface:
          scopeKind = VPIKind::Interface;
          break;
        case slang::ast::DefinitionKind::Program:
          scopeKind = VPIKind::Program;
          break;
        }
        attrs.set("vpi_scope_kind",
                  builder.getI32IntegerAttr(static_cast<uint16_t>(scopeKind)));
        bool isScopeMember = false;
        if (const slang::ast::Scope *parent =
                node.parentInstance->getParentScope())
          for (const slang::ast::Symbol &member : parent->members())
            isScopeMember |= &member == node.parentInstance;
        if (!isScopeMember)
          attrs.set("is_virtual_interface_type_instance",
                    builder.getBoolAttr(true));
      }
    }

    if constexpr (std::same_as<T, slang::ast::PackageSymbol>) {
      attrs.set("obelisk_sim.vpi_definition_name",
                builder.getStringAttr(node.name));
      attrs.set("obelisk_sim.vpi_automatic",
                builder.getBoolAttr(node.defaultLifetime ==
                                    slang::ast::VariableLifetime::Automatic));
    }

    if constexpr (std::same_as<T, slang::ast::CompilationUnitSymbol>)
      attrs.set("obelisk_sim.vpi_definition_name",
                builder.getStringAttr("$unit"));

    if constexpr (std::same_as<T, slang::ast::InstanceArraySymbol>) {
      SET_OP_ATTR(ArrayRange, builder.getDenseI64ArrayAttr(
                                  {node.range.left, node.range.right}));
    }

    if constexpr (std::same_as<T, slang::ast::GenerateBlockArraySymbol>) {
      SmallVector<int64_t> indices;
      indices.reserve(node.entries.size());
      bool valid = true;
      for (const slang::ast::GenerateBlockSymbol *entry : node.entries) {
        const slang::SVInt *index = entry->getArrayIndex();
        std::optional<int64_t> value =
            index ? index->as<int64_t>() : std::nullopt;
        if (!value) {
          emitError(sourceLocation(entry->location))
              << "generate block index is not representable as signed i64";
          valid = false;
          continue;
        }
        indices.push_back(*value);
      }
      if (valid)
        SET_OP_ATTR(ArrayIndices, builder.getDenseI64ArrayAttr(indices));
    }

    if constexpr (std::same_as<T, slang::ast::PrimitiveSymbol> ||
                  std::same_as<T, slang::ast::PrimitiveInstanceSymbol>) {
      const slang::ast::PrimitiveSymbol &primitive = [&]() -> const auto & {
        if constexpr (std::same_as<T, slang::ast::PrimitiveSymbol>)
          return node;
        else
          return node.primitiveType;
      }();
      if (primitive.primitiveKind == slang::ast::PrimitiveSymbol::UserDefined) {
        SmallVector<NamedAttribute> metadata;
        SmallVector<Attribute> portNames;
        SmallVector<int64_t> portDirections;
        portNames.reserve(primitive.ports.size());
        portDirections.reserve(primitive.ports.size());
        for (const slang::ast::PrimitivePortSymbol *port : primitive.ports) {
          portNames.push_back(builder.getStringAttr(port->name));
          portDirections.push_back(static_cast<int64_t>(port->direction));
        }
        SmallVector<Attribute> tableInputs;
        SmallVector<int64_t> tableStates;
        SmallVector<int64_t> tableOutputs;
        SmallVector<int64_t> tableEdges;
        tableInputs.reserve(primitive.table.size());
        tableStates.reserve(primitive.table.size());
        tableOutputs.reserve(primitive.table.size());
        tableEdges.reserve(primitive.table.size());
        for (const slang::ast::PrimitiveSymbol::TableEntry &entry :
             primitive.table) {
          tableInputs.push_back(builder.getStringAttr(entry.inputs));
          tableStates.push_back(static_cast<unsigned char>(entry.state));
          tableOutputs.push_back(static_cast<unsigned char>(entry.output));
          tableEdges.push_back(entry.isEdgeSensitive);
        }
        metadata.push_back(builder.getNamedAttr(
            "name", builder.getStringAttr(primitive.name)));
        metadata.push_back(builder.getNamedAttr(
            "port_names", builder.getArrayAttr(portNames)));
        metadata.push_back(builder.getNamedAttr(
            "port_directions", builder.getDenseI64ArrayAttr(portDirections)));
        metadata.push_back(builder.getNamedAttr(
            "table_inputs", builder.getArrayAttr(tableInputs)));
        metadata.push_back(builder.getNamedAttr(
            "table_states", builder.getDenseI64ArrayAttr(tableStates)));
        metadata.push_back(builder.getNamedAttr(
            "table_outputs", builder.getDenseI64ArrayAttr(tableOutputs)));
        metadata.push_back(builder.getNamedAttr(
            "table_edges", builder.getDenseI64ArrayAttr(tableEdges)));
        metadata.push_back(builder.getNamedAttr(
            "is_sequential", builder.getBoolAttr(primitive.isSequential)));
        metadata.push_back(builder.getNamedAttr(
            "is_edge_sensitive",
            builder.getBoolAttr(primitive.isEdgeSensitive)));
        if (primitive.initVal)
          metadata.push_back(builder.getNamedAttr(
              "init_value",
              builder.getStringAttr(formatConstant(*primitive.initVal))));
        attrs.set(udpMetadataAttrName, builder.getDictionaryAttr(metadata));
      }
    }

    if constexpr (std::same_as<T, slang::ast::PrimitiveInstanceSymbol>) {
      attrs.set("primitive_name",
                builder.getStringAttr(node.primitiveType.name));
      auto [strength0, strength1] = node.getDriveStrength();
      if (strength0)
        SET_OP_ATTR(DriveStrength0,
                    slangir::DriveStrengthAttr::get(builder.getContext(),
                                                    convertEnum(*strength0)));
      if (strength1)
        SET_OP_ATTR(DriveStrength1,
                    slangir::DriveStrengthAttr::get(builder.getContext(),
                                                    convertEnum(*strength1)));
      if (const slang::ast::TimingControl *delay = node.getDelay();
          delay && !addStaticPropagationDelay(attrs, delay, node)) {
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

    // Elaboration already folds the expressions that SystemVerilog requires to
    // be constant - a part-select bound, a replication count - and caches the
    // result on the node. Carrying that value across means those reach lowering
    // as the constants they are, instead of re-implementing constant evaluation
    // against the imported tree. Nodes that carry their own literal value are
    // left alone; the folded attribute is only for what is computed.
    // IEEE 1800-2017 6.23: a type reference may be compared with another type
    // reference, and 6.22.1 settles that comparison by whether the referenced
    // types match. Number each distinct matching type so those comparisons can
    // be resolved later by comparing numbers.
    // A `type(type(...))` reference targets the type-reference type itself,
    // which names no data type to match against, so it stays unnumbered and
    // its comparisons are refused rather than silently resolved.
    if constexpr (std::same_as<T, slang::ast::TypeReferenceExpression>)
      if (node.targetType.kind != slang::ast::SymbolKind::TypeRefType)
        attrs.set(
            typeReferenceIdentityAttrName,
            builder.getI64IntegerAttr(matchingTypeIdentity(node.targetType)));

    constexpr bool carriesOwnLiteralValue =
        std::same_as<T, slang::ast::IntegerLiteral> ||
        std::same_as<T, slang::ast::UnbasedUnsizedIntegerLiteral>;
    if constexpr (std::derived_from<T, slang::ast::Expression> &&
                  !carriesOwnLiteralValue) {
      if (const slang::ConstantValue *folded = node.getConstant();
          folded && folded->isInteger())
        attrs.set(foldedConstantAttrName,
                  builder.getStringAttr(formatConstant(*folded)));
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

    if constexpr (std::same_as<T, slang::ast::IntegerLiteral>)
      if (node.isDeclaredUnsized)
        SET_OP_ATTR(IsDeclaredUnsized, builder.getBoolAttr(true));

    if constexpr (std::same_as<T, slang::ast::MinTypMaxExpression>) {
      unsigned selectedIndex = &node.selected() == &node.min()   ? 0
                               : &node.selected() == &node.typ() ? 1
                                                                 : 2;
      attrs.set("selected_index", builder.getI64IntegerAttr(selectedIndex));
    }

    if constexpr (std::same_as<
                      T, slang::ast::StructuredAssignmentPatternExpression>) {
      SET_OP_ATTR(MemberSetterCount,
                  builder.getI64IntegerAttr(node.memberSetters.size()));
      SmallVector<int64_t> memberOrdinals;
      memberOrdinals.reserve(node.memberSetters.size());
      for (const auto &setter : node.memberSetters)
        memberOrdinals.push_back(
            setter.member->template as<slang::ast::FieldSymbol>().fieldIndex);
      SET_OP_ATTR(MemberSetterOrdinals,
                  builder.getDenseI64ArrayAttr(memberOrdinals));
      SET_OP_ATTR(TypeSetterCount,
                  builder.getI64IntegerAttr(node.typeSetters.size()));
      SmallVector<Type> typeSetterTypes;
      typeSetterTypes.reserve(node.typeSetters.size());
      for (const auto &setter : node.typeSetters)
        typeSetterTypes.push_back(typeConverter.convert(*setter.type));
      SET_OP_ATTR(TypeSetterTypes, builder.getTypeArrayAttr(typeSetterTypes));
      SET_OP_ATTR(IndexSetterCount,
                  builder.getI64IntegerAttr(node.indexSetters.size()));
      SET_OP_ATTR(HasDefaultSetter,
                  builder.getBoolAttr(node.defaultSetter != nullptr));
    }

    if constexpr (std::same_as<T,
                               slang::ast::StreamingConcatenationExpression>) {
      SET_OP_ATTR(SliceSize, builder.getI64IntegerAttr(node.getSliceSize()));
      SET_OP_ATTR(BitstreamWidth,
                  builder.getI64IntegerAttr(node.getBitstreamWidth()));
      SET_OP_ATTR(StreamCount,
                  builder.getI64IntegerAttr(node.streams().size()));
      SmallVector<int64_t> withFlags;
      withFlags.reserve(node.streams().size());
      for (const auto &stream : node.streams())
        withFlags.push_back(stream.withExpr != nullptr);
      SET_OP_ATTR(StreamWithFlags, builder.getDenseI64ArrayAttr(withFlags));
      SET_OP_ATTR(IsFixedSize, builder.getBoolAttr(node.isFixedSize()));
    }

    if constexpr (std::same_as<T, slang::ast::NamedValueExpression> ||
                  std::same_as<T, slang::ast::HierarchicalValueExpression>) {
      setReferencedSymbol<Op>(attrs, node.symbol);
      if (node.symbol.kind == slang::ast::SymbolKind::ClockVar)
        addStaticClockingVariable(
            attrs, node.symbol.template as<slang::ast::ClockVarSymbol>());
    } else if constexpr (std::same_as<T,
                                      slang::ast::ArbitrarySymbolExpression>) {
      setReferencedSymbol<Op>(attrs, *node.symbol);
      if (node.symbol->kind == slang::ast::SymbolKind::ClockingBlock)
        addStaticClockingEvent(
            attrs, node.symbol->template as<slang::ast::ClockingBlockSymbol>());
    } else if constexpr (std::same_as<T, slang::ast::MemberAccessExpression>) {
      setReferencedSymbol<Op>(attrs, node.member);
      attrs.set("member_name", builder.getStringAttr(node.member.name));
      if (node.member.kind == slang::ast::SymbolKind::ModportPort) {
        const auto &port =
            node.member.template as<slang::ast::ModportPortSymbol>();
        attrs.set("virtual_interface_access_direction",
                  slangir::ArgumentDirectionAttr::get(
                      builder.getContext(), convertEnum(port.direction)));
        if (const slang::ast::Scope *scope = port.getParentScope())
          attrs.set("virtual_interface_modport",
                    builder.getStringAttr(scope->asSymbol().name));
      } else if (node.member.kind == slang::ast::SymbolKind::ClockVar) {
        const auto &clockVar =
            node.member.template as<slang::ast::ClockVarSymbol>();
        if (const slang::ast::Expression *source = clockVar.getInitializer()) {
          const slang::ast::Symbol *sourceSymbol = nullptr;
          if (auto *named = source->as_if<slang::ast::NamedValueExpression>())
            sourceSymbol = &named->symbol;
          else if (auto *hierarchical =
                       source->as_if<slang::ast::HierarchicalValueExpression>())
            sourceSymbol = &hierarchical->symbol;
          if (sourceSymbol)
            attrs.set("virtual_interface_clocking_signal_member",
                      builder.getStringAttr(sourceSymbol->name));
          else if (clockVar.direction != slang::ast::ArgumentDirection::Out)
            attrs.set("virtual_interface_clocking_source_expression",
                      builder.getUnitAttr());
        }
        attrs.set("virtual_interface_access_direction",
                  slangir::ArgumentDirectionAttr::get(
                      builder.getContext(), convertEnum(clockVar.direction)));
        attrs.set("virtual_interface_clocking", builder.getUnitAttr());
        const slang::ast::Type &receiverType =
            unwrapTypeAliases(node.value().type->getCanonicalType());
        if (receiverType.kind == slang::ast::SymbolKind::VirtualInterfaceType) {
          const auto &virtualType =
              receiverType.as<slang::ast::VirtualInterfaceType>();
          if (virtualType.modport)
            attrs.set("virtual_interface_modport",
                      builder.getStringAttr(virtualType.modport->name));
        }
        if (const slang::ast::Scope *scope = clockVar.getParentScope()) {
          attrs.set("virtual_interface_clocking_block",
                    builder.getStringAttr(scope->asSymbol().name));
          const auto &clocking =
              scope->asSymbol().as<slang::ast::ClockingBlockSymbol>();
          addVirtualClockingEventDescriptor(attrs, clocking);

          slang::TimeScale scale =
              scope->getTimeScale().value_or(slang::TimeScale{});
          attrs.set("virtual_interface_clock_time_unit_fs",
                    builder.getI64IntegerAttr(getFemtoseconds(scale.base)));
          attrs.set(
              "virtual_interface_clock_time_precision_fs",
              builder.getI64IntegerAttr(getFemtoseconds(scale.precision)));
        }
        auto addSkew = [&](StringRef prefix,
                           const slang::ast::ClockingSkew &skew,
                           bool defaultOneStep) {
          attrs.set((prefix + "_edge").str(),
                    slangir::EdgeKindAttr::get(builder.getContext(),
                                               convertEnum(skew.edge)));
          if (!skew.delay) {
            if (!skew.hasValue() && defaultOneStep)
              attrs.set((prefix + "_one_step").str(), builder.getUnitAttr());
            else if (!skew.hasValue())
              attrs.set((prefix + "_delay").str(), builder.getStringAttr("0"));
            else
              attrs.set((prefix + "_edge_only").str(), builder.getUnitAttr());
            return;
          }
          if (skew.delay->kind == slang::ast::TimingControlKind::OneStepDelay) {
            attrs.set((prefix + "_one_step").str(), builder.getUnitAttr());
            return;
          }
          if (const auto *delay =
                  skew.delay->as_if<slang::ast::DelayControl>()) {
            slang::ast::EvalContext evalContext(clockVar);
            slang::ConstantValue value = delay->expr.eval(evalContext);
            if (value) {
              attrs.set((prefix + "_delay").str(),
                        builder.getStringAttr(formatConstant(value)));
              attrs.set(
                  (prefix + "_delay_is_real").str(),
                  builder.getBoolAttr(value.isReal() || value.isShortReal()));
            }
          }
        };
        const auto &clocking =
            clockVar.getParentScope()
                ->asSymbol()
                .template as<slang::ast::ClockingBlockSymbol>();
        slang::ast::ClockingSkew inputSkew =
            clockVar.inputSkew.hasValue() ? clockVar.inputSkew
                                          : clocking.getDefaultInputSkew();
        slang::ast::ClockingSkew outputSkew =
            clockVar.outputSkew.hasValue() ? clockVar.outputSkew
                                           : clocking.getDefaultOutputSkew();
        addSkew("virtual_interface_clock_input_skew", inputSkew,
                /*defaultOneStep=*/true);
        addSkew("virtual_interface_clock_output_skew", outputSkew,
                /*defaultOneStep=*/false);
      } else if (node.member.kind == slang::ast::SymbolKind::ClockingBlock) {
        const auto &clocking =
            node.member.template as<slang::ast::ClockingBlockSymbol>();
        attrs.set("virtual_interface_clocking_block_event",
                  builder.getUnitAttr());
        const slang::ast::Type &receiverType =
            unwrapTypeAliases(node.value().type->getCanonicalType());
        if (receiverType.kind == slang::ast::SymbolKind::VirtualInterfaceType) {
          const auto &virtualType =
              receiverType.as<slang::ast::VirtualInterfaceType>();
          if (virtualType.modport)
            attrs.set("virtual_interface_modport",
                      builder.getStringAttr(virtualType.modport->name));
        }
        addVirtualClockingEventDescriptor(attrs, clocking);
      }
      if (node.member.kind == slang::ast::SymbolKind::Field) {
        const auto &field = node.member.template as<slang::ast::FieldSymbol>();
        attrs.set("field_ordinal", builder.getI64IntegerAttr(field.fieldIndex));
        attrs.set("packed_offset", builder.getI64IntegerAttr(field.bitOffset));
      }
    } else if constexpr (std::same_as<T, slang::ast::TaggedPattern>) {
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
      if (sdfAnnotations.isAppliedCall(node))
        attrs.set("obelisk.sdf_compile_time_applied", builder.getUnitAttr());
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
      bool isArrayQuery =
          node.isSystemCall() &&
          llvm::StringSwitch<bool>(node.getSubroutineName())
              .Cases({"$dimensions", "$unpacked_dimensions", "$left", "$right",
                      "$low", "$high", "$increment", "$size"},
                     true)
              .Default(false);
      if (isArrayQuery && !node.arguments().empty() && node.arguments().front())
        if (ArrayAttr dimensions =
                getArrayQueryDimensions(*node.arguments().front()->type))
          attrs.set(arrayQueryDimensionsAttrName, dimensions);
      if (const slang::ast::ClockingBlockSymbol *clocking =
              getGlobalClocking(node))
        addStaticClockingEvent(attrs, *clocking);
      SmallVector<int64_t> defaultedArguments;
      defaultedArguments.reserve(node.arguments().size());
      const slang::ast::SubroutineSymbol *calledSubroutine = nullptr;
      if (const auto *subroutine =
              std::get_if<const slang::ast::SubroutineSymbol *>(
                  &node.subroutine))
        calledSubroutine = *subroutine;
      if (node.thisClass()) {
        const slang::ast::Type &receiverType = *node.thisClass()->type;
        if (receiverType.isVirtualInterface()) {
          const auto &virtualType =
              receiverType.as<slang::ast::VirtualInterfaceType>();
          if (virtualType.modport) {
            attrs.set("virtual_interface_call_modport",
                      builder.getStringAttr(virtualType.modport->name));
            for (const auto &prototype :
                 virtualType.modport
                     ->membersOfType<slang::ast::MethodPrototypeSymbol>()) {
              if (prototype.name != node.getSubroutineName())
                continue;
              if (prototype.flags.has(slang::ast::MethodFlags::ModportImport))
                attrs.set("virtual_interface_call_import",
                          builder.getUnitAttr());
              if (prototype.flags.has(slang::ast::MethodFlags::ModportExport))
                attrs.set("virtual_interface_call_export",
                          builder.getUnitAttr());
            }
          }
        }
      }
      auto formalArguments =
          calledSubroutine
              ? calledSubroutine->getArguments()
              : std::span<const slang::ast::FormalArgumentSymbol *const>();
      for (auto [index, argument] : llvm::enumerate(node.arguments())) {
        bool isDefaulted =
            index < formalArguments.size() &&
            formalArguments[index]->getDefaultValue() == argument;
        defaultedArguments.push_back(isDefaulted);
      }
      SET_OP_ATTR(DefaultedArguments,
                  builder.getDenseI64ArrayAttr(defaultedArguments));
      if (node.isSystemCall() && node.getSubroutineName() == "$typename" &&
          node.arguments().size() == 1) {
        slang::ast::TypePrinter printer;
        printer.append(*node.arguments().front()->type);
        SET_OP_ATTR(TypenameSpelling,
                    builder.getStringAttr(printer.toString()));
      }
      bool enumMethod =
          node.isSystemCall() &&
          llvm::StringSwitch<bool>(node.getSubroutineName())
              .Cases({"first", "last", "next", "prev", "num", "name"}, true)
              .Default(false);
      if (enumMethod && !node.arguments().empty()) {
        const slang::ast::Type &receiverType =
            node.arguments().front()->type->getCanonicalType();
        if (receiverType.isEnum()) {
          SmallVector<Attribute> values;
          SmallVector<Attribute> names;
          for (const slang::ast::EnumValueSymbol &value :
               receiverType.as<slang::ast::EnumType>()
                   .membersOfType<slang::ast::EnumValueSymbol>()) {
            values.push_back(
                builder.getStringAttr(formatConstant(value.getValue())));
            names.push_back(builder.getStringAttr(value.name));
          }
          SET_OP_ATTR(EnumMethodValues, builder.getArrayAttr(values));
          SET_OP_ATTR(EnumMethodNames, builder.getArrayAttr(names));
        }
      }
      if (node.isSystemCall() && node.getSubroutineName() == "$cast" &&
          node.arguments().size() == 2) {
        const slang::ast::Type &destinationType =
            node.arguments().front()->type->getCanonicalType();
        const slang::ast::Type &sourceType =
            node.arguments()[1]->type->getCanonicalType();
        slangir::DynamicCastKind kind;
        if (!destinationType.isClass()) {
          if (destinationType.isEnum() && sourceType.isIntegral())
            kind = slangir::DynamicCastKind::EnumMembership;
          else if ((destinationType.isSingular() && sourceType.isSingular()
                        ? destinationType.isCastCompatible(sourceType)
                        : destinationType.isAssignmentCompatible(sourceType)))
            kind = slangir::DynamicCastKind::AlwaysSuccess;
          else
            kind = slangir::DynamicCastKind::AlwaysFail;
        } else if (destinationType.isAssignmentCompatible(sourceType)) {
          kind = slangir::DynamicCastKind::AlwaysSuccess;
        } else if (!sourceType.isClass() ||
                   !sourceType.isAssignmentCompatible(destinationType)) {
          kind = slangir::DynamicCastKind::AlwaysFail;
        } else {
          kind = slangir::DynamicCastKind::ClassRuntime;
        }
        SET_OP_ATTR(DynamicCastKind, slangir::DynamicCastKindAttr::get(
                                         builder.getContext(), kind));
        if (kind == slangir::DynamicCastKind::EnumMembership) {
          SmallVector<Attribute> values;
          for (const slang::ast::EnumValueSymbol &value :
               destinationType.as<slang::ast::EnumType>()
                   .membersOfType<slang::ast::EnumValueSymbol>())
            values.push_back(
                builder.getStringAttr(formatConstant(value.getValue())));
          SET_OP_ATTR(DynamicCastEnumValues, builder.getArrayAttr(values));
        }
      }
      // IEEE 1800-2017 13.4.1: `void'(some_function())` uses a call as a
      // statement and discards its return value, so the call keeps its
      // function semantics. slang drops the void conversion, leaving the
      // syntax as the only record of the spelling, and 6.24.2 makes the
      // difference observable: only the task form of $cast reports a failed
      // cast as a run-time error.
      if (node.syntax && node.syntax->parent &&
          node.syntax->parent->kind ==
              slang::syntax::SyntaxKind::VoidCastedCallStatement)
        SET_OP_ATTR(IsVoidCasted, builder.getUnitAttr());
      if (node.isSystemCall() &&
          (node.getSubroutineName() == "$readmemb" ||
           node.getSubroutineName() == "$readmemh") &&
          node.arguments().size() >= 2) {
        auto enumValues = [&](const slang::ast::Type &type) {
          SmallVector<Attribute> values;
          for (const slang::ast::EnumValueSymbol &value :
               type.getCanonicalType()
                   .as<slang::ast::EnumType>()
                   .membersOfType<slang::ast::EnumValueSymbol>())
            values.push_back(
                builder.getStringAttr(formatConstant(value.getValue())));
          return builder.getArrayAttr(values);
        };
        const slang::ast::Type *memory =
            &node.arguments()[1]->type->getCanonicalType();
        while (memory->isUnpackedArray()) {
          if (memory->isAssociativeArray()) {
            const slang::ast::Type *index = memory->getAssociativeIndexType();
            if (index && index->getCanonicalType().isEnum())
              SET_OP_ATTR(ReadmemEnumKeyValues,
                          enumValues(index->getCanonicalType()));
          }
          memory = memory->getArrayElementType();
        }
        if (memory->getCanonicalType().isEnum())
          SET_OP_ATTR(ReadmemEnumElementValues,
                      enumValues(memory->getCanonicalType()));
      }
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
        StringRef systemName = node.getSubroutineName();
        if (systemName == "$printtimescale" || systemName == "$timeunit" ||
            systemName == "$timeprecision") {
          if (std::optional<slang::TimeScale> scale =
                  system->scope->getTimeScale()) {
            attrs.set("system_scope_time_unit_fs",
                      builder.getI64IntegerAttr(getFemtoseconds(scale->base)));
            attrs.set(
                "system_scope_time_precision_fs",
                builder.getI64IntegerAttr(getFemtoseconds(scale->precision)));
          }
          const slang::ast::Scope *timeScope = system->scope;
          while (timeScope) {
            const slang::ast::Symbol &timeScopeSymbol = timeScope->asSymbol();
            if (timeScopeSymbol.kind == slang::ast::SymbolKind::InstanceBody ||
                timeScopeSymbol.kind == slang::ast::SymbolKind::Package ||
                timeScopeSymbol.kind ==
                    slang::ast::SymbolKind::CompilationUnit) {
              std::string path = getSymbolPath(timeScopeSymbol);
              if (timeScopeSymbol.kind != slang::ast::SymbolKind::InstanceBody)
                path += "::";
              attrs.set("system_time_scope_path", builder.getStringAttr(path));
              break;
            }
            timeScope = timeScopeSymbol.getParentScope();
          }
        }
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
      // Slang materializes parameterized virtual-interface types as synthetic
      // InstanceSymbols so their member types can be resolved. Unlike an
      // elaborated design instance, such a symbol is parented for lookup but
      // is deliberately not inserted into its parent's member list.
      bool isScopeMember = false;
      if (const slang::ast::Scope *parent = node.getParentScope())
        for (const slang::ast::Symbol &member : parent->members())
          isScopeMember |= &member == &node;
      if (!isScopeMember)
        SET_OP_ATTR(IsVirtualInterfaceTypeInstance, builder.getBoolAttr(true));

      const auto flags = node.body.flags;
      bool fromBind = flags.has(slang::ast::InstanceFlags::FromBind);
      // Slang carries ParentFromBind on the directly inserted instance too,
      // and propagates TargetedByBind through that inserted subtree. Normalize
      // those implementation flags into the three disjoint provenance facts
      // that the semantic tree and user-facing report promise.
      bool belowBind =
          !fromBind && flags.has(slang::ast::InstanceFlags::ParentFromBind);
      bool bindTarget =
          flags.has(slang::ast::InstanceFlags::TargetedByBind) &&
          llvm::any_of(node.body.members(),
                       [](const slang::ast::Symbol &member) {
                         return containsDirectBoundInstance(member);
                       });
      if (fromBind)
        attrs.set("is_from_bind", builder.getBoolAttr(true));
      if (belowBind)
        attrs.set("is_below_bind", builder.getBoolAttr(true));
      if (bindTarget)
        attrs.set("is_bind_target", builder.getBoolAttr(true));

      // Slang has already applied every configuration and bind rule by this
      // point. Freeze the effective result rather than rebuilding the source
      // rule tree downstream; configuration declarations are intentionally
      // not part of Slang's semantic AST visitation surface.
      if (node.resolvedConfig || fromBind || belowBind || bindTarget)
        attrs.set("selected_cell",
                  builder.getStringAttr(getQualifiedLibraryCell(
                      node.getDefinition(), node.getDefinition().name)));
      if (const slang::ast::ResolvedConfig *config = node.resolvedConfig) {
        attrs.set("configuration",
                  builder.getStringAttr(getQualifiedLibraryCell(
                      config->useConfig, config->useConfig.name)));
        attrs.set("configuration_root",
                  builder.getStringAttr(getSymbolPath(config->rootInstance)));
        SmallVector<Attribute> liblist;
        liblist.reserve(config->liblist.size());
        for (const slang::SourceLibrary *library : config->liblist)
          liblist.push_back(builder.getStringAttr(library->name));
        attrs.set("configuration_liblist", builder.getArrayAttr(liblist));
        if (const slang::ast::ConfigRule *rule = config->configRule) {
          attrs.set("configuration_rule_kind",
                    builder.getStringAttr(getConfigurationRuleKind(*rule)));
          if (std::optional<TypeAttr> range =
                  sourceRangeAttr(rule->syntax->sourceRange()))
            attrs.set("configuration_rule_source_range", *range);
        }
      }
    } else if constexpr (std::same_as<T, slang::ast::ContinuousAssignSymbol>) {
      auto [strength0, strength1] = node.getDriveStrength();
      if (strength0)
        SET_OP_ATTR(DriveStrength0,
                    slangir::DriveStrengthAttr::get(builder.getContext(),
                                                    convertEnum(*strength0)));
      if (strength1)
        SET_OP_ATTR(DriveStrength1,
                    slangir::DriveStrengthAttr::get(builder.getContext(),
                                                    convertEnum(*strength1)));
      if (const slang::ast::TimingControl *delay = node.getDelay();
          delay && !addStaticPropagationDelay(attrs, delay, node)) {
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
      if (const auto *resolutionFunction =
              getEffectiveResolutionFunction(node.netType))
        setSymbolReference(
            attrs, *resolutionFunction,
            Op::getResolutionFunctionSymbolAttrName(operationName),
            Op::getResolutionFunctionPathAttrName(operationName));
      auto [strength0, strength1] = node.getDriveStrength();
      if (std::optional<slang::ast::ChargeStrength> charge =
              node.getChargeStrength()) {
        SET_OP_ATTR(ChargeStrength,
                    slangir::ChargeStrengthAttr::get(builder.getContext(),
                                                     convertEnum(*charge)));
      } else {
        if (strength0)
          SET_OP_ATTR(DriveStrength0,
                      slangir::DriveStrengthAttr::get(builder.getContext(),
                                                      convertEnum(*strength0)));
        if (strength1)
          SET_OP_ATTR(DriveStrength1,
                      slangir::DriveStrengthAttr::get(builder.getContext(),
                                                      convertEnum(*strength1)));
      }
      if (const slang::ast::TimingControl *delay = node.getDelay();
          delay && !addStaticPropagationDelay(attrs, delay, node)) {
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
    } else if constexpr (std::same_as<T, slang::ast::ConversionExpression>) {
      attrs.set("is_implicit", builder.getBoolAttr(node.isImplicit()));
      if (node.isConstCast)
        attrs.set("is_const_cast", builder.getBoolAttr(true));
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
      const slang::ast::Symbol *targetSymbol = node.target.getSymbolReference();
      if (targetSymbol)
        setSymbolReference(attrs, *targetSymbol,
                           Op::getTargetSymbolAttrName(operationName),
                           Op::getTargetPathAttrName(operationName));
      const auto &targetExpression =
          node.target.template as<slang::ast::ArbitrarySymbolExpression>();
      SET_OP_ATTR(
          IsHierarchical,
          builder.getBoolAttr(targetExpression.hierRef.target != nullptr));
      // Slang resolves `module_instance.interface_port.extern_task` to the
      // interface stub symbol. For a fork/join extern, however, IEEE 25.7.4
      // defines that spelling as the one provider activation owned by the
      // module instance. Preserve the resolved symbol dependency but freeze
      // the provider task's elaborated path for control-target assignment.
      const auto *targetSubroutine =
          targetSymbol &&
                  targetSymbol->kind == slang::ast::SymbolKind::Subroutine
              ? &targetSymbol->as<slang::ast::SubroutineSymbol>()
              : nullptr;
      const auto *targetPrototype =
          targetSubroutine && targetSubroutine->flags.has(
                                  slang::ast::MethodFlags::InterfaceExtern)
              ? targetSubroutine->getPrototype()
              : nullptr;
      const slang::ast::InterfacePortSymbol *interfacePort = nullptr;
      if (targetPrototype &&
          targetPrototype->flags.has(slang::ast::MethodFlags::ForkJoin))
        for (const auto &element : targetExpression.hierRef.path) {
          if (element.symbol->kind != slang::ast::SymbolKind::InterfacePort)
            continue;
          interfacePort =
              &element.symbol->template as<slang::ast::InterfacePortSymbol>();
        }
      if (interfacePort) {
        if (const slang::ast::Scope *scope = interfacePort->getParentScope()) {
          std::string providerPath = getSymbolPath(scope->asSymbol());
          if (!providerPath.empty())
            providerPath += '.';
          providerPath += targetSymbol->name;
          attrs.set(Op::getTargetPathAttrName(operationName),
                    builder.getStringAttr(providerPath));
        }
      }
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
          if (dpi.specString.valueText() == "DPI") {
            emitError(sourceLocation(dpi.specString.location()))
                << "legacy SystemVerilog 3.1a `DPI` imports are unsupported; "
                   "use `DPI-C`";
            sawInvalidNode = true;
          }
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
    } else if constexpr (std::same_as<T, slang::ast::ModportPortSymbol>) {
      attrs.set("direction",
                slangir::ArgumentDirectionAttr::get(
                    builder.getContext(), convertEnum(node.direction)));
      if (const slang::syntax::SyntaxNode *syntax = node.getSyntax();
          syntax &&
          syntax->kind == slang::syntax::SyntaxKind::ModportExplicitPort)
        attrs.set("modport_explicit_connection", builder.getUnitAttr());
    } else if constexpr (std::same_as<T, slang::ast::ClockingBlockSymbol>) {
      SET_OP_ATTR(IsDefault, builder.getBoolAttr(node.isDefault));
      SET_OP_ATTR(IsGlobal, builder.getBoolAttr(node.isGlobal));
      if (requiresClockingEventMonitor(node)) {
        attrs.set("clocking_event_monitor", builder.getUnitAttr());
        if (node.getEvent().template as_if<slang::ast::EventListControl>())
          attrs.set("clocking_event_list", builder.getUnitAttr());
      }
    } else if constexpr (std::same_as<T, slang::ast::AssertionPortSymbol>) {
      if (node.direction)
        SET_OP_ATTR(Direction,
                    slangir::ArgumentDirectionAttr::get(
                        builder.getContext(), convertEnum(*node.direction)));
      SET_OP_ATTR(HasDefaultValue,
                  builder.getBoolAttr(node.defaultValueSyntax != nullptr));
      SET_OP_ATTR(IsLocalVariable, builder.getBoolAttr(node.isLocalVar()));
    } else if constexpr (std::same_as<T, slang::ast::SequenceSymbol> ||
                         std::same_as<T, slang::ast::PropertySymbol>) {
      SmallVector<Attribute> portSymbols;
      SmallVector<Attribute> portPaths;
      for (const slang::ast::AssertionPortSymbol *port : node.ports) {
        portSymbols.push_back(getSemanticSymbolReference(*port));
        portPaths.push_back(builder.getStringAttr(getSymbolPath(*port)));
      }
      SET_OP_ATTR(PortCount, builder.getI64IntegerAttr(node.ports.size()));
      SET_OP_ATTR(PortSymbols, builder.getArrayAttr(portSymbols));
      SET_OP_ATTR(PortPaths, builder.getArrayAttr(portPaths));
      SmallVector<const slang::ast::Symbol *> ports(node.ports.begin(),
                                                    node.ports.end());
      currentPendingReferenceArrays.push_back(
          {std::move(ports), Op::getPortSymbolsAttrName(operationName)});
      SET_OP_ATTR(HasDefaultInstance,
                  builder.getBoolAttr(canMakeDefaultAssertionInstance(node)));
      addDefaultClocking<Op>(attrs, node.getParentScope());
    } else if constexpr (std::same_as<T, slang::ast::CheckerSymbol>) {
      SmallVector<Attribute> portSymbols;
      SmallVector<Attribute> portPaths;
      SmallVector<const slang::ast::Symbol *> ports;
      portSymbols.reserve(node.ports.size());
      portPaths.reserve(node.ports.size());
      ports.reserve(node.ports.size());
      for (const slang::ast::AssertionPortSymbol *port : node.ports) {
        portSymbols.push_back(getSemanticSymbolReference(*port));
        portPaths.push_back(builder.getStringAttr(getSymbolPath(*port)));
        ports.push_back(port);
      }
      SET_OP_ATTR(PortCount, builder.getI64IntegerAttr(node.ports.size()));
      SET_OP_ATTR(PortSymbols, builder.getArrayAttr(portSymbols));
      SET_OP_ATTR(PortPaths, builder.getArrayAttr(portPaths));
      currentPendingReferenceArrays.push_back(
          {std::move(ports), Op::getPortSymbolsAttrName(operationName)});
    } else if constexpr (std::same_as<T, slang::ast::CheckerInstanceSymbol>) {
      setSymbolReference(attrs, node.body.checker,
                         Op::getReferencedCheckerSymbolAttrName(operationName),
                         Op::getReferencedCheckerPathAttrName(operationName));

      const auto flags = node.body.flags;
      bool fromBind = flags.has(slang::ast::InstanceFlags::FromBind);
      bool belowBind =
          !fromBind && flags.has(slang::ast::InstanceFlags::ParentFromBind);
      bool bindTarget =
          flags.has(slang::ast::InstanceFlags::TargetedByBind) &&
          llvm::any_of(node.body.members(),
                       [](const slang::ast::Symbol &member) {
                         return containsDirectBoundInstance(member);
                       });
      if (fromBind)
        attrs.set("is_from_bind", builder.getBoolAttr(true));
      if (belowBind)
        attrs.set("is_below_bind", builder.getBoolAttr(true));
      if (bindTarget)
        attrs.set("is_bind_target", builder.getBoolAttr(true));
      if (fromBind || belowBind || bindTarget)
        attrs.set("selected_cell",
                  builder.getStringAttr(getQualifiedLibraryCell(
                      node.body.checker, node.body.checker.name)));

      SmallVector<Attribute> formalSymbols;
      SmallVector<Attribute> formalPaths;
      SmallVector<int64_t> actualKinds;
      SmallVector<int64_t> hasActual;
      SmallVector<int64_t> hasOutputInitial;
      SmallVector<int64_t> attributeCounts;
      SmallVector<const slang::ast::Symbol *> formals;
      auto connections = node.getPortConnections();
      formalSymbols.reserve(connections.size());
      formalPaths.reserve(connections.size());
      actualKinds.reserve(connections.size());
      hasActual.reserve(connections.size());
      hasOutputInitial.reserve(connections.size());
      attributeCounts.reserve(connections.size());
      formals.reserve(connections.size());
      for (const slang::ast::CheckerInstanceSymbol::Connection &connection :
           connections) {
        formalSymbols.push_back(getSemanticSymbolReference(connection.formal));
        formalPaths.push_back(
            builder.getStringAttr(getSymbolPath(connection.formal)));
        formals.push_back(&connection.formal);
        int64_t actualKind = 2;
        if (std::holds_alternative<const slang::ast::Expression *>(
                connection.actual))
          actualKind = 0;
        else if (std::holds_alternative<const slang::ast::AssertionExpr *>(
                     connection.actual))
          actualKind = 1;
        actualKinds.push_back(actualKind);
        hasActual.push_back(
            std::visit([](const auto *actual) { return actual != nullptr; },
                       connection.actual));
        hasOutputInitial.push_back(connection.getOutputInitialExpr() !=
                                   nullptr);
        attributeCounts.push_back(connection.attributes.size());
      }
      SET_OP_ATTR(ConnectionCount,
                  builder.getI64IntegerAttr(connections.size()));
      SET_OP_ATTR(ConnectionFormalSymbols, builder.getArrayAttr(formalSymbols));
      SET_OP_ATTR(ConnectionFormalPaths, builder.getArrayAttr(formalPaths));
      SET_OP_ATTR(ConnectionActualKinds,
                  builder.getDenseI64ArrayAttr(actualKinds));
      SET_OP_ATTR(ConnectionHasActual, builder.getDenseI64ArrayAttr(hasActual));
      SET_OP_ATTR(ConnectionHasOutputInitial,
                  builder.getDenseI64ArrayAttr(hasOutputInitial));
      SET_OP_ATTR(ConnectionAttributeCounts,
                  builder.getDenseI64ArrayAttr(attributeCounts));
      SET_OP_ATTR(IsProcedural, builder.getBoolAttr(node.body.isProcedural));
      currentPendingReferenceArrays.push_back(
          {std::move(formals),
           Op::getConnectionFormalSymbolsAttrName(operationName)});
    } else if constexpr (std::same_as<T,
                                      slang::ast::CheckerInstanceBodySymbol>) {
      setSymbolReference(attrs, node.checker,
                         Op::getReferencedCheckerSymbolAttrName(operationName),
                         Op::getReferencedCheckerPathAttrName(operationName));
      assert(node.parentInstance && "checker body has no parent instance");
      setSymbolReference(attrs, *node.parentInstance,
                         Op::getParentInstanceSymbolAttrName(operationName),
                         Op::getParentInstancePathAttrName(operationName));
      SET_OP_ATTR(InstanceDepth, builder.getI64IntegerAttr(node.instanceDepth));
      SET_OP_ATTR(InstanceFlags, builder.getI64IntegerAttr(node.flags.bits()));
      SET_OP_ATTR(IsProcedural, builder.getBoolAttr(node.isProcedural));
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
      if (const auto *resolutionFunction = getEffectiveResolutionFunction(node))
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
        sampleFormals +=
            formal.flags.has(slang::ast::VariableFlags::CoverageSampleFormal);
      SET_OP_ATTR(SampleFormalCount, builder.getI64IntegerAttr(sampleFormals));
      SET_OP_ATTR(HasCoverageEvent,
                  builder.getBoolAttr(node.getCoverageEvent() != nullptr));
    } else if constexpr (std::same_as<T, slang::ast::CovergroupBodySymbol>) {
      SET_OP_ATTR(OptionCount, builder.getI64IntegerAttr(node.options.size()));
    } else if constexpr (std::same_as<T, slang::ast::CoverpointSymbol>) {
      SET_OP_ATTR(HasIff, builder.getBoolAttr(node.getIffExpr() != nullptr));
      SET_OP_ATTR(OptionCount, builder.getI64IntegerAttr(node.options.size()));
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
      SET_OP_ATTR(HasWith, builder.getBoolAttr(node.getWithExpr() != nullptr));
      SET_OP_ATTR(ValueCount,
                  builder.getI64IntegerAttr(node.getValues().size()));
      SET_OP_ATTR(TransitionSetCount,
                  builder.getI64IntegerAttr(node.getTransList().size()));
    } else if constexpr (std::same_as<T, slang::ast::NewCovergroupExpression>) {
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
    } else if constexpr (std::same_as<T, slang::ast::ConditionalExpression>) {
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
    } else if constexpr (std::same_as<T, slang::ast::RandCaseStatement>) {
      SET_OP_ATTR(ItemCount, builder.getI64IntegerAttr(node.items.size()));
    } else if constexpr (std::same_as<T, slang::ast::RandSequenceStatement>) {
      attrs.set("production_count",
                builder.getI64IntegerAttr(node.productions.size()));
      attrs.set("has_first_production",
                builder.getBoolAttr(node.firstProduction != nullptr));
      if (node.firstProduction) {
        attrs.set("first_production_path",
                  builder.getStringAttr(getSymbolPath(*node.firstProduction)));
        currentPendingReferences.push_back(
            {node.firstProduction, builder.getStringAttr("first_production")});
      }
    } else if constexpr (std::same_as<T, slang::ast::RandSeqProductionSymbol>) {
      SmallVector<int64_t> itemCounts;
      SmallVector<int64_t> hasWeights;
      SmallVector<int64_t> hasWeightCodeBlocks;
      SmallVector<int64_t> isRandJoin;
      SmallVector<int64_t> hasRandJoinExpressions;
      SmallVector<const slang::ast::Symbol *> ruleBlocks;
      for (const auto &rule : node.getRules()) {
        itemCounts.push_back(rule.prods.size());
        hasWeights.push_back(rule.weightExpr != nullptr);
        hasWeightCodeBlocks.push_back(rule.codeBlock.has_value());
        isRandJoin.push_back(rule.isRandJoin);
        hasRandJoinExpressions.push_back(rule.randJoinExpr != nullptr);
        ruleBlocks.push_back(rule.ruleBlock);
      }
      attrs.set("argument_count",
                builder.getI64IntegerAttr(node.arguments.size()));
      attrs.set("rule_count",
                builder.getI64IntegerAttr(node.getRules().size()));
      attrs.set("rule_item_counts", builder.getDenseI64ArrayAttr(itemCounts));
      attrs.set("rule_has_weights", builder.getDenseI64ArrayAttr(hasWeights));
      attrs.set("rule_has_weight_code_blocks",
                builder.getDenseI64ArrayAttr(hasWeightCodeBlocks));
      attrs.set("rule_is_rand_join", builder.getDenseI64ArrayAttr(isRandJoin));
      attrs.set("rule_has_rand_join_expressions",
                builder.getDenseI64ArrayAttr(hasRandJoinExpressions));
      currentPendingReferenceArrays.push_back(
          {std::move(ruleBlocks), builder.getStringAttr("rule_blocks")});
    } else if constexpr (std::same_as<T, slang::ast::ProdItem>) {
      attrs.set("argument_count", builder.getI64IntegerAttr(node.args.size()));
      attrs.set("has_target", builder.getBoolAttr(node.target != nullptr));
      if (node.target) {
        attrs.set("target_path",
                  builder.getStringAttr(getSymbolPath(*node.target)));
        currentPendingReferences.push_back(
            {node.target, builder.getStringAttr("target")});
      }
    } else if constexpr (std::same_as<T, slang::ast::CodeBlockProd>) {
      attrs.set("block_path",
                builder.getStringAttr(getSymbolPath(*node.block)));
      currentPendingReferences.push_back(
          {node.block, builder.getStringAttr("block")});
    } else if constexpr (std::same_as<T, slang::ast::IfElseProd>) {
      attrs.set("has_else", builder.getBoolAttr(node.elseItem.has_value()));
    } else if constexpr (std::same_as<T, slang::ast::CaseProd>) {
      SmallVector<int64_t> expressionCounts;
      expressionCounts.reserve(node.items.size());
      for (const auto &item : node.items)
        expressionCounts.push_back(item.expressions.size());
      attrs.set("item_count", builder.getI64IntegerAttr(node.items.size()));
      attrs.set("item_expression_counts",
                builder.getDenseI64ArrayAttr(expressionCounts));
      attrs.set("has_default",
                builder.getBoolAttr(node.defaultItem.has_value()));
    } else if constexpr (std::same_as<T, slang::ast::InsideExpression>) {
      SET_OP_ATTR(ItemCount,
                  builder.getI64IntegerAttr(node.rangeList().size()));
    } else if constexpr (std::same_as<T, slang::ast::ValueRangeExpression>) {
      SET_OP_ATTR(RangeKind, slangir::ValueRangeKindAttr::get(
                                 builder.getContext(),
                                 static_cast<slangir::ValueRangeKind>(
                                     static_cast<int>(node.rangeKind))));
    } else if constexpr (std::same_as<T, slang::ast::DistExpression>) {
      SmallVector<int64_t> hasWeight;
      SmallVector<int64_t> weightKinds;
      hasWeight.reserve(node.items().size());
      weightKinds.reserve(node.items().size());
      for (const auto &item : node.items()) {
        hasWeight.push_back(item.weight.has_value());
        weightKinds.push_back(
            item.weight &&
            item.weight->kind ==
                slang::ast::DistExpression::DistWeight::PerRange);
      }
      SET_OP_ATTR(ItemCount, builder.getI64IntegerAttr(node.items().size()));
      SET_OP_ATTR(ItemHasWeight, builder.getDenseI64ArrayAttr(hasWeight));
      SET_OP_ATTR(ItemWeightKinds, builder.getDenseI64ArrayAttr(weightKinds));
      const auto *defaultWeight = node.defaultWeight();
      SET_OP_ATTR(HasDefaultWeight,
                  builder.getBoolAttr(defaultWeight != nullptr));
      SET_OP_ATTR(DefaultWeightKind,
                  builder.getI64IntegerAttr(
                      defaultWeight &&
                      defaultWeight->kind ==
                          slang::ast::DistExpression::DistWeight::PerRange));
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
      if (!currentProcedures.empty())
        cacheContextualAssertionClocks(*currentProcedures.back());
      bool isProcedural = proceduralAssertions.contains(&node);
      bool hasContextualClock = contextualAssertionClocks.contains(&node);
      SET_OP_ATTR(AssertionKind,
                  slangir::AssertionKindAttr::get(
                      builder.getContext(), convertEnum(node.assertionKind)));
      SET_OP_ATTR(HasPassAction, builder.getBoolAttr(node.ifTrue != nullptr));
      SET_OP_ATTR(HasFailAction, builder.getBoolAttr(node.ifFalse != nullptr));
      if (isProcedural)
        SET_OP_ATTR(IsProcedural, builder.getBoolAttr(true));
      if (isProcedural && !currentProcedures.empty() &&
          isStaticTimeZeroEquivalent(*currentProcedures.back(), node))
        SET_OP_ATTR(IsStaticTimeZeroEquivalent, builder.getBoolAttr(true));
      if (isProcedural && !currentProcedures.empty() &&
          hasNamedOutermostProcessScope(*currentProcedures.back()))
        attrs.set("outer_process_scope_named", builder.getBoolAttr(true));
      if (hasContextualClock)
        SET_OP_ATTR(HasContextualClock, builder.getBoolAttr(true));
      else
        addDefaultClocking<Op>(attrs, getCurrentScope());
      if (proceduralAssertionsStartingOnCurrentClock.contains(&node))
        SET_OP_ATTR(StartsOnCurrentClock, builder.getBoolAttr(true));
      SET_OP_ATTR(HasDefaultDisable,
                  builder.getBoolAttr(getCurrentDefaultDisable() != nullptr));
    } else if constexpr (std::same_as<T,
                                      slang::ast::ProceduralCheckerStatement>) {
      SmallVector<Attribute> instanceSymbols;
      SmallVector<Attribute> instancePaths;
      SmallVector<const slang::ast::Symbol *> instances;
      instanceSymbols.reserve(node.instances.size());
      instancePaths.reserve(node.instances.size());
      instances.reserve(node.instances.size());
      for (const slang::ast::Symbol *instance : node.instances) {
        instanceSymbols.push_back(getSemanticSymbolReference(*instance));
        instancePaths.push_back(
            builder.getStringAttr(getSymbolPath(*instance)));
        instances.push_back(instance);
      }
      SET_OP_ATTR(InstanceCount,
                  builder.getI64IntegerAttr(node.instances.size()));
      SET_OP_ATTR(InstanceSymbols, builder.getArrayAttr(instanceSymbols));
      SET_OP_ATTR(InstancePaths, builder.getArrayAttr(instancePaths));
      currentPendingReferenceArrays.push_back(
          {std::move(instances),
           Op::getInstanceSymbolsAttrName(operationName)});
    } else if constexpr (std::same_as<T,
                                      slang::ast::ProceduralAssignStatement>) {
      SET_OP_ATTR(IsForce, builder.getBoolAttr(node.isForce));
    } else if constexpr (std::same_as<
                             T, slang::ast::ProceduralDeassignStatement>) {
      SET_OP_ATTR(IsRelease, builder.getBoolAttr(node.isRelease));
    } else if constexpr (std::same_as<T, slang::ast::Delay3Control>) {
      int64_t delayCount = node.expr3 ? 3 : node.expr2 ? 2 : 1;
      SET_OP_ATTR(DelayCount, builder.getI64IntegerAttr(delayCount));
    } else if constexpr (std::same_as<T, slang::ast::CycleDelayControl>) {
      if (const slang::ast::Scope *scope = getCurrentScope())
        if (const slang::ast::Symbol *clocking =
                compilation.getDefaultClocking(*scope))
          addStaticClockingEventDescriptor(
              attrs, clocking->as<slang::ast::ClockingBlockSymbol>());
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

    if constexpr (std::same_as<T, slang::ast::AssertionInstanceExpression>) {
      setReferencedSymbol<Op>(attrs, node.symbol);
      SmallVector<Attribute> formalSymbols;
      SmallVector<Attribute> formalPaths;
      SmallVector<int64_t> argumentKinds;
      for (const auto &[formal, actual] : node.arguments) {
        formalSymbols.push_back(getSemanticSymbolReference(*formal));
        formalPaths.push_back(builder.getStringAttr(getSymbolPath(*formal)));
        argumentKinds.push_back(
            std::holds_alternative<const slang::ast::Expression *>(actual) ? 0
            : std::holds_alternative<const slang::ast::AssertionExpr *>(actual)
                ? 1
                : 2);
      }
      SmallVector<Attribute> localSymbols;
      SmallVector<Attribute> localPaths;
      SmallVector<int64_t> localHasInitializer;
      for (const slang::ast::LocalAssertionVarSymbol *local : node.localVars) {
        localSymbols.push_back(getSemanticSymbolReference(*local));
        localPaths.push_back(builder.getStringAttr(getSymbolPath(*local)));
        localHasInitializer.push_back(local->getInitializer() != nullptr);
      }
      SET_OP_ATTR(ArgumentCount,
                  builder.getI64IntegerAttr(node.arguments.size()));
      SET_OP_ATTR(ArgumentFormalSymbols, builder.getArrayAttr(formalSymbols));
      SET_OP_ATTR(ArgumentFormalPaths, builder.getArrayAttr(formalPaths));
      SET_OP_ATTR(ArgumentKinds, builder.getDenseI64ArrayAttr(argumentKinds));
      SET_OP_ATTR(LocalVariableCount,
                  builder.getI64IntegerAttr(node.localVars.size()));
      SET_OP_ATTR(LocalVariableSymbols, builder.getArrayAttr(localSymbols));
      SET_OP_ATTR(LocalVariablePaths, builder.getArrayAttr(localPaths));
      SET_OP_ATTR(LocalVariableHasInitializer,
                  builder.getDenseI64ArrayAttr(localHasInitializer));
      SET_OP_ATTR(IsRecursiveProperty,
                  builder.getBoolAttr(node.isRecursiveProperty));
      SET_OP_ATTR(HasExpandedBody,
                  builder.getBoolAttr(!node.isRecursiveProperty));
      SmallVector<const slang::ast::Symbol *> formals;
      formals.reserve(node.arguments.size());
      for (const auto &[formal, actual] : node.arguments) {
        (void)actual;
        formals.push_back(formal);
      }
      currentPendingReferenceArrays.push_back(
          {std::move(formals),
           Op::getArgumentFormalSymbolsAttrName(operationName)});
      SmallVector<const slang::ast::Symbol *> locals(node.localVars.begin(),
                                                     node.localVars.end());
      currentPendingReferenceArrays.push_back(
          {std::move(locals),
           Op::getLocalVariableSymbolsAttrName(operationName)});
    } else if constexpr (std::same_as<T, slang::ast::SimpleAssertionExpr>) {
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
    using BareNode = std::remove_cvref_t<Node>;
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
      if (const slang::ast::Type *source = getUncanonicalizedSemanticType(node))
        if (ArrayAttr layers = getVPITypedefLayers(*source))
          attrs.set("vpi_typedef_layers", layers);
      // A display name is not an enum identity: separate compilation units may
      // each legally declare `$unit::state_t`. Freeze Slang's exact matching
      // type identity on aliases, constants, and enum-bearing values that must
      // reconnect to one persistent typespec after the AST is erased.
      if (const slang::ast::Type *source =
              getUncanonicalizedSemanticType(node)) {
        const slang::ast::Type &identityType = unwrapTypeAliases(*source);
        bool retainIdentity = identityType.isEnum();
        if constexpr (std::same_as<BareNode, slang::ast::TypeAliasType> ||
                      std::same_as<BareNode, slang::ast::EnumValueSymbol>)
          retainIdentity = true;
        if (retainIdentity)
          attrs.set(
              "vpi_source_type_identity",
              builder.getI64IntegerAttr(matchingTypeIdentity(identityType)));
      }
    }
    if constexpr (std::derived_from<Node, slang::ast::Expression>)
      attrs.set("is_signed", builder.getBoolAttr(isEffectivelySigned(node)));
    currentPendingReferences.clear();
    currentPendingReferenceArrays.clear();
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
    for (PendingReferenceArraySeed &pending : currentPendingReferenceArrays)
      pendingReferenceArrays.push_back(
          {operation, std::move(pending.targets), pending.attributeName});

    OpBuilder::InsertionGuard guard{builder};
    builder.setInsertionPointToStart(&body);
    using T = BareNode;
    if constexpr (std::derived_from<Node, slang::ast::Scope>)
      currentScopes.push_back(&node);
    bool pushedProcedure = false;
    if constexpr (std::same_as<T, slang::ast::ProceduralBlockSymbol>) {
      if (!node.isFromAssertion) {
        currentProcedures.push_back(&node);
        pushedProcedure = true;
      }
    } else if constexpr (std::same_as<T, slang::ast::SubroutineSymbol>) {
      currentProcedures.push_back(&node);
      pushedProcedure = true;
    }
    if constexpr (std::same_as<T, slang::ast::GenericClassDefSymbol>) {
      // Slang stores specializations in a hash map. Importing that iteration
      // order directly makes semantic symbol and node IDs depend on allocator
      // layout, invalidating deterministic native partitions and ThinLTO cache
      // keys between identical builds.
      SmallVector<std::pair<std::string, const slang::ast::ClassType *>>
          specializations;
      for (const slang::ast::Type &specialization : node.specializations()) {
        const auto &classType = specialization.as<slang::ast::ClassType>();
        specializations.emplace_back(getStableSpecializationKey(classType),
                                     &classType);
      }
      llvm::sort(specializations, [](const auto &lhs, const auto &rhs) {
        return lhs.first < rhs.first;
      });
      for (const auto &entry : specializations)
        entry.second->visit(*this);
    } else if constexpr (std::same_as<T, slang::ast::ClassType>) {
      this->visitDefault(node);
      if (const auto *baseConstructorCall = node.getBaseConstructorCall())
        baseConstructorCall->visit(*this);
    } else if constexpr (std::same_as<T, slang::ast::InstanceSymbol>) {
      importPortConnections(node);
      node.body.visit(*this);
    } else if constexpr (std::same_as<T, slang::ast::NamedValueExpression> ||
                         std::same_as<
                             T, slang::ast::HierarchicalValueExpression>) {
      this->visitDefault(node);
      if (node.symbol.kind == slang::ast::SymbolKind::ClockVar) {
        const auto &clockVar =
            node.symbol.template as<slang::ast::ClockVarSymbol>();
        const slang::ast::Expression *source = clockVar.getInitializer();
        if (source &&
            !source->template as_if<slang::ast::NamedValueExpression>() &&
            !source
                 ->template as_if<slang::ast::HierarchicalValueExpression>() &&
            clockVar.direction != slang::ast::ArgumentDirection::Out)
          source->visit(*this);
        const auto &clocking =
            clockVar.getParentScope()
                ->asSymbol()
                .template as<slang::ast::ClockingBlockSymbol>();
        if (const auto *event =
                clocking.getEvent()
                    .template as_if<slang::ast::SignalEventControl>();
            event && event->iffCondition &&
            !requiresClockingEventMonitor(clocking)) {
          event->expr.visit(*this);
          event->iffCondition->visit(*this);
        }
      }
    } else if constexpr (std::same_as<T, slang::ast::MemberAccessExpression>) {
      this->visitDefault(node);
      if (node.member.kind == slang::ast::SymbolKind::ClockVar ||
          node.member.kind == slang::ast::SymbolKind::ClockingBlock) {
        const slang::ast::ClockingBlockSymbol *clocking = nullptr;
        if (node.member.kind == slang::ast::SymbolKind::ClockVar) {
          const auto &clockVar =
              node.member.template as<slang::ast::ClockVarSymbol>();
          const slang::ast::Expression *source = clockVar.getInitializer();
          if (source &&
              !source->template as_if<slang::ast::NamedValueExpression>() &&
              !source->template as_if<
                  slang::ast::HierarchicalValueExpression>() &&
              clockVar.direction != slang::ast::ArgumentDirection::Out)
            source->visit(*this);
          clocking = &clockVar.getParentScope()
                          ->asSymbol()
                          .template as<slang::ast::ClockingBlockSymbol>();
        } else {
          clocking =
              &node.member.template as<slang::ast::ClockingBlockSymbol>();
        }
        if (const auto *event =
                clocking->getEvent()
                    .template as_if<slang::ast::SignalEventControl>();
            event && event->iffCondition &&
            !requiresClockingEventMonitor(*clocking)) {
          event->expr.visit(*this);
          event->iffCondition->visit(*this);
        }
      }
    } else if constexpr (std::same_as<T, slang::ast::CallExpression>) {
      this->visitDefault(node);
      if (const slang::ast::ClockingBlockSymbol *clocking =
              getGlobalClocking(node)) {
        if (const auto *event =
                clocking->getEvent()
                    .template as_if<slang::ast::SignalEventControl>();
            event && event->iffCondition &&
            !requiresClockingEventMonitor(*clocking)) {
          event->expr.visit(*this);
          event->iffCondition->visit(*this);
        }
      }
    } else if constexpr (std::same_as<T,
                                      slang::ast::ArbitrarySymbolExpression>) {
      this->visitDefault(node);
      if (node.symbol &&
          node.symbol->kind == slang::ast::SymbolKind::ClockingBlock) {
        const auto &clocking =
            node.symbol->template as<slang::ast::ClockingBlockSymbol>();
        if (const auto *event =
                clocking.getEvent()
                    .template as_if<slang::ast::SignalEventControl>();
            event && event->iffCondition &&
            !requiresClockingEventMonitor(clocking)) {
          event->expr.visit(*this);
          event->iffCondition->visit(*this);
        }
      }
    } else if constexpr (std::same_as<T, slang::ast::CycleDelayControl>) {
      this->visitDefault(node);
      if (const slang::ast::Scope *scope = getCurrentScope())
        if (const slang::ast::Symbol *clocking =
                compilation.getDefaultClocking(*scope))
          if (const auto *event =
                  clocking->as<slang::ast::ClockingBlockSymbol>()
                      .getEvent()
                      .template as_if<slang::ast::SignalEventControl>();
              event && event->iffCondition &&
              !requiresClockingEventMonitor(
                  clocking->as<slang::ast::ClockingBlockSymbol>())) {
            event->expr.visit(*this);
            event->iffCondition->visit(*this);
          }
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
    } else if constexpr (std::same_as<T, slang::ast::TimingPathSymbol>) {
      // Slang intentionally treats specify-path expressions as resolved
      // metadata rather than ordinary symbol children. Keep the condition and
      // edge-path data source in semantic IR. The latter is analysis metadata
      // only under Clause 30.4.3 and is never evaluated by simulation.
      this->visitDefault(node);
      if (const slang::ast::Expression *condition = node.getConditionExpr())
        condition->visit(*this);
      if (const slang::ast::Expression *edgeSource = node.getEdgeSourceExpr())
        edgeSource->visit(*this);
    } else if constexpr (std::same_as<T, slang::ast::SystemTimingCheckSymbol>) {
      // Slang stores resolved timing-check operands as ordered metadata, not
      // owned symbol children. Visit expression then &&& condition for each
      // formal slot; the aligned Clause 31 attributes above preserve holes
      // and exact boundaries without wrapper operations.
      for (const auto &argument : node.getArguments()) {
        if (argument.expr)
          argument.expr->visit(*this);
        if (argument.condition)
          argument.condition->visit(*this);
      }
    } else if constexpr (std::same_as<T, slang::ast::ClockVarSymbol>) {
      this->visitDefault(node);
      if (node.inputSkew.delay)
        node.inputSkew.delay->visit(*this);
      if (node.outputSkew.delay &&
          node.outputSkew.delay != node.inputSkew.delay)
        node.outputSkew.delay->visit(*this);
    } else if constexpr (std::same_as<T, slang::ast::ModportPortSymbol>) {
      // Slang keeps a modport port's resolved connection outside the symbol's
      // ordinary ownership walk. Preserve it in the symbol body so topology
      // analysis can distinguish `.alias(signal)` from an implicit `alias`.
      if (const slang::ast::Expression *connection = node.getConnectionExpr())
        connection->visit(*this);
    } else if constexpr (std::same_as<T, slang::ast::SequenceSymbol> ||
                         std::same_as<T, slang::ast::PropertySymbol>) {
      this->visitDefault(node);
      if (canMakeDefaultAssertionInstance(node))
        slang::ast::AssertionInstanceExpression::makeDefault(node).visit(*this);
    } else if constexpr (std::same_as<
                             T, slang::ast::AssertionInstanceExpression>) {
      if (!node.isRecursiveProperty)
        node.body.visit(*this);
      for (const auto &[formal, actual] : node.arguments) {
        (void)formal;
        std::visit([&](const auto *value) { value->visit(*this); }, actual);
      }
      for (const slang::ast::LocalAssertionVarSymbol *local : node.localVars)
        if (const slang::ast::Expression *initializer = local->getInitializer())
          initializer->visit(*this);
    } else if constexpr (std::same_as<
                             T, slang::ast::ConcurrentAssertionStatement>) {
      if (const slang::ast::Expression *disable = getCurrentDefaultDisable())
        disable->visit(*this);
      auto contextualClock = contextualAssertionClocks.find(&node);
      if (contextualClock != contextualAssertionClocks.end()) {
        contextualClock->second->visit(*this);
      } else {
        // Keep the resolved default clock event in the executable semantic
        // subtree.  The symbol reference above preserves declaration identity,
        // while this clone makes the event's signal references ordinary frozen
        // code-unit captures.  Explicit assertion clocks still take precedence
        // during monitor compilation.
        if (const slang::ast::Scope *scope = getCurrentScope())
          if (const slang::ast::Symbol *clocking =
                  compilation.getDefaultClocking(*scope))
            clocking->as<slang::ast::ClockingBlockSymbol>().getEvent().visit(
                *this);
      }
      this->visitDefault(node);
    } else if constexpr (std::same_as<T, slang::ast::ConstraintBlockSymbol>) {
      // Importing the body placeholder of a prototype would reject the whole
      // compilation as invalid. Leave the body region empty instead and keep
      // the members (the implicit `this` variable); the declaration flags
      // already record that the block is pure or extern, and Prepare rejects
      // such a block only once it reaches an executable randomize().
      if (isBodylessConstraintPrototype(node)) {
        for (const slang::ast::Symbol &member : node.members())
          member.visit(*this);
      } else {
        this->visitDefault(node);
      }
    } else if constexpr (std::same_as<T, slang::ast::RandSeqProductionSymbol>) {
      // Rule blocks and formal arguments are ordinary owned symbols. The
      // production graph itself is lazily elaborated data, so ASTVisitor's
      // default ownership walk cannot see it. Preserve a deterministic flat
      // stream for each rule: optional weight, optional rand-join expression,
      // production items, and optional weight code block. The rule metadata
      // above provides the exact boundaries needed by semantic lowering.
      for (const slang::ast::Symbol &member : node.members())
        member.visit(*this);
      for (const auto &rule : node.getRules()) {
        if (rule.weightExpr)
          rule.weightExpr->visit(*this);
        if (rule.randJoinExpr)
          rule.randJoinExpr->visit(*this);
        for (const auto *production : rule.prods)
          production->visit(*this);
        if (rule.codeBlock)
          rule.codeBlock->visit(*this);
      }
    } else if constexpr (std::same_as<T, slang::ast::BlockEventListControl>) {
      for (const auto &event : node.events)
        if (event.target)
          event.target->visit(*this);
    } else if constexpr (std::same_as<T, slang::ast::ProdItem>) {
      node.visitExprs(*this);
    } else if constexpr (std::same_as<T, slang::ast::CodeBlockProd>) {
      if (const slang::ast::Statement *statement =
              node.block->tryGetStatement())
        statement->visit(*this);
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
    if (pushedProcedure)
      currentProcedures.pop_back();
    if constexpr (std::derived_from<Node, slang::ast::Scope>)
      currentScopes.pop_back();
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
        if (isNet && direction != slang::ast::ArgumentDirection::InOut &&
            isPortCoercedToInOut(instance, *connection, port))
          direction = slang::ast::ArgumentDirection::InOut;
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
      if (formal.kind == slang::ast::SymbolKind::Port) {
        const auto &port = formal.as<slang::ast::PortSymbol>();
        if (ArrayAttr layers = getVPITypedefLayers(port.getType()))
          attrs.set("vpi_typedef_layers", layers);
      }
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
      if (direction == slang::ast::ArgumentDirection::In &&
          !connection->getExpression()) {
        switch (instance.body.getDefinition().unconnectedDrive) {
        case slang::ast::UnconnectedDrive::Pull0:
          attrs.set("unconnected_drive_value", builder.getBoolAttr(false));
          actualIsConstant = true;
          break;
        case slang::ast::UnconnectedDrive::Pull1:
          attrs.set("unconnected_drive_value", builder.getBoolAttr(true));
          actualIsConstant = true;
          break;
        case slang::ast::UnconnectedDrive::None:
          break;
        }
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

  bool isPortCoercedToInOut(const slang::ast::InstanceSymbol &instance,
                            const slang::ast::PortConnection &connection,
                            const slang::ast::PortSymbol &port) const {
    using slang::analysis::DriverFlags;
    using slang::analysis::ValueDriver;
    using slang::ast::ArgumentDirection;
    using slang::ast::ValueSymbol;

    SmallVector<const ValueSymbol *> endpoints;
    if (port.direction == ArgumentDirection::In) {
      if (port.internalSymbol &&
          slang::ast::ValueSymbol::isKind(port.internalSymbol->kind))
        endpoints.push_back(&port.internalSymbol->as<ValueSymbol>());
      else if (const slang::ast::Expression *expression =
                   port.getInternalExpr()) {
        slang::ast::EvalContext context(instance.body);
        slang::ast::ValuePath::visitPaths(
            *expression, context,
            [&](const slang::ast::ValuePath &path) {
              if (const ValueSymbol *symbol = path.rootSymbol())
                endpoints.push_back(symbol);
            },
            /*skipSelectors=*/true);
      }
      // Slang analyzes one canonical body for equivalent instances. Query its
      // corresponding endpoint as well so coercion is preserved on every
      // elaborated instance, not only the canonical representative.
      if (const slang::ast::InstanceBodySymbol *canonical =
              instance.getCanonicalBody())
        if (const slang::ast::Symbol *canonicalPortSymbol =
                canonical->findPort(port.name))
          if (canonicalPortSymbol->kind == slang::ast::SymbolKind::Port) {
            const auto &canonicalPort =
                canonicalPortSymbol->as<slang::ast::PortSymbol>();
            if (canonicalPort.internalSymbol &&
                slang::ast::ValueSymbol::isKind(
                    canonicalPort.internalSymbol->kind))
              endpoints.push_back(
                  &canonicalPort.internalSymbol->as<ValueSymbol>());
          }
    } else if (port.direction == ArgumentDirection::Out) {
      const slang::ast::Expression *expression = connection.getExpression();
      if (expression &&
          expression->kind == slang::ast::ExpressionKind::Assignment)
        expression = &expression->as<slang::ast::AssignmentExpression>().left();
      if (expression) {
        slang::ast::EvalContext context(instance);
        slang::ast::ValuePath::visitPaths(
            *expression, context,
            [&](const slang::ast::ValuePath &path) {
              if (const ValueSymbol *symbol = path.rootSymbol())
                endpoints.push_back(symbol);
            },
            /*skipSelectors=*/true);
      }
    } else {
      return false;
    }

    const DriverFlags portFlag = port.direction == ArgumentDirection::In
                                     ? DriverFlags::InputPort
                                     : DriverFlags::OutputPort;
    auto overlaps = [](const ValueDriver &lhs, const ValueDriver &rhs) {
      auto lhsBounds = lhs.getBounds();
      auto rhsBounds = rhs.getBounds();
      return lhsBounds.first <= rhsBounds.second &&
             rhsBounds.first <= lhsBounds.second;
    };
    for (const ValueSymbol *endpoint : endpoints) {
      std::vector<const ValueDriver *> drivers =
          analysisManager.getDrivers(*endpoint);
      for (const ValueDriver *portDriver : drivers) {
        if (!portDriver->flags.has(portFlag))
          continue;
        if (port.direction == ArgumentDirection::Out &&
            portDriver->containingSymbol != &instance)
          continue;
        for (const ValueDriver *other : drivers)
          if (other != portDriver && !other->isUnidirectionalPort() &&
              overlaps(*portDriver, *other))
            return true;
      }
    }
    return false;
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

  struct PendingReferenceArray {
    Operation *operation;
    SmallVector<const slang::ast::Symbol *, 4> targets;
    StringAttr attributeName;
  };

  struct PendingReferenceArraySeed {
    SmallVector<const slang::ast::Symbol *, 4> targets;
    StringAttr attributeName;
  };

  OpBuilder builder;
  const slang::SourceManager &sourceManager;
  const slang::ast::Compilation &compilation;
  const slang::analysis::AnalysisManager &analysisManager;
  const SDFAnnotationDatabase &sdfAnnotations;
  SlangTypeConverter typeConverter;
  llvm::DenseMap<const slang::ast::Type *, ArrayAttr> arrayQueryDimensionCache;
  llvm::DenseMap<const slang::ast::Type *, ArrayAttr> vpiTypedefLayerCache;
  llvm::DenseMap<const slang::ast::Symbol *, std::string> anonymousSymbolPaths;
  llvm::DenseMap<const slang::ast::Symbol *, std::string> resolvedSymbolPaths;
  llvm::StringMap<const slang::ast::Symbol *> claimedVariablePaths;
  llvm::DenseMap<const slang::ast::Symbol *, StringAttr> internalSymbolNames;
  llvm::DenseMap<const slang::ast::Symbol *, SmallVector<std::string, 8>>
      emittedSymbolPaths;
  llvm::DenseMap<const slang::ast::ConcurrentAssertionStatement *,
                 const slang::ast::TimingControl *>
      contextualAssertionClocks;
  llvm::SmallPtrSet<const slang::ast::ConcurrentAssertionStatement *, 16>
      proceduralAssertions;
  llvm::SmallPtrSet<const slang::ast::ConcurrentAssertionStatement *, 16>
      proceduralAssertionsStartingOnCurrentClock;
  llvm::SmallPtrSet<const slang::ast::Scope *, 16> indexedAnalysisScopes;
  llvm::DenseMap<const slang::ast::Symbol *,
                 const slang::analysis::AnalyzedProcedure *>
      analyzedProcedures;
  llvm::SmallPtrSet<const slang::ast::Symbol *, 16> analyzedAssertionProcedures;
  llvm::DenseMap<const slang::ast::Symbol *, Operation *>
      emittedSymbolOperations;
  llvm::DenseMap<const slang::syntax::SyntaxNode *, SmallVector<Operation *, 1>>
      dpiExportOperationsBySyntax;
  bool dpiExportSyntaxIndexBuilt = false;
  SmallVector<std::string, 8> currentSymbolPath;
  SmallVector<const slang::ast::Scope *, 8> currentScopes;
  SmallVector<const slang::ast::Symbol *, 4> currentProcedures;
  SmallVector<PendingReference, 0> pendingReferences;
  SmallVector<PendingReferenceArray, 0> pendingReferenceArrays;
  SmallVector<const slang::ast::Symbol *, 0> semanticDependencies;
  SmallVector<PendingReferenceSeed, 2> currentPendingReferences;
  SmallVector<PendingReferenceArraySeed, 2> currentPendingReferenceArrays;
  llvm::DenseMap<const slang::ast::Type *, int64_t> matchingTypeIdentities;
  SmallVector<const slang::ast::Type *, 4> matchingTypeRepresentatives;
  int64_t nextNodeId = 0;
  uint64_t nextAnonymousSymbolId = 0;
  uint64_t nextShadowedSymbolId = 0;
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
  appendValues(result, "-y", options.libDirs);
  appendValues(result, "-Y", options.libExts);
  for (const LibraryInput &input : options.libraryInputs) {
    result.emplace_back(input.kind == LibraryInputKind::File ? "-v"
                                                             : "--libmap");
    result.push_back(input.path);
  }
  appendValues(result, "--top", options.topModules);
  appendValues(result, "-G", options.paramOverrides);
  // IEEE 1800-2017 11.5.1 and 7.4.6 define what an out-of-range select reads
  // and writes, so simulating one is conforming rather than an error. Slang
  // raises these to errors by default; put the downgrade ahead of the user's
  // own options so `-Werror=range-oob` still wins.
  for (llvm::StringRef warning :
       {"index-oob", "range-oob", "range-width-oob", "format-too-many-args"})
    result.emplace_back(("-Wno-error=" + warning).str());
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
  result.emplace_back("--timing");
  switch (options.minTypMax) {
  case MinTypMax::Min:
    result.emplace_back("min");
    break;
  case MinTypMax::Typ:
    result.emplace_back("typ");
    break;
  case MinTypMax::Max:
    result.emplace_back("max");
    break;
  }
  if (options.numThreads) {
    result.emplace_back("-j");
    result.push_back(std::to_string(*options.numThreads));
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
        driver.processOptions();
    if (succeeded) {
      installProtectedEnvelopeProvider(driver, options);
      succeeded = driver.runPreprocessor(flags);
    }
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
      !driver.processOptions())
    return failure();
  installProtectedEnvelopeProvider(driver, options);
  if (!driver.parseAllSources())
    return failure();

  std::unique_ptr<slang::ast::Compilation> compilation =
      driver.createCompilation();
  driver.reportCompilation(*compilation, /*quiet=*/true);

  // Slang's value-driver checks are part of semantic analysis rather than
  // AST construction. Run them through the driver so their diagnostics cross
  // the frontend boundary as well; otherwise illegal mixed procedural /
  // continuous assignments and multiple continuous assignments to variables
  // are silently imported as executable IR.
  std::unique_ptr<slang::analysis::AnalysisManager> analysisManager =
      driver.runAnalysis(*compilation);
  if (!driver.reportDiagnostics(/*quiet=*/true))
    return failure();

  std::unique_ptr<SDFAnnotationDatabase> sdfAnnotations =
      buildSDFAnnotationDatabase(*compilation, driver.sourceManager);
  if (!sdfAnnotations)
    return failure();

  OwningOpRef<ModuleOp> module(ModuleOp::create(UnknownLoc::get(&context)));
  SlangASTImporter importer(*module, driver.sourceManager, *compilation,
                            *analysisManager, *sdfAnnotations);
  // Definitions are kept in Compilation's deterministic definition map and
  // are not children of RootSymbol. Import them explicitly so modules,
  // interfaces, programs, and primitives remain represented even when they
  // have no elaborated instance.
  for (const slang::ast::Symbol *definition : compilation->getDefinitions())
    definition->visit(importer);
  compilation->getRoot().visit(importer);
  if (failed(importer.finalizeReferences()) || !importer.succeeded())
    return failure();
  for (const slang::ast::Compilation::DPIExport &entry :
       compilation->getDPIExports())
    importer.markDPIExport(*entry.subroutine, entry.cIdentifier, entry.syntax);
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
