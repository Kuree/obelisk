//===- SDF.cpp - Static Standard Delay Format annotation -----------------===//

#include "SDF.h"

#include "obelisk/Dialect/SDF/SDFOps.h"
#include "obelisk/Frontend/SDF.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/Verifier.h"

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/expressions/CallExpression.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/SpecifySymbols.h"
#include "slang/parsing/KnownSystemName.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/syntax/SyntaxVisitor.h"
#include "slang/text/SourceManager.h"

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

namespace obelisk::frontend {
namespace {

struct SDFNode {
  enum class Kind { Atom, String, List } kind = Kind::Atom;
  std::string text;
  std::vector<SDFNode> children;
  unsigned line = 1;
  unsigned column = 1;
};

class SDFParser {
public:
  SDFParser(StringRef filename, StringRef input)
      : filename(filename), input(input) {}

  std::optional<SDFNode> parse() {
    constexpr size_t maxInputBytes = 64 * 1024 * 1024;
    if (input.size() > maxInputBytes) {
      error(1, 1, "SDF input exceeds the supported 64 MiB limit");
      return std::nullopt;
    }
    skipTrivia();
    std::optional<SDFNode> result = parseNode();
    skipTrivia();
    if (result && cursor != input.size())
      error(line, column, "unexpected text after DELAYFILE");
    return failed ? std::nullopt : result;
  }

private:
  void error(unsigned errorLine, unsigned errorColumn, const Twine &message) {
    errs() << filename << ':' << errorLine << ':' << errorColumn
           << ": error: " << message << '\n';
    failed = true;
  }

  char peek(size_t offset = 0) const {
    return cursor + offset < input.size() ? input[cursor + offset] : '\0';
  }

  char consume() {
    char value = peek();
    if (!value)
      return value;
    ++cursor;
    if (value == '\n') {
      ++line;
      column = 1;
    } else {
      ++column;
    }
    return value;
  }

  void skipTrivia() {
    while (true) {
      while (std::isspace(static_cast<unsigned char>(peek())))
        consume();
      if (peek() == '/' && peek(1) == '/') {
        while (peek() && consume() != '\n')
          ;
        continue;
      }
      if (peek() == '/' && peek(1) == '*') {
        unsigned startLine = line;
        unsigned startColumn = column;
        consume();
        consume();
        while (peek() && !(peek() == '*' && peek(1) == '/'))
          consume();
        if (!peek()) {
          error(startLine, startColumn, "unterminated block comment");
          return;
        }
        consume();
        consume();
        continue;
      }
      break;
    }
  }

  std::optional<SDFNode> parseNode(unsigned depth = 0) {
    constexpr size_t maxNodes = 1'000'000;
    if (++nodes > maxNodes) {
      error(line, column, "SDF input exceeds the supported node limit");
      return std::nullopt;
    }
    skipTrivia();
    unsigned startLine = line;
    unsigned startColumn = column;
    if (!peek()) {
      error(startLine, startColumn, "expected SDF form");
      return std::nullopt;
    }
    if (peek() == '(') {
      constexpr unsigned maxNestingDepth = 64;
      if (depth == maxNestingDepth) {
        error(startLine, startColumn,
              "SDF nesting exceeds the supported limit of 64 lists");
        return std::nullopt;
      }
      consume();
      SDFNode result{SDFNode::Kind::List, {}, {}, startLine, startColumn};
      skipTrivia();
      while (peek() && peek() != ')') {
        std::optional<SDFNode> child = parseNode(depth + 1);
        if (!child)
          return std::nullopt;
        result.children.push_back(std::move(*child));
        skipTrivia();
      }
      if (peek() != ')') {
        error(startLine, startColumn, "unterminated SDF list");
        return std::nullopt;
      }
      consume();
      return result;
    }
    if (peek() == '"') {
      consume();
      std::string value;
      while (peek() && peek() != '"') {
        char current = consume();
        if (current == '\\' && peek())
          current = consume();
        value.push_back(current);
      }
      if (peek() != '"') {
        error(startLine, startColumn, "unterminated SDF string");
        return std::nullopt;
      }
      consume();
      return SDFNode{
          SDFNode::Kind::String, std::move(value), {}, startLine, startColumn};
    }
    std::string value;
    while (peek() && !std::isspace(static_cast<unsigned char>(peek())) &&
           peek() != '(' && peek() != ')')
      value.push_back(consume());
    if (value.empty()) {
      error(startLine, startColumn, "expected SDF token");
      return std::nullopt;
    }
    return SDFNode{
        SDFNode::Kind::Atom, std::move(value), {}, startLine, startColumn};
  }

