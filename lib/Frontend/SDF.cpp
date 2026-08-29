//===- SDF.cpp - Static Standard Delay Format annotation -----------------===//

#include "SDF.h"

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
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
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

static void sdfError(StringRef filename, const SDFNode &node,
                     const Twine &message) {
  errs() << filename << ':' << node.line << ':' << node.column
         << ": error: " << message << '\n';
}

struct ExactDecimal {
  uint64_t numerator = 0;
  uint64_t denominator = 1;
};

static std::optional<ExactDecimal> parseDecimal(StringRef spelling) {
  spelling = spelling.trim();
  if (spelling.empty() || spelling.front() == '-')
    return std::nullopt;
  if (spelling.front() == '+')
    spelling = spelling.drop_front();
  StringRef mantissa = spelling;
  int exponent = 0;
  size_t exponentAt = spelling.find_first_of("eE");
  if (exponentAt != StringRef::npos) {
    mantissa = spelling.take_front(exponentAt);
    StringRef exponentText = spelling.drop_front(exponentAt + 1);
    if (exponentText.empty() || exponentText.getAsInteger(10, exponent))
      return std::nullopt;
  }
  size_t dot = mantissa.find('.');
  StringRef whole =
      dot == StringRef::npos ? mantissa : mantissa.take_front(dot);
  StringRef fraction =
      dot == StringRef::npos ? StringRef{} : mantissa.drop_front(dot + 1);
  if (whole.empty() && fraction.empty())
    return std::nullopt;
  if (whole.empty())
    whole = "0";
  if (whole.size() + fraction.size() > 18 || exponent < -18 || exponent > 18)
    return std::nullopt;
  uint64_t numerator = 0;
  for (char value : (whole + fraction).str()) {
    if (!std::isdigit(static_cast<unsigned char>(value)))
      return std::nullopt;
    numerator = numerator * 10 + static_cast<unsigned>(value - '0');
  }
  uint64_t denominator = 1;
  for (size_t index = 0; index != fraction.size(); ++index)
    denominator *= 10;
  if (exponent > 0) {
    for (int index = 0; index != exponent; ++index) {
      if (numerator > std::numeric_limits<uint64_t>::max() / 10)
        return std::nullopt;
      numerator *= 10;
    }
  } else {
    for (int index = 0; index != -exponent; ++index) {
      if (denominator > std::numeric_limits<uint64_t>::max() / 10)
        return std::nullopt;
      denominator *= 10;
    }
  }
  return ExactDecimal{numerator, denominator};
}

