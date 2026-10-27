//===- ActivationOracleTest.cpp - Exhaustive activation-proof oracle ------===//
//
// Check accepted idempotence proofs and activation input dependencies by
// exhaustive finite-state execution of every two-bit state. IEEE 1800-2023 4.6
// and 9.2.2.2.1 require preserving source-order snapshots and retained state;
// always_comb is not a license to evaluate a state transition twice. The
// oracle executes each program directly, independently of the analyses.
//
//===----------------------------------------------------------------------===//

#include "talos/Analysis/ActivationIdempotenceAnalysis.h"
#include "talos/Analysis/ActivationRegionAnalysis.h"
#include "talos/Analysis/HandleDataflowAnalysis.h"
#include "talos/Dialect/Schedule/ScheduleDialect.h"
#include "talos/Dialect/Simulation/SimulationDialect.h"
#include "talos/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"

#include "llvm/Support/raw_ostream.h"

#include "gtest/gtest.h"

#include <array>
#include <set>
#include <string>
#include <vector>

using namespace mlir;
using talos::analysis::ActivationMemoryAnalysis;
using talos::analysis::ActivationRegionAnalysis;

namespace {

enum class Action {
  Hold,
  Replace,
  Counter,
  Swap,
  Snapshot,
  Copy,
  Sequential,
  HoldDerived,
  OverwrittenSnapshot,
  PartialHold,
};
constexpr Action actions[] = {
    Action::Hold,     Action::Replace,     Action::Counter,
    Action::Swap,     Action::Snapshot,    Action::Copy,
    Action::Sequential, Action::HoldDerived, Action::OverwrittenSnapshot,
    Action::PartialHold};
constexpr const char *controls[] = {"input", "old_a", "old_b", "xor_ab"};

struct Case {
  StringRef control;
  Action yes, no;
  bool useSwitch;
};

std::vector<Case> allCases() {
  std::vector<Case> cases;
  for (StringRef control : controls)
    for (Action yes : actions)
      for (Action no : actions)
        for (bool useSwitch : {false, true})
          cases.push_back({control, yes, no, useSwitch});
  return cases;
}

/// Root 0 is the input; roots 1 and 2 are the retained state `a` and `b`.
using State = std::array<unsigned, 3>;
/// One source-order publication: root and stored value.
using Publication = std::pair<unsigned, unsigned>;

void emitAction(raw_ostream &os, Action action, StringRef prefix) {
  auto store = [&](StringRef value, StringRef target) {
    os << "  simulation.ref.store %" << value << " to %" << target
       << " : i2, !simulation.ref<i2>\n";
  };
  switch (action) {
  case Action::Hold:
    store("old_a", "a");
    store("old_b", "b");
    break;
  case Action::Replace:
    store("input", "a");
    store("input_xor", "b");
    break;
  case Action::Counter:
    store("increment", "a");
    store("old_b", "b");
    break;
  case Action::Swap:
    store("old_b", "a");
    store("old_a", "b");
    break;
  case Action::Snapshot:
    store("input", "a");
    store("old_a", "b");
    break;
  case Action::Copy:
    store("old_b", "a");
    store("old_b", "b");
    break;
  case Action::Sequential:
    store("input", "a");
    os << "  %" << prefix
       << "_fresh = simulation.ref.load %a : !simulation.ref<i2> -> i2\n";
    store((prefix + "_fresh").str(), "b");
    break;
  case Action::HoldDerived:
    store("old_a", "a");
    store("derived", "b");
    break;
  case Action::OverwrittenSnapshot:
    store("old_a", "b");
    store("input", "a");
    store("input", "b");
    break;
  case Action::PartialHold:
    store("input", "a");
    break;
  }
  os << "  cf.br ^exit\n";
}

Action selected(const Case &c, const State &state) {
  auto [input, a, b] = state;
  unsigned condition = c.control == "input"   ? input
                       : c.control == "old_a" ? a
                       : c.control == "old_b" ? b
                                              : a ^ b;
  return condition == 0 ? c.yes : c.no;
}

/// Source-order writes, including overwritten publications. The final-state
/// oracle below deliberately has a different observation.
std::vector<Publication> publications(const Case &c, const State &state) {
  auto [input, a, b] = state;
  switch (selected(c, state)) {
  case Action::Hold:
    return {{1, a}, {2, b}};
  case Action::Replace:
    return {{1, input}, {2, input ^ 1}};
  case Action::Counter:
    return {{1, (a + 1) % 4}, {2, b}};
  case Action::Swap:
    return {{1, b}, {2, a}};
  case Action::Snapshot:
    return {{1, input}, {2, a}};
  case Action::Copy:
    return {{1, b}, {2, b}};
  case Action::Sequential:
    return {{1, input}, {2, input}};
  case Action::HoldDerived:
    return {{1, a}, {2, a ^ input}};
  case Action::OverwrittenSnapshot:
    return {{2, a}, {1, input}, {2, input}};
  case Action::PartialHold:
    return {{1, input}};
  }
  llvm_unreachable("unknown action");
}

/// The retained state `a` and `b` after one activation.
std::pair<unsigned, unsigned> execute(const Case &c, const State &state) {
  auto [input, a, b] = state;
  switch (selected(c, state)) {
  case Action::Hold:
    return {a, b};
  case Action::Replace:
    return {input, input ^ 1};
  case Action::Counter:
    return {(a + 1) % 4, b};
  case Action::Swap:
    return {b, a};
  case Action::Snapshot:
    return {input, a};
  case Action::Copy:
    return {b, b};
  case Action::Sequential:
    return {input, input};
  case Action::HoldDerived:
    return {a, a ^ input};
  case Action::OverwrittenSnapshot:
    return {input, input};
  case Action::PartialHold:
    return {input, b};
  }
  llvm_unreachable("unknown action");
}

std::vector<State> allStates() {
  std::vector<State> states;
  for (unsigned input = 0; input < 4; ++input)
    for (unsigned a = 0; a < 4; ++a)
      for (unsigned b = 0; b < 4; ++b)
        states.push_back({input, a, b});
  return states;
}

std::string program(ArrayRef<Case> cases) {
  std::string source;
  llvm::raw_string_ostream os(source);
  os << "module { simulation.design @oracle { simulation.scope.decl 0\n";
  for (unsigned root = 0; root < 3; ++root)
    os << "simulation.storage.decl " << root << " in 0 : i2 design\n";
  for (auto [index, c] : llvm::enumerate(cases)) {
    os << "simulation.code_unit.decl " << index + 1
       << " in 0 always_comb hierarchy \"f" << index << "\"\n"
       << "simulation.func @f" << index
       << "(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})\n"
       << "    attributes {entry_kind = 4 : i32, code_unit_id = " << index + 1
       << " : i64} {\n"
       << "  %in = simulation.context.storage %ctx[0] : !simulation.ref<i2>\n"
       << "  %a = simulation.context.storage %ctx[1] : !simulation.ref<i2>\n"
       << "  %b = simulation.context.storage %ctx[2] : !simulation.ref<i2>\n"
       << "  %zero = arith.constant 0 : i2\n"
       << "  %one = arith.constant 1 : i2\n"
       << "  cf.br ^body\n^body:\n"
       << "  %input = simulation.ref.load %in : !simulation.ref<i2> -> i2\n"
       << "  %old_a = simulation.ref.load %a : !simulation.ref<i2> -> i2\n"
       << "  %old_b = simulation.ref.load %b : !simulation.ref<i2> -> i2\n"
       << "  %increment = arith.addi %old_a, %one : i2\n"
       << "  %input_xor = arith.xori %input, %one : i2\n"
       << "  %xor_ab = arith.xori %old_a, %old_b : i2\n"
       << "  %derived = arith.xori %old_a, %input : i2\n";
    if (c.useSwitch)
      os << "  cf.switch %" << c.control
         << " : i2, [default: ^no, 0: ^yes]\n";
    else
      os << "  %test = arith.cmpi eq, %" << c.control << ", %zero : i2\n"
         << "  cf.cond_br %test, ^yes, ^no\n";
    os << "^yes:\n";
    emitAction(os, c.yes, "yes");
    os << "^no:\n";
    emitAction(os, c.no, "no");
    os << "^exit:\n"
       << "  simulation.suspend.change %in to ^body : !simulation.ref<i2>\n"
       << "}\n";
  }
  os << "} }\n";
  return source;
}

class ActivationOracle : public testing::Test {
protected:
  void SetUp() override {
    context.loadDialect<arith::ArithDialect, cf::ControlFlowDialect,
                        func::FuncDialect, scf::SCFDialect, ub::UBDialect,
                        talos::schedule::ScheduleDialect,
                        talos::sim::TalosSimulationDialect>();
    cases = allCases();
    module = parseSourceString<ModuleOp>(program(cases), &context);
    ASSERT_TRUE(module);
    module->walk([&](talos::sim::SimDesignOp op) { design = op; });
    for (auto function : design.getBody().getOps<talos::sim::SimFuncOp>())
      functions.push_back(function);
    ASSERT_EQ(functions.size(), cases.size());
  }

