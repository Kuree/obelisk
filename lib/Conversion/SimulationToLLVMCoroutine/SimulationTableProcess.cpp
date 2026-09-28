//===- SimulationTableProcess.cpp - Table process admission and lowering
//---===//

#include "SimulationTableProcess.h"
#include "SimulationProcessCoroutineLowering.h"
#include "mlir/Interfaces/CallInterfaces.h"
#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Conversion/SimulationRuntime.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

using namespace mlir;
namespace obelisk::detail {

std::optional<NativeTableProcess>
analyzeTableProcess(sim::SimFuncOp function,
                    const SimulationProcessFrameAnalysis &analysis) {
  if (function.isExternal() || analysis.getSuspensions().empty() ||
      function.getEntryKind() == sim::EntryKind::Task ||
      function.getEntryKind() == sim::EntryKind::Function ||
      function.getEntryKind() == sim::EntryKind::Observer ||
      !isUnmanagedNativeProcess(function))
    return std::nullopt;
  for (BlockArgument argument : function.getBody().front().getArguments()) {
    Type type = argument.getType();
    if (isa<sim::RefType>(type)) {
      auto kind = function.getArgAttrOfType<IntegerAttr>(
          argument.getArgNumber(), sim::metadata::captureKind);
      if (!kind ||
          kind.getInt() != static_cast<int32_t>(sim::CaptureKind::Storage))
        return std::nullopt;
    } else if (!isa<sim::ContextType, IntegerType, FloatType, sim::LogicType,
                    sim::TimeType, sim::PackedArrayType, sim::PackedStructType,
                    sim::PackedUnionType>(type)) {
      return std::nullopt;
    }
  }
  for (uint32_t id : analysis.getContinuations())
    if (!analysis.getContinuationLayout(id).empty())
      return std::nullopt;
  for (const auto &field : analysis.getFields())
    if (field.kind == ProcessFrameFieldKind::Live ||
        field.kind == ProcessFrameFieldKind::Continuation ||
        field.flags == ProcessFrameFieldFlags::ManagedRoot ||
        field.flags == ProcessFrameFieldFlags::CandidateRoot)
      return std::nullopt;
  // A frame layout alone does not prove lifetime or control independence.
  // Exclude allocations, calls, fork, process identities and named control
  // scopes. Other actors can still suspend, resume or kill this actor.
  auto unsafeType = [](Type type) {
    return isa<sim::ProcessType, sim::ControlType, LLVM::LLVMPointerType>(type);
  };
  if (llvm::any_of(function.getBody().front().getArgumentTypes(), unsafeType) ||
      function
          .walk([&](Operation *op) {
            if (op == function.getOperation())
              return WalkResult::advance();
            StringRef name = op->getName().getStringRef();
            if (isa<CallOpInterface, sim::SimRefAllocOp, sim::SimSpawnOp,
                    sim::SimDPICallOp>(op) ||
                name.starts_with("simulation.control.") ||
                name.starts_with("simulation.process.") ||
                llvm::any_of(op->getOperandTypes(), unsafeType) ||
                llvm::any_of(op->getResultTypes(), unsafeType))
              return WalkResult::interrupt();
            return WalkResult::advance();
          })
          .wasInterrupted())
    return std::nullopt;

  NativeTableProcess plan;
  for (const auto &suspension : analysis.getSuspensions()) {
    Operation *op = suspension.operation;
    NativeTableWait wait{suspension.continuationID,
                         getRuntimeResumeActionFlags(op),
                         suspension.waitOffset,
                         suspension.waitSize,
                         0,
                         0,
                         {}};
    if (wait.actionFlags == UINT32_MAX)
      return std::nullopt;
    SmallVector<Value> watched;
    SmallVector<uint32_t> edges;
    if (auto change = dyn_cast<sim::SimSuspendChangeOp>(op)) {
      wait.kind = OBELISK_RT_SUSPEND_CHANGE;
      watched.push_back(change.getWatched());
      edges.push_back(OBELISK_RT_WAIT_EDGE_CHANGE);
    } else if (auto edge = dyn_cast<sim::SimSuspendEdgeOp>(op)) {
      wait.kind = OBELISK_RT_SUSPEND_EDGE;
      watched.push_back(edge.getWatched());
      edges.push_back(static_cast<uint32_t>(edge.getEdge()));
    } else if (auto any = dyn_cast<sim::SimSuspendAnyOp>(op)) {
      wait.kind = OBELISK_RT_SUSPEND_EDGE;
      llvm::append_range(watched, any.getWatched());
      for (int32_t edge : any.getEdges())
        edges.push_back(edge);
    } else {
      return std::nullopt;
    }
    if ((schedule::has<schedule::metadata::topLevelWildcardWait>(op) ||
         schedule::has<schedule::metadata::proceduralEventWait>(op)) &&
        !schedule::has<schedule::metadata::repeatingAlwaysWait>(op))
      wait.flags = OBELISK_RT_WAIT_SUPPRESS_ACTIVE_SELF;
    for (auto [value, edge] : llvm::zip_equal(watched, edges)) {
      auto argument = dyn_cast<BlockArgument>(value);
      auto reference = dyn_cast<sim::RefType>(value.getType());
      if (!argument || argument.getOwner() != &function.getBody().front() ||
          !reference ||
          !isa<IntegerType, sim::LogicType, sim::PackedArrayType,
               sim::PackedStructType, sim::PackedUnionType>(
              reference.getElementType()))
        return std::nullopt;
      auto kind = function.getArgAttrOfType<IntegerAttr>(
          argument.getArgNumber(), sim::metadata::captureKind);
      if (!kind ||
          kind.getInt() != static_cast<int32_t>(sim::CaptureKind::Storage))
        return std::nullopt;
      auto width =
          analysis::getSimulationStorageBitWidth(reference.getElementType());
      const auto &slot =
          analysis.getEntryCaptureLayout()[argument.getArgNumber()];
      if (!width || *width == 0 || *width > UINT32_MAX ||
          !slot.hasValueStorage() || slot.storageSize != 8 ||
          slot.hasSecondaryStorage())
        return std::nullopt;
      // IEEE 1800-2023 9.4.2: edge events observe only the least significant
      // bit.
      wait.watches.push_back(
          {slot.valueOffset, edge,
           edge == OBELISK_RT_WAIT_EDGE_CHANGE ? uint32_t(*width) : 1u});
    }
    if (watched.empty())
      return std::nullopt;
    plan.waits.push_back(std::move(wait));
  }
  for (auto [index, suspension] : llvm::enumerate(analysis.getSuspensions()))
    suspension.operation->setAttr(
        tableWaitIndexAttr,
        IntegerAttr::get(IntegerType::get(function.getContext(), 32), index));
  return plan;
}

void materializeTableProcess(PreparedSuspendableProcess &process) {
  ModuleOp module = process.module;
  Location location = process.location;
  MLIRContext *context = module.getContext();
  OpBuilder builder(context);
  Type i32 = builder.getI32Type(), i64 = builder.getI64Type();
  Type pointer = LLVM::LLVMPointerType::get(context);
  auto watchType = LLVM::LLVMStructType::getLiteral(context, {i64, i32, i32});
  auto recordType =
      LLVM::LLVMStructType::getLiteral(context, {i32, i32, i32, i32, i64, i64});
  auto waitType = LLVM::LLVMStructType::getLiteral(
      context, {i32, i32, i64, i64, recordType, pointer});
  auto &waits = process.tableProcess->waits;
  std::string prefix = process.baseName + ".__obelisk_table";
  for (auto [index, wait] : llvm::enumerate(waits)) {
    auto arrayType = LLVM::LLVMArrayType::get(watchType, wait.watches.size());
    makeConstantGlobal(
        module, location, arrayType,
        prefix + ".watches_" + std::to_string(index), LLVM::Linkage::Internal,
        8, [&](OpBuilder &builder) {
          Value array = LLVM::ZeroOp::create(builder, location, arrayType);
          for (auto [index, watch] : llvm::enumerate(wait.watches)) {
            Value item = LLVM::ZeroOp::create(builder, location, watchType);
            item = insertValue(
                builder, location, item,
                llvmConstant(builder, location, i64, watch.capture_offset), 0);
            item = insertValue(builder, location, item,
                               llvmConstant(builder, location, i32, watch.edge),
                               1);
            item = insertValue(
                builder, location, item,
                llvmConstant(builder, location, i32, watch.width), 2);
            array =
                LLVM::InsertValueOp::create(builder, location, array, item,
                                            ArrayRef<int64_t>{int64_t(index)});
          }
          return array;
        });
  }
  auto waitsType = LLVM::LLVMArrayType::get(waitType, waits.size());
  makeConstantGlobal(
      module, location, waitsType, prefix + ".waits", LLVM::Linkage::Internal,
      8, [&](OpBuilder &builder) {
        Value array = LLVM::ZeroOp::create(builder, location, waitsType);
        for (auto [index, wait] : llvm::enumerate(waits)) {
          Value record = LLVM::ZeroOp::create(builder, location, recordType);
          const uint32_t header[] = {OBELISK_RT_VERSION, wait.kind, wait.flags,
                                     uint32_t(wait.watches.size())};
          for (auto [field, value] : llvm::enumerate(header))
            record =
                insertValue(builder, location, record,
                            llvmConstant(builder, location, i32, value), field);
          Value item = LLVM::ZeroOp::create(builder, location, waitType);
          item = insertValue(
              builder, location, item,
              llvmConstant(builder, location, i32, wait.continuation), 0);
          item = insertValue(
              builder, location, item,
              llvmConstant(builder, location, i32, wait.actionFlags), 1);
          item = insertValue(
              builder, location, item,
              llvmConstant(builder, location, i64, wait.frameOffset), 2);
          item = insertValue(
              builder, location, item,
              llvmConstant(builder, location, i64, wait.frameSize), 3);
          item = insertValue(builder, location, item, record, 4);
          item = insertValue(builder, location, item,
                             LLVM::AddressOfOp::create(
                                 builder, location, pointer,
                                 prefix + ".watches_" + std::to_string(index)),
                             5);
          array =
              LLVM::InsertValueOp::create(builder, location, array, item,
                                          ArrayRef<int64_t>{int64_t(index)});
        }
        return array;
      });
  auto planType =
      LLVM::LLVMStructType::getLiteral(context, {i32, i32, pointer, pointer});
  makeConstantGlobal(
      module, location, planType, prefix + ".plan", LLVM::Linkage::Internal, 8,
      [&](OpBuilder &builder) {
        Value plan = LLVM::ZeroOp::create(builder, location, planType);
        plan = insertValue(
            builder, location, plan,
            llvmConstant(builder, location, i32, OBELISK_RT_VERSION), 0);
        plan =
            insertValue(builder, location, plan,
                        llvmConstant(builder, location, i32, waits.size()), 1);
        plan = insertValue(builder, location, plan,
                           LLVM::AddressOfOp::create(builder, location, pointer,
                                                     process.ramp.getSymName()),
                           2);
        return insertValue(builder, location, plan,
                           LLVM::AddressOfOp::create(builder, location, pointer,
                                                     prefix + ".waits"),
                           3);
      });
}

void declareTableProcessRuntimeABI(ModuleOp module) {
  MLIRContext *context = module.getContext();
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_table_process_execute",
                           IntegerType::get(context, 32),
                           {LLVM::LLVMPointerType::get(context)});
}
} // namespace obelisk::detail