static std::optional<uint64_t> unitFemtoseconds(StringRef unit) {
  return StringSwitch<std::optional<uint64_t>>(unit.lower())
      .Case("s", UINT64_C(1'000'000'000'000'000))
      .Case("ms", UINT64_C(1'000'000'000'000))
      .Case("us", UINT64_C(1'000'000'000))
      .Case("ns", UINT64_C(1'000'000))
      .Case("ps", UINT64_C(1'000))
      .Case("fs", UINT64_C(1))
      .Default(std::nullopt);
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

static std::optional<uint64_t> timeScaleFemtoseconds(const SDFNode &form) {
  if ((form.children.size() != 2 && form.children.size() != 3) ||
      form.children[1].kind != SDFNode::Kind::Atom ||
      (form.children.size() == 3 &&
       form.children[2].kind != SDFNode::Kind::Atom))
    return std::nullopt;
  std::string joined;
  StringRef spelling = form.children[1].text;
  if (form.children.size() == 3) {
    joined = (Twine(spelling) + form.children[2].text).str();
    spelling = joined;
  }
  size_t split = 0;
  while (split < spelling.size() &&
         (std::isdigit(static_cast<unsigned char>(spelling[split])) ||
          spelling[split] == '.'))
    ++split;
  std::optional<ExactDecimal> amount = parseDecimal(spelling.take_front(split));
  std::optional<uint64_t> unit = unitFemtoseconds(spelling.drop_front(split));
  if (!amount || !unit)
    return std::nullopt;
  unsigned __int128 scaled = static_cast<unsigned __int128>(amount->numerator) *
                             static_cast<unsigned __int128>(*unit);
  if (scaled % amount->denominator != 0)
    return std::nullopt;
  scaled /= amount->denominator;
  if (scaled == 0 || scaled > std::numeric_limits<uint64_t>::max())
    return std::nullopt;
  return static_cast<uint64_t>(scaled);
}

static std::optional<int64_t> roundDelay(const ExactDecimal &delay,
                                         uint64_t sdfUnitFs,
                                         uint64_t targetPrecisionFs) {
  if (targetPrecisionFs == 0)
    return std::nullopt;
  unsigned __int128 numerator =
      static_cast<unsigned __int128>(delay.numerator) * sdfUnitFs;
  unsigned __int128 quantum =
      static_cast<unsigned __int128>(delay.denominator) * targetPrecisionFs;
  // IEEE 1800-2017 3.14.1 rounds a time value to the destination scope's
  // precision before simulation. Keep this calculation rational so decimal
  // SDF text cannot pick up binary floating-point error at a half quantum.
  unsigned __int128 steps = (numerator + quantum / 2) / quantum;
  unsigned __int128 femtoseconds = steps * targetPrecisionFs;
  if (femtoseconds > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
    return std::nullopt;
  return static_cast<int64_t>(femtoseconds);
}

struct ParsedIOPath {
  struct Port {
    std::string name;
    std::optional<int32_t> index;
  };

  Port input;
  Port output;
  std::optional<slang::ast::EdgeKind> edge;
  SmallVector<std::optional<ExactDecimal>, 12> delays;
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

static std::optional<ParsedIOPath::Port> parsePort(StringRef filename,
                                                   const SDFNode &node) {
  if (node.kind != SDFNode::Kind::Atom) {
    sdfError(filename, node, "unsupported IOPATH port identifier");
    return std::nullopt;
  }
  StringRef spelling = node.text;
  ParsedIOPath::Port result;
  size_t open = spelling.rfind('[');
  if (open == StringRef::npos) {
    if (spelling.contains(']')) {
      sdfError(filename, node, "malformed indexed IOPATH port");
      return std::nullopt;
    }
    result.name = spelling.str();
    return result;
  }
  if (!spelling.ends_with("]") || open == 0) {
    sdfError(filename, node, "malformed indexed IOPATH port");
    return std::nullopt;
  }
  int32_t index = 0;
  StringRef indexText = spelling.slice(open + 1, spelling.size() - 1);
  if (indexText.empty() || indexText.getAsInteger(10, index)) {
    sdfError(filename, node,
             "IOPATH port selects require a constant integer index");
    return std::nullopt;
  }
  result.name = spelling.take_front(open).str();
  result.index = index;
  return result;
}

static std::optional<
    std::pair<ParsedIOPath::Port, std::optional<slang::ast::EdgeKind>>>
parsePortSpec(StringRef filename, const SDFNode &node) {
  if (node.kind == SDFNode::Kind::Atom) {
    auto port = parsePort(filename, node);
    if (!port)
      return std::nullopt;
    return std::make_pair(std::move(*port),
                          std::optional<slang::ast::EdgeKind>{});
  }
  if (node.kind != SDFNode::Kind::List || node.children.size() != 2 ||
      node.children[1].kind != SDFNode::Kind::Atom) {
    sdfError(filename, node, "unsupported IOPATH port specification");
    return std::nullopt;
  }
  slang::ast::EdgeKind edge;
  if (keyword(node.children[0], "POSEDGE"))
    edge = slang::ast::EdgeKind::PosEdge;
  else if (keyword(node.children[0], "NEGEDGE"))
    edge = slang::ast::EdgeKind::NegEdge;
  else {
    sdfError(filename, node, "unsupported IOPATH edge identifier");
    return std::nullopt;
  }
  auto port = parsePort(filename, node.children[1]);
  if (!port)
    return std::nullopt;
  return std::make_pair(std::move(*port),
                        std::optional<slang::ast::EdgeKind>(edge));
}

struct ParsedDelayValue {
  std::optional<ExactDecimal> value;
};

static std::optional<ParsedDelayValue> parseDelayValue(StringRef filename,
                                                       const SDFNode &node) {
  if (node.kind != SDFNode::Kind::List || node.children.size() > 1 ||
      (node.children.size() == 1 &&
       node.children[0].kind != SDFNode::Kind::Atom)) {
    sdfError(filename, node,
             "this SDF tranche requires one scalar per delay value");
    return std::nullopt;
  }
  if (node.children.empty())
    return ParsedDelayValue{};
  std::optional<ExactDecimal> value = parseDecimal(node.children[0].text);
  if (!value) {
    sdfError(filename, node, "invalid nonnegative SDF delay value");
    return std::nullopt;
  }
  return ParsedDelayValue{*value};
}

static std::optional<ParsedSDF> parseSDFFile(StringRef filename,
                                             StringRef contents) {
  std::optional<SDFNode> root = SDFParser(filename, contents).parse();
  if (!root)
    return std::nullopt;
  if (!formHead(*root, "DELAYFILE")) {
    sdfError(filename, *root, "SDF file must contain one DELAYFILE form");
    return std::nullopt;
  }
  ParsedSDF result;
  bool valid = true;
  for (const SDFNode &entry : ArrayRef(root->children).drop_front()) {
    if (const SDFNode *form = formHead(entry, "DIVIDER")) {
      if (form->children.size() != 2 ||
          form->children[1].kind != SDFNode::Kind::Atom ||
          form->children[1].text.size() != 1) {
        sdfError(filename, entry, "malformed DIVIDER header");
        valid = false;
      } else {
        result.divider = form->children[1].text.front();
      }
      continue;
    }
    if (const SDFNode *form = formHead(entry, "TIMESCALE")) {
      std::optional<uint64_t> scale = timeScaleFemtoseconds(*form);
      if (!scale) {
        sdfError(filename, entry, "invalid exact TIMESCALE header");
        valid = false;
      } else {
        result.timeScaleFs = *scale;
      }
      continue;
    }
    const SDFNode *cellForm = formHead(entry, "CELL");
    if (!cellForm)
      continue;
    ParsedCell cell;
    cell.line = entry.line;
    cell.column = entry.column;
    bool sawCellType = false;
    bool sawInstance = false;
    for (const SDFNode &member : ArrayRef(cellForm->children).drop_front()) {
      if (const SDFNode *form = formHead(member, "CELLTYPE")) {
        if (form->children.size() != 2 ||
            form->children[1].kind != SDFNode::Kind::String) {
          sdfError(filename, member, "malformed CELLTYPE");
          valid = false;
        } else {
          cell.cellType = form->children[1].text;
          sawCellType = true;
        }
        continue;
      }
      if (const SDFNode *form = formHead(member, "INSTANCE")) {
        if (form->children.size() == 1) {
          cell.instance.clear();
        } else if (form->children.size() == 2 &&
                   form->children[1].kind == SDFNode::Kind::Atom) {
          cell.instance = form->children[1].text;
          cell.wildcard = cell.instance == "*";
        } else {
          sdfError(filename, member, "malformed INSTANCE");
          valid = false;
        }
        sawInstance = true;
        continue;
      }
      const SDFNode *delay = formHead(member, "DELAY");
      if (!delay) {
        if (formHead(member, "TIMINGCHECK") || formHead(member, "LABEL"))
          cell.unsupportedTimingData.push_back(
              {member.children.front().text, member.line, member.column});
        continue;
      }
      for (const SDFNode &mode : ArrayRef(delay->children).drop_front()) {
        const SDFNode *absolute = formHead(mode, "ABSOLUTE");
        if (!absolute) {
          cell.unsupportedTimingData.push_back(
              {"non-ABSOLUTE DELAY", mode.line, mode.column});
          continue;
        }
        for (const SDFNode &item : ArrayRef(absolute->children).drop_front()) {
          const SDFNode *iopath = formHead(item, "IOPATH");
          if (!iopath) {
            cell.unsupportedTimingData.push_back(
                {"non-IOPATH ABSOLUTE delay", item.line, item.column});
            continue;
          }
          if (iopath->children.size() < 4) {
            sdfError(filename, item, "IOPATH requires endpoints and delays");
            valid = false;
            continue;
          }
          auto input = parsePortSpec(filename, iopath->children[1]);
          auto output = parsePort(filename, iopath->children[2]);
          if (!input || !output) {
            valid = false;
            continue;
          }
          ParsedIOPath path;
          path.input = std::move(input->first);
          path.edge = input->second;
          path.output = std::move(*output);
          path.line = item.line;
          path.column = item.column;
          for (const SDFNode &delayValue :
               ArrayRef(iopath->children).drop_front(3)) {
            std::optional<ParsedDelayValue> value =
                parseDelayValue(filename, delayValue);
            if (!value) {
              valid = false;
              break;
            }
            path.delays.push_back(value->value);
          }
          if (path.delays.size() != 1 && path.delays.size() != 2 &&
              path.delays.size() != 3 && path.delays.size() != 6 &&
              path.delays.size() != 12) {
            sdfError(filename, item,
                     "IOPATH requires 1, 2, 3, 6, or 12 delay values");
            valid = false;
            continue;
          }
          cell.paths.push_back(std::move(path));
        }
      }
    }
    if (!sawCellType || !sawInstance) {
      sdfError(filename, entry, "CELL requires CELLTYPE and INSTANCE");
      valid = false;
    }
    result.cells.push_back(std::move(cell));
  }
  if (!result.timeScaleFs) {
    errs() << filename << ": error: static SDF annotation requires TIMESCALE\n";
    valid = false;
  }
  return valid ? std::optional<ParsedSDF>(std::move(result)) : std::nullopt;
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
  DenseMap<const slang::ast::CallExpression *,
           const slang::ast::ProceduralBlockSymbol *>
      startupBlock;
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
        startupBlock[call] = &proceduralBlock;
        return true;
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
        call.getSubroutineName() == "$sdf_annotate")
      calls.push_back({&call, currentInstance});
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

} // namespace

const SDFAnnotationDatabase::DelayVector *
SDFAnnotationDatabase::getTimingPathDelays(
    const slang::ast::TimingPathSymbol &path) const {
  auto found = timingPathDelays.find(&path);
  return found == timingPathDelays.end() ? nullptr : &found->second;
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
  DenseSet<const slang::ast::CallExpression *> unorderedCalls;
  auto contains = [](StringRef outer, StringRef inner) {
    return inner == outer ||
           (inner.starts_with(outer) &&
            inner.drop_front(outer.size()).starts_with("."));
  };
  for (auto [leftIndex, left] : llvm::enumerate(orderedCandidates)) {
    for (const OrderedCandidate &right :
         ArrayRef(orderedCandidates).drop_front(leftIndex + 1)) {
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
  bool invalid = false;

  for (const DesignInventory::AnnotationCall &annotationCall :
       inventory.calls) {
    const slang::ast::CallExpression *call = annotationCall.call;
    // IEEE 1800-2017 32.9 defines `$sdf_annotate` as an executing system
    // task. Baking it into the elaborated timing metadata is equivalent only
    // when execution is provably once, at startup, and in source order.
    if (!inventory.startupCalls.contains(call) || unorderedCalls.contains(call)) {
      callDiagnostic(sourceManager, *call, "error",
                     unorderedCalls.contains(call)
                         ? "static $sdf_annotate cannot order calls across "
                           "multiple initial blocks with overlapping scopes"
                         : "static $sdf_annotate must be an unconditional, "
                           "undelayed statement in one initial block");
      invalid = true;
      continue;
    }
    if (call->arguments().size() > 2) {
      callDiagnostic(sourceManager, *call, "error",
                     "this static SDF tranche supports only filename and "
                     "optional module scope arguments");
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
    ErrorOr<std::unique_ptr<MemoryBuffer>> buffer =
        MemoryBuffer::getFile(*filename, /*IsText=*/true);
    if (!buffer) {
      callDiagnostic(sourceManager, *call, "error",
                     Twine("could not read SDF file '") + *filename +
                         "': " + buffer.getError().message());
      invalid = true;
      continue;
    }
    std::optional<ParsedSDF> sdf =
        parseSDFFile(*filename, (*buffer)->getBuffer());
    if (!sdf) {
      invalid = true;
      continue;
    }

    std::string scopePath = scope->getHierarchicalPath();
    for (const ParsedCell &cell : sdf->cells) {
      SmallVector<const slang::ast::InstanceSymbol *, 2> targets;
      if (cell.wildcard) {
        for (const auto &entry : inventory.instances) {
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
            if (!pathMatches(*path, annotation))
              continue;
            SDFAnnotationDatabase::DelayVector delays;
            for (const std::optional<ExactDecimal> &value : annotation.delays) {
              if (!value) {
                // IEEE 1800-2017 32.3 leaves a preannotation timing value
                // unchanged when the SDF field is empty.
                delays.push_back(std::nullopt);
                continue;
              }
              std::optional<int64_t> rounded =
                  roundDelay(*value, sdf->timeScaleFs, precisionFs);
              if (!rounded) {
                errs() << *filename << ':' << annotation.line << ':'
                       << annotation.column
                       << ": error: SDF delay is incompatible with target "
                          "precision\n";
                invalid = true;
                break;
              }
              delays.push_back(*rounded);
            }
            if (delays.size() != annotation.delays.size())
              continue;
            // IEEE 1800-2017 32.4 replaces the matched SystemVerilog timing
            // values. The frontend therefore emits exactly the same frozen
            // Clause 30 attribute as an equivalent source path, leaving no
            // SDF table or lookup in simulation MLIR.
            result->timingPathDelays[path] = std::move(delays);
            matched = true;
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
