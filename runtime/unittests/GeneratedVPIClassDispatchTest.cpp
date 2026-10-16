//===- GeneratedVPIClassDispatchTest.cpp - Exact frame dispatch targets ---===//

#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/Parser/Parser.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Reflection/DesignReflection.h"
#include "gtest/gtest.h"
#include <cstdlib>
#include <set>

namespace {
using namespace obelisk::reflection;

template <typename Bytes> uint64_t imageChecksum(const Bytes &bytes) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (size_t index = 0; index != bytes.size(); ++index) {
    hash ^= index >= 32 && index < 40 ? uint8_t{0} : bytes[index];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

TEST(GeneratedDesignDatabase, ClassCallDispatch) {
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
  uint64_t checksum = 0;
  for (unsigned byte = 0; byte != 8; ++byte)
    checksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
  obelisk_rt_execution_descriptor_v1 execution{OBELISK_RT_VERSION,
                                               encoded->executionFlags,
                                               0,
                                               encoded->bytecode.data(),
                                               encoded->bytecode.size(),
                                               encoded->designDatabase.data(),
                                               encoded->designDatabase.size(),
                                               encoded->stateBitCount,
                                               checksum};
  ASSERT_EQ(obelisk_rt_v1_design_validate(&execution), OBELISK_RT_OK);
  obelisk_rt_context *runtime = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &runtime),
            OBELISK_RT_OK);
  const std::set<uint64_t> expected[] = {
      {101, 103, 104}, {101, 103, 104}, {102}, {104}, {}, {105, 106}};
  for (unsigned call = 0; call != std::size(expected); ++call) {
    SCOPED_TRACE(call);
    for (uint64_t callee : {0, 44, 101, 102, 103, 104, 105, 106, 107, 999})
      EXPECT_EQ(obelisk_rt_cached_vpi_call_target(runtime, 20 + call, callee),
                expected[call].count(callee) ? OBELISK_RT_OK : OBELISK_RT_EOF);
  }
  EXPECT_EQ(obelisk_rt_cached_vpi_call_target(runtime, 999, 101),
            OBELISK_RT_EOF);
  obelisk_rt_v1_context_destroy(runtime);

