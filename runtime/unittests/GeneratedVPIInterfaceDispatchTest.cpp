//===- GeneratedVPIInterfaceDispatchTest.cpp - Interface invocation proof
//--===//

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
#include <cstring>
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

TEST(GeneratedDesignDatabase, InterfaceCallDispatch) {
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
  // The call's physical top never filters compatible instances (25.9).
  // Extern functions name their provider; forkjoin tasks invoke their retained
  // aggregation declaration, including an empty provider list (25.7.4).
  const std::set<uint64_t> expected[] = {{45, 54}, {48},     {},   {45, 54},
                                         {45, 54}, {70, 71}, {90}, {76},
                                         {100},    {101}};
  for (unsigned call = 0; call != std::size(expected); ++call) {
    SCOPED_TRACE(call);
    for (uint64_t callee : {0,  44, 45, 47, 48, 54, 57, 60,  64,  70, 71,
                            72, 73, 74, 75, 76, 80, 90, 100, 101, 999})
      EXPECT_EQ(obelisk_rt_cached_vpi_call_target(runtime, 10 + call, callee),
                expected[call].count(callee) ? OBELISK_RT_OK : OBELISK_RT_EOF);
  }
  EXPECT_EQ(obelisk_rt_cached_vpi_call_target(runtime, 999, 45),
            OBELISK_RT_EOF);
  obelisk_rt_v1_context_destroy(runtime);

  const auto &image = encoded->designDatabase;
  HeaderView header(image.data());
  SemanticDirectoryView directory(image.data() + header.getReserved());
  ASSERT_EQ(directory.getInterfaceCallableCount(), 8u);
  ASSERT_EQ(directory.getInterfaceInstanceCount(), 4u);
  ASSERT_EQ(directory.getInterfaceCallableBindingCount(), 11u);
  ASSERT_EQ(directory.getInterfaceCallBindingCount(), 10u);
  ASSERT_EQ(directory.getCallableSignatureBindingCount(), 14u);
  auto objectIndex = [&](uint64_t id) {
    for (uint32_t i = 0; i != header.getObjectCount(); ++i)
      if (ObjectView(image.data() + header.getObjectOffset() +
                     uint64_t{i} * ObjectLayout.size)
              .getID() == id)
        return i;
    return UINT32_MAX;
  };
  auto callRow = [&](uint64_t id, bool contract) {
    uint64_t first = contract ? directory.getInterfaceCallBindingOffset()
                              : directory.getCallSiteBindingOffset();
    uint64_t count = contract ? directory.getInterfaceCallBindingCount()
                              : directory.getCallSiteBindingCount();
    uint64_t size =
        contract ? InterfaceCallBindingLayout.size : CallSiteBindingLayout.size;
    for (uint64_t i = 0; i != count; ++i) {
      uint64_t row = first + i * size;
      uint64_t actual =
          contract ? InterfaceCallBindingView(image.data() + row).getCallID()
                   : CallSiteBindingView(image.data() + row).getID();
      if (actual == id)
        return row;
    }
    return uint64_t{0};
  };
  const uint64_t call = callRow(10, true);
  const uint64_t callSite = callRow(10, false);
  const uint64_t exportCall = callRow(11, true);
  ASSERT_NE(call, 0u);
  ASSERT_NE(callSite, 0u);
  ASSERT_NE(exportCall, 0u);
  const uint32_t f =
      InterfaceCallBindingView(image.data() + call).getMemberIndex();
  const uint32_t g =
      InterfaceCallBindingView(image.data() + exportCall).getMemberIndex();
  const uint64_t member = directory.getInterfaceCallableOffset() +
                          uint64_t{f} * InterfaceCallableLayout.size;
  const uint64_t exportMember = directory.getInterfaceCallableOffset() +
                                uint64_t{g} * InterfaceCallableLayout.size;
  uint64_t ordinary = 0, provider = 0, exactName = 0, signature = 0;
  uint64_t taskProvider = 0, forkJoin = 0, emptyForkJoin = 0;
  for (uint64_t i = 0; i != directory.getInterfaceCallableBindingCount(); ++i) {
    uint64_t row = directory.getInterfaceCallableBindingOffset() +
                   i * InterfaceCallableBindingLayout.size;
    InterfaceCallableBindingView binding(image.data() + row);
    if (binding.getDeclarationIndex() == objectIndex(45))
      ordinary = row;
    if (binding.getDeclarationIndex() == objectIndex(47))
      provider = row;
    if (binding.getDeclarationIndex() == objectIndex(74))
      taskProvider = row;
    if (binding.getDeclarationIndex() == objectIndex(70))
      forkJoin = row;
    if (binding.getDeclarationIndex() == objectIndex(71))
      emptyForkJoin = row;
  }
  uint32_t packedDeclaration = 0;
  ASSERT_TRUE(
      tryPackTableIndex(TableKind::Object, objectIndex(45), packedDeclaration));
  for (uint64_t i = 0; i != directory.getFixedPropertyCount(); ++i) {
    uint64_t row =
        directory.getFixedPropertyOffset() + i * FixedPropertyLayout.size;
    FixedPropertyView property(image.data() + row);
    if (property.getSourceIndexAndTable() == packedDeclaration &&
        property.getSelector() == 2)
      exactName = row;
  }
  for (uint64_t i = 0; i != directory.getCallableSignatureBindingCount(); ++i) {
    uint64_t row = directory.getCallableSignatureBindingOffset() +
                   i * CallableSignatureBindingLayout.size;
    if (CallableSignatureBindingView(image.data() + row).getCodeUnitIndex() ==
        objectIndex(45))
      signature = row;
  }
  ASSERT_NE(ordinary, 0u);
  ASSERT_NE(provider, 0u);
  ASSERT_NE(exactName, 0u);
  ASSERT_NE(signature, 0u);
  ASSERT_NE(taskProvider, 0u);
  ASSERT_NE(forkJoin, 0u);
  ASSERT_NE(emptyForkJoin, 0u);
  ASSERT_NE(objectIndex(90), UINT32_MAX);
  const uint32_t otherSignature =
      InterfaceCallableView(
          image.data() + directory.getInterfaceCallableOffset() +
          uint64_t{InterfaceCallBindingView(image.data() + callRow(16, true))
                       .getMemberIndex()} *
              InterfaceCallableLayout.size)
          .getSignatureID();
  ASSERT_NE(InterfaceCallableView(image.data() + member).getSignatureID(),
            otherSignature);

  auto signatureRow = [&](uint64_t unit) {
    for (uint64_t i = 0; i != directory.getCallableSignatureBindingCount();
         ++i) {
      uint64_t row = directory.getCallableSignatureBindingOffset() +
                     i * CallableSignatureBindingLayout.size;
      if (CallableSignatureBindingView(image.data() + row).getCodeUnitIndex() ==
          objectIndex(unit))
        return row;
    }
    return uint64_t{0};
  };
  ASSERT_NE(signatureRow(70), 0u);
  ASSERT_NE(signatureRow(74), 0u);
  ASSERT_NE(signatureRow(76), 0u);
  auto invalid = [&](const char *label, auto mutate) {
    SCOPED_TRACE(label);
    auto bytes = image;
    mutate(bytes);
    HeaderWriter(bytes.data()).setChecksum(imageChecksum(bytes));
    auto corrupt = execution;
    corrupt.design_database = bytes.data();
    corrupt.design_database_size = bytes.size();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&corrupt),
              OBELISK_RT_INVALID_DESIGN);
  };
  for (uint32_t flags : {1, 4, 7, 13})
    invalid("invalid call proof flags", [&](auto &bytes) {
      CallSiteBindingWriter(bytes.data() + callSite).setFlags(flags);
    });
  invalid("nominal must be exact canonical interface typespec",
          [&](auto &bytes) {
            uint32_t scope = 0;
            ASSERT_TRUE(tryPackTableIndex(TableKind::Scope, 2, scope));
            InterfaceCallableWriter(bytes.data() + member)
                .setTypeIndexAndTable(scope);
          });
  invalid("exact member name length", [&](auto &bytes) {
    InterfaceCallableWriter(bytes.data() + member).setNameSize(0);
  });
  invalid("reserved member flags", [&](auto &bytes) {
    InterfaceCallableWriter(bytes.data() + member).setFlags(2);
  });
  invalid("reserved member payload", [&](auto &bytes) {
    InterfaceCallableWriter(bytes.data() + member).setReserved(1);
  });
  invalid("member signature zero", [&](auto &bytes) {
    InterfaceCallableWriter(bytes.data() + member).setSignatureID(0);
  });
  invalid("independent signature mismatch", [&](auto &bytes) {
    CallableSignatureBindingWriter(bytes.data() + signature)
        .setSignatureID(otherSignature);
  });
  invalid("signature fact must name callable unit", [&](auto &bytes) {
    CallableSignatureBindingWriter(bytes.data() + signature)
        .setCodeUnitIndex(UINT32_MAX);
  });
  invalid("declaration belongs to exact interface instance", [&](auto &bytes) {
    InterfaceCallableBindingWriter(bytes.data() + ordinary)
        .setDeclarationIndex(objectIndex(54));
    InterfaceCallableBindingWriter(bytes.data() + ordinary)
        .setImplementationIndex(objectIndex(54));
  });
  invalid("ordinary declaration is implementation", [&](auto &bytes) {
    InterfaceCallableBindingWriter(bytes.data() + ordinary)
        .setImplementationIndex(objectIndex(54));
  });
  invalid("same-kind provider has wrong original ABI", [&](auto &bytes) {
    InterfaceCallableBindingWriter(bytes.data() + provider)
        .setImplementationIndex(objectIndex(90));
  });
  invalid("wrong provider callable kind", [&](auto &bytes) {
    InterfaceCallableBindingWriter(bytes.data() + provider)
        .setImplementationIndex(objectIndex(70));
  });
  invalid("extern function requires provider", [&](auto &bytes) {
    InterfaceCallableBindingWriter(bytes.data() + provider)
        .setImplementationIndex(objectIndex(47));
  });
  invalid("extern provider cannot itself export", [&](auto &bytes) {
    InterfaceCallableBindingWriter(bytes.data() + provider)
        .setImplementationIndex(objectIndex(57));
  });
  invalid("ordinary extern task cannot invoke itself", [&](auto &bytes) {
    InterfaceCallableBindingWriter(bytes.data() + taskProvider)
        .setImplementationIndex(objectIndex(74));
  });
  invalid("forkjoin invokes aggregator, not a compatible provider",
          [&](auto &bytes) {
            InterfaceCallableBindingWriter(bytes.data() + forkJoin)
                .setImplementationIndex(objectIndex(76));
          });
  invalid("empty forkjoin still invokes its aggregator", [&](auto &bytes) {
    InterfaceCallableBindingWriter(bytes.data() + emptyForkJoin)
        .setImplementationIndex(objectIndex(76));
  });
  invalid("provider source fact cannot export", [&](auto &bytes) {
    CallableSignatureBindingWriter(bytes.data() + signatureRow(76)).setFlags(1);
  });
  invalid("forkjoin source fact requires extern", [&](auto &bytes) {
    CallableSignatureBindingWriter(bytes.data() + signatureRow(70)).setFlags(2);
  });
  invalid("declaration extern fact is independent", [&](auto &bytes) {
    CallableSignatureBindingWriter(bytes.data() + signatureRow(74)).setFlags(0);
  });
  invalid("reserved callable flags", [&](auto &bytes) {
    CallableSignatureBindingWriter(bytes.data() + signature).setFlags(4);
  });
  invalid("function cannot be forkjoin", [&](auto &bytes) {
    CallableSignatureBindingWriter(bytes.data() + signature).setFlags(3);
  });
  invalid("provider needs extern declaration", [&](auto &bytes) {
    InterfaceCallableWriter(bytes.data() + exportMember).setFlags(0);
  });
  invalid("member completeness", [&](auto &bytes) {
    SemanticDirectoryWriter(bytes.data() + header.getReserved())
        .setInterfaceCallableBindingCount(10);
  });
  invalid("instance completeness", [&](auto &bytes) {
    SemanticDirectoryWriter(bytes.data() + header.getReserved())
        .setInterfaceInstanceCount(3);
  });
  invalid("missing source contract", [&](auto &bytes) {
    SemanticDirectoryWriter(bytes.data() + header.getReserved())
        .setInterfaceCallBindingCount(9);
  });
  invalid("missing callable signature", [&](auto &bytes) {
    SemanticDirectoryWriter(bytes.data() + header.getReserved())
        .setCallableSignatureBindingCount(13);
  });
  invalid("unknown member", [&](auto &bytes) {
    InterfaceCallBindingWriter(bytes.data() + call).setMemberIndex(UINT32_MAX);
  });
  invalid("unknown access", [&](auto &bytes) {
    InterfaceCallBindingWriter(bytes.data() + call).setAccess(3);
  });
  invalid("export access requires extern member", [&](auto &bytes) {
    InterfaceCallBindingWriter(bytes.data() + call).setAccess(2);
  });
  invalid("duplicate call contract", [&](auto &bytes) {
    InterfaceCallBindingWriter(bytes.data() + exportCall).setCallID(10);
  });
  invalid("exact source name cannot fall back to hierarchy", [&](auto &bytes) {
    uint64_t end = directory.getFixedPropertyOffset() +
                   directory.getFixedPropertyCount() * FixedPropertyLayout.size;
    std::memmove(bytes.data() + exactName,
                 bytes.data() + exactName + FixedPropertyLayout.size,
                 end - exactName - FixedPropertyLayout.size);
    SemanticDirectoryWriter(bytes.data() + header.getReserved())
        .setFixedPropertyCount(directory.getFixedPropertyCount() - 1);
  });
}
} // namespace
