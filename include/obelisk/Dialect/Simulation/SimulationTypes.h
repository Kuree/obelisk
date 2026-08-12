//===- SimulationTypes.h - Executable simulation types ---------*- C++ -*-===//

#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONTYPES_H
#define OBELISK_DIALECT_SIMULATION_SIMULATIONTYPES_H

#include "obelisk/Dialect/Simulation/SimulationDialect.h"

#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/Types.h"
#include "mlir/Interfaces/MemorySlotInterfaces.h"

#include "llvm/ADT/TypeSwitch.h"

#include "obelisk/Dialect/Simulation/SimulationEnums.h.inc"

#define GET_TYPEDEF_CLASSES
#include "obelisk/Dialect/Simulation/SimulationTypes.h.inc"

namespace obelisk::sim {

/// Bit width of a normalized packed simulation value, which is either a
/// signless builtin integer or an exact four-state `!obelisk_sim.logic`.
/// Returns `std::nullopt` for every other type.
std::optional<unsigned> getPackedWidth(::mlir::Type type);

/// Scalar representation used by packed-value operators. Packed aggregates
/// map to an integer or logic value of the same width and state domain;
/// unpacked values have no scalar representation.
::mlir::Type getPackedScalarType(::mlir::Type type);

/// Whether `type` is one of the fixed first-class aggregate types.
bool isAggregateType(::mlir::Type type);

/// Number and element type of declaration-order aggregate subelements.
unsigned getAggregateNumElements(::mlir::Type type);
::mlir::Type getAggregateElementType(::mlir::Type type, unsigned index);

/// Convert a source array index into its zero-based declaration ordinal.
std::optional<unsigned> getArrayElementOrdinal(::mlir::Type type,
                                               int64_t sourceIndex);

/// Structural span used only by descriptor provenance analysis. Unlike packed
/// width, this assigns ABI-stable, naturally aligned intervals to unpacked
/// struct/array children. Managed handles occupy one 64-bit aligned word.
std::optional<uint64_t> getProvenanceSpan(::mlir::Type type);

/// Natural bit alignment used by structural provenance layout.
std::optional<uint64_t> getProvenanceAlignment(::mlir::Type type);

/// Structural offset/span for one declaration-order child. Union children all
/// overlap at offset zero.
std::optional<std::pair<uint64_t, uint64_t>>
getAggregateProvenanceSubelement(::mlir::Type type, unsigned index);

/// Runtime-managed categories that can occupy a source value word. These are
/// bit flags because an overlapping union slot may legally represent more
/// than one category.
enum class ManagedHandleKind : uint32_t {
  Class = 1u << 0,
  String = 1u << 1,
  Container = 1u << 2,
  ReferencePath = 1u << 3,
};

/// Runtime trace-layout encoding shared by native class and container
/// descriptors. Exact slots use the one-based managed kind enumerators;
/// candidate slots carry this flag plus a ManagedHandleKind mask.
constexpr uint32_t managedHandleCandidateFlag = uint32_t{1} << 31;

struct ManagedHandleSlot {
  uint64_t bitOffset;
  uint32_t kindMask;
  /// An overlapping source union can also contain ordinary bits. Such a slot
  /// is a root only when its current word names a live object of an allowed
  /// kind; exact slots must always contain a well-typed managed word.
  bool conditional;

  bool operator==(const ManagedHandleSlot &other) const {
    return bitOffset == other.bitOffset && kindMask == other.kindMask &&
           conditional == other.conditional;
  }
};

/// Encode a structured managed slot for the public runtime trace ABI.
std::optional<uint32_t>
getManagedHandleTraceKind(const ManagedHandleSlot &slot);

/// Append every managed-handle word in `type`, including its allowed runtime
/// categories and whether tracing is conditional on the current word.
bool getManagedHandleSlots(::mlir::Type type,
                           ::llvm::SmallVectorImpl<ManagedHandleSlot> &slots);

/// Compatibility projection used by analyses that only need root positions.
/// Append every managed-handle word in `type`, as a bit offset from the start
/// of its structural provenance representation.
bool getManagedHandleOffsets(::mlir::Type type,
                             ::llvm::SmallVectorImpl<uint64_t> &offsets);

/// True for a nullable one-word value owned by the precise managed heap.
bool isManagedHandleType(::mlir::Type type);

} // namespace obelisk::sim

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONTYPES_H
