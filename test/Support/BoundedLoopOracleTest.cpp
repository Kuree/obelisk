//===- BoundedLoopOracleTest.cpp - Exhaustive termination-proof oracle ----===//
//
// Check every termination proof of analyzeBoundedLoops against exhaustive
// execution of small bit-vector recurrences. The oracle follows the actual
// finite state machine; it deliberately uses neither a closed-form trip count
// nor the analysis's arithmetic proof.
//
//===----------------------------------------------------------------------===//

#include "talos/Analysis/BoundedLoopAnalysis.h"
#include "talos/Dialect/Schedule/ScheduleDialect.h"
#include "talos/Dialect/Simulation/SimulationDialect.h"
#include "talos/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"

#include "llvm/Support/raw_ostream.h"

#include "gtest/gtest.h"

#include <set>
#include <string>
#include <vector>

using namespace mlir;

namespace {

constexpr const char *predicates[] = {"eq",  "ne",  "slt", "sle", "sgt",
                                      "sge", "ult", "ule", "ugt", "uge"};

bool continues(StringRef predicate, unsigned value, unsigned bound,
               unsigned modulus) {
  int lhs = value, rhs = bound;
  if (predicate.starts_with("s")) {
    lhs = value < modulus / 2 ? int(value) : int(value) - int(modulus);
    rhs = bound < modulus / 2 ? int(bound) : int(bound) - int(modulus);
  }
  StringRef comparison =
      predicate == "eq" || predicate == "ne" ? predicate : predicate.drop_front();
  if (comparison == "eq")
    return lhs == rhs;
  if (comparison == "ne")
    return lhs != rhs;
  if (comparison == "lt")
    return lhs < rhs;
  if (comparison == "le")
    return lhs <= rhs;
  if (comparison == "gt")
    return lhs > rhs;
  return lhs >= rhs;
}

bool terminates(StringRef predicate, unsigned start, unsigned bound,
                unsigned stride, bool subtract, unsigned modulus) {
  std::set<unsigned> seen;
  unsigned value = start;
  while (continues(predicate, value, bound, modulus)) {
    if (!seen.insert(value).second)
      return false;
    value = (subtract ? value + modulus - stride : value + stride) % modulus;
  }
  return true;
}

/// Parse `simulation.design @proof` holding one function per case, then
/// return the number of loops the analysis proves bounded in each case.
std::vector<size_t> provenLoops(StringRef functions, size_t count,
                                StringRef aliases = "") {
  MLIRContext context;
  context.loadDialect<arith::ArithDialect, cf::ControlFlowDialect,
                      talos::schedule::ScheduleDialect,
                      talos::sim::TalosSimulationDialect>();
  std::string source;
  llvm::raw_string_ostream os(source);
  os << aliases << "module { simulation.design @proof {\n"
     << "simulation.scope.decl 0\n";
  for (size_t index = 0; index < count; ++index)
    os << "simulation.code_unit.decl " << index + 1
       << " in 0 initial hierarchy \"f" << index << "\"\n";
  os << functions << "} }\n";
  OwningOpRef<ModuleOp> module = parseSourceString<ModuleOp>(source, &context);
  EXPECT_TRUE(module);
  std::vector<size_t> loops(count, ~size_t(0));
  if (!module)
    return loops;
  module->walk([&](talos::sim::SimFuncOp function) {
    size_t index = std::stoul(function.getSymName().drop_front().str());
    loops[index] = talos::analysis::analyzeBoundedLoops(function.getBody())
                       .size();
  });
  return loops;
}

std::string header(size_t index) {
  return "simulation.func @f" + std::to_string(index) +
         "(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})\n"
         "    attributes {entry_kind = 1 : i32, code_unit_id = " +
         std::to_string(index + 1) + " : i64} {\n";
}

struct Interval {
  unsigned low, high;
};

std::vector<Interval> intervals(unsigned modulus) {
  std::vector<Interval> result;
  for (unsigned low = 0; low < modulus; ++low)
    for (unsigned high = low; high < modulus; ++high)
      result.push_back({low, high});
  return result;
}

TEST(BoundedLoopOracle, ConstantInitializer) {
  constexpr unsigned width = 3, modulus = 1u << width;
  struct Case {
    StringRef predicate;
    unsigned start, bound, stride;
    bool subtract;
  };
  std::vector<Case> cases;
  for (StringRef predicate : predicates)
    for (unsigned start = 0; start < modulus; ++start)
      for (unsigned bound = 0; bound < modulus; ++bound)
        for (unsigned stride = 0; stride < modulus; ++stride)
          for (bool subtract : {false, true})
            cases.push_back({predicate, start, bound, stride, subtract});
  std::string functions;
  llvm::raw_string_ostream os(functions);
  std::string type = "i" + std::to_string(width);
  for (auto [index, c] : llvm::enumerate(cases))
    os << header(index) << "  %start = arith.constant " << c.start << " : "
       << type << "\n  cf.br ^head(%start : " << type << ")\n^head(%i: "
       << type << "):\n  %bound = arith.constant " << c.bound << " : "
       << type << "\n  %test = arith.cmpi " << c.predicate
       << ", %i, %bound : " << type
       << "\n  cf.cond_br %test, ^body, ^exit\n^body:\n  %step = "
          "arith.constant "
       << c.stride << " : " << type << "\n  %next = arith."
       << (c.subtract ? "subi" : "addi") << " %i, %step : " << type
       << "\n  cf.br ^head(%next : " << type
       << ")\n^exit:\n  simulation.return\n}\n";
  std::vector<size_t> loops = provenLoops(functions, cases.size());
  size_t marked = 0;
  for (auto [index, c] : llvm::enumerate(cases)) {
    SCOPED_TRACE(testing::Message()
                 << c.predicate.str() << " start=" << c.start
                 << " bound=" << c.bound << " stride=" << c.stride
                 << " subtract=" << c.subtract);
    ASSERT_LE(loops[index], 1u);
    bool proven = loops[index];
    bool finite =
        terminates(c.predicate, c.start, c.bound, c.stride, c.subtract, modulus);
    EXPECT_TRUE(!proven || finite) << "nonterminating loop proven bounded";
    // Equality reachability and zero-trip recognition should be complete.
    if (c.predicate == "eq" || c.predicate == "ne" ||
        !continues(c.predicate, c.start, c.bound, modulus))
      EXPECT_EQ(proven, finite) << "missed exact termination proof";
    marked += proven;
  }
  EXPECT_GT(marked, cases.size() / 2);
}

// The initializer varies across an enclosing induction. Exhaust every
// nonempty interval of two-bit initializers, predicate, bound and modular
// step, and check each admitted nested loop against execution from every
// initializer, including interior residue classes and zero-trip entries.
TEST(BoundedLoopOracle, InitializerIntervals) {
  constexpr unsigned width = 2, modulus = 1u << width;
  struct Case {
    StringRef predicate;
    Interval range;
    unsigned bound, stride;
    bool subtract;
  };
  std::vector<Case> cases;
  for (StringRef predicate : predicates)
    for (Interval range : intervals(modulus))
      for (unsigned bound = 0; bound < modulus; ++bound)
        for (unsigned stride = 0; stride < modulus; ++stride)
          for (bool subtract : {false, true})
            cases.push_back({predicate, range, bound, stride, subtract});
  std::string functions;
  llvm::raw_string_ostream os(functions);
  for (auto [index, c] : llvm::enumerate(cases))
    os << header(index) << "  %low = arith.constant " << c.range.low
       << " : i4\n  %end = arith.constant " << c.range.high + 1
       << " : i4\n  %one = arith.constant 1 : i4\n"
          "  cf.br ^outer(%low : i4)\n^outer(%i: i4):\n"
          "  %more = arith.cmpi ult, %i, %end : i4\n"
          "  cf.cond_br %more, ^enter, ^exit\n^enter:\n"
          "  %start = arith.trunci %i : i4 to i2\n"
          "  cf.br ^head(%start : i2)\n^head(%k: i2):\n"
          "  %bound = arith.constant "
       << c.bound << " : i2\n  %test = arith.cmpi " << c.predicate
       << ", %k, %bound : i2\n  cf.cond_br %test, ^body, ^advance\n^body:\n"
          "  %step = arith.constant "
       << c.stride << " : i2\n  %next = arith."
       << (c.subtract ? "subi" : "addi")
       << " %k, %step : i2\n  cf.br ^head(%next : i2)\n^advance:\n"
          "  %inext = arith.addi %i, %one : i4\n"
          "  cf.br ^outer(%inext : i4)\n^exit:\n  simulation.return\n}\n";
  std::vector<size_t> loops = provenLoops(functions, cases.size());
  size_t marked = 0;
  for (auto [index, c] : llvm::enumerate(cases)) {
    SCOPED_TRACE(testing::Message()
                 << c.predicate.str() << " start=[" << c.range.low << ','
                 << c.range.high << "] bound=" << c.bound
                 << " stride=" << c.stride << " subtract=" << c.subtract);
    ASSERT_TRUE(loops[index] == 0 || loops[index] == 2) << loops[index];
    if (!loops[index])
      continue;
    for (unsigned start = c.range.low; start <= c.range.high; ++start)
      EXPECT_TRUE(terminates(c.predicate, start, c.bound, c.stride,
                             c.subtract, modulus))
          << "start " << start << " does not terminate";
    ++marked;
  }
  EXPECT_GT(marked, cases.size() / 4);
}

// Independently vary the bound instead. In particular, an extreme bound may be
// reached without wrap while another stride residue or signed bound cannot.
// Also check correlated initializers and bounds with wrapping invariant
// arithmetic. IEEE 1800-2023 11.6.1 sizes the arithmetic, 12.7.1 tests before
// each iteration, and 12.8 permits early exits but gives no assumption that an
// arbitrary recurrence finishes.
void checkBoundIntervals(bool correlated) {
  constexpr unsigned width = 2, modulus = 1u << width;
  struct Case {
    StringRef predicate;
    Interval range;
    unsigned parameter, stride;
    bool subtract;
  };
  std::vector<unsigned> parameters =
      correlated ? std::vector<unsigned>{0, 1, 3}
                 : std::vector<unsigned>{0, 1, 2, 3};
  std::vector<Case> cases;
  for (StringRef predicate : predicates)
    for (Interval range : intervals(modulus))
      for (unsigned parameter : parameters)
        for (unsigned stride = 0; stride < modulus; ++stride)
          for (bool subtract : {false, true})
            cases.push_back({predicate, range, parameter, stride, subtract});
  std::string functions;
  llvm::raw_string_ostream os(functions);
  StringRef initial = correlated ? "%outer_value" : "%parameter";
  StringRef bound = correlated ? "%sum" : "%outer_value";
  for (auto [index, c] : llvm::enumerate(cases))
    os << header(index) << "  %low = arith.constant " << c.range.low
       << " : i4\n  %end = arith.constant " << c.range.high + 1
       << " : i4\n  %one = arith.constant 1 : i4\n"
          "  %parameter = arith.constant "
       << c.parameter
       << " : i2\n  cf.br ^outer(%low : i4)\n^outer(%i: i4):\n"
          "  %more = arith.cmpi ult, %i, %end : i4\n"
          "  cf.cond_br %more, ^enter, ^exit\n^enter:\n"
          "  %outer_value = arith.trunci %i : i4 to i2\n"
          "  %sum = arith.addi %outer_value, %parameter : i2\n"
          "  cf.br ^head("
       << initial << " : i2)\n^head(%k: i2):\n  %test = arith.cmpi "
       << c.predicate << ", %k, " << bound
       << " : i2\n  cf.cond_br %test, ^body, ^advance\n^body:\n"
          "  %step = arith.constant "
       << c.stride << " : i2\n  %next = arith."
       << (c.subtract ? "subi" : "addi")
       << " %k, %step : i2\n  cf.br ^head(%next : i2)\n^advance:\n"
          "  %inext = arith.addi %i, %one : i4\n"
          "  cf.br ^outer(%inext : i4)\n^exit:\n  simulation.return\n}\n";
  std::vector<size_t> loops = provenLoops(functions, cases.size());
  size_t marked = 0;
  for (auto [index, c] : llvm::enumerate(cases)) {
    SCOPED_TRACE(testing::Message()
                 << c.predicate.str() << " outer=[" << c.range.low << ','
                 << c.range.high << "] parameter=" << c.parameter
                 << " stride=" << c.stride << " subtract=" << c.subtract);
    ASSERT_TRUE(loops[index] == 0 || loops[index] == 2) << loops[index];
    if (!loops[index])
      continue;
    for (unsigned outer = c.range.low; outer <= c.range.high; ++outer) {
      unsigned start = correlated ? outer : c.parameter;
      unsigned limit = correlated ? (outer + c.parameter) % modulus : outer;
      EXPECT_TRUE(terminates(c.predicate, start, limit, c.stride, c.subtract,
                             modulus))
          << "outer value " << outer << " does not terminate";
    }
    ++marked;
  }
  EXPECT_GT(marked, cases.size() / 10);
}

TEST(BoundedLoopOracle, BoundIntervals) { checkBoundIntervals(false); }

TEST(BoundedLoopOracle, CorrelatedIntervals) { checkBoundIntervals(true); }

// Packed four-state counters initialized with known planes. Arithmetic may be
// widened, but assignment truncates back to the two-bit recurrence. Compare
// every admitted proof to that narrow finite state machine, not a wider one.
TEST(BoundedLoopOracle, PackedRecurrences) {
  constexpr unsigned width = 2, modulus = 1u << width;
  struct Case {
    StringRef predicate;
    unsigned start, bound, stride;
    bool subtract;
    unsigned wide;
  };
  std::vector<Case> cases;
  for (StringRef predicate : {"eq", "ne", "ult", "ule", "ugt", "uge"})
    for (unsigned start = 0; start < modulus; ++start)
      for (unsigned bound = 0; bound < modulus; ++bound)
        for (unsigned stride = 0; stride < modulus; ++stride)
          for (bool subtract : {false, true})
            for (unsigned wide : {width, width + 2})
              cases.push_back({predicate, start, bound, stride, subtract, wide});
  std::string functions;
  llvm::raw_string_ostream os(functions);
  for (auto [index, c] : llvm::enumerate(cases)) {
    std::string narrow = "!simulation.logic<2>";
    std::string logic = "!simulation.logic<" + std::to_string(c.wide) + ">";
    std::string bits = "i" + std::to_string(c.wide);
    os << header(index) << "  %bits = simulation.logic.constant " << c.start
       << " : i2, 0 : i2 : " << narrow
       << "\n  %start = simulation.packed.unflatten %bits : (" << narrow
       << ") -> !counter\n  cf.br ^head(%start : !counter)\n"
          "^head(%i: !counter):\n"
          "  %narrow = simulation.packed.flatten %i : (!counter) -> "
       << narrow << "\n  %value = simulation.logic.resize %narrow signed = "
       << "false : " << narrow << " -> " << logic
       << "\n  %bound = simulation.logic.constant " << c.bound << " : "
       << bits << ", 0 : " << bits << " : " << logic
       << "\n  %compare = simulation.logic.compare " << c.predicate
       << " %value, %bound : (" << logic << ", " << logic
       << ") -> !simulation.logic<1>\n"
          "  %test = simulation.logic.is_true %compare : !simulation.logic<1>\n"
          "  cf.cond_br %test, ^body, ^exit\n^body:\n"
          "  %step = simulation.logic.constant "
       << c.stride << " : " << bits << ", 0 : " << bits << " : " << logic
       << "\n  %sum = simulation.logic.binary "
       << (c.subtract ? "sub" : "add") << " %value, %step : " << logic
       << "\n  %nextbits = simulation.logic.resize %sum signed = false : "
       << logic << " -> " << narrow
       << "\n  %next = simulation.packed.unflatten %nextbits : (" << narrow
       << ") -> !counter\n  cf.br ^head(%next : !counter)\n^exit:\n"
          "  simulation.return\n}\n";
  }
  std::vector<size_t> loops = provenLoops(
      functions, cases.size(),
      "!counter = !simulation.packed_array<1 : 0 x !simulation.logic<1>>\n");
  size_t marked = 0;
  for (auto [index, c] : llvm::enumerate(cases)) {
    SCOPED_TRACE(testing::Message()
                 << c.predicate.str() << " start=" << c.start
                 << " bound=" << c.bound << " stride=" << c.stride
                 << " subtract=" << c.subtract << " wide=" << c.wide);
    ASSERT_LE(loops[index], 1u);
    bool proven = loops[index];
    bool finite =
        terminates(c.predicate, c.start, c.bound, c.stride, c.subtract, modulus);
    EXPECT_TRUE(!proven || finite) << "packed recurrence wraps";
    if (c.predicate == "eq" || c.predicate == "ne" ||
        !continues(c.predicate, c.start, c.bound, modulus))
      EXPECT_EQ(proven, finite) << "missed exact packed proof";
    marked += proven;
  }
  EXPECT_GT(marked, cases.size() / 2);
}

} // namespace
