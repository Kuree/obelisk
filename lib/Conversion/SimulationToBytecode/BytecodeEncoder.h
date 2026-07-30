//===- BytecodeEncoder.h - Private design bytecode encoder ------*- C++ -*-===//
//
// Private interface shared by the semantic instruction-selection groups.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOBYTECODE_BYTECODEENCODER_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOBYTECODE_BYTECODEENCODER_H

#include "BytecodeEncoding.h"
#include "BytecodePlan.h"
#include "obelisk/Conversion/SimulationToBytecode.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/Support/LLVM.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace llvm {
class DataLayout;
}

namespace obelisk::bytecode {

class Encoder {
public:
  Encoder(sim::SimDesignOp design, const SimulationBytecodeOptions &options,
          const llvm::DataLayout &dataLayout);

  mlir::FailureOr<EncodedSimulationDesign> encode();

private:
  mlir::LogicalResult prepareStaticSpecializationSites();
  mlir::LogicalResult prepareClassLayouts();
  mlir::FailureOr<uint64_t> classID(mlir::SymbolRefAttr symbol,
                                    mlir::Operation *anchor) const;
  mlir::LogicalResult planTwoStateRegisters();
  mlir::FailureOr<Layout> getValueLayout(mlir::Value value) const;
  mlir::LogicalResult planFunctions();
  mlir::LogicalResult planScheduleRanks();

  uint32_t reg(const FunctionPlan &plan, mlir::Value value) const;
  uint32_t temporary(FunctionPlan &plan, mlir::Type type);
  uint32_t temporaryLike(FunctionPlan &plan, mlir::Type type,
                         mlir::Value model);
  uint64_t addConstant(const Layout &layout, const llvm::APInt &value,
                       const llvm::APInt *unknown = nullptr);
  uint64_t addZeroConstant(const Layout &layout);
  uint64_t addRawConstant(llvm::ArrayRef<uint8_t> bytes);
  uint64_t addBytesConstant(llvm::ArrayRef<uint8_t> bytes);

  uint32_t addIntrinsicSignature(uint32_t id, uint32_t inputCount,
                                 uint32_t outputCount, uint32_t flags = 0);
  mlir::LogicalResult emitIntrinsicRegisters(FunctionPlan &plan, uint32_t id,
                                             llvm::ArrayRef<uint32_t> inputs,
                                             llvm::ArrayRef<uint32_t> outputs,
                                             uint32_t flags = 0);
  mlir::LogicalResult emitIntrinsic(FunctionPlan &plan, uint32_t id,
                                    llvm::ArrayRef<mlir::Value> inputs,
                                    llvm::ArrayRef<mlir::Value> outputs,
                                    uint32_t flags = 0);
  uint32_t emitBytesConstant(FunctionPlan &plan, llvm::ArrayRef<uint8_t> bytes);
  uint32_t emitU64Constant(FunctionPlan &plan, uint64_t value);

  mlir::LogicalResult encodeClassDirectCall(FunctionPlan &plan,
                                            sim::SimClassDirectCallOp call);
  mlir::LogicalResult encodeClassVirtualCall(FunctionPlan &plan,
                                             sim::SimClassVirtualCallOp call);
  std::optional<mlir::LogicalResult>
  encodeClassOperation(FunctionPlan &plan, mlir::Operation *operation);
  mlir::LogicalResult encodeDisplay(FunctionPlan &plan, sim::SimDisplayOp op);
  uint64_t emit(Instruction instruction);
  std::pair<uint64_t, uint64_t> addMap(FunctionPlan &destinationPlan,
                                       mlir::ValueRange destination,
                                       FunctionPlan &sourcePlan,
                                       mlir::ValueRange source);
  std::pair<uint64_t, uint64_t>
  addRegistersMap(llvm::ArrayRef<uint32_t> destinations,
                  FunctionPlan &sourcePlan, mlir::ValueRange source);

  mlir::LogicalResult encodeFunctions();
  static bool mayCollect(mlir::Operation *operation);
  mlir::LogicalResult emitAggregateManagedRoots(FunctionPlan &plan,
                                                mlir::Operation *operation);
  void emitDeadManagedClears(FunctionPlan &plan, mlir::Operation *operation);
  mlir::LogicalResult emitContinuationEntries(FunctionPlan &plan);
  mlir::LogicalResult emitConstant(FunctionPlan &plan, mlir::Value result,
                                   const llvm::APInt &value,
                                   const llvm::APInt *unknown = nullptr);

