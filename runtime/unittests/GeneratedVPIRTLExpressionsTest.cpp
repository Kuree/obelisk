//===- GeneratedVPIRTLExpressionsTest.cpp - Direct source operands
//---------===//

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/Parser/Parser.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <array>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

TEST(GeneratedDesignDatabase, RTLDeclarationExpressions) {
  const char *input = std::getenv("OBELISK_TEST_INPUT");
  ASSERT_NE(input, nullptr);
  mlir::DialectRegistry registry;
  registry.insert<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
                  obelisk::sim::ObeliskSimulationDialect>();
  mlir::MLIRContext context(registry);
  auto module = mlir::parseSourceFile<mlir::ModuleOp>(input, &context);
  ASSERT_TRUE(module);
  auto design = *module->getOps<obelisk::sim::SimDesignOp>().begin();
  obelisk::SimulationBytecodeOptions options;
  options.vpi = "read";
  auto encoded = obelisk::encodeSimulationDesign(design, options);
  ASSERT_TRUE(mlir::succeeded(encoded));
  uint64_t checksum = 0;
  for (unsigned byte = 0; byte != 8; ++byte)
    checksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
  const obelisk_rt_execution_descriptor_v1 execution{
      OBELISK_RT_VERSION,
      encoded->executionFlags,
      0,
      encoded->bytecode.data(),
      encoded->bytecode.size(),
      encoded->designDatabase.data(),
      encoded->designDatabase.size(),
      encoded->stateBitCount,
      checksum,
      nullptr,
      0,
      0,
      0,
      nullptr,
      0,
      nullptr,
      0};
  ASSERT_EQ(obelisk_rt_v1_design_validate(&execution), OBELISK_RT_OK);
  obelisk_rt_context *runtime = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &runtime),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(runtime, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(runtime), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(runtime), OBELISK_RT_OK);
  auto named = [](const char *name) {
    return vpi_handle_by_name(const_cast<char *>(name), nullptr);
  };
  std::vector<vpiHandle> handles;
  auto keep = [&](vpiHandle handle) {
    if (handle)
      handles.push_back(handle);
    return handle;
  };
  vpiHandle top = keep(named("top"));
  ASSERT_NE(top, nullptr);
  std::map<std::string, vpiHandle> declarations;
  for (const char *name : {"q", "d", "flag", "n"}) {
    auto handle = keep(named((std::string("top.") + name).c_str()));
    ASSERT_NE(handle, nullptr) << name;
    declarations[name] = handle;
  }
  declarations["p"] = keep(named("pkg::p"));
  ASSERT_NE(declarations["p"], nullptr);
  struct Expected {
    int selector;
    const char *name;
  };
  std::map<int, std::vector<Expected>> expected{
      {10, {{vpiCondition, "flag"}}},
      {11, {{vpiLhs, "q"}, {vpiRhs, "d"}}},
      {12, {{vpiLhs, "q"}, {vpiRhs, "q"}}},
      {20, {{vpiCondition, "flag"}}},
      {30, {{vpiCondition, "flag"}}},
      {40, {{vpiCondition, "d"}}},
      {50, {{vpiCondition, "flag"}}},
      {51, {{vpiLhs, "q"}, {vpiRhs, "d"}}},
      {52, {{vpiLhs, "q"}, {vpiRhs, "q"}}},
      {60, {{vpiCondition, "q"}}},
      {70, {{vpiLhs, "q"}, {vpiRhs, "n"}}},
      {80, {{vpiCondition, "p"}}},
      {90, {{vpiLhs, "flag"}, {vpiRhs, nullptr}}},
      {91, {{vpiLhs, "q"}, {vpiRhs, nullptr}}},
      {100, {{vpiCondition, "d"}}},
      {111, {{vpiLhs, "local"}, {vpiRhs, "d"}}},
      {112, {{vpiCondition, "local"}}},
      {120, {{vpiCondition, nullptr}}}};
  std::vector<vpiHandle> pending;
  auto iterate = [&](int selector, vpiHandle source) {
    std::vector<vpiHandle> values;
    if (vpiHandle iterator = vpi_iterate(selector, source))
      while (vpiHandle value = vpi_scan(iterator))
        values.push_back(keep(value));
    return values;
  };
  for (vpiHandle process : iterate(vpiProcess, top)) {
    auto statement = keep(vpi_handle(vpiStmt, process));
    ASSERT_NE(statement, nullptr);
    pending.push_back(statement);
  }
  for (const char *name : {"top.value", "top.work", "top.alias"}) {
    vpiHandle function = keep(named(name));
    ASSERT_NE(function, nullptr);
    auto statement = keep(vpi_handle(vpiStmt, function));
    ASSERT_NE(statement, nullptr);
    pending.push_back(statement);
    if (std::string(name) == "top.work") {
      auto locals = iterate(vpiVariables, function);
      ASSERT_EQ(locals.size(), 1u);
      declarations["local"] = locals.front();
      EXPECT_EQ(vpi_get(vpiType, locals.front()), vpiIntVar);
      EXPECT_STREQ(vpi_get_str(vpiName, locals.front()), "local");
      s_vpi_value value{};
      value.format = vpiIntVal;
      vpi_get_value(locals.front(), &value);
      s_vpi_error_info error{};
      ASSERT_NE(vpi_chk_error(&error), 0);
      EXPECT_EQ(error.level, vpiNotice);
    }
  }
  std::set<int> visited;
  while (!pending.empty()) {
    vpiHandle statement = pending.back();
    pending.pop_back();
    int line = vpi_get(vpiLineNo, statement);
    const char *file = vpi_get_str(vpiFile, statement);
    auto found = file && std::string(file) == "rtl_expressions.sv"
                     ? expected.find(line)
                     : expected.end();
    if (found != expected.end()) {
      ASSERT_TRUE(visited.insert(line).second) << line;
      SCOPED_TRACE(line);
      for (const Expected &relation : found->second) {
        vpiHandle target = keep(vpi_handle(relation.selector, statement));
        if (!relation.name) {
          EXPECT_EQ(target, nullptr);
          continue;
        }
        ASSERT_NE(target, nullptr) << relation.selector;
        EXPECT_EQ(vpi_compare_objects(target, declarations.at(relation.name)),
                  1);
      }
    }
    const int kind = vpi_get(vpiType, statement);
    auto append = [&](vpiHandle child) {
      if (child)
        pending.push_back(keep(child));
    };
    if (kind == vpiBegin || kind == vpiNamedBegin) {
      auto children = iterate(vpiStmt, statement);
      pending.insert(pending.end(), children.begin(), children.end());
    } else if (kind == vpiCase) {
      auto children = iterate(vpiCaseItem, statement);
      pending.insert(pending.end(), children.begin(), children.end());
    } else if (kind == vpiIf || kind == vpiIfElse || kind == vpiWhile ||
               kind == vpiDoWhile || kind == vpiRepeat || kind == vpiFor ||
               kind == vpiCaseItem) {
      vpiHandle body = vpi_handle(vpiStmt, statement);
      ASSERT_NE(body, nullptr) << kind;
      if (kind == vpiDoWhile)
        EXPECT_EQ(vpi_get(vpiType, body), vpiNullStmt);
      append(body);
      if (kind == vpiIfElse)
        append(vpi_handle(vpiElseStmt, statement));
      if (kind == vpiFor)
        for (int selector : {vpiForInitStmt, vpiForIncStmt}) {
          auto controls = iterate(selector, statement);
          pending.insert(pending.end(), controls.begin(), controls.end());
        }
    }
  }
  EXPECT_EQ(visited.size(), expected.size());
  // A declaration used twice in one assignment has one reverse use. The
  // forward Lhs/Rhs still both return the exact same declaration object.
  auto qUses = iterate(vpiUse, declarations.at("q"));
  std::set<int> qLines;
  for (vpiHandle use : qUses)
    EXPECT_TRUE(qLines.insert(vpi_get(vpiLineNo, use)).second);
  EXPECT_EQ(qLines, (std::set<int>{11, 12, 51, 52, 60, 70, 91}));
  for (vpiHandle handle : handles)
    EXPECT_EQ(vpi_release_handle(handle), 1);
  EXPECT_EQ(obelisk_rt_v1_vpi_end_simulation(runtime), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(runtime);
}
} // namespace
