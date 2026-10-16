//===- Primitive driver values -------------------------------------------===//

#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Reflection/DesignReflection.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <vector>

namespace {
TEST(GeneratedDesignDatabase, PrimitiveDriverValues) {
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
  options.vpi = "full";
  auto encoded = obelisk::encodeSimulationDesign(design, options);
  ASSERT_TRUE(mlir::succeeded(encoded));
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.flags = encoded->executionFlags;
  execution.bytecode = encoded->bytecode.data();
  execution.bytecode_size = encoded->bytecode.size();
  execution.design_database = encoded->designDatabase.data();
  execution.design_database_size = encoded->designDatabase.size();
  execution.state_bit_count = encoded->stateBitCount;
  for (unsigned byte = 0; byte != 8; ++byte)
    execution.checksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
  ASSERT_EQ(obelisk_rt_v1_design_validate(&execution), OBELISK_RT_OK);
  using namespace obelisk::reflection;
  const auto &image = encoded->designDatabase;
  HeaderView header(image.data());
  SemanticDirectoryView directory(image.data() + header.getReserved());
  EXPECT_EQ(directory.getTerminalDriverBindingCount(), 5u);
  EXPECT_EQ(directory.getExpressionRootBindingCount(), 2u);
  uint32_t conditionalRow = UINT32_MAX, ordinaryRow = UINT32_MAX;
  uint32_t foreignHigh = UINT32_MAX, netObject = UINT32_MAX;
  for (uint32_t index = 0; index != header.getObjectCount(); ++index) {
    ObjectView object(image.data() + header.getObjectOffset() +
                      index * ObjectLayout.size);
    uint32_t kind = object.getKindAndVPIKind() & 0xffff;
    if (kind == OBELISK_RT_DESIGN_RECORD_DRIVER && object.getID() == 5)
      foreignHigh = index;
    if (kind == OBELISK_RT_DESIGN_RECORD_NET && object.getID() == 0)
      netObject = index;
  }
  for (uint32_t index = 0; index != directory.getTerminalDriverBindingCount();
       ++index) {
    TerminalDriverBindingView binding(
        image.data() + directory.getTerminalDriverBindingOffset() +
        index * TerminalDriverBindingLayout.size);
    ObjectView low(image.data() + header.getObjectOffset() +
                   binding.getLowDriverIndex() * ObjectLayout.size);
    if (low.getID() == 2)
      conditionalRow = index;
    if (low.getID() == 0)
      ordinaryRow = index;
  }
  ASSERT_NE(conditionalRow, UINT32_MAX);
  ASSERT_NE(ordinaryRow, UINT32_MAX);
  ASSERT_NE(foreignHigh, UINT32_MAX);
  ASSERT_NE(netObject, UINT32_MAX);
  auto checksum = [](auto &bytes) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t index = 0; index != bytes.size(); ++index) {
      hash ^=
          index >= field::HeaderChecksum && index < field::HeaderChecksum + 8
              ? 0
              : bytes[index];
      hash *= UINT64_C(1099511628211);
    }
    HeaderWriter(bytes.data()).setChecksum(hash);
  };
  auto mutate = [&](uint32_t row, auto change) {
    auto bytes = image;
    TerminalDriverBindingWriter binding(
        bytes.data() + directory.getTerminalDriverBindingOffset() +
        row * TerminalDriverBindingLayout.size);
    change(binding);
    checksum(bytes);
    return bytes;
  };
  auto rejects = [&](auto bytes) {
    auto invalid = execution;
    invalid.design_database = bytes.data();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&invalid),
              OBELISK_RT_INVALID_DESIGN);
  };
  rejects(mutate(conditionalRow, [](auto &binding) {
    binding.setHighDriverIndex(UINT32_MAX);
  }));
  rejects(mutate(ordinaryRow, [&](auto &binding) {
    binding.setHighDriverIndex(foreignHigh);
  }));
  rejects(mutate(conditionalRow,
                 [&](auto &binding) { binding.setLowDriverIndex(netObject); }));
  rejects(
      mutate(conditionalRow, [](auto &binding) { binding.setBitOffset(1); }));

  auto run = [&](obelisk_rt_context *runtime, const auto &descriptor,
                 unsigned phase) {
    std::string name = "phase" + std::to_string(phase);
    auto function =
        std::find_if(encoded->functions.begin(), encoded->functions.end(),
                     [&](const auto &entry) { return entry.symbol == name; });
    ASSERT_NE(function, encoded->functions.end());
    obelisk_rt_design_bytecode_entry_v1 entry{&descriptor, function->index, 0};
    uint64_t size = 0, alignment = 0;
    ASSERT_EQ(
        obelisk_rt_validate_design_bytecode(entry, runtime, &size, &alignment),
        OBELISK_RT_OK);
    ASSERT_LE(alignment, alignof(uint64_t));
    std::vector<uint64_t> frame((size + 7) / 8);
    obelisk_rt_fragment_action_v1 action{};
    ASSERT_EQ(obelisk_rt_execute_design_bytecode(entry, runtime, frame.data(),
                                                 frame.size() * 8, 0, size, 0,
                                                 0, &action),
              OBELISK_RT_OK);
    ASSERT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  };
  auto exercise = [&](const auto &descriptor, bool forgedPair) {
    obelisk_rt_context *raw = nullptr;
    ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&descriptor, &raw),
              OBELISK_RT_OK);
    std::unique_ptr<obelisk_rt_context,
                    decltype(&obelisk_rt_v1_context_destroy)>
        runtime(raw, obelisk_rt_v1_context_destroy);
    ASSERT_EQ(obelisk_rt_v1_vpi_startup(raw, nullptr, 0), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(raw), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(raw), OBELISK_RT_OK);
    std::vector<vpiHandle> handles;
    auto keep = [&](vpiHandle h) {
      if (h)
        handles.push_back(h);
      return h;
    };
    auto named = [&](const char *name) {
      return keep(vpi_handle_by_name(const_cast<char *>(name), nullptr));
    };
    auto terminals = [&](vpiHandle primitive) {
      std::vector<vpiHandle> result;
      auto iterator = vpi_iterate(vpiPrimTerm, primitive);
      if (iterator)
        while (auto terminal = vpi_scan(iterator))
          result.push_back(keep(terminal));
      return result;
    };
    auto ordinary = named("top.ordinary");
    auto conditional = named("top.conditional");
    auto wide = named("top.wide");
    auto multiple = named("top.multi");
    auto unbound = named("top.unbound");
    auto out = named("top.out");
    ASSERT_NE(ordinary, nullptr);
    ASSERT_NE(conditional, nullptr);
    ASSERT_NE(wide, nullptr);
    auto o = terminals(ordinary), c = terminals(conditional),
         w = terminals(wide), m = terminals(multiple), u = terminals(unbound);
    ASSERT_EQ(o.size(), 2u);
    ASSERT_EQ(c.size(), 3u);
    ASSERT_EQ(w.size(), 2u);
    ASSERT_EQ(m.size(), 3u);
    ASSERT_EQ(u.size(), 2u);
    auto scalar = [&](vpiHandle h, int expected) {
      s_vpi_value value{};
      value.format = vpiObjTypeVal;
      vpi_get_value(h, &value);
      EXPECT_EQ(vpi_chk_error(nullptr), 0);
      EXPECT_EQ(value.format, vpiScalarVal);
      EXPECT_EQ(value.value.scalar, expected);
    };
    auto unavailable = [&](vpiHandle h) {
      s_vpi_value value{};
      value.format = vpiScalarVal;
      vpi_get_value(h, &value);
      s_vpi_error_info error{};
      EXPECT_NE(vpi_chk_error(&error), 0);
      EXPECT_EQ(error.level, vpiNotice);
    };
    if (forgedPair) {
      run(raw, descriptor, 0);
      unavailable(c[0]);
      unavailable(conditional);
    } else {
      scalar(o[0], vpiZ);
      scalar(ordinary, vpiZ);
      for (unsigned phase = 0; phase != 3; ++phase) {
        run(raw, descriptor, phase);
        scalar(o[0], vpi0);
        scalar(ordinary, vpi0);
        scalar(out, vpiX);
        scalar(o[1], phase == 1 ? vpi1 : vpi0);
        scalar(c[0], phase == 0 ? vpiL : phase == 1 ? vpiH : vpiZ);
        scalar(conditional, phase == 0 ? vpiL : phase == 1 ? vpiH : vpiZ);
        scalar(w[0], vpi1);
        scalar(w[1], vpi1);
        scalar(u[1], phase == 0 ? vpi1 : phase == 1 ? vpi0 : vpiX);
        auto strength = [&](vpiHandle terminal, int logic, int s0, int s1) {
          s_vpi_value result{};
          result.format = vpiStrengthVal;
          vpi_get_value(terminal, &result);
          EXPECT_EQ(vpi_chk_error(nullptr), 0);
          ASSERT_NE(result.value.strength, nullptr);
          EXPECT_EQ(result.value.strength[0].logic, logic);
          EXPECT_EQ(result.value.strength[0].s0, s0);
          EXPECT_EQ(result.value.strength[0].s1, s1);
        };
        strength(w[0], vpi1, vpiWeakDrive, vpiWeakDrive);
        strength(w[1], vpi1, vpiWeakDrive, vpiWeakDrive);
        strength(u[1],
                 phase == 0   ? vpi1
                 : phase == 1 ? vpi0
                              : vpiX,
                 phase == 0   ? vpiWeakDrive
                 : phase == 1 ? vpiPullDrive
                              : vpiStrongDrive,
                 phase == 0   ? vpiWeakDrive
                 : phase == 1 ? vpiPullDrive
                              : vpiStrongDrive);
        scalar(wide, vpi1);
        scalar(m[0], vpi0);
        scalar(m[1], vpi1);
        unavailable(multiple);
        unavailable(unbound);
        EXPECT_EQ(vpi_get(vpiSize, conditional), 2);
        s_vpi_value value{};
        value.format = vpiStrengthVal;
        vpi_get_value(c[0], &value);
        EXPECT_EQ(vpi_chk_error(nullptr), 0);
        ASSERT_NE(value.value.strength, nullptr);
        EXPECT_EQ(value.value.strength[0].logic, phase == 2 ? vpiZ : vpiX);
        EXPECT_EQ(value.value.strength[0].s0,
                  phase == 0 ? vpiStrongDrive : vpiHiZ);
        EXPECT_EQ(value.value.strength[0].s1,
                  phase == 1 ? vpiStrongDrive : vpiHiZ);
        value.format = vpiVectorVal;
        vpi_get_value(o[0], &value);
        EXPECT_EQ(vpi_chk_error(nullptr), 0);
        ASSERT_NE(value.value.vector, nullptr);
        EXPECT_EQ(value.value.vector[0].aval, 0);
        EXPECT_EQ(value.value.vector[0].bval, 0);
      }
    }
    for (auto handle : handles)
      EXPECT_EQ(vpi_release_handle(handle), 1);
  };
  exercise(execution, false);
  // Shape and scope alone cannot identify a complementary bank. The public
  // query cross-checks the exact independently validated execution descriptors.
  auto bytes = mutate(conditionalRow, [&](auto &binding) {
    binding.setHighDriverIndex(foreignHigh);
  });
  auto forged = execution;
  forged.design_database = bytes.data();
  ASSERT_EQ(obelisk_rt_v1_design_validate(&forged), OBELISK_RT_OK);
  exercise(forged, true);
}
TEST(GeneratedDesignDatabase, SourcePrimitiveValues) {
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
  options.vpi = "full";
  auto encoded = obelisk::encodeSimulationDesign(design, options);
  ASSERT_TRUE(mlir::succeeded(encoded));
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.flags = encoded->executionFlags;
  execution.bytecode = encoded->bytecode.data();
  execution.bytecode_size = encoded->bytecode.size();
  execution.design_database = encoded->designDatabase.data();
  execution.design_database_size = encoded->designDatabase.size();
  execution.state_bit_count = encoded->stateBitCount;
  for (unsigned byte = 0; byte != 8; ++byte)
    execution.checksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
  ASSERT_EQ(obelisk_rt_v1_design_validate(&execution), OBELISK_RT_OK);
  using namespace obelisk::reflection;
  HeaderView header(encoded->designDatabase.data());
  SemanticDirectoryView directory(encoded->designDatabase.data() +
                                  header.getReserved());
  EXPECT_EQ(directory.getTerminalDriverBindingCount(), 6u);
  obelisk_rt_context *raw = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &raw),
            OBELISK_RT_OK);
  std::unique_ptr<obelisk_rt_context, decltype(&obelisk_rt_v1_context_destroy)>
      runtime(raw, obelisk_rt_v1_context_destroy);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(raw, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(raw), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(raw), OBELISK_RT_OK);
  auto root = std::find_if(
      encoded->functions.begin(), encoded->functions.end(),
      [](const auto &function) { return function.symbol == "__obelisk_root"; });
  ASSERT_NE(root, encoded->functions.end());
  obelisk_rt_design_bytecode_entry_v1 entry{&execution, root->index, 0};
  uint64_t size = 0, alignment = 0;
  ASSERT_EQ(obelisk_rt_validate_design_bytecode(entry, raw, &size, &alignment),
            OBELISK_RT_OK);
  ASSERT_LE(alignment, alignof(uint64_t));
  std::vector<uint64_t> frame((size + 7) / 8);
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_execute_design_bytecode(entry, raw, frame.data(),
                                               frame.size() * 8, 0, size, 0, 0,
                                               &action),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(raw), OBELISK_RT_OK);
  std::vector<vpiHandle> handles;
  auto keep = [&](vpiHandle h) {
    if (h)
      handles.push_back(h);
    return h;
  };
  auto named = [&](const char *name) {
    return keep(vpi_handle_by_name(const_cast<char *>(name), nullptr));
  };
  auto a = named("top.a"), b = named("top.b");
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  std::vector<vpiHandle> primitives;
  for (const char *name :
       {"top.g", "top.g2", "top.multi", "top.u", "other.g"}) {
    auto primitive = named(name);
    ASSERT_NE(primitive, nullptr);
    primitives.push_back(primitive);
  }
  auto check = [&](vpiHandle value, int expected) {
    s_vpi_value scalar{};
    scalar.format = vpiScalarVal;
    vpi_get_value(value, &scalar);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(scalar.value.scalar, expected);
  };
  for (unsigned phase = 0; phase != 2; ++phase) {
    s_vpi_value value{};
    value.format = vpiScalarVal;
    value.value.scalar = vpi1;
    vpi_put_value(a, &value, nullptr, vpiForceFlag);
    ASSERT_EQ(vpi_chk_error(nullptr), 0);
    value.value.scalar = phase ? vpi0 : vpi1;
    vpi_put_value(b, &value, nullptr, vpiForceFlag);
    ASSERT_EQ(vpi_chk_error(nullptr), 0);
    ASSERT_EQ(obelisk_rt_v1_scheduler_run(raw), OBELISK_RT_OK);
    for (size_t index = 0; index != primitives.size(); ++index) {
      int expected = index < 2 && phase ? vpi0 : vpi1;
      auto terminals = vpi_iterate(vpiPrimTerm, primitives[index]);
      ASSERT_NE(terminals, nullptr);
      while (auto terminal = vpi_scan(terminals)) {
        keep(terminal);
        if (vpi_get(vpiDirection, terminal) == vpiOutput)
          check(terminal, expected);
      }
      if (index != 2)
        check(primitives[index], expected);
    }
  }
  for (auto handle : handles)
    EXPECT_EQ(vpi_release_handle(handle), 1);
}
} // namespace