  mlir::LogicalResult encodeOperation(FunctionPlan &plan,
                                      mlir::Operation *operation);
  std::optional<mlir::LogicalResult>
  encodeArithmeticOperation(FunctionPlan &plan, mlir::Operation *operation);
  std::optional<mlir::LogicalResult>
  encodeContainerOperation(FunctionPlan &plan, mlir::Operation *operation);
  std::optional<mlir::LogicalResult>
  encodeStringOperation(FunctionPlan &plan, mlir::Operation *operation);
  mlir::LogicalResult encodeCall(FunctionPlan &plan, sim::SimCallOp call);
  mlir::LogicalResult encodeTaskCall(FunctionPlan &plan,
                                     sim::SimTaskCallOp call);
  mlir::LogicalResult encodeDPICall(FunctionPlan &plan, sim::SimDPICallOp call);
  mlir::LogicalResult encodeReturn(FunctionPlan &plan, sim::SimReturnOp op);

  mlir::LogicalResult encodeLogicBinary(FunctionPlan &plan,
                                        sim::SimLogicBinaryOp op);
  mlir::LogicalResult encodeLogicCompare(FunctionPlan &plan,
                                         sim::SimLogicCompareOp op);
  mlir::LogicalResult encodeConcat(FunctionPlan &plan,
                                   sim::SimLogicConcatOp op);
  mlir::LogicalResult encodeReplicate(FunctionPlan &plan,
                                      sim::SimLogicReplicateOp op);
  mlir::FailureOr<uint32_t> encodeArrayOffset(FunctionPlan &plan,
                                              mlir::Type array,
                                              mlir::Value indexValue,
                                              mlir::Operation *anchor);
  mlir::LogicalResult encodeArrayExtract(FunctionPlan &plan,
                                         sim::SimArrayDynExtractOp op);
  mlir::LogicalResult encodeUnionConstruct(FunctionPlan &plan,
                                           sim::SimUnionConstructOp op);
  mlir::LogicalResult encodeUnionIsActive(FunctionPlan &plan,
                                          sim::SimUnionIsActiveOp op);
  mlir::LogicalResult
  encodeHandle(FunctionPlan &plan, mlir::Value result, uint64_t id,
               const llvm::DenseMap<uint64_t, uint64_t> &offsets,
               uint32_t kind);
  mlir::LogicalResult encodeHandleOffsetRegister(FunctionPlan &plan,
                                                 mlir::Value result,
                                                 mlir::Value input,
                                                 uint64_t offset,
                                                 uint32_t dynamic);
  mlir::LogicalResult encodeHandleOffset(FunctionPlan &plan, mlir::Value result,
                                         mlir::Value input, uint64_t offset,
                                         mlir::Value dynamic);
  mlir::LogicalResult encodeSubelementView(FunctionPlan &plan,
                                           mlir::Value result,
                                           mlir::Value input,
                                           llvm::ArrayRef<int64_t> indices,
                                           mlir::Operation *anchor);
  mlir::LogicalResult encodeArrayView(FunctionPlan &plan, mlir::Value result,
                                      mlir::Value input, mlir::Value index,
                                      mlir::Operation *anchor);

  void emitFrameTransfer(FunctionPlan &plan, uint16_t opcode, mlir::Value value,
                         uint64_t offset, uint32_t transferSize = 0);
  mlir::LogicalResult encodeObserverWait(FunctionPlan &plan,
                                         sim::SimSuspendObserveOp operation);
  mlir::LogicalResult encodeWait(FunctionPlan &plan, mlir::Operation *operation,
                                 mlir::ValueRange continuationOperands,
                                 uint32_t kind, uint32_t flags,
                                 llvm::ArrayRef<uint32_t> edges,
                                 llvm::ArrayRef<mlir::Value> watched,
                                 mlir::Value delay = {});

  uint32_t getVPIProfile();
  llvm::SmallVector<uint8_t> serializeBytecode();
  llvm::SmallVector<uint8_t> serializeDatabase(uint32_t profile);

  sim::SimDesignOp design;
  SimulationBytecodeOptions options;
  const llvm::DataLayout &dataLayout;
  StateLayout state;
  llvm::DenseSet<mlir::Value> twoStateLogicRegisters;
  llvm::DenseSet<uint64_t> staticNBASites;
  llvm::SmallVector<FunctionPlan, 0> plans;
  llvm::StringMap<uint32_t> indices;
  llvm::StringMap<uint64_t> classIDs;
  llvm::StringMap<sim::SimFuncOp> externalFunctions;
  llvm::DenseMap<uint32_t, std::string> importSymbols;
  llvm::SmallVector<Instruction> instructions;
  llvm::SmallVector<OperandMap> operandMaps;
  llvm::SmallVector<uint8_t> constants;
  llvm::DenseMap<uint64_t, uint64_t> zeroConstants;
  llvm::SmallVector<IntrinsicSignature> intrinsicSignatures;
  llvm::SmallVector<IntrinsicSite> intrinsicSites;
  llvm::SmallVector<CaptureRecord> captureRecords;
};

} // namespace obelisk::bytecode

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOBYTECODE_BYTECODEENCODER_H