  StringRef filename;
  StringRef input;
  size_t cursor = 0;
  unsigned line = 1;
  unsigned column = 1;
  bool failed = false;
  size_t nodes = 0;
};

static bool keyword(const SDFNode &node, StringRef expected) {
  return node.kind == SDFNode::Kind::Atom &&
         StringRef(node.text).equals_insensitive(expected);
}

static const SDFNode *formHead(const SDFNode &node, StringRef expected) {
  if (node.kind != SDFNode::Kind::List || node.children.empty() ||
      !keyword(node.children.front(), expected))
    return nullptr;
  return &node;
}

struct ExactDecimal {
  bool negative = false;
  // The shared typed importer retains the source spelling; canonicalize only
  // in this production consumer, immediately before target-precision folding.
  std::string significand = "0";
  int64_t exponent10 = 0;
};

static std::optional<ExactDecimal> parseDecimal(StringRef spelling) {
  spelling = spelling.trim();
  if (spelling.empty())
    return std::nullopt;
  bool negative = spelling.front() == '-';
  if (negative || spelling.front() == '+')
    spelling = spelling.drop_front();
  StringRef mantissa = spelling;
  int64_t exponent = 0;
  size_t exponentAt = spelling.find_first_of("eE");
  if (exponentAt != StringRef::npos) {
    if (spelling.drop_front(exponentAt + 1).contains_insensitive("e"))
      return std::nullopt;
    mantissa = spelling.take_front(exponentAt);
    StringRef exponentText = spelling.drop_front(exponentAt + 1);
    if (exponentText.empty())
      return std::nullopt;
    bool exponentNegative = exponentText.front() == '-';
    if (exponentNegative || exponentText.front() == '+')
      exponentText = exponentText.drop_front();
    if (exponentText.empty())
      return std::nullopt;
    uint64_t magnitude = 0;
    bool exponentOverflow = false;
    for (char value : exponentText) {
      if (!std::isdigit(static_cast<unsigned char>(value)))
        return std::nullopt;
      unsigned digit = static_cast<unsigned>(value - '0');
      if (magnitude >
          (static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) - digit) /
              10) {
        exponentOverflow = true;
      } else if (!exponentOverflow) {
        magnitude = magnitude * 10 + digit;
      }
    }
    if (exponentOverflow)
      exponent = exponentNegative ? std::numeric_limits<int64_t>::min()
                                  : std::numeric_limits<int64_t>::max();
    else
      exponent = exponentNegative ? -static_cast<int64_t>(magnitude)
                                  : static_cast<int64_t>(magnitude);
  }
  size_t dot = mantissa.find('.');
  if (dot != StringRef::npos && mantissa.drop_front(dot + 1).contains('.'))
    return std::nullopt;
  StringRef whole =
      dot == StringRef::npos ? mantissa : mantissa.take_front(dot);
  StringRef fraction =
      dot == StringRef::npos ? StringRef{} : mantissa.drop_front(dot + 1);
  if (whole.empty() && fraction.empty())
    return std::nullopt;
  std::string digits = (whole + fraction).str();
  for (char value : digits) {
    if (!std::isdigit(static_cast<unsigned char>(value)))
      return std::nullopt;
  }
  size_t firstNonzero = digits.find_first_not_of('0');
  if (firstNonzero == std::string::npos)
    return ExactDecimal{negative, "0", 0};
  digits.erase(0, firstNonzero);
  size_t trailingZeros = 0;
  while (digits.size() > 1 && digits.back() == '0') {
    digits.pop_back();
    ++trailingZeros;
  }
  // Clause 32 decimals remain exact, but work is deterministically bounded.
  constexpr size_t maxSignificantDecimalDigits = 16 * 1024;
  if (digits.size() > maxSignificantDecimalDigits)
    return std::nullopt;
  int64_t scale = exponent;
  auto addScale = [&](size_t amount) {
    if (amount > static_cast<size_t>(std::numeric_limits<int64_t>::max()) ||
        scale >
            std::numeric_limits<int64_t>::max() - static_cast<int64_t>(amount))
      scale = std::numeric_limits<int64_t>::max();
    else
      scale += static_cast<int64_t>(amount);
  };
  auto subtractScale = [&](size_t amount) {
    if (amount > static_cast<size_t>(std::numeric_limits<int64_t>::max()) ||
        scale <
            std::numeric_limits<int64_t>::min() + static_cast<int64_t>(amount))
      scale = std::numeric_limits<int64_t>::min();
    else
      scale -= static_cast<int64_t>(amount);
  };
  subtractScale(fraction.size());
  addScale(trailingZeros);
  return ExactDecimal{negative, std::move(digits), scale};
}

static std::optional<unsigned> powerOfTenOrder(uint64_t value) {
  unsigned order = 0;
  while (value > 1 && value % 10 == 0) {
    value /= 10;
    ++order;
  }
  return value == 1 ? std::optional<unsigned>(order) : std::nullopt;
}

static int64_t addDecimalOrder(int64_t exponent, int adjustment) {
  if (adjustment > 0 &&
      exponent > std::numeric_limits<int64_t>::max() - adjustment)
    return std::numeric_limits<int64_t>::max();
  if (adjustment < 0 &&
      exponent < std::numeric_limits<int64_t>::min() - adjustment)
    return std::numeric_limits<int64_t>::min();
  return exponent + adjustment;
}

static std::optional<uint64_t> parseBoundedDecimal(StringRef digits,
                                                   uint64_t limit) {
  uint64_t result = 0;
  for (char value : digits) {
    unsigned digit = static_cast<unsigned>(value - '0');
    if (digit > limit || result > (limit - digit) / 10)
      return std::nullopt;
    result = result * 10 + digit;
  }
  return result;
}

/// Round canonical `significand * 10^order` half upward to a bounded integer.
/// Decimal-order classification makes the residual arithmetic at most 19
/// digits even for adversarially long exponent spellings.
static std::optional<uint64_t>
roundDecimalToLimit(const ExactDecimal &value, int64_t order, uint64_t limit) {
  StringRef digits = value.significand;
  if (digits == "0")
    return 0;
  size_t limitDigits = std::to_string(limit).size();
  if (order >= 0) {
    uint64_t zeroCount = static_cast<uint64_t>(order);
    if (zeroCount > limitDigits || digits.size() > limitDigits - zeroCount)
      return std::nullopt;
    std::optional<uint64_t> result = parseBoundedDecimal(digits, limit);
    if (!result)
      return std::nullopt;
    for (uint64_t index = 0; index < zeroCount; ++index) {
      if (*result > limit / 10)
        return std::nullopt;
      *result *= 10;
    }
    return result;
  }
  uint64_t discarded = order == std::numeric_limits<int64_t>::min()
                           ? uint64_t(std::numeric_limits<int64_t>::max()) + 1
                           : static_cast<uint64_t>(-order);
  if (discarded > digits.size())
    return 0;
  size_t kept = digits.size() - static_cast<size_t>(discarded);
  if (kept > limitDigits)
    return std::nullopt;
  std::optional<uint64_t> result =
      parseBoundedDecimal(digits.take_front(kept), limit);
  if (!result)
    return std::nullopt;
  if (kept < digits.size() && digits[kept] >= '5') {
    if (*result == limit)
      return std::nullopt;
    ++*result;
  }
  return result;
}

static std::optional<uint64_t>
scaleDecimalExactlyToLimit(const ExactDecimal &value, int64_t order,
                           uint64_t limit) {
  if (value.significand == "0")
    return 0;
  // parseDecimal transfers every significand trailing zero to exponent10, so
  // a remaining negative order is necessarily a non-integral femtosecond.
  if (order < 0)
    return std::nullopt;
  return roundDecimalToLimit(value, order, limit);
}

static uint64_t timeScaleValueFemtoseconds(slang::TimeScaleValue value) {
  uint64_t unit = 1;
  switch (value.unit) {
  case slang::TimeUnit::Seconds:
    unit = UINT64_C(1'000'000'000'000'000);
    break;
  case slang::TimeUnit::Milliseconds:
    unit = UINT64_C(1'000'000'000'000);
    break;
  case slang::TimeUnit::Microseconds:
    unit = UINT64_C(1'000'000'000);
    break;
  case slang::TimeUnit::Nanoseconds:
    unit = UINT64_C(1'000'000);
    break;
  case slang::TimeUnit::Picoseconds:
    unit = UINT64_C(1'000);
    break;
  case slang::TimeUnit::Femtoseconds:
    break;
  }
  return unit * static_cast<uint64_t>(value.magnitude);
}

static std::optional<int64_t> roundDelay(const ExactDecimal &delay,
                                         uint64_t sdfUnitFs,
                                         uint64_t targetPrecisionFs) {
  if (targetPrecisionFs == 0)
    return std::nullopt;
  if (delay.significand == "0")
    return 0;
  std::optional<unsigned> unitOrder = powerOfTenOrder(sdfUnitFs);
  std::optional<unsigned> precisionOrder = powerOfTenOrder(targetPrecisionFs);
  if (!unitOrder || !precisionOrder)
    return std::nullopt;
  int adjustment =
      static_cast<int>(*unitOrder) - static_cast<int>(*precisionOrder);
  int64_t order = addDecimalOrder(delay.exponent10, adjustment);
  uint64_t magnitudeLimit =
      delay.negative
          ? static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1
          : static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
  uint64_t stepLimit = magnitudeLimit / targetPrecisionFs;
  std::optional<uint64_t> steps = roundDecimalToLimit(delay, order, stepLimit);
  if (!steps)
    return std::nullopt;
  // IEEE 1800-2017 3.14.1 rounds a time value to the destination scope's
  // precision before simulation. Keep this calculation rational so decimal
  // SDF text cannot pick up binary floating-point error at a half quantum.
  uint64_t femtoseconds = *steps * targetPrecisionFs;
  if (!delay.negative)
    return static_cast<int64_t>(femtoseconds);
  if (femtoseconds ==
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1)
    return std::numeric_limits<int64_t>::min();
  return -static_cast<int64_t>(femtoseconds);
}

struct ParsedIOPath {
  struct Port {
    std::string name;
    std::optional<int32_t> index;
  };

  Port input;
  Port output;
  SDFAnnotationDatabase::DelayAnnotation::Kind kind =
      SDFAnnotationDatabase::DelayAnnotation::Kind::Absolute;
  std::optional<slang::ast::EdgeKind> edge;
  struct DelayValue {
    sdf::DelayValueForm form = sdf::DelayValueForm::Empty;
    std::optional<ExactDecimal> min;
    std::optional<ExactDecimal> typ;
    std::optional<ExactDecimal> max;
  };
  SmallVector<DelayValue, 12> delays;
  unsigned line = 1;
  unsigned column = 1;
};

struct ParsedCell {
  struct UnsupportedTimingData {
    std::string description;
    unsigned line = 1;
    unsigned column = 1;
  };

  std::string cellType;
  std::string instance;
  bool wildcard = false;
  SmallVector<ParsedIOPath, 4> paths;
  SmallVector<UnsupportedTimingData, 2> unsupportedTimingData;
  unsigned line = 1;
  unsigned column = 1;
};

struct ParsedSDF {
  uint64_t timeScaleFs = 0;
  char divider = '/';
  SmallVector<ParsedCell, 8> cells;
};

static std::optional<size_t> countParsedSDFEntries(const ParsedSDF &sdf) {
  size_t count = 1;
  auto add = [&](size_t amount) {
    if (amount > std::numeric_limits<size_t>::max() - count)
      return false;
    count += amount;
    return true;
  };
  for (const ParsedCell &cell : sdf.cells) {
    if (!add(1) || !add(cell.paths.size()) ||
        !add(cell.unsupportedTimingData.size()))
      return std::nullopt;
    for (const ParsedIOPath &path : cell.paths)
      if (!add(path.delays.size()))
        return std::nullopt;
  }
  return count;
}

struct DesignInventory
    : slang::ast::ASTVisitor<DesignInventory, slang::ast::VisitFlags::AllGood> {
  struct AnnotationCall {
    const slang::ast::CallExpression *call;
    const slang::ast::InstanceSymbol *owner;
  };

  StringMap<const slang::ast::InstanceSymbol *> instances;
  DenseMap<const slang::ast::InstanceBodySymbol *,
           const slang::ast::InstanceSymbol *>
      instanceByBody;
  SmallVector<AnnotationCall, 2> calls;
  DenseSet<const slang::ast::CallExpression *> startupCalls;
  SmallVector<const slang::ast::CallExpression *, 2> orderedStartupCalls;
  DenseMap<const slang::ast::CallExpression *,
           const slang::ast::ProceduralBlockSymbol *>
      startupBlock;
  DenseMap<const slang::ast::CallExpression *,
           const slang::ast::InstanceSymbol *>
      callOwner;
  const slang::ast::InstanceSymbol *currentInstance = nullptr;

  const slang::ast::CallExpression *
  sdfCall(const slang::ast::Expression &expr) {
    if (expr.kind != slang::ast::ExpressionKind::Call)
      return nullptr;
    const auto &call = expr.as<slang::ast::CallExpression>();
    return call.isSystemCall() && call.getSubroutineName() == "$sdf_annotate"
               ? &call
               : nullptr;
  }

  bool scanStartupStatement(
      const slang::ast::Statement &statement,
      const slang::ast::ProceduralBlockSymbol &proceduralBlock) {
    using slang::ast::StatementKind;
    switch (statement.kind) {
    case StatementKind::Empty:
    case StatementKind::VariableDeclaration:
      return true;
    case StatementKind::List:
      for (const slang::ast::Statement *child :
           statement.as<slang::ast::StatementList>().list)
        if (!scanStartupStatement(*child, proceduralBlock))
          return false;
      return true;
    case StatementKind::Block: {
      const auto &block = statement.as<slang::ast::BlockStatement>();
      if (block.blockKind != slang::ast::StatementBlockKind::Sequential)
        return false;
      return scanStartupStatement(block.body, proceduralBlock);
    }
    case StatementKind::ExpressionStatement: {
      const auto &expression =
          statement.as<slang::ast::ExpressionStatement>().expr;
      if (const slang::ast::CallExpression *call = sdfCall(expression)) {
        startupCalls.insert(call);
        orderedStartupCalls.push_back(call);
        startupBlock[call] = &proceduralBlock;
        return true;
      }
      if (expression.kind == slang::ast::ExpressionKind::Call) {
        const auto &call = expression.as<slang::ast::CallExpression>();
        if (call.isSystemCall()) {
          StringRef name = call.getSubroutineName();
          // IEEE 1800-2017 21.2.3 makes these calls immediate monitor-state
          // updates. They neither suspend nor terminate the initial process,
          // so a following static annotation still executes once at time
          // zero. Keep the whitelist narrow: an arbitrary system task could
          // terminate the process or otherwise invalidate static ordering.
          if (name == "$monitor" || name == "$monitorb" ||
              name == "$monitoro" || name == "$monitorh" ||
              name == "$monitoron" || name == "$monitoroff")
            return true;
        }
      }
      // A preceding task call can suspend internally. Functions in ordinary
      // value expressions cannot consume simulation time, but a standalone
      // call is conservatively a task boundary in this static tranche.
      return expression.kind != slang::ast::ExpressionKind::Call;
    }
    default:
      // This compile-time tranche must prove source order and time-zero
      // execution. A control-flow, timing, loop, wait, or fork boundary makes
      // both nested calls and later statements conservatively runtime-only.
      return false;
    }
  }

  void handle(const slang::ast::InstanceSymbol &instance) {
    if (instance.isModule())
      instances[instance.getHierarchicalPath()] = &instance;
    instanceByBody[&instance.body] = &instance;
    const slang::ast::InstanceSymbol *previous = currentInstance;
    currentInstance = &instance;
    visitDefault(instance);
    currentInstance = previous;
  }

  void handle(const slang::ast::CallExpression &call) {
    if (currentInstance && call.isSystemCall() &&
        call.getSubroutineName() == "$sdf_annotate") {
      calls.push_back({&call, currentInstance});
      callOwner[&call] = currentInstance;
    }
    visitDefault(call);
  }

  void handle(const slang::ast::ProceduralBlockSymbol &block) {
    if (currentInstance &&
        block.procedureKind == slang::ast::ProceduralBlockKind::Initial)
      scanStartupStatement(block.getBody(), block);
    visitDefault(block);
  }
};

struct TimingPathInventory
    : slang::ast::ASTVisitor<TimingPathInventory,
                             slang::ast::VisitFlags::AllGood> {
  SmallVector<const slang::ast::TimingPathSymbol *, 4> paths;

  void handle(const slang::ast::TimingPathSymbol &path) {
    paths.push_back(&path);
  }
  void handle(const slang::ast::InstanceSymbol &) {}
};

static const slang::ast::InstanceSymbol *
owningInstance(const slang::ast::Scope &scope,
               const DesignInventory &inventory) {
  const slang::ast::Symbol *symbol = &scope.asSymbol();
  while (symbol) {
    if (symbol->kind == slang::ast::SymbolKind::InstanceBody) {
      auto found = inventory.instanceByBody.find(
          &symbol->as<slang::ast::InstanceBodySymbol>());
      if (found != inventory.instanceByBody.end())
        return found->second;
    }
    const slang::ast::Scope *parent = symbol->getParentScope();
    symbol = parent ? &parent->asSymbol() : nullptr;
  }
  return nullptr;
}

static std::optional<std::string>
constantFilename(const slang::ast::CallExpression &call) {
  if (call.arguments().empty() || !call.arguments().front())
    return std::nullopt;
  slang::ast::EvalContext context(
      std::get<slang::ast::CallExpression::SystemCallInfo>(call.subroutine)
          .scope->asSymbol());
  slang::ConstantValue value = call.arguments().front()->eval(context);
  if (!value)
    return std::nullopt;
  slang::ConstantValue stringValue = value.convertToStr();
  if (!stringValue || !stringValue.isString())
    return std::nullopt;
  return stringValue.str();
}

struct OptionalStringArgument {
  bool valid = true;
  std::optional<std::string> value;
};

static void callDiagnostic(const slang::SourceManager &sourceManager,
                           const slang::ast::CallExpression &call,
                           StringRef severity, const Twine &message);

static OptionalStringArgument
constantOptionalString(const slang::ast::CallExpression &call, size_t index) {
  if (index >= call.arguments().size() || !call.arguments()[index] ||
      call.arguments()[index]->kind ==
          slang::ast::ExpressionKind::EmptyArgument)
    return {};
  slang::ast::EvalContext context(
      std::get<slang::ast::CallExpression::SystemCallInfo>(call.subroutine)
          .scope->asSymbol());
  slang::ConstantValue value = call.arguments()[index]->eval(context);
  if (!value)
    return {false, std::nullopt};
  slang::ConstantValue stringValue = value.convertToStr();
  if (!stringValue || !stringValue.isString())
    return {false, std::nullopt};
  return {true, stringValue.str()};
}

enum class SDFMTMSelection { Minimum, Typical, Maximum };

static std::optional<SDFMTMSelection>
annotationMTMSelection(const slang::ast::CallExpression &call,
                       const slang::SourceManager &sourceManager) {
  OptionalStringArgument config = constantOptionalString(call, 2);
  OptionalStringArgument log = constantOptionalString(call, 3);
  OptionalStringArgument mtm = constantOptionalString(call, 4);
  OptionalStringArgument factors = constantOptionalString(call, 5);
  OptionalStringArgument scale = constantOptionalString(call, 6);
  if (!config.valid || !log.valid || !mtm.valid || !factors.valid ||
      !scale.valid) {
    callDiagnostic(sourceManager, call, "error",
                   "$sdf_annotate control arguments must be constant strings");
    return std::nullopt;
  }
  if ((config.value && !config.value->empty()) ||
      (log.value && !log.value->empty())) {
    callDiagnostic(sourceManager, call, "error",
                   "static $sdf_annotate does not support nonempty config_file "
                   "or log_file arguments");
    return std::nullopt;
  }
  if ((factors.value && !factors.value->empty()) ||
      (scale.value && !scale.value->empty())) {
    callDiagnostic(sourceManager, call, "error",
                   "static $sdf_annotate does not support scale_factors or "
                   "scale_type arguments");
    return std::nullopt;
  }

  // IEEE 1800-2017 32.9, Table 32-5: mtm_spec selects one member of every
  // min:typ:max triple. Only an omitted argument defaults to TOOL_CONTROL;
  // an explicitly supplied string must be one of the four table keywords.
  // TOOL_CONTROL is intentionally the existing typical policy, and selection
  // stays compile-time so no SDF state reaches AOT.
  if (!mtm.value)
    return SDFMTMSelection::Typical;
  StringRef spelling = *mtm.value;
  auto selection =
      StringSwitch<std::optional<SDFMTMSelection>>(spelling)
          .Cases({"TOOL_CONTROL", "TYPICAL"}, SDFMTMSelection::Typical)
          .Case("MINIMUM", SDFMTMSelection::Minimum)
          .Case("MAXIMUM", SDFMTMSelection::Maximum)
          .Default(std::nullopt);
  if (!selection)
    callDiagnostic(sourceManager, call, "error",
                   "invalid $sdf_annotate mtm_spec; expected MINIMUM, "
                   "TYPICAL, MAXIMUM, or TOOL_CONTROL");
  return selection;
}

static const slang::ast::InstanceSymbol *
annotationScope(const slang::ast::CallExpression &call,
                const DesignInventory &inventory,
                const slang::ast::InstanceSymbol *owner) {
  // IEEE 1800-2017 32.9: an explicit module_instance is the annotation root;
  // an omitted (or empty) argument uses the module containing this call.
  const auto &info =
      std::get<slang::ast::CallExpression::SystemCallInfo>(call.subroutine);
  if (call.arguments().size() < 2)
    return owner ? owner : owningInstance(*info.scope, inventory);
  const slang::ast::Expression *argument = call.arguments()[1];
  if (!argument || argument->kind == slang::ast::ExpressionKind::EmptyArgument)
    return owner ? owner : owningInstance(*info.scope, inventory);
  const slang::ast::Symbol *symbol = argument->getSymbolReference();
  return symbol ? symbol->as_if<slang::ast::InstanceSymbol>() : nullptr;
}

static std::string normalizeInstance(StringRef value, char divider) {
  std::string result = value.str();
  if (divider != '.')
    std::replace(result.begin(), result.end(), divider, '.');
  return result;
}

static bool terminalMatches(const slang::ast::TimingPathSymbol &path,
                            const slang::ast::Expression *expression,
                            const ParsedIOPath::Port &annotation) {
  const slang::ast::Symbol *symbol =
      expression ? expression->getSymbolReference() : nullptr;
  if (!symbol || symbol->name != annotation.name)
    return false;
  if (!annotation.index)
    return expression->kind != slang::ast::ExpressionKind::ElementSelect &&
           expression->kind != slang::ast::ExpressionKind::RangeSelect;
  if (expression->kind != slang::ast::ExpressionKind::ElementSelect)
    return false;
  slang::ast::EvalContext context(path);
  slang::ConstantValue selected =
      expression->as<slang::ast::ElementSelectExpression>().selector().eval(
          context);
  if (!selected || !selected.isInteger() || selected.integer().hasUnknown())
    return false;
  std::optional<int32_t> value = selected.integer().as<int32_t>();
  return value && *value == *annotation.index;
}

static bool pathMatches(const slang::ast::TimingPathSymbol &path,
                        const ParsedIOPath &annotation) {
  auto inputs = path.getInputs();
  auto outputs = path.getOutputs();
  // IEEE 1800-2017 32.4.1 identifies IOPATH by its input and output port
  // specifications. A full-connection `(a,b *> y)` is one shared path, so
  // annotating only `a -> y` would also mutate b's delay. This first tranche
  // therefore accepts only an exact one-input / one-output path identity.
  if (inputs.size() != 1 || outputs.size() != 1 ||
      !terminalMatches(path, inputs.front(), annotation.input) ||
      !terminalMatches(path, outputs.front(), annotation.output))
    return false;
  // IEEE 1800-2017 32.4.1: an edge-qualified SDF IOPATH annotates only a
  // path with the same edge. An unqualified IOPATH can annotate conditional
  // and nonconditional declarations between the named terminals.
  return !annotation.edge || path.edgeIdentifier == *annotation.edge;
}

static void callDiagnostic(const slang::SourceManager &sourceManager,
                           const slang::ast::CallExpression &call,
                           StringRef severity, const Twine &message) {
  slang::SourceLocation location =
      sourceManager.getFullyExpandedLoc(call.sourceRange.start());
  if (location.valid() && sourceManager.isFileLoc(location))
    errs() << sourceManager.getFileName(location) << ':'
           << sourceManager.getLineNumber(location) << ':'
           << sourceManager.getColumnNumber(location) << ": ";
  errs() << severity << ": " << message << '\n';
}

static bool hasSDFAnnotationToken(const slang::ast::Compilation &compilation) {
  struct Visitor : slang::syntax::SyntaxVisitor<Visitor> {
    bool found = false;

    void handle(const slang::syntax::SyntaxNode &node) {
      if (!found)
        visitDefault(node);
    }

    void visitToken(slang::parsing::Token token) {
      if (token.kind == slang::parsing::TokenKind::SystemIdentifier &&
          token.systemName() == slang::parsing::KnownSystemName::SdfAnnotate)
        found = true;
    }
  } visitor;
  for (const std::shared_ptr<slang::syntax::SyntaxTree> &tree :
       compilation.getSyntaxTrees()) {
    tree->root().visit(visitor);
    if (visitor.found)
      return true;
  }
  return false;
}

class SDFMLIRImporter {
public:
  SDFMLIRImporter(StringRef sourceName, mlir::MLIRContext &context)
      : sourceName(sourceName), context(context), builder(&context) {}

  mlir::FailureOr<mlir::OwningOpRef<mlir::ModuleOp>>
  import(const SDFNode &root) {
    using namespace mlir;
    if (!formHead(root, "DELAYFILE")) {
      error(root, "SDF file must contain one DELAYFILE form");
      return failure();
    }

    SmallVector<NamedAttribute> headers;
    llvm::StringSet<> seenHeaders;
    headers.emplace_back(builder.getStringAttr("source"),
                         builder.getStringAttr(sourceName));
    StringAttr version;
    for (const SDFNode &entry : ArrayRef(root.children).drop_front()) {
      if (const SDFNode *form = formHead(entry, "SDFVERSION")) {
        if (!seenHeaders.insert("sdf_version").second) {
          error(entry, "duplicate singleton SDFVERSION header");
          continue;
        }
        if (form->children.size() != 2 ||
            form->children[1].kind != SDFNode::Kind::String) {
          error(entry, "malformed SDFVERSION header");
          continue;
        }
        version = builder.getStringAttr(form->children[1].text);
        continue;
      }
      if (parseHeader(entry, headers, seenHeaders))
        continue;
      if (!formHead(entry, "CELL") && !formHead(entry, "TIMINGENV"))
        error(entry, "unsupported or misplaced SDF DELAYFILE form");
    }
    if (!version) {
      error(root, "DELAYFILE requires an SDFVERSION header");
      return failure();
    }
    headers.emplace_back(builder.getStringAttr("sdf_version"), version);
    // Header names are singleton grammar productions in Clause 32.  Stop at
    // the normalized boundary before constructing an operation whenever a
    // malformed or duplicate header was diagnosed; MLIR must never receive a
    // duplicate attribute dictionary as a secondary parser mechanism.
    if (failedState)
      return failure();

    auto module = mlir::ModuleOp::create(location(root));
    builder.setInsertionPointToStart(module.getBody());
    Operation *delayFile = createRegionOp<sdf::SDFDelayFileOp>(root, headers);
    builder.setInsertionPointToStart(&delayFile->getRegion(0).front());
    for (const SDFNode &entry : ArrayRef(root.children).drop_front())
      if (formHead(entry, "CELL"))
        parseCell(entry);
    if (failedState)
      return failure();
    return mlir::OwningOpRef<mlir::ModuleOp>(module);
  }

private:
  mlir::Location location(const SDFNode &node) const {
    return mlir::FileLineColLoc::get(&context, sourceName, node.line,
                                     node.column);
  }

  void error(const SDFNode &node, const Twine &message) {
    mlir::emitError(location(node)) << message;
    failedState = true;
  }

  template <typename Op>
  mlir::Operation *createOp(const SDFNode &node,
                            ArrayRef<mlir::NamedAttribute> attributes) {
    mlir::OperationState state(location(node), Op::getOperationName());
    state.addAttributes(attributes);
    return builder.create(state);
  }

  template <typename Op>
  mlir::Operation *createRegionOp(const SDFNode &node,
                                  ArrayRef<mlir::NamedAttribute> attributes) {
    mlir::OperationState state(location(node), Op::getOperationName());
    state.addAttributes(attributes);
    mlir::Region *region = state.addRegion();
    region->push_back(new mlir::Block);
    return builder.create(state);
  }

  mlir::NamedAttribute named(StringRef name, mlir::Attribute value) {
    return {builder.getStringAttr(name), value};
  }

  template <typename Attr, typename... Args>
  Attr checked(const SDFNode &node, Args &&...arguments) {
    return Attr::getChecked(
        [&]() {
          failedState = true;
          return mlir::emitError(location(node));
        },
        &context, std::forward<Args>(arguments)...);
  }

  bool addHeader(const SDFNode &node, StringRef name, mlir::Attribute value,
                 SmallVectorImpl<mlir::NamedAttribute> &attributes,
                 llvm::StringSet<> &seenHeaders) {
    if (!seenHeaders.insert(name).second) {
      error(node, Twine("duplicate singleton ") + name + " header");
      return false;
    }
    attributes.push_back(named(name, value));
    return true;
  }

  bool parseHeader(const SDFNode &node,
                   SmallVectorImpl<mlir::NamedAttribute> &attributes,
                   llvm::StringSet<> &seenHeaders) {
    static constexpr StringLiteral stringHeaders[] = {
        "DESIGN", "DATE", "VENDOR", "PROGRAM", "VERSION", "PROCESS"};
    for (StringRef keywordName : stringHeaders) {
      const SDFNode *form = formHead(node, keywordName);
      if (!form)
        continue;
      if (form->children.size() != 2 ||
          (form->children[1].kind != SDFNode::Kind::String &&
           form->children[1].kind != SDFNode::Kind::Atom)) {
        error(node, Twine("malformed ") + keywordName + " header");
        return true;
      }
      std::string attrName =
          keywordName == "VERSION" ? "program_version" : keywordName.lower();
      addHeader(node, attrName, builder.getStringAttr(form->children[1].text),
                attributes, seenHeaders);
      return true;
    }
    if (const SDFNode *form = formHead(node, "DIVIDER")) {
      if (form->children.size() != 2 ||
          form->children[1].kind != SDFNode::Kind::Atom)
        error(node, "malformed DIVIDER header");
      else
        addHeader(node, "divider",
                  builder.getStringAttr(form->children[1].text), attributes,
                  seenHeaders);
      return true;
    }
    if (const SDFNode *form = formHead(node, "TIMESCALE")) {
      parseTimeScale(*form, attributes, seenHeaders);
      return true;
    }
    for (StringRef keywordName :
         {StringRef("VOLTAGE"), StringRef("TEMPERATURE")}) {
      if (const SDFNode *form = formHead(node, keywordName)) {
        if (form->children.size() != 2) {
          error(node, Twine("malformed ") + keywordName + " header");
          return true;
        }
        if (auto value = parseDelayValue(form->children[1]))
          addHeader(node, keywordName.lower(), value, attributes, seenHeaders);
        return true;
      }
    }
    return false;
  }

  void parseTimeScale(const SDFNode &form,
                      SmallVectorImpl<mlir::NamedAttribute> &attributes,
                      llvm::StringSet<> &seenHeaders) {
    if (form.children.size() != 2 && form.children.size() != 3) {
      error(form, "malformed TIMESCALE header");
      return;
    }
    std::string spelling = form.children[1].text;
    if (form.children.size() == 3)
      spelling += form.children[2].text;
    StringRef combined = spelling;
    size_t unitLength = combined.ends_with_insensitive("ms") ||
                                combined.ends_with_insensitive("us") ||
                                combined.ends_with_insensitive("ns") ||
                                combined.ends_with_insensitive("ps") ||
                                combined.ends_with_insensitive("fs")
                            ? 2
                            : 1;
    if (combined.size() <= unitLength) {
      error(form, "malformed exact TIMESCALE header");
      return;
    }
    auto amount = decimal(form, combined.drop_back(unitLength));
    auto unit = llvm::StringSwitch<std::optional<sdf::TimeUnit>>(
                    combined.take_back(unitLength).lower())
                    .Case("s", sdf::TimeUnit::Seconds)
                    .Case("ms", sdf::TimeUnit::Milliseconds)
                    .Case("us", sdf::TimeUnit::Microseconds)
                    .Case("ns", sdf::TimeUnit::Nanoseconds)
                    .Case("ps", sdf::TimeUnit::Picoseconds)
                    .Case("fs", sdf::TimeUnit::Femtoseconds)
                    .Default(std::nullopt);
    if (!amount || !unit) {
      error(form, "malformed exact TIMESCALE header");
      return;
    }
    auto timescale = checked<sdf::TimeScaleAttr>(form, amount, *unit);
    if (timescale)
      addHeader(form, "timescale", timescale, attributes, seenHeaders);
  }

  sdf::DecimalAttr decimal(const SDFNode &node, StringRef spelling) {
    auto result =
        checked<sdf::DecimalAttr>(node, builder.getStringAttr(spelling));
    return result;
  }

  sdf::DelayValueAttr parseDelayValue(const SDFNode &node) {
    if (node.kind == SDFNode::Kind::Atom) {
      auto value = decimal(node, node.text);
      return value ? checked<sdf::DelayValueAttr>(
                         node, sdf::DelayValueForm::Scalar, sdf::DecimalAttr{},
                         value, sdf::DecimalAttr{})
                   : sdf::DelayValueAttr{};
    }
    if (node.kind != SDFNode::Kind::List || node.children.size() > 1 ||
        (!node.children.empty() &&
         node.children.front().kind != SDFNode::Kind::Atom)) {
      error(node, "delay value must be an empty or one-token list");
      return {};
    }
    if (node.children.empty())
      return checked<sdf::DelayValueAttr>(
          node, sdf::DelayValueForm::Empty, sdf::DecimalAttr{},
          sdf::DecimalAttr{}, sdf::DecimalAttr{});
    StringRef token = node.children.front().text;
    SmallVector<StringRef, 3> fields;
    token.split(fields, ':', /*MaxSplit=*/2, /*KeepEmpty=*/true);
    sdf::DecimalAttr min, typ, max;
    if (fields.size() == 1) {
      typ = decimal(node, fields[0]);
    } else if (fields.size() == 3) {
      if (!fields[0].empty())
        min = decimal(node, fields[0]);
      if (!fields[1].empty())
        typ = decimal(node, fields[1]);
      if (!fields[2].empty())
        max = decimal(node, fields[2]);
    } else {
      error(node, "delay value must be scalar or min:typ:max");
      return {};
    }
    return checked<sdf::DelayValueAttr>(node,
                                        fields.size() == 1
                                            ? sdf::DelayValueForm::Scalar
                                            : sdf::DelayValueForm::Triple,
                                        min, typ, max);
  }

  mlir::ArrayAttr parseDelayList(ArrayRef<SDFNode> nodes) {
    SmallVector<mlir::Attribute, 12> values;
    for (const SDFNode &node : nodes) {
      auto value = parseDelayValue(node);
      if (value)
        values.push_back(value);
    }
    return builder.getArrayAttr(values);
  }

  sdf::PortAttr parsePort(const SDFNode &node) {
    sdf::Edge edge = sdf::Edge::None;
    const SDFNode *nameNode = &node;
    if (node.kind == SDFNode::Kind::List && node.children.size() == 2) {
      auto parsedEdge = llvm::StringSwitch<std::optional<sdf::Edge>>(
                            StringRef(node.children[0].text).lower())
                            .Case("posedge", sdf::Edge::Posedge)
                            .Case("negedge", sdf::Edge::Negedge)
                            .Case("01", sdf::Edge::ZeroOne)
                            .Case("10", sdf::Edge::OneZero)
                            .Case("0z", sdf::Edge::ZeroZ)
                            .Case("z1", sdf::Edge::ZOne)
                            .Case("1z", sdf::Edge::OneZ)
                            .Case("z0", sdf::Edge::ZZero)
                            .Default(std::nullopt);
      if (!parsedEdge) {
        error(node, "unsupported edge identifier");
        return {};
      }
      edge = *parsedEdge;
      nameNode = &node.children[1];
    }
    if (nameNode->kind != SDFNode::Kind::Atom) {
      error(node, "port must be an identifier or edge-qualified identifier");
      return {};
    }
    StringRef spelling = nameNode->text;
    if (!spelling.starts_with("\\") &&
        spelling.find_first_of("!~&|^=") != StringRef::npos) {
      error(node, "port identifier cannot contain a condition operator");
      return {};
    }
    bool hasIndex = false;
    int64_t index = 0;
    size_t open = spelling.rfind('[');
    if (open != StringRef::npos && spelling.ends_with("]")) {
      StringRef indexText = spelling.slice(open + 1, spelling.size() - 1);
      if (indexText.empty() || indexText.getAsInteger(10, index)) {
        error(node, "port select requires a constant integer index");
        return {};
      }
      hasIndex = true;
      spelling = spelling.take_front(open);
    }
    return checked<sdf::PortAttr>(node, builder.getStringAttr(spelling),
                                  hasIndex, index, edge);
  }

  std::optional<sdf::ConditionOpcode> conditionOpcode(StringRef spelling) {
    return llvm::StringSwitch<std::optional<sdf::ConditionOpcode>>(
               spelling.lower())
        .Cases({"!", "~"}, sdf::ConditionOpcode::Not)
        .Cases({"&&", "&"}, sdf::ConditionOpcode::And)
        .Cases({"||", "|"}, sdf::ConditionOpcode::Or)
        .Case("^", sdf::ConditionOpcode::Xor)
        .Case("==", sdf::ConditionOpcode::Eq)
        .Case("!=", sdf::ConditionOpcode::Ne)
        .Case("===", sdf::ConditionOpcode::CaseEq)
        .Case("!==", sdf::ConditionOpcode::CaseNe)
        .Default(std::nullopt);
  }

  struct ConditionLexeme {
    enum class Kind { Operand, Operator } kind;
    StringRef spelling;
    sdf::ConditionOpcode opcode = sdf::ConditionOpcode::Port;
  };

  bool lexCompactCondition(const SDFNode &node,
                           SmallVectorImpl<ConditionLexeme> &lexemes) {
    StringRef spelling = node.text;
    // An escaped SDF identifier ends at token whitespace; operator glyphs in
    // that spelling are identifier data, not condition syntax.
    if (spelling.starts_with("\\")) {
      lexemes.push_back({ConditionLexeme::Kind::Operand, spelling,
                         sdf::ConditionOpcode::Port});
      return true;
    }
    size_t cursor = 0;
    auto operatorAt = [&](size_t at)
        -> std::optional<std::pair<sdf::ConditionOpcode, size_t>> {
      StringRef remaining = spelling.drop_front(at);
      for (auto candidate : {std::pair<StringLiteral, sdf::ConditionOpcode>{
                                 "!==", sdf::ConditionOpcode::CaseNe},
                             {"===", sdf::ConditionOpcode::CaseEq},
                             {"!=", sdf::ConditionOpcode::Ne},
                             {"==", sdf::ConditionOpcode::Eq},
                             {"&&", sdf::ConditionOpcode::And},
                             {"||", sdf::ConditionOpcode::Or},
                             {"!", sdf::ConditionOpcode::Not},
                             {"~", sdf::ConditionOpcode::Not},
                             {"&", sdf::ConditionOpcode::And},
                             {"|", sdf::ConditionOpcode::Or},
                             {"^", sdf::ConditionOpcode::Xor}})
        if (remaining.starts_with(candidate.first))
          return std::pair(candidate.second, candidate.first.size());
      return std::nullopt;
    };
    while (cursor < spelling.size()) {
      if (auto op = operatorAt(cursor)) {
        lexemes.push_back({ConditionLexeme::Kind::Operator,
                           spelling.slice(cursor, cursor + op->second),
                           op->first});
        cursor += op->second;
      } else {
        size_t begin = cursor;
        while (cursor < spelling.size() && !operatorAt(cursor))
          ++cursor;
        if (begin == cursor) {
          error(node, "condition contains an empty operand");
          return false;
        }
        lexemes.push_back({ConditionLexeme::Kind::Operand,
                           spelling.slice(begin, cursor),
                           sdf::ConditionOpcode::Port});
      }
      if (lexemes.size() > 4096) {
        error(node, "condition exceeds the supported token limit of 4096");
        return false;
      }
    }
    return !lexemes.empty();
  }

  bool appendConditionOperand(const SDFNode &node, StringRef spelling,
                              SmallVectorImpl<mlir::Attribute> &tokens) {
    std::string lowerStorage = spelling.lower();
    StringRef lower = lowerStorage;
    std::optional<int64_t> constant =
        llvm::StringSwitch<std::optional<int64_t>>(lower)
            .Cases({"0", "1'b0"}, 0)
            .Cases({"1", "1'b1"}, 1)
            .Cases({"x", "1'bx"}, 2)
            .Cases({"z", "1'bz"}, 3)
            .Default(std::nullopt);
    if (constant) {
      if (tokens.size() >= 4096) {
        error(node, "condition exceeds the supported token limit of 4096");
        return false;
      }
      auto token = checked<sdf::ConditionTokenAttr>(
          node, sdf::ConditionOpcode::Constant, sdf::PortAttr{},
          builder.getI64IntegerAttr(*constant));
      if (!token)
        return false;
      tokens.push_back(token);
      return true;
    }
    SDFNode operand = node;
    operand.text = spelling.str();
    auto port = parsePort(operand);
    if (!port)
      return false;
    if (tokens.size() >= 4096) {
      error(node, "condition exceeds the supported token limit of 4096");
      return false;
    }
    auto token = checked<sdf::ConditionTokenAttr>(
        node, sdf::ConditionOpcode::Port, port, mlir::IntegerAttr{});
    if (!token)
      return false;
    tokens.push_back(token);
    return true;
  }

  bool appendCompactCondition(const SDFNode &node,
                              SmallVectorImpl<mlir::Attribute> &tokens) {
    SmallVector<ConditionLexeme, 16> lexemes;
    if (!lexCompactCondition(node, lexemes))
      return false;
    size_t cursor = 0;
    auto appendOperator = [&](sdf::ConditionOpcode opcode) {
      if (tokens.size() >= 4096) {
        error(node, "condition exceeds the supported token limit of 4096");
        return false;
      }
      auto token = checked<sdf::ConditionTokenAttr>(
          node, opcode, sdf::PortAttr{}, mlir::IntegerAttr{});
      if (!token)
        return false;
      tokens.push_back(token);
      return true;
    };
    std::function<bool(unsigned)> parseExpression;
    auto precedence = [](sdf::ConditionOpcode opcode) -> unsigned {
      switch (opcode) {
      case sdf::ConditionOpcode::Or:
        return 1;
      case sdf::ConditionOpcode::Xor:
        return 2;
      case sdf::ConditionOpcode::And:
        return 3;
      case sdf::ConditionOpcode::Eq:
      case sdf::ConditionOpcode::Ne:
      case sdf::ConditionOpcode::CaseEq:
      case sdf::ConditionOpcode::CaseNe:
        return 4;
      default:
        return 0;
      }
    };
    std::function<bool()> parseUnary = [&]() {
      size_t notCount = 0;
      while (cursor < lexemes.size() &&
             lexemes[cursor].kind == ConditionLexeme::Kind::Operator &&
             lexemes[cursor].opcode == sdf::ConditionOpcode::Not) {
        ++cursor;
        ++notCount;
      }
      if (cursor >= lexemes.size() ||
          lexemes[cursor].kind != ConditionLexeme::Kind::Operand)
        return false;
      if (!appendConditionOperand(node, lexemes[cursor++].spelling, tokens))
        return false;
      while (notCount--)
        if (!appendOperator(sdf::ConditionOpcode::Not))
          return false;
      return true;
    };
    parseExpression = [&](unsigned minimumPrecedence) {
      if (!parseUnary())
        return false;
      while (cursor < lexemes.size() &&
             lexemes[cursor].kind == ConditionLexeme::Kind::Operator) {
        sdf::ConditionOpcode opcode = lexemes[cursor].opcode;
        unsigned currentPrecedence = precedence(opcode);
        if (currentPrecedence < minimumPrecedence || currentPrecedence == 0)
          break;
        ++cursor;
        if (!parseExpression(currentPrecedence + 1))
          return false;
        if (!appendOperator(opcode))
          return false;
      }
      return true;
    };
    if (!parseExpression(1) || cursor != lexemes.size()) {
      error(node, "condition has malformed compact operator syntax");
      return false;
    }
    return true;
  }

  bool appendCondition(const SDFNode &node,
                       SmallVectorImpl<mlir::Attribute> &tokens) {
    auto appendOperator = [&](sdf::ConditionOpcode opcode) {
      if (tokens.size() >= 4096) {
        error(node, "condition exceeds the supported token limit of 4096");
        return false;
      }
      auto token = checked<sdf::ConditionTokenAttr>(
          node, opcode, sdf::PortAttr{}, mlir::IntegerAttr{});
      if (!token)
        return false;
      tokens.push_back(token);
      return true;
    };
    if (node.kind == SDFNode::Kind::Atom) {
      return appendCompactCondition(node, tokens);
    }
    if (node.kind != SDFNode::Kind::List || node.children.empty()) {
      error(node, "condition contains an unsupported operand");
      return false;
    }
    if (node.children.size() == 1)
      return appendCondition(node.children.front(), tokens);
    if (node.children.size() == 2) {
      auto opcode = conditionOpcode(node.children[0].text);
      if (!opcode || *opcode != sdf::ConditionOpcode::Not ||
          !appendCondition(node.children[1], tokens)) {
        error(node, "condition requires a supported unary operator");
        return false;
      }
      return appendOperator(*opcode);
    }
    if (node.children.size() == 3) {
      auto prefix = conditionOpcode(node.children[0].text);
      auto infix = conditionOpcode(node.children[1].text);
      std::optional<sdf::ConditionOpcode> opcode = prefix ? prefix : infix;
      const SDFNode &left = prefix ? node.children[1] : node.children[0];
      const SDFNode &right = node.children[2];
      if (!opcode || *opcode == sdf::ConditionOpcode::Not ||
          !appendCondition(left, tokens) || !appendCondition(right, tokens)) {
        error(node, "condition requires a supported binary operator");
        return false;
      }
      return appendOperator(*opcode);
    }
    error(node, "condition expression exceeds the normalized operator arity");
    return false;
  }

  sdf::ConditionAttr parseCondition(const SDFNode &node) {
    SmallVector<mlir::Attribute, 16> tokens;
    if (!appendCondition(node, tokens) || tokens.size() > 4096)
      return {};
    return checked<sdf::ConditionAttr>(node, builder.getArrayAttr(tokens),
                                       false);
  }

  sdf::TimingEventAttr parseTimingEvent(const SDFNode &node) {
    sdf::ConditionAttr condition;
    const SDFNode *portNode = &node;
    if (node.kind == SDFNode::Kind::List && !node.children.empty() &&
        (keyword(node.children.front(), "COND") ||
         keyword(node.children.front(), "SCOND") ||
         keyword(node.children.front(), "CCOND"))) {
      if (node.children.size() != 3) {
        error(node, "conditional timing event requires exactly one port and "
                    "expression");
        return {};
      }
      portNode = &node.children[1];
      condition = parseCondition(node.children[2]);
    }
    auto port = parsePort(*portNode);
    return port ? checked<sdf::TimingEventAttr>(node, port, condition)
                : sdf::TimingEventAttr{};
  }

  void parseCell(const SDFNode &cell) {
    mlir::StringAttr cellType, instance;
    bool wildcard = false;
    for (const SDFNode &member : ArrayRef(cell.children).drop_front()) {
      if (const SDFNode *form = formHead(member, "CELLTYPE")) {
        if (form->children.size() == 2)
          cellType = builder.getStringAttr(form->children[1].text);
      } else if (const SDFNode *form = formHead(member, "INSTANCE")) {
        if (form->children.size() == 2 && form->children[1].text == "*")
          wildcard = true;
        else if (form->children.size() == 2)
          instance = builder.getStringAttr(form->children[1].text);
      }
    }
    if (!cellType) {
      error(cell, "CELL requires CELLTYPE");
      return;
    }
    SmallVector<mlir::NamedAttribute> attrs{named("cell_type", cellType)};
    if (instance)
      attrs.push_back(named("instance", instance));
    if (wildcard)
      attrs.push_back(named("wildcard", builder.getUnitAttr()));
    mlir::Operation *cellOp = createRegionOp<sdf::SDFCellOp>(cell, attrs);
    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPointToStart(&cellOp->getRegion(0).front());
    for (const SDFNode &member : ArrayRef(cell.children).drop_front()) {
      if (const SDFNode *delay = formHead(member, "DELAY"))
        parseDelaySection(*delay);
      else if (const SDFNode *timing = formHead(member, "TIMINGCHECK"))
        parseTimingChecks(*timing);
      else if (const SDFNode *labels = formHead(member, "LABEL"))
        parseLabels(*labels);
    }
  }

  void parseDelaySection(const SDFNode &section) {
    for (const SDFNode &modeNode : ArrayRef(section.children).drop_front()) {
      sdf::DelayMode mode;
      if (formHead(modeNode, "ABSOLUTE"))
        mode = sdf::DelayMode::Absolute;
      else if (formHead(modeNode, "INCREMENT"))
        mode = sdf::DelayMode::Increment;
      else {
        error(modeNode, "DELAY requires ABSOLUTE or INCREMENT records");
        continue;
      }
      for (const SDFNode &record : ArrayRef(modeNode.children).drop_front())
        parseDelayRecord(record, mode);
    }
  }

  void parseDelayRecord(const SDFNode &record, sdf::DelayMode mode) {
    if (formHead(record, "IOPATH"))
      return parsePath(record, mode, sdf::PathKind::IOPath, {});
    if (const SDFNode *form = formHead(record, "COND")) {
      if (form->children.size() != 3) {
        error(record, "COND requires exactly one expression and IOPATH");
        return;
      }
      const SDFNode &path = form->children.back();
      if (!formHead(path, "IOPATH")) {
        error(record, "COND must contain IOPATH");
        return;
      }
      auto condition = parseCondition(form->children[1]);
      return parsePath(path, mode, sdf::PathKind::Cond, condition);
    }
    if (const SDFNode *form = formHead(record, "CONDELSE")) {
      if (form->children.size() != 2 ||
          !formHead(form->children[1], "IOPATH")) {
        error(record, "CONDELSE must contain one IOPATH");
        return;
      }
      return parsePath(form->children[1], mode, sdf::PathKind::CondElse, {});
    }
    if (formHead(record, "INTERCONNECT"))
      return parseInterconnect(record, mode);
    if (formHead(record, "PORT"))
      return parseTerminal(record, mode, sdf::TerminalDelayKind::Port);
    if (formHead(record, "NETDELAY"))
      return parseTerminal(record, mode, sdf::TerminalDelayKind::NetDelay);
    if (formHead(record, "DEVICE"))
      return parseTerminal(record, mode, sdf::TerminalDelayKind::Device);
    if (formHead(record, "PATHPULSE"))
      return parsePulse(record, sdf::PulseKind::Absolute);
    if (formHead(record, "PATHPULSEPERCENT"))
      return parsePulse(record, sdf::PulseKind::Percent);
    error(record, "unsupported DELAY annotation record");
  }

  void parsePath(const SDFNode &record, sdf::DelayMode mode, sdf::PathKind kind,
                 sdf::ConditionAttr condition) {
    if (record.children.size() < 4) {
      error(record, "IOPATH requires input, output, and delay values");
      return;
    }
    auto input = parsePort(record.children[1]);
    auto output = parsePort(record.children[2]);
    SmallVector<mlir::NamedAttribute> attrs{
        named("mode", builder.getI32IntegerAttr(static_cast<int32_t>(mode))),
        named("kind", builder.getI32IntegerAttr(static_cast<int32_t>(kind))),
        named("input", input), named("output", output),
        named("delays",
              parseDelayList(ArrayRef(record.children).drop_front(3)))};
    if (condition)
      attrs.push_back(named("condition", condition));
    createOp<sdf::SDFPathDelayOp>(record, attrs);
  }

  void parseInterconnect(const SDFNode &record, sdf::DelayMode mode) {
    if (record.children.size() < 4) {
      error(record, "INTERCONNECT requires two ports and delay values");
      return;
    }
    createOp<sdf::SDFInterconnectDelayOp>(
        record,
        {named("mode", builder.getI32IntegerAttr(static_cast<int32_t>(mode))),
         named("source_port", parsePort(record.children[1])),
         named("destination_port", parsePort(record.children[2])),
         named("delays",
               parseDelayList(ArrayRef(record.children).drop_front(3)))});
  }

  void parseTerminal(const SDFNode &record, sdf::DelayMode mode,
                     sdf::TerminalDelayKind kind) {
    size_t delayStart = 1;
    sdf::PortAttr terminal;
    if (kind != sdf::TerminalDelayKind::Device ||
        (record.children.size() > 1 &&
         record.children[1].kind != SDFNode::Kind::List)) {
      if (record.children.size() < 3) {
        error(record, "terminal delay requires a terminal and delay values");
        return;
      }
      terminal = parsePort(record.children[1]);
      delayStart = 2;
    }
    SmallVector<mlir::NamedAttribute> attrs{
        named("mode", builder.getI32IntegerAttr(static_cast<int32_t>(mode))),
        named("kind", builder.getI32IntegerAttr(static_cast<int32_t>(kind))),
        named("delays", parseDelayList(
                            ArrayRef(record.children).drop_front(delayStart)))};
    if (terminal)
      attrs.push_back(named("terminal", terminal));
    createOp<sdf::SDFTerminalDelayOp>(record, attrs);
  }

  void parsePulse(const SDFNode &record, sdf::PulseKind kind) {
    size_t cursor = 1;
    sdf::PortAttr input, output;
    if (record.children.size() >= 5) {
      input = parsePort(record.children[cursor++]);
      output = parsePort(record.children[cursor++]);
    }
    if (record.children.size() - cursor != 2) {
      error(record, "pulse record requires reject and error limits");
      return;
    }
    SmallVector<mlir::NamedAttribute> attrs{
        named("kind", builder.getI32IntegerAttr(static_cast<int32_t>(kind))),
        named("reject", parseDelayValue(record.children[cursor])),
        named("error", parseDelayValue(record.children[cursor + 1]))};
    if (input) {
      attrs.push_back(named("input", input));
      attrs.push_back(named("output", output));
    }
    createOp<sdf::SDFPulseOp>(record, attrs);
  }

  void parseTimingChecks(const SDFNode &section) {
    for (const SDFNode &record : ArrayRef(section.children).drop_front()) {
      auto kind = llvm::StringSwitch<std::optional<sdf::TimingCheckKind>>(
                      record.children.empty()
                          ? StringRef{}
                          : StringRef(record.children.front().text).lower())
                      .Case("setup", sdf::TimingCheckKind::Setup)
                      .Case("hold", sdf::TimingCheckKind::Hold)
                      .Case("setuphold", sdf::TimingCheckKind::SetupHold)
                      .Case("recovery", sdf::TimingCheckKind::Recovery)
                      .Case("removal", sdf::TimingCheckKind::Removal)
                      .Case("recrem", sdf::TimingCheckKind::Recrem)
                      .Case("skew", sdf::TimingCheckKind::Skew)
                      .Case("bidirectskew", sdf::TimingCheckKind::BidirectSkew)
                      .Case("period", sdf::TimingCheckKind::Period)
                      .Case("width", sdf::TimingCheckKind::Width)
                      .Case("nochange", sdf::TimingCheckKind::NoChange)
                      .Default(std::nullopt);
      if (!kind) {
        error(record, "unsupported TIMINGCHECK record");
        continue;
      }
      size_t eventCount = (*kind == sdf::TimingCheckKind::Period ||
                           *kind == sdf::TimingCheckKind::Width)
                              ? 1
                              : 2;
      size_t limitCount = (*kind == sdf::TimingCheckKind::SetupHold ||
                           *kind == sdf::TimingCheckKind::Recrem ||
                           *kind == sdf::TimingCheckKind::BidirectSkew ||
                           *kind == sdf::TimingCheckKind::NoChange)
                              ? 2
                              : 1;
      if (record.children.size() != 1 + eventCount + limitCount) {
        error(record, "timing-check record has the wrong arity");
        continue;
      }
      SmallVector<mlir::Attribute> events, limits;
      for (size_t i = 0; i < eventCount; ++i)
        if (auto event = parseTimingEvent(record.children[1 + i]))
          events.push_back(event);
      for (size_t i = 0; i < limitCount; ++i)
        if (auto limit = parseDelayValue(record.children[1 + eventCount + i]))
          limits.push_back(limit);
      createOp<sdf::SDFTimingCheckOp>(
          record, {named("kind", builder.getI32IntegerAttr(
                                     static_cast<int32_t>(*kind))),
                   named("events", builder.getArrayAttr(events)),
                   named("limits", builder.getArrayAttr(limits))});
    }
  }

  void parseLabels(const SDFNode &section) {
    for (const SDFNode &modeNode : ArrayRef(section.children).drop_front()) {
      sdf::DelayMode mode;
      if (formHead(modeNode, "ABSOLUTE"))
        mode = sdf::DelayMode::Absolute;
      else if (formHead(modeNode, "INCREMENT"))
        mode = sdf::DelayMode::Increment;
      else {
        // IEEE 1800-2017 32.4.3: LABEL changes specparams using the SDF
        // ABSOLUTE/INCREMENT grouping. Keep the mode explicit in transient IR
        // so ordered application never has to reconstruct source structure.
        error(modeNode, "LABEL requires ABSOLUTE or INCREMENT records");
        continue;
      }
      for (const SDFNode &record : ArrayRef(modeNode.children).drop_front()) {
        if (record.kind != SDFNode::Kind::List || record.children.size() != 2 ||
            record.children.front().kind != SDFNode::Kind::Atom) {
          error(record, "LABEL entry requires a name and value");
          continue;
        }
        createOp<sdf::SDFLabelOp>(
            record,
            {named("mode",
                   builder.getI32IntegerAttr(static_cast<int32_t>(mode))),
             named("name", builder.getStringAttr(record.children[0].text)),
             named("value", parseDelayValue(record.children[1]))});
      }
    }
  }

  StringRef sourceName;
  mlir::MLIRContext &context;
  mlir::OpBuilder builder;
  bool failedState = false;
};

} // namespace

mlir::FailureOr<mlir::OwningOpRef<mlir::ModuleOp>>
importSDF(StringRef sourceName, StringRef contents, mlir::MLIRContext &context,
          bool verifyIR) {
  context.getOrLoadDialect<sdf::ObeliskSDFDialect>();
  std::optional<SDFNode> root = SDFParser(sourceName, contents).parse();
  if (!root)
    return mlir::failure();
  auto module = SDFMLIRImporter(sourceName, context).import(*root);
  if (mlir::failed(module))
    return mlir::failure();
  if (verifyIR && mlir::failed(mlir::verify(**module)))
    return mlir::failure();
  return module;
}

namespace {

static std::pair<unsigned, unsigned>
sourcePosition(mlir::Operation *operation) {
  if (auto location = dyn_cast<mlir::FileLineColLoc>(operation->getLoc()))
    return {location.getLine(), location.getColumn()};
  return {1, 1};
}

static void sdfIRDiagnostic(sdf::SDFDelayFileOp file,
                            mlir::Operation *operation, const Twine &message) {
  auto [line, column] = sourcePosition(operation);
  errs() << file.getSource() << ':' << line << ':' << column
         << ": error: " << message << '\n';
}

static std::optional<ParsedSDF> consumeSDFIR(mlir::ModuleOp module) {
  auto files = module.getOps<sdf::SDFDelayFileOp>();
  if (!llvm::hasSingleElement(files))
    return std::nullopt;
  sdf::SDFDelayFileOp file = *files.begin();
  ParsedSDF result;
  if (auto divider = file.getDividerAttr())
    result.divider = divider.getValue().front();
  auto timescale = file.getTimescaleAttr();
  if (!timescale) {
    sdfIRDiagnostic(file, file, "static SDF annotation requires TIMESCALE");
    return std::nullopt;
  }
  auto amount = parseDecimal(timescale.getAmount().getSpelling().getValue());
  if (!amount || amount->negative) {
    sdfIRDiagnostic(file, file, "invalid SDF TIMESCALE amount");
    return std::nullopt;
  }
  uint64_t unit = 1;
  switch (timescale.getUnit()) {
  case sdf::TimeUnit::Seconds:
    unit = UINT64_C(1'000'000'000'000'000);
    break;
  case sdf::TimeUnit::Milliseconds:
    unit = UINT64_C(1'000'000'000'000);
    break;
  case sdf::TimeUnit::Microseconds:
    unit = UINT64_C(1'000'000'000);
    break;
  case sdf::TimeUnit::Nanoseconds:
    unit = UINT64_C(1'000'000);
    break;
  case sdf::TimeUnit::Picoseconds:
    unit = UINT64_C(1'000);
    break;
  case sdf::TimeUnit::Femtoseconds:
    break;
  }
  std::optional<unsigned> unitOrder = powerOfTenOrder(unit);
  if (!unitOrder) {
    sdfIRDiagnostic(file, file, "unsupported SDF TIMESCALE unit");
    return std::nullopt;
  }
  int64_t timeScaleOrder =
      addDecimalOrder(amount->exponent10, static_cast<int>(*unitOrder));
  std::optional<uint64_t> scaled = scaleDecimalExactlyToLimit(
      *amount, timeScaleOrder, std::numeric_limits<uint64_t>::max());
  if (!scaled || !*scaled) {
    sdfIRDiagnostic(file, file,
                    "SDF TIMESCALE is incompatible with static annotation");
    return std::nullopt;
  }
  result.timeScaleFs = *scaled;

  for (sdf::SDFCellOp cellOp :
       file.getBody().front().getOps<sdf::SDFCellOp>()) {
    ParsedCell cell;
    std::tie(cell.line, cell.column) = sourcePosition(cellOp);
    cell.cellType = cellOp.getCellType().str();
    if (auto instance = cellOp.getInstanceAttr())
      cell.instance = instance.getValue().str();
    cell.wildcard = static_cast<bool>(cellOp.getWildcardAttr());
    for (mlir::Operation &operation : cellOp.getBody().front()) {
      auto pathOp = dyn_cast<sdf::SDFPathDelayOp>(operation);
      if (!pathOp || pathOp.getKind() != sdf::PathKind::IOPath) {
        auto [line, column] = sourcePosition(&operation);
        StringRef description = isa<sdf::SDFTimingCheckOp>(operation)
                                    ? "TIMINGCHECK"
                                    : operation.getName().getStringRef();
        cell.unsupportedTimingData.push_back({description.str(), line, column});
        continue;
      }
      ParsedIOPath path;
      std::tie(path.line, path.column) = sourcePosition(pathOp);
      path.kind = pathOp.getMode() == sdf::DelayMode::Increment
                      ? SDFAnnotationDatabase::DelayAnnotation::Kind::Increment
                      : SDFAnnotationDatabase::DelayAnnotation::Kind::Absolute;
      auto copyPort = [](sdf::PortAttr port) {
        ParsedIOPath::Port result{port.getName().getValue().str(),
                                  std::nullopt};
        if (port.getHasIndex())
          result.index = static_cast<int32_t>(port.getIndex());
        return result;
      };
      path.input = copyPort(pathOp.getInput());
      path.output = copyPort(pathOp.getOutput());
      if (pathOp.getInput().getEdge() == sdf::Edge::Posedge)
        path.edge = slang::ast::EdgeKind::PosEdge;
      else if (pathOp.getInput().getEdge() == sdf::Edge::Negedge)
        path.edge = slang::ast::EdgeKind::NegEdge;
      for (mlir::Attribute attribute : pathOp.getDelays()) {
        auto value = cast<sdf::DelayValueAttr>(attribute);
        ParsedIOPath::DelayValue parsedValue;
        parsedValue.form = value.getForm();
        auto parseMember = [&](sdf::DecimalAttr member,
                               std::optional<ExactDecimal> &destination) {
          if (!member)
            return true;
          destination = parseDecimal(member.getSpelling().getValue());
          if (!destination) {
            sdfIRDiagnostic(file, pathOp, "invalid SDF delay value");
            return false;
          }
          if (path.kind ==
                  SDFAnnotationDatabase::DelayAnnotation::Kind::Absolute &&
              destination->negative) {
            // IEEE 1800-2017 32.5/.6: only INCREMENT is signed. Check every
            // preserved MTM member while precise transient-IR location remains.
            sdfIRDiagnostic(file, pathOp,
                            "invalid nonnegative SDF delay value");
            return false;
          }
          return true;
        };
        if (!parseMember(value.getMin(), parsedValue.min) ||
            !parseMember(value.getTyp(), parsedValue.typ) ||
            !parseMember(value.getMax(), parsedValue.max))
          return std::nullopt;
        path.delays.push_back(std::move(parsedValue));
      }
      cell.paths.push_back(std::move(path));
    }
    result.cells.push_back(std::move(cell));
  }
  return result;
}

} // namespace

const SDFAnnotationDatabase::DelayAnnotations *
SDFAnnotationDatabase::getTimingPathAnnotations(
    const slang::ast::TimingPathSymbol &path) const {
  auto found = timingPathAnnotations.find(&path);
  return found == timingPathAnnotations.end() ? nullptr : &found->second;
}

bool SDFAnnotationDatabase::isAppliedCall(
    const slang::ast::CallExpression &call) const {
  return appliedCalls.contains(&call);
}

std::unique_ptr<SDFAnnotationDatabase>
buildSDFAnnotationDatabase(slang::ast::Compilation &compilation,
                           const slang::SourceManager &sourceManager) {
  auto result = std::make_unique<SDFAnnotationDatabase>();
  // The parser has already classified every expanded system-identifier
  // token, including macro expansion and token pasting. Use that compact CST
  // inventory as the pay-for-play gate: ordinary designs never force a full
  // elaborated-AST walk or allocate instance/path maps for SDF.
  if (!hasSDFAnnotationToken(compilation))
    return result;
  // IEEE 1800-2017 32.9 assigns annotation to an elaborated module scope.
  // Inspect the elaborated calls rather than source-buffer spelling: token
  // pasting can create `$sdf_annotate` without that text appearing in any
  // user buffer. The timing-path inventory remains lazy after this gate.
  DesignInventory inventory;
  compilation.getRoot().visit(inventory);
  if (inventory.calls.empty())
    return result;

  struct OrderedCandidate {
    const slang::ast::CallExpression *call;
    const slang::ast::InstanceSymbol *scope;
    const slang::ast::ProceduralBlockSymbol *block;
  };
  SmallVector<OrderedCandidate, 2> orderedCandidates;
  for (const DesignInventory::AnnotationCall &candidate : inventory.calls) {
    auto startup = inventory.startupBlock.find(candidate.call);
    if (startup == inventory.startupBlock.end())
      continue;
    if (const slang::ast::InstanceSymbol *scope =
            annotationScope(*candidate.call, inventory, candidate.owner))
      orderedCandidates.push_back({candidate.call, scope, startup->second});
  }
  constexpr size_t maxAnnotationApplicationWork = 1 << 22;
  size_t annotationApplicationWork = 0;
  DenseSet<const slang::ast::CallExpression *> unorderedCalls;
  auto contains = [](StringRef outer, StringRef inner) {
    return inner == outer || (inner.starts_with(outer) &&
                              inner.drop_front(outer.size()).starts_with("."));
  };
  for (auto [leftIndex, left] : llvm::enumerate(orderedCandidates)) {
    for (const OrderedCandidate &right :
         ArrayRef(orderedCandidates).drop_front(leftIndex + 1)) {
      if (annotationApplicationWork == maxAnnotationApplicationWork) {
        callDiagnostic(sourceManager, *right.call, "error",
                       "static SDF annotation application-work resource "
                       "limit exceeded");
        return nullptr;
      }
      ++annotationApplicationWork;
      if (left.block == right.block)
        continue;
      StringRef leftPath = left.scope->getHierarchicalPath();
      StringRef rightPath = right.scope->getHierarchicalPath();
      if (!contains(leftPath, rightPath) && !contains(rightPath, leftPath))
        continue;
      // IEEE 1800-2017 32.5/.6 make successive annotations observable. Calls
      // in different initial processes have no language order; reject only
      // overlapping annotation roots, while proving disjoint roots commute.
      unorderedCalls.insert(left.call);
      unorderedCalls.insert(right.call);
    }
  }
  DenseMap<const slang::ast::InstanceSymbol *,
           SmallVector<const slang::ast::TimingPathSymbol *, 4>>
      pathCache;
  struct CachedSDF {
    std::shared_ptr<const std::string> filename;
    std::shared_ptr<const ParsedSDF> contents;
  };
  StringMap<CachedSDF> parsedSDFCache;
  constexpr size_t maxStaticSDFFileBytes = 64 * 1024 * 1024;
  constexpr size_t maxStaticSDFCacheBytes = 256 * 1024 * 1024;
  constexpr size_t maxStaticSDFCacheFiles = 256;
  constexpr size_t maxStaticSDFCacheEntries = 1 << 20;
  constexpr size_t maxMatchedDelayUpdates = 1 << 20;
  size_t parsedSDFCacheBytes = 0;
  size_t parsedSDFCacheEntries = 0;
  size_t matchedDelayUpdates = 0;
  bool invalid = false;
  mlir::MLIRContext sdfContext;

  SmallVector<DesignInventory::AnnotationCall, 2> annotationCalls;
  for (const slang::ast::CallExpression *call : inventory.orderedStartupCalls) {
    auto owner = inventory.callOwner.find(call);
    if (owner != inventory.callOwner.end())
      annotationCalls.push_back({call, owner->second});
  }
  for (const DesignInventory::AnnotationCall &call : inventory.calls)
    if (!inventory.startupCalls.contains(call.call))
      annotationCalls.push_back(call);

  // IEEE 1800-2017 32.5/.6: successive annotations are not commutative.
  // The startup scanner records sequential-block statement order directly;
  // calls in distinct overlapping initial processes were rejected above and
  // calls in disjoint roots commute, so this is the total observable order.
  for (const DesignInventory::AnnotationCall &annotationCall :
       annotationCalls) {
    const slang::ast::CallExpression *call = annotationCall.call;
    // IEEE 1800-2017 32.9 defines `$sdf_annotate` as an executing system
    // task. Baking it into the elaborated timing metadata is equivalent only
    // when execution is provably once, at startup, and in source order.
    if (!inventory.startupCalls.contains(call) ||
        unorderedCalls.contains(call)) {
      callDiagnostic(sourceManager, *call, "error",
                     unorderedCalls.contains(call)
                         ? "static $sdf_annotate cannot order calls across "
                           "multiple initial blocks with overlapping scopes"
                         : "static $sdf_annotate must be an unconditional, "
                           "undelayed statement in one initial block");
      invalid = true;
      continue;
    }
    if (call->arguments().size() > 7) {
      callDiagnostic(sourceManager, *call, "error",
                     "$sdf_annotate accepts at most seven arguments");
      invalid = true;
      continue;
    }
    std::optional<std::string> filename = constantFilename(*call);
    const slang::ast::InstanceSymbol *scope =
        annotationScope(*call, inventory, annotationCall.owner);
    if (!filename || !scope) {
      callDiagnostic(sourceManager, *call, "error",
                     "$sdf_annotate requires a constant string filename and "
                     "an elaborated module scope");
      invalid = true;
      continue;
    }
    std::optional<SDFMTMSelection> mtm =
        annotationMTMSelection(*call, sourceManager);
    if (!mtm) {
      invalid = true;
      continue;
    }
    std::shared_ptr<const ParsedSDF> sdf;
    std::shared_ptr<const std::string> annotationFilename;
    auto cachedSDF = parsedSDFCache.find(*filename);
    if (cachedSDF != parsedSDFCache.end()) {
      sdf = cachedSDF->second.contents;
      annotationFilename = cachedSDF->second.filename;
      if (!sdf) {
        invalid = true;
        continue;
      }
    } else {
      if (parsedSDFCache.size() == maxStaticSDFCacheFiles) {
        callDiagnostic(sourceManager, *call, "error",
                       "static SDF annotation file-count resource limit "
                       "exceeded");
        invalid = true;
        continue;
      }
      annotationFilename = std::make_shared<const std::string>(*filename);
      // IEEE 1800-2017 32.3 and 32.9 permit repeated annotation files. Cache
      // failed I/O/import states too: later calls see the same deterministic
      // failure without reparsing hostile text or multiplying diagnostics.
      auto [newCacheEntry, inserted] = parsedSDFCache.try_emplace(
          *annotationFilename, CachedSDF{annotationFilename, nullptr});
      (void)inserted;
      ErrorOr<std::unique_ptr<MemoryBuffer>> buffer =
          MemoryBuffer::getFile(*filename, /*IsText=*/true);
      if (!buffer) {
        callDiagnostic(sourceManager, *call, "error",
                       Twine("could not read SDF file '") + *filename +
                           "': " + buffer.getError().message());
        invalid = true;
        continue;
      }
      size_t sourceBytes = (*buffer)->getBufferSize();
      if (sourceBytes > maxStaticSDFFileBytes) {
        callDiagnostic(sourceManager, *call, "error",
                       "SDF file exceeds the 64 MiB static annotation "
                       "resource limit");
        invalid = true;
        continue;
      }
      if (sourceBytes > maxStaticSDFCacheBytes - parsedSDFCacheBytes) {
        callDiagnostic(sourceManager, *call, "error",
                       "static SDF annotation aggregate byte resource limit "
                       "exceeded");
        invalid = true;
        continue;
      }
      parsedSDFCacheBytes += sourceBytes;
      // Production and obelisk-translate share this exact typed Clause 32
      // normalization boundary. The database consumes that transient IR and
      // retains no SDF operation or runtime state.
      auto sdfModule =
          importSDF(*filename, (*buffer)->getBuffer(), sdfContext, true);
      std::optional<ParsedSDF> parsed =
          failed(sdfModule) ? std::nullopt : consumeSDFIR(**sdfModule);
      if (!parsed) {
        invalid = true;
        continue;
      }
      std::optional<size_t> entryCount = countParsedSDFEntries(*parsed);
      if (!entryCount ||
          *entryCount > maxStaticSDFCacheEntries - parsedSDFCacheEntries) {
        callDiagnostic(sourceManager, *call, "error",
                       "static SDF annotation parsed-entry resource limit "
                       "exceeded");
        invalid = true;
        continue;
      }
      parsedSDFCacheEntries += *entryCount;
      sdf = std::make_shared<const ParsedSDF>(std::move(*parsed));
      newCacheEntry->second.contents = sdf;
    }

    std::string scopePath = scope->getHierarchicalPath();
    for (const ParsedCell &cell : sdf->cells) {
      SmallVector<const slang::ast::InstanceSymbol *, 2> targets;
      if (cell.wildcard) {
        for (const auto &entry : inventory.instances) {
          if (annotationApplicationWork == maxAnnotationApplicationWork) {
            errs() << *filename << ':' << cell.line << ':' << cell.column
                   << ": error: static SDF annotation application-work "
                      "resource limit exceeded\n";
            return nullptr;
          }
          ++annotationApplicationWork;
          StringRef path = entry.getKey();
          bool inScope = path == scopePath ||
                         (path.starts_with(scopePath) &&
                          path.drop_front(scopePath.size()).starts_with("."));
          if (inScope && entry.getValue()->body.name == cell.cellType)
            targets.push_back(entry.getValue());
        }
      } else if (cell.instance.empty()) {
        if (scope->body.name == cell.cellType)
          targets.push_back(scope);
      } else {
        std::string relative = normalizeInstance(cell.instance, sdf->divider);
        const slang::ast::InstanceSymbol *target = nullptr;
        std::string nested = (Twine(scopePath) + "." + relative).str();
        auto found = inventory.instances.find(nested);
        if (found != inventory.instances.end())
          target = found->second;
        // IEEE 1800-2017 32.9: module_instance is the annotation root.
        // Accept an already-rooted spelling only when it is still contained
        // by that hierarchy; never fall back to a global sibling instance.
        bool rootedInScope =
            relative == scopePath ||
            (StringRef(relative).starts_with(scopePath) &&
             StringRef(relative).drop_front(scopePath.size()).starts_with("."));
        if (!target && rootedInScope) {
          auto rooted = inventory.instances.find(relative);
          if (rooted != inventory.instances.end())
            target = rooted->second;
        }
        if (target && target->body.name == cell.cellType)
          targets.push_back(target);
      }

      if (targets.empty()) {
        // IEEE 1800-2017 32.3 requires a warning for timing data that cannot
        // be annotated, while unrelated SDF constructs are ignored silently.
        errs() << *filename << ':' << cell.line << ':' << cell.column
               << ": warning: SDF CELL did not match an elaborated instance\n";
        continue;
      }

      for (const ParsedCell::UnsupportedTimingData &unsupported :
           cell.unsupportedTimingData)
        errs() << *filename << ':' << unsupported.line << ':'
               << unsupported.column
               << ": warning: unsupported SDF timing data in matching CELL: "
               << unsupported.description << '\n';

      for (const slang::ast::InstanceSymbol *target : targets) {
        auto [cached, inserted] = pathCache.try_emplace(target);
        if (inserted) {
          TimingPathInventory paths;
          target->body.visit(paths);
          cached->second = std::move(paths.paths);
        }
        std::optional<slang::TimeScale> timeScale = target->body.getTimeScale();
        uint64_t precisionFs =
            timeScale ? timeScaleValueFemtoseconds(timeScale->precision) : 0;
        for (const ParsedIOPath &annotation : cell.paths) {
          bool matched = false;
          for (const slang::ast::TimingPathSymbol *path : cached->second) {
            if (annotationApplicationWork == maxAnnotationApplicationWork) {
              // Clause 32.4 matching is compile-time work. Bound failed and
              // successful candidates together with earlier call-order and
              // wildcard probes so no hostile cross product can evade the
              // design-global application-work budget.
              errs() << *filename << ':' << annotation.line << ':'
                     << annotation.column
                     << ": error: static SDF annotation application-work "
                        "resource limit exceeded\n";
              return nullptr;
            }
            ++annotationApplicationWork;
            if (!pathMatches(*path, annotation))
              continue;
            matched = true;
            if (matchedDelayUpdates == maxMatchedDelayUpdates) {
              // This Clause 32 annotation-update limit is design-global.
              // Stop immediately so a hostile wildcard cannot amplify either
              // work or diagnostics after the permanent bound is reached.
              errs() << *filename << ':' << annotation.line << ':'
                     << annotation.column
                     << ": error: static SDF annotation-update resource "
                        "limit exceeded\n";
              return nullptr;
            }
            ++matchedDelayUpdates;
            SDFAnnotationDatabase::DelayAnnotation update;
            update.kind = annotation.kind;
            update.filename = annotationFilename;
            update.line = annotation.line;
            update.column = annotation.column;
            for (const ParsedIOPath::DelayValue &value : annotation.delays) {
              const std::optional<ExactDecimal> *selected = &value.typ;
              if (value.form == sdf::DelayValueForm::Triple) {
                if (*mtm == SDFMTMSelection::Minimum)
                  selected = &value.min;
                else if (*mtm == SDFMTMSelection::Maximum)
                  selected = &value.max;
              }
              if (!*selected) {
                // IEEE 1800-2017 32.3 leaves a preannotation timing value
                // unchanged when the SDF field is empty.
                update.delays.push_back(std::nullopt);
                continue;
              }
              std::optional<int64_t> rounded =
                  roundDelay(**selected, sdf->timeScaleFs, precisionFs);
              if (!rounded) {
                errs() << *filename << ':' << annotation.line << ':'
                       << annotation.column
                       << ": error: SDF delay is incompatible with target "
                          "precision\n";
                invalid = true;
                break;
              }
              update.delays.push_back(*rounded);
            }
            if (update.delays.size() != annotation.delays.size())
              continue;
            // IEEE 1800-2017 32.5/.6 make ABSOLUTE replacement and INCREMENT
            // addition observable in annotation order. Retain only this
            // bounded compile-time sequence; semantic import folds it into
            // the existing Clause 30 delay attribute.
            result->timingPathAnnotations[path].push_back(std::move(update));
          }
          if (!matched)
            errs() << *filename << ':' << annotation.line << ':'
                   << annotation.column
                   << ": warning: SDF IOPATH did not match a specify path\n";
        }
      }
    }
    result->appliedCalls.insert(call);
  }

  if (invalid)
    return nullptr;
  return result;
}

} // namespace obelisk::frontend