  HeaderView header(encoded->designDatabase.data());
  SemanticDirectoryView directory(encoded->designDatabase.data() +
                                  header.getReserved());
  ASSERT_EQ(directory.getCallDispatchBindingCount(), 16u);
  const uint64_t calls = directory.getCallDispatchBindingOffset();
  const uint64_t methods = directory.getClassDispatchMethodBindingOffset();
  const uint64_t parents = directory.getClassDispatchParentBindingOffset();
  auto invalid = [&](auto mutate) {
    auto bytes = encoded->designDatabase;
    mutate(bytes);
    HeaderWriter(bytes.data()).setChecksum(imageChecksum(bytes));
    auto corrupt = execution;
    corrupt.design_database = bytes.data();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&corrupt),
              OBELISK_RT_INVALID_DESIGN);
  };
  invalid([&](auto &bytes) {
    CallDispatchBindingWriter(bytes.data() + calls +
                              CallDispatchBindingLayout.size)
        .setCodeUnitID(107); // Unrelated class, same method slot/signature.
  });
  invalid([&](auto &bytes) {
    CallDispatchBindingWriter(bytes.data() + calls).setCodeUnitID(101);
  });
  invalid([&](auto &bytes) {
    // The interface's lowest effective slot wins over the same-signature slot5.
    CallDispatchBindingWriter(bytes.data() + calls +
                              5 * CallDispatchBindingLayout.size)
        .setCodeUnitID(102);
  });
  invalid([&](auto &bytes) {
    CallDispatchBindingWriter(bytes.data() + calls).setReceiverClassID(999);
  });
  invalid([&](auto &bytes) {
    CallDispatchBindingWriter(bytes.data() + calls).setSignatureID(0);
  });
  invalid([&](auto &bytes) {
    SemanticDirectoryWriter(bytes.data() + header.getReserved())
        .setCallDispatchBindingCount(0);
  });
  invalid([&](auto &bytes) {
    SemanticDirectoryWriter(bytes.data() + header.getReserved())
        .setCallDispatchBindingCount(15);
  });
  invalid([&](auto &bytes) {
    CallSiteBindingWriter(bytes.data() + directory.getCallSiteBindingOffset())
        .setFlags(1); // Existing group must have an explicit source contract.
  });
  invalid([&](auto &bytes) {
    ClassDispatchMethodBindingWriter(bytes.data() + methods)
        .setFlags(2); // A pure declaration cannot claim an implementation.
  });
  invalid([&](auto &bytes) {
    ClassDispatchMethodBindingWriter(bytes.data() + methods +
                                     ClassDispatchMethodBindingLayout.size)
        .setCodeUnitIndex(UINT32_MAX);
  });
  invalid([&](auto &bytes) {
    // An interface header cannot masquerade as a concrete ordinary class.
    ClassDispatchParentBindingWriter(bytes.data() + parents).setFlags(0);
  });
  invalid([&](auto &bytes) {
    ClassDispatchParentBindingWriter(bytes.data() + parents +
                                     4 * ClassDispatchParentBindingLayout.size)
        .setParentClassID(3); // Child inherits itself.
  });
  invalid([&](auto &bytes) {
    for (uint64_t row = 0;
         row != directory.getClassDispatchParentBindingCount(); ++row) {
      uint64_t offset = parents + row * ClassDispatchParentBindingLayout.size;
      ClassDispatchParentBindingView entry(bytes.data() + offset);
      if (entry.getClassID() == 8 && entry.getKind() == 0) {
        // A pure effective slot cannot belong to a concrete class.
        ClassDispatchParentBindingWriter(bytes.data() + offset).setFlags(0);
        return;
      }
    }
    FAIL() << "missing Empty class header";
  });
}
TEST(GeneratedDesignDatabase, ClassCallDispatchProducer) {
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
  uint64_t checksum = 0;
  for (unsigned byte = 0; byte != 8; ++byte)
    checksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
  obelisk_rt_execution_descriptor_v1 execution{OBELISK_RT_VERSION,
                                               encoded->executionFlags,
                                               0,
                                               encoded->bytecode.data(),
                                               encoded->bytecode.size(),
                                               encoded->designDatabase.data(),
                                               encoded->designDatabase.size(),
                                               encoded->stateBitCount,
                                               checksum};
  ASSERT_EQ(obelisk_rt_v1_design_validate(&execution), OBELISK_RT_OK);
  obelisk_rt_context *runtime = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &runtime),
            OBELISK_RT_OK);
  auto calls = design.getOps<obelisk::sim::SimVPICallDeclOp>();
  ASSERT_EQ(std::distance(calls.begin(), calls.end()), 1);
  auto call = *calls.begin();
  auto dispatches = design.getOps<obelisk::sim::SimVPIClassCallDispatchOp>();
  ASSERT_EQ(std::distance(dispatches.begin(), dispatches.end()), 1);
  auto dispatch = *dispatches.begin();
  mlir::SymbolTableCollection symbols;
  auto method =
      symbols.lookupNearestSymbolFrom<obelisk::sim::SimClassMethodDeclOp>(
          dispatch, dispatch.getMethodAttr());
  ASSERT_TRUE(method);
  auto function = symbols.lookupNearestSymbolFrom<obelisk::sim::SimFuncOp>(
      method, method.getImplementationAttr());
  ASSERT_TRUE(function);
  ASSERT_TRUE(function.getCodeUnitId());
  EXPECT_EQ(obelisk_rt_cached_vpi_call_target(runtime, call.getId(),
                                              *function.getCodeUnitId()),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_cached_vpi_call_target(runtime, call.getId(),
                                              call.getCodeUnitId()),
            OBELISK_RT_EOF);
  obelisk_rt_v1_context_destroy(runtime);
}
} // namespace