  MLIRContext context;
  std::vector<Case> cases;
  OwningOpRef<ModuleOp> module;
  talos::sim::SimDesignOp design;
  std::vector<talos::sim::SimFuncOp> functions;
};

std::string describe(const Case &c) {
  return (c.control + " yes=" + Twine(unsigned(c.yes)) +
          " no=" + Twine(unsigned(c.no)) + " switch=" + Twine(c.useSwitch))
      .str();
}

TEST_F(ActivationOracle, IdempotenceProofsAreSound) {
  talos::analysis::HandleDataflowAnalysis handles(design);
  std::vector<State> states = allStates();
  unsigned accepted = 0, rejectedNonidempotent = 0;
  for (auto [c, function] : llvm::zip_equal(cases, functions)) {
    SCOPED_TRACE(describe(c));
    ActivationRegionAnalysis analysis(
        function, handles, {},
        ActivationMemoryAnalysis::DependencyMode::ReachingValues);
    ASSERT_TRUE(analysis.isValid());
    bool admitted = talos::analysis::hasIdempotentStateTransition(
        analysis.getActivationFunction(), analysis.getMemory(), handles);
    bool idempotent = true;
    for (const State &state : states) {
      auto first = execute(c, state);
      auto second = execute(c, {state[0], first.first, first.second});
      if (first == second)
        continue;
      idempotent = false;
      EXPECT_FALSE(admitted) << "admitted, but state " << state[0] << ','
                             << state[1] << ',' << state[2]
                             << " changes on a second activation";
    }
    accepted += admitted;
    rejectedNonidempotent += !idempotent;
  }
  // Prevent a vacuous all-reject implementation from passing the oracle.
  EXPECT_GE(accepted, 100u);
  EXPECT_GE(rejectedNonidempotent, 100u);
}

using Bits = std::set<std::pair<uint64_t, uint64_t>>;

Bits bits(ArrayRef<ActivationMemoryAnalysis::Range> ranges) {
  Bits result;
  for (const auto &range : ranges)
    for (uint64_t bit = range.low; bit < range.low + range.width; ++bit)
      result.insert({range.root, bit});
  return result;
}

TEST_F(ActivationOracle, InputDependenciesCoverEveryObservedBit) {
  talos::analysis::HandleDataflowAnalysis handles(design);
  std::vector<State> states = allStates();
  for (auto [c, function] : llvm::zip_equal(cases, functions)) {
    SCOPED_TRACE(describe(c));
    ActivationRegionAnalysis full(
        function, handles, {},
        ActivationMemoryAnalysis::DependencyMode::ReachingValues);
    ASSERT_TRUE(full.isValid());
    ASSERT_TRUE(full.getMemory().hasValueDependencies());
    // The cheaper input-only proof must agree with full publication analysis.
    ActivationRegionAnalysis inputs(
        function, handles, {}, ActivationMemoryAnalysis::DependencyMode::Inputs);
    ASSERT_TRUE(inputs.isValid());
    ASSERT_TRUE(inputs.getMemory().hasValueDependencies() ||
                inputs.getMemory().hasInputDependencies());
    Bits valueInputs = bits(full.getMemory().getValueInputs());
    EXPECT_EQ(valueInputs, bits(inputs.getMemory().getValueInputs()));
    Bits finalInputs = bits(full.getMemory().getFinalInputs());
    std::set<uint64_t> written;
    for (auto [root, bit] : bits(full.getMemory().getMayWrites()))
      written.insert(root);
    auto final = [&, &c = c](const State &state) {
      auto [a, b] = execute(c, state);
      std::vector<unsigned> observed;
      if (written.count(1))
        observed.push_back(a);
      if (written.count(2))
        observed.push_back(b);
      return observed;
    };
    for (const State &state : states)
      for (uint64_t root = 0; root < 3; ++root)
        for (uint64_t bit = 0; bit < 2; ++bit) {
          State other = state;
          other[root] ^= 1u << bit;
          if (publications(c, state) != publications(c, other))
            EXPECT_TRUE(valueInputs.count({root, bit}))
                << "missing publication input s" << root << '[' << bit << ']';
          if (final(state) != final(other))
            EXPECT_TRUE(finalInputs.count({root, bit}))
                << "missing final input s" << root << '[' << bit << ']';
        }
  }
}

} // namespace
